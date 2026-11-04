#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_event.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace winios::trace;

namespace {

// Instruction encoders
constexpr std::uint32_t enc_b_cond(unsigned cond, std::int32_t word_offset) {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (cond & 0xFu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_subs_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0xF1000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_ldr64(unsigned rt, unsigned rn, std::uint16_t imm12 = 0) {
    return 0xF9400000u | (((imm12 >> 3u) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str64(unsigned rt, unsigned rn, std::uint16_t imm12 = 0) {
    return 0xF9000000u | (((imm12 >> 3u) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldp64(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9400000u | ((rt2 & 0x1Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt1 & 0x1Fu);
}

constexpr std::uint32_t enc_stp64(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9000000u | ((rt2 & 0x1Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt1 & 0x1Fu);
}

constexpr std::uint32_t enc_ldadd64(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8200000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_swp64(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8208000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_msr_reg(unsigned o0, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5100000u | ((o0 & 1u) << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) | ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_mrs_reg(unsigned o0, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5300000u | ((o0 & 1u) << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) | ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// Workload setup: Mixed memory workload
void populate_mixed_memory_workload(Memory& memory) {
    memory.write_u32(0x1000, enc_str64(2, 0, 0));
    memory.write_u32(0x1004, enc_ldr64(3, 0, 0));
    memory.write_u32(0x1008, enc_stp64(4, 5, 0));
    memory.write_u32(0x100C, enc_ldp64(6, 7, 0));
    memory.write_u32(0x1010, enc_ldadd64(8, 9, 0));
    memory.write_u32(0x1014, enc_swp64(10, 11, 0));
    memory.write_u32(0x1018, enc_msr_reg(1, 3, 4, 2, 1, 2));
    memory.write_u32(0x101C, enc_subs_imm(1, 1, 1));
    memory.write_u32(0x1020, enc_b_cond(1, -8));
}

// Read-heavy workload (tight loop of LDRs)
void populate_read_heavy_workload(Memory& memory) {
    memory.write_u32(0x1000, enc_ldr64(2, 0, 0));
    memory.write_u32(0x1004, enc_ldr64(3, 0, 8));
    memory.write_u32(0x1008, enc_ldr64(4, 0, 16));
    memory.write_u32(0x100C, enc_ldr64(5, 0, 24));
    memory.write_u32(0x1010, enc_subs_imm(1, 1, 1));
    memory.write_u32(0x1014, enc_b_cond(1, -5));
}

// Write-heavy workload (tight loop of STRs)
void populate_write_heavy_workload(Memory& memory) {
    memory.write_u32(0x1000, enc_str64(2, 0, 0));
    memory.write_u32(0x1004, enc_str64(3, 0, 8));
    memory.write_u32(0x1008, enc_str64(4, 0, 16));
    memory.write_u32(0x100C, enc_str64(5, 0, 24));
    memory.write_u32(0x1010, enc_subs_imm(1, 1, 1));
    memory.write_u32(0x1014, enc_b_cond(1, -5));
}

struct BenchResult {
    double sec{0.0};
    double m_inst_per_sec{0.0};
    double m_mem_per_sec{0.0};
    double ns_per_inst{0.0};
    double overhead{0.0};
    double events_per_inst{0.0};
    std::uint64_t total_events{0};
    double mb_per_sec{0.0};
};

BenchResult run_benchmark_iteration(int mode, std::size_t iterations, double baseline_ns) {
    constexpr std::size_t kStepsPerIteration = 9;
    constexpr std::size_t kMemPerIteration = 8;
    const std::size_t total_instructions = iterations * kStepsPerIteration;
    const std::size_t total_mem_ops = iterations * kMemPerIteration;

    Memory memory(0x100000);
    populate_mixed_memory_workload(memory);

    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 0x4000); // Data buffer
    cpu.write_register(1, iterations);
    cpu.write_register(2, 0x1111);
    cpu.write_register(4, 0x2222);
    cpu.write_register(5, 0x3333);
    cpu.write_register(8, 1);
    cpu.write_register(10, 0x9999);

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 524288;
    cfg.file_output_enabled = false;

    const std::filesystem::path trace_path = std::filesystem::temp_directory_path() / "winios_memory_bench.wtr";

    switch (mode) {
        case 1: // 1. Baseline: Tracing disabled entirely
            cfg.enabled = false;
            break;
        case 2: // 2. Retirement tracing enabled only
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            break;
        case 3: // 3. Retirement + branch tracing
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            break;
        case 4: // 4. Retirement + branch + exception tracing
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            break;
        case 5: // 5. Retirement + branch + exception + system register tracing
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            break;
        case 6: // 6. Memory tracing enabled in CPU, but MemoryRead/Write filtered out in TraceManager
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, false);
            cfg.event_types.set(TraceEventType::MemoryWrite, false);
            break;
        case 7: // 7. MemoryRead enabled only (+ retirement, branch, exception, sysreg)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, false);
            break;
        case 8: // 8. MemoryWrite enabled only (+ retirement, branch, exception, sysreg)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, false);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            break;
        case 9: // 9. Both MemoryRead and MemoryWrite enabled (in-memory ring buffer sink)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            break;
        case 10: // 10. Full trace pipeline with file persistence (.wtr output on disk)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.file_output_enabled = true;
            cfg.output_path = trace_path;
            cfg.writer_batch_size = 2048;
            std::filesystem::remove(trace_path);
            break;
        case 11: // 11. Full architectural tracing including DebugEvent
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            break;
        case 12: // 12. Full architectural tracing including InterruptDelivered
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            cfg.event_types.set(TraceEventType::InterruptDelivered, true);
            break;
        case 13: // 13. Full Pipeline + CPUStarted & CPUStopped
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            cfg.event_types.set(TraceEventType::InterruptDelivered, true);
            cfg.event_types.set(TraceEventType::CPUStarted, true);
            cfg.event_types.set(TraceEventType::CPUStopped, true);
            break;
        case 14: // 14. Full Pipeline + ContextChanged
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            cfg.event_types.set(TraceEventType::InterruptDelivered, true);
            cfg.event_types.set(TraceEventType::ContextChanged, true);
            break;
        case 15: // 15. Full Combined Architectural Tracing (All 12 event types enabled)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            cfg.event_types.set(TraceEventType::InterruptDelivered, true);
            cfg.event_types.set(TraceEventType::CPUStarted, true);
            cfg.event_types.set(TraceEventType::CPUStopped, true);
            cfg.event_types.set(TraceEventType::ContextChanged, true);
            break;
        case 16: // 16. Full Combined Architectural Tracing (All 13 event types enabled including TimerEvent)
            cfg.event_types.reset();
            cfg.event_types.set(TraceEventType::InstructionRetired, true);
            cfg.event_types.set(TraceEventType::BranchTaken, true);
            cfg.event_types.set(TraceEventType::ExceptionEntered, true);
            cfg.event_types.set(TraceEventType::ExceptionReturned, true);
            cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
            cfg.event_types.set(TraceEventType::MemoryRead, true);
            cfg.event_types.set(TraceEventType::MemoryWrite, true);
            cfg.event_types.set(TraceEventType::DebugEvent, true);
            cfg.event_types.set(TraceEventType::InterruptDelivered, true);
            cfg.event_types.set(TraceEventType::CPUStarted, true);
            cfg.event_types.set(TraceEventType::CPUStopped, true);
            cfg.event_types.set(TraceEventType::ContextChanged, true);
            cfg.event_types.set(TraceEventType::TimerEvent, true);
            break;
    }

    if (cfg.enabled) {
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < total_instructions; ++i) {
        cpu.step(memory);
    }
    const auto stats_snapshot = mgr.statistics();
    if (cfg.enabled) {
        mgr.stop();
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    std::uintmax_t file_bytes = 0;
    if (mode == 10) {
        std::error_code ec;
        file_bytes = std::filesystem::file_size(trace_path, ec);
        std::filesystem::remove(trace_path, ec);
    }

    const double sec = std::chrono::duration<double>(t1 - t0).count();
    BenchResult res;
    res.sec = sec;
    res.m_inst_per_sec = (static_cast<double>(total_instructions) / 1e6) / sec;
    res.m_mem_per_sec = (static_cast<double>(total_mem_ops) / 1e6) / sec;
    res.ns_per_inst = (sec * 1e9) / static_cast<double>(total_instructions);
    res.overhead = baseline_ns > 0 ? ((res.ns_per_inst - baseline_ns) / baseline_ns) * 100.0 : 0.0;
    res.total_events = stats_snapshot.events_accepted;
    res.events_per_inst = static_cast<double>(res.total_events) / static_cast<double>(total_instructions);
    if (mode == 10 && sec > 0.0) {
        res.mb_per_sec = (static_cast<double>(file_bytes) / (1024.0 * 1024.0)) / sec;
    }
    return res;
}

// Workload-specific microbenchmarks
void run_microbenchmarks(std::size_t iterations) {
    std::cout << "--- Workload-Specific Microbenchmarks (" << iterations << " iterations) ---\n";

    // 1. Read-heavy workload
    {
        constexpr std::size_t kSteps = 6;
        Memory mem(0x100000);
        populate_read_heavy_workload(mem);
        CPU cpu;
        cpu.write_register(0, 0x4000);
        cpu.write_register(1, iterations);
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations * kSteps; ++i) {
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * kSteps) / 1e6) / sec;
        std::cout << "  Read-heavy Workload  (4 LDRs/loop): "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * kSteps) << " ns/inst\n";
    }

    // 2. Write-heavy workload
    {
        constexpr std::size_t kSteps = 6;
        Memory mem(0x100000);
        populate_write_heavy_workload(mem);
        CPU cpu;
        cpu.write_register(0, 0x4000);
        cpu.write_register(1, iterations);
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations * kSteps; ++i) {
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * kSteps) / 1e6) / sec;
        std::cout << "  Write-heavy Workload (4 STRs/loop): "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * kSteps) << " ns/inst\n";
    }

    // 3. MMIO-heavy workload (Access GICD_CTLR at 0x08000000)
    {
        constexpr std::size_t kSteps = 6;
        Memory mem(0x100000);
        populate_read_heavy_workload(mem);
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.write_register(0, 0x08000000ULL); // GICD_CTLR
        cpu.write_register(1, iterations);
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations * kSteps; ++i) {
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * kSteps) / 1e6) / sec;
        std::cout << "  MMIO-heavy Workload  (4 GIC MMIO reads/loop): "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * kSteps) << " ns/inst\n";
    }

    // 4. Atomic-heavy workload
    {
        constexpr std::size_t kSteps = 6;
        Memory mem(0x100000);
        // Loop with LDADD and SWP
        mem.write_u32(0x1000, enc_ldadd64(2, 3, 0));
        mem.write_u32(0x1004, enc_swp64(4, 5, 0));
        mem.write_u32(0x1008, enc_ldadd64(6, 7, 0));
        mem.write_u32(0x100C, enc_swp64(8, 9, 0));
        mem.write_u32(0x1010, enc_subs_imm(1, 1, 1));
        mem.write_u32(0x1014, enc_b_cond(1, -5));

        CPU cpu;
        cpu.write_register(0, 0x4000);
        cpu.write_register(1, iterations);
        cpu.set_program_counter(0x1000);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations * kSteps; ++i) {
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * kSteps) / 1e6) / sec;
        std::cout << "  Atomic-heavy Workload (LDADD/SWP loop): "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * kSteps) << " ns/inst\n";
    }
    std::cout << "\n";
}

// Phase 52 Stage 11: Debug-specific microbenchmarks
void run_debug_microbenchmarks(std::size_t iterations) {
    std::cout << "--- Debug Architecture Tracing Microbenchmarks (" << iterations << " iterations) ---\n";

    // 1. Hardware Breakpoint + ERET Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu); // NOP
        }
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        mem.write_u32(0x10200, 0xD69F03E0u); // ERET at vector

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            cpu.step(mem); // Hits breakpoint, vectors to 0x10200
            cpu.step(mem); // ERET returns
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Breakpoint Hit & Return Workload:  "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/step\n";
    }

    // 2. Hardware Watchpoint + ERET Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu);
        }
        mem.write_u32(0x2000, enc_str64(0, 1, 0)); // STR X0, [X1]
        mem.write_u32(0x10200, 0xD69F03E0u);       // ERET

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x2u << 3u) | (0xFFu << 5u)); // Store watchpoint

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            cpu.step(mem); // Hits watchpoint, vectors to 0x10200
            cpu.step(mem); // ERET returns
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Watchpoint Hit & Return Workload:  "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/step\n";
    }

    // 3. Hardware Single-Step + ERET Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu);
        }
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        mem.write_u32(0x10200, 0xD69F03E0u);

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpu.set_daif(0x0u);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            cpu.set_pstate_ss(true);
            cpu.step(mem); // Retires ADD, steps, vectors to 0x10200
            cpu.step(mem); // ERET returns
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Single-Step & Return Workload:     "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/step\n";
    }
    std::cout << "\n";
}

// Phase 52 Stage 12: Interrupt-specific microbenchmarks
void run_interrupt_microbenchmarks(std::size_t iterations) {
    std::cout << "--- Interrupt Architecture Tracing Microbenchmarks (" << iterations << " iterations) ---\n";

    // 1. Timer IRQ Delivery & Return Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu); // NOP
        }
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        mem.write_u32(0x10280, 0xD69F03E0u); // ERET at EL1h IRQ vector

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.set_daif(0);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            cpu.set_phys_timer(winios::TimerState{true, false, true, 0});
            cpu.step(mem); // Accepts IRQ, vectors to 0x10280
            cpu.set_phys_timer(winios::TimerState{false, false, false, 0});
            cpu.step(mem); // ERET returns
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Timer IRQ Delivery & Return Workload: "
                  << std::fixed << std::setprecision(2) << mips << " M steps/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/step\n";
    }

    // 2. GIC FIQ Delivery & Return Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu);
        }
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        mem.write_u32(0x10300, 0xD69F03E0u); // ERET at EL1h FIQ vector

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.set_daif(0);

        auto& gic = cpu.get_gic();
        gic.write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
        gic.write_icc_sre_el1(1, 0);
        gic.write_icc_igrpen0(1, 0);
        gic.write_icc_pmr(0xF0, 0);
        gic.write_mmio(0x080B0080, 0, 4); // Group 0
        gic.write_mmio(0x080B0400 + 1, 0x30, 1);
        gic.write_mmio(0x080B0100, 1u << 1, 4);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            gic.write_mmio(0x080B0200, 1u << 1, 4); // assert FIQ
            cpu.step(mem); // Accepts FIQ, vectors to 0x10300
            gic.write_mmio(0x080B0280, 1u << 1, 4); // clear FIQ
            cpu.step(mem); // ERET returns
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  GIC FIQ Delivery & Return Workload:   "
                  << std::fixed << std::setprecision(2) << mips << " M steps/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/step\n";
    }

    // 3. Masked Interrupt Execution Workload
    {
        Memory mem(0x40000);
        for (std::size_t i = 0; i < mem.size(); i += 4) {
            mem.write_u32(i, 0xD503201Fu);
        }
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x10000);
        cpu.set_daif(0x2u); // Mask IRQ

        cpu.set_phys_timer(winios::TimerState{true, false, true, 0}); // Asserted & pending

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x2000);
            cpu.step(mem); // Executes NOP/ADD, does NOT accept interrupt
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations) / 1e6) / sec;
        std::cout << "  Masked Interrupt Stepping Workload:   "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / iterations << " ns/inst\n";
    }
    std::cout << "\n";
}

void run_lifecycle_microbenchmarks(std::size_t iterations) {
    std::cout << "--- CPU Lifecycle & Context-Change Microbenchmarks (" << iterations << " iterations) ---\n";

    // 1. Direct Start / Stop Emit Latency
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.emit_cpu_started_trace();
            cpu.emit_cpu_stopped_trace(0);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double ops_per_sec = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Start/Stop Emit Latency:             "
                  << std::fixed << std::setprecision(2) << ops_per_sec << " M ev/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/event\n";
    }

    // 2. Direct ContextChanged Emit Latency
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.event_types.set(TraceEventType::ContextChanged, true);
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.emit_context_changed_trace(
                0x1000, 0x1004, 0, 1, false, true,
                winios::trace::ContextChangeReason::ExceptionEntry);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double ops_per_sec = (static_cast<double>(iterations) / 1e6) / sec;
        std::cout << "  ContextChanged Emit Latency:         "
                  << std::fixed << std::setprecision(2) << ops_per_sec << " M ev/s | "
                  << std::setprecision(1) << (sec * 1e9) / iterations << " ns/event\n";
    }

    // 3. MSR SPSel Register Bank Selection Stepping Workload
    {
        Memory mem(0x10000);
        mem.write_u32(0x1000, 0xD50040BFu); // SPSel #0
        mem.write_u32(0x1004, 0xD50041BFu); // SPSel #1

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.event_types.set(TraceEventType::ContextChanged, true);
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x1000);
            cpu.step(mem);
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  MSR SPSel Stepping Workload:         "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/inst\n";
    }
    std::cout << "\n";
}

void run_timer_microbenchmarks(std::size_t iterations) {
    std::cout << "--- Stage 14: Timer Trace Microbenchmarks ---\n";

    // 1. Timer Event Direct Emit Latency
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.event_types.set(TraceEventType::TimerEvent, true);
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.emit_timer_event_trace(winios::trace::TimerOp::CompareUpdate,
                                       winios::trace::TimerType::Physical,
                                       1000 + i, 500 + i, true, false, false);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double ops_per_sec = (static_cast<double>(iterations) / 1e6) / sec;
        std::cout << "  TimerEvent Direct Emit Latency:       "
                  << std::fixed << std::setprecision(2) << ops_per_sec << " M ev/s | "
                  << std::setprecision(1) << (sec * 1e9) / iterations << " ns/event\n";
    }

    // 2. Timer Control and Compare Register MSR Stepping Workload
    {
        Memory mem(0x10000);
        mem.write_u32(0x1000, enc_msr_reg(1, 3, 14, 2, 1, 0)); // MSR CNTP_CTL_EL0, X0
        mem.write_u32(0x1004, enc_msr_reg(1, 3, 14, 2, 0, 1)); // MSR CNTP_CVAL_EL0, X1

        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.write_register(0, 1ULL);
        cpu.write_register(1, 50000ULL);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.event_types.set(TraceEventType::TimerEvent, true);
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_program_counter(0x1000);
            cpu.step(mem);
            cpu.step(mem);
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double mips = (static_cast<double>(iterations * 2) / 1e6) / sec;
        std::cout << "  Timer MSR Stepping Workload:          "
                  << std::fixed << std::setprecision(2) << mips << " M inst/s | "
                  << std::setprecision(1) << (sec * 1e9) / (iterations * 2) << " ns/inst\n";
    }

    // 3. Timer Expiration and Interrupt Check Latency
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.event_types.set(TraceEventType::TimerEvent, true);
        cfg.ring_capacity = 262144;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            cpu.set_phys_timer(CPU::TimerState{true, false, false, 100ULL});
            cpu.set_guest_counter(150ULL);
            cpu.check_timer_status();
            cpu.set_phys_timer(CPU::TimerState{false, false, false, 0ULL});
            cpu.check_timer_status();
        }
        mgr.stop();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double ops_per_sec = (static_cast<double>(iterations) / 1e6) / sec;
        std::cout << "  Timer Expiration and Check:           "
                  << std::fixed << std::setprecision(2) << ops_per_sec << " M checks/s | "
                  << std::setprecision(1) << (sec * 1e9) / iterations << " ns/check\n";
    }
    std::cout << "\n";
}
} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 14: Timer Event Trace Benchmark\n";
    std::cout << "=================================================================\n\n";

    constexpr std::size_t kIterations = 20000; // 20,000 loop iterations = 180,000 instructions
    constexpr std::size_t kTotalInst = kIterations * 9;
    constexpr std::size_t kTotalMemOps = kIterations * 8;

    std::cout << "Workload: Mixed Memory Loop (" << kTotalInst << " instructions, "
              << kTotalMemOps << " memory transfers [stores, loads, pair, atomics], "
              << kIterations << " taken branches)\n\n";

    // 1. Baseline: Tracing Disabled
    BenchResult r1 = run_benchmark_iteration(1, kIterations, 0.0);
    std::cout << "  [1] Tracing Disabled (Baseline):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r1.sec << " s | "
              << std::setprecision(2) << r1.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r1.ns_per_inst << " ns/inst | Overhead: 0.0%\n\n";

    // 2. Retirement Tracing Enabled Only
    BenchResult r2 = run_benchmark_iteration(2, kIterations, r1.ns_per_inst);
    std::cout << "  [2] Retirement Tracing Enabled Only:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r2.sec << " s | "
              << std::setprecision(2) << r2.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r2.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r2.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r2.events_per_inst << " ev/inst | Total Events: "
              << r2.total_events << "\n\n";

    // 3. Retirement + Branch Tracing
    BenchResult r3 = run_benchmark_iteration(3, kIterations, r1.ns_per_inst);
    std::cout << "  [3] Retirement + Branch Tracing:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r3.sec << " s | "
              << std::setprecision(2) << r3.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r3.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r3.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r3.events_per_inst << " ev/inst | Total Events: "
              << r3.total_events << "\n\n";

    // 4. Retirement + Branch + Exception Tracing
    BenchResult r4 = run_benchmark_iteration(4, kIterations, r1.ns_per_inst);
    std::cout << "  [4] Retirement + Branch + Exception Tracing:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r4.sec << " s | "
              << std::setprecision(2) << r4.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r4.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r4.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r4.events_per_inst << " ev/inst | Total Events: "
              << r4.total_events << "\n\n";

    // 5. Retirement + Branch + Exception + System Register Tracing
    BenchResult r5 = run_benchmark_iteration(5, kIterations, r1.ns_per_inst);
    std::cout << "  [5] Retirement + Branch + Exception + SysReg Tracing:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r5.sec << " s | "
              << std::setprecision(2) << r5.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r5.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r5.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r5.events_per_inst << " ev/inst | Total Events: "
              << r5.total_events << "\n\n";

    // 6. Memory Tracing Enabled in CPU, but MemoryRead/Write Filtered Out in TraceManager
    BenchResult r6 = run_benchmark_iteration(6, kIterations, r1.ns_per_inst);
    std::cout << "  [6] MemoryRead & MemoryWrite Filtered Out in TraceManager:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r6.sec << " s | "
              << std::setprecision(2) << r6.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r6.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r6.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r6.events_per_inst << " ev/inst | Total Events: "
              << r6.total_events << "\n\n";

    // 7. MemoryRead Enabled Only
    BenchResult r7 = run_benchmark_iteration(7, kIterations, r1.ns_per_inst);
    std::cout << "  [7] MemoryRead Enabled Only:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r7.sec << " s | "
              << std::setprecision(2) << r7.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r7.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r7.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r7.events_per_inst << " ev/inst | Total Events: "
              << r7.total_events << "\n\n";

    // 8. MemoryWrite Enabled Only
    BenchResult r8 = run_benchmark_iteration(8, kIterations, r1.ns_per_inst);
    std::cout << "  [8] MemoryWrite Enabled Only:\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r8.sec << " s | "
              << std::setprecision(2) << r8.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r8.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r8.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r8.events_per_inst << " ev/inst | Total Events: "
              << r8.total_events << "\n\n";

    // 9. Both MemoryRead and MemoryWrite Enabled (In-Memory Ring Buffer Sink)
    BenchResult r9 = run_benchmark_iteration(9, kIterations, r1.ns_per_inst);
    std::cout << "  [9] Both MemoryRead & MemoryWrite Enabled (Ring Buffer Sink):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r9.sec << " s | "
              << std::setprecision(2) << r9.m_inst_per_sec << " M inst/s | "
              << std::setprecision(2) << r9.m_mem_per_sec << " M mem/s | "
              << std::setprecision(1) << r9.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r9.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r9.events_per_inst << " ev/inst | Total Events: "
              << r9.total_events << "\n\n";

    // 10. Full Trace Pipeline with File Persistence (.wtr output on disk)
    BenchResult r10 = run_benchmark_iteration(10, kIterations, r1.ns_per_inst);
    std::cout << " [10] Full Pipeline with File Persistence (.wtr output):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r10.sec << " s | "
              << std::setprecision(2) << r10.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r10.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r10.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r10.events_per_inst << " ev/inst | "
              << "File Throughput: " << std::fixed << std::setprecision(1) << r10.mb_per_sec << " MB/s | "
              << "Total Events: " << r10.total_events << "\n\n";

    // 11. Full Architectural Tracing + DebugEvent Enabled (Ring Buffer Sink)
    BenchResult r11 = run_benchmark_iteration(11, kIterations, r1.ns_per_inst);
    std::cout << " [11] Full Pipeline + DebugEvent Enabled (Ring Buffer Sink):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r11.sec << " s | "
              << std::setprecision(2) << r11.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r11.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r11.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r11.events_per_inst << " ev/inst | Total Events: "
              << r11.total_events << "\n\n";

    // 12. Full Architectural Tracing + InterruptDelivered Enabled (Ring Buffer Sink)
    BenchResult r12 = run_benchmark_iteration(12, kIterations, r1.ns_per_inst);
    std::cout << " [12] Full Pipeline + InterruptDelivered Enabled (Ring Buffer Sink):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r12.sec << " s | "
              << std::setprecision(2) << r12.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r12.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r12.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r12.events_per_inst << " ev/inst | Total Events: "
              << r12.total_events << "\n\n";

    // 13. Full Pipeline + CPUStarted & CPUStopped (Ring Buffer Sink)
    BenchResult r13 = run_benchmark_iteration(13, kIterations, r1.ns_per_inst);
    std::cout << " [13] Full Pipeline + CPUStarted & CPUStopped (Ring Buffer Sink):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r13.sec << " s | "
              << std::setprecision(2) << r13.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r13.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r13.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r13.events_per_inst << " ev/inst | Total Events: "
              << r13.total_events << "\n\n";

    // 14. Full Pipeline + ContextChanged (Ring Buffer Sink)
    BenchResult r14 = run_benchmark_iteration(14, kIterations, r1.ns_per_inst);
    std::cout << " [14] Full Pipeline + ContextChanged (Ring Buffer Sink):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r14.sec << " s | "
              << std::setprecision(2) << r14.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r14.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r14.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r14.events_per_inst << " ev/inst | Total Events: "
              << r14.total_events << "\n\n";

    // 15. Full Combined Architectural Tracing (All 12 Event Types Enabled)
    BenchResult r15 = run_benchmark_iteration(15, kIterations, r1.ns_per_inst);
    std::cout << " [15] Full Combined Architectural Tracing (All 12 Types Enabled):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r15.sec << " s | "
              << std::setprecision(2) << r15.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r15.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r15.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r15.events_per_inst << " ev/inst | Total Events: "
              << r15.total_events << "\n\n";

    // 16. Full Combined Architectural Tracing (All 13 Event Types Enabled)
    BenchResult r16 = run_benchmark_iteration(16, kIterations, r1.ns_per_inst);
    std::cout << " [16] Full Combined Architectural Tracing (All 13 Types Enabled):\n"
              << "      Time: " << std::fixed << std::setprecision(3) << r16.sec << " s | "
              << std::setprecision(2) << r16.m_inst_per_sec << " M inst/s | "
              << std::setprecision(1) << r16.ns_per_inst << " ns/inst | Overhead: "
              << std::showpos << std::setprecision(1) << r16.overhead << "% | "
              << std::noshowpos << std::setprecision(2) << r16.events_per_inst << " ev/inst | Total Events: "
              << r16.total_events << "\n\n";

    // Microbenchmarks
    run_microbenchmarks(kIterations);
    run_debug_microbenchmarks(5000);
    run_interrupt_microbenchmarks(5000);
    run_lifecycle_microbenchmarks(5000);
    run_timer_microbenchmarks(5000);

    std::cout << "Benchmark complete.\n";
    return 0;
}
