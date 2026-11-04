#include "cpu/cpu.h"
#include "memory/memory.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at line " << __LINE__    \
                      << std::endl;                                            \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned op0 = 3) {
    const unsigned o0 = (op0 == 3) ? 1u : 0u;
    return 0xD5300000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt, unsigned op0 = 3) {
    const unsigned o0 = (op0 == 3) ? 1u : 0u;
    return 0xD5100000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// B #+imm (offset in bytes, signed)
constexpr std::uint32_t enc_b(std::int32_t byte_offset) {
    const std::uint32_t imm26 = static_cast<std::uint32_t>((byte_offset >> 2) & 0x03FFFFFF);
    return 0x14000000u | imm26;
}

// SVC #imm16
constexpr std::uint32_t enc_svc(std::uint16_t imm16) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u);
}

// ERET
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t instruction) {
    memory.write_u32(address, instruction);
}

std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> results;
    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (!rb) {
        return results;
    }
    while (rb->try_pop(ev)) {
        results.push_back(ev);
    }
    return results;
}

struct CpuSnapshot {
    std::uint64_t x[31];
    std::uint64_t pc;
    std::uint64_t sp_el0;
    std::uint64_t sp_el1;
    std::uint8_t nzcv;
    std::uint8_t daif;
    std::uint64_t sctlr_el1;
    std::uint64_t tcr_el1;
    std::uint64_t ttbr0_el1;
};

CpuSnapshot capture_snapshot(CPU& cpu) {
    CpuSnapshot s{};
    for (unsigned i = 0; i < 31; ++i) {
        s.x[i] = cpu.read_register(i);
    }
    s.pc = cpu.get_program_counter();
    s.sp_el0 = cpu.get_sp_el0();
    s.sp_el1 = cpu.get_sp_el1();
    s.nzcv = cpu.get_nzcv();
    s.daif = cpu.get_daif();
    s.sctlr_el1 = cpu.get_mmu().get_sctlr_el1();
    s.tcr_el1 = cpu.get_mmu().get_tcr_el1();
    s.ttbr0_el1 = cpu.get_mmu().get_ttbr0_el1();
    return s;
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 9: System Register Trace Pipeline & Integration Tests ===\n\n";

    // -------------------------------------------------------------------------
    // Test 1: Differential Validation (Run A: Disabled vs Run B: Tracing Enabled)
    // -------------------------------------------------------------------------
    std::cout << "  Test 1: Differential Validation (Run A: Disabled vs Run B: Enabled)... " << std::flush;
    CpuSnapshot snapshot_untraced{};
    CpuSnapshot snapshot_traced{};

    auto setup_workload = [](Memory& mem) {
        // Set up a loop at 0x1000 with mixed sysreg accesses:
        // 0x1000: MSR SCTLR_EL1, X1
        // 0x1004: MRS X2, SCTLR_EL1
        // 0x1008: MSR TCR_EL1, X3
        // 0x100C: MRS X4, TCR_EL1
        // 0x1010: MSR DAIF, X5
        // 0x1014: MRS X6, DAIF
        // 0x1018: MSR NZCV, X7
        // 0x101C: MRS X8, NZCV
        // 0x1020: NOP
        write_instruction(mem, 0x1000, enc_msr(0, 1, 0, 0, 1));
        write_instruction(mem, 0x1004, enc_mrs(2, 0, 1, 0, 0));
        write_instruction(mem, 0x1008, enc_msr(0, 2, 0, 2, 3));
        write_instruction(mem, 0x100C, enc_mrs(4, 0, 2, 0, 2));
        write_instruction(mem, 0x1010, enc_msr(3, 4, 2, 1, 5));
        write_instruction(mem, 0x1014, enc_mrs(6, 3, 4, 2, 1));
        write_instruction(mem, 0x1018, enc_msr(3, 4, 2, 0, 7));
        write_instruction(mem, 0x101C, enc_mrs(8, 3, 4, 2, 0));
        write_instruction(mem, 0x1020, enc_nop());
    };

    // Run A: Tracing Disabled
    {
        Memory memA(0x100000);
        setup_workload(memA);
        CPU cpuA;
        cpuA.set_architectural_exceptions(true);
        cpuA.set_current_el(1);
        cpuA.set_vbar_el1(0x80000);
        cpuA.write_register(1, 0x1004ULL);
        cpuA.write_register(3, 0x2BULL);
        cpuA.write_register(5, 0x140ULL); // DAIF = 5
        cpuA.write_register(7, 0x9ULL << 28u); // N=1, V=1
        cpuA.set_program_counter(0x1000);

        for (int i = 0; i < 9; ++i) {
            cpuA.step(memA);
        }
        snapshot_untraced = capture_snapshot(cpuA);
    }

    // Run B: Full Tracing Enabled (Retirement, Branch, Exception, System Register)
    {
        Memory memB(0x100000);
        setup_workload(memB);

        TraceManager mgr;
        auto cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 4096;
        mgr.configure(cfg);
        mgr.start();

        CPU cpuB;
        cpuB.bind_trace_manager(&mgr);
        cpuB.set_architectural_exceptions(true);
        cpuB.set_current_el(1);
        cpuB.set_vbar_el1(0x80000);
        cpuB.write_register(1, 0x1004ULL);
        cpuB.write_register(3, 0x2BULL);
        cpuB.write_register(5, 0x140ULL);
        cpuB.write_register(7, 0x9ULL << 28u);
        cpuB.set_program_counter(0x1000);

        for (int i = 0; i < 9; ++i) {
            cpuB.step(memB);
        }
        snapshot_traced = capture_snapshot(cpuB);
        mgr.stop();
    }

    // Differential Verification: 100% Bit-Exact Equivalence
    for (unsigned i = 0; i < 31; ++i) {
        TEST_ASSERT(snapshot_untraced.x[i] == snapshot_traced.x[i]);
    }
    TEST_ASSERT(snapshot_untraced.pc == snapshot_traced.pc);
    TEST_ASSERT(snapshot_untraced.sp_el0 == snapshot_traced.sp_el0);
    TEST_ASSERT(snapshot_untraced.sp_el1 == snapshot_traced.sp_el1);
    TEST_ASSERT(snapshot_untraced.nzcv == snapshot_traced.nzcv);
    TEST_ASSERT(snapshot_untraced.daif == snapshot_traced.daif);
    TEST_ASSERT(snapshot_untraced.sctlr_el1 == snapshot_traced.sctlr_el1);
    TEST_ASSERT(snapshot_untraced.tcr_el1 == snapshot_traced.tcr_el1);
    TEST_ASSERT(snapshot_untraced.ttbr0_el1 == snapshot_traced.ttbr0_el1);
    std::cout << "PASS (100% Bit-Exact Equivalence)\n";

    // -------------------------------------------------------------------------
    // Test 2: Stream Monotonicity and Event Correlation
    // -------------------------------------------------------------------------
    std::cout << "  Test 2: Stream Monotonicity & Event Correlation... " << std::flush;
    {
        Memory mem(0x100000);
        TraceManager mgr;
        auto cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 4096;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x80000);

        // Sequence:
        // 0x2000: MSR NZCV, X1 (MSR -> SysReg + Retired)
        // 0x2004: B 0x2010     (Branch -> Retired + BranchTaken)
        // 0x2010: MRS X2, NZCV (MRS -> SysReg + Retired)
        write_instruction(mem, 0x2000, enc_msr(3, 4, 2, 0, 1));
        write_instruction(mem, 0x2004, enc_b(12));
        write_instruction(mem, 0x2010, enc_mrs(2, 3, 4, 2, 0));

        cpu.write_register(1, 0x3ULL << 28u);
        cpu.set_program_counter(0x2000);

        cpu.step(mem); // MSR
        cpu.step(mem); // B
        cpu.step(mem); // MRS

        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 6);

        // 1. Monotonicity
        for (std::size_t i = 1; i < evs.size(); ++i) {
            TEST_ASSERT(evs[i].sequence_number == evs[i - 1].sequence_number + 1);
        }

        // 2. Types & Ordering:
        // evs[0]: SystemRegisterAccess (MSR at 0x2000)
        // evs[1]: InstructionRetired (MSR at 0x2000)
        // evs[2]: InstructionRetired (B at 0x2004)
        // evs[3]: BranchTaken (B at 0x2004 to 0x2010)
        // evs[4]: SystemRegisterAccess (MRS at 0x2010)
        // evs[5]: InstructionRetired (MRS at 0x2010)
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].pc == 0x2000ULL);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));

        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].pc == 0x2000ULL);

        TEST_ASSERT(evs[2].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[2].pc == 0x2004ULL);

        TEST_ASSERT(evs[3].get_event_type() == TraceEventType::BranchTaken);
        TEST_ASSERT(evs[3].pc == 0x2004ULL);
        TEST_ASSERT(evs[3].address == 0x2010ULL);

        TEST_ASSERT(evs[4].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[4].pc == 0x2010ULL);
        TEST_ASSERT(!evs[4].has_flag(TraceEventFlags::IsWrite));

        TEST_ASSERT(evs[5].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[5].pc == 0x2010ULL);

        mgr.stop();
    }
    std::cout << "PASS\n";

    // -------------------------------------------------------------------------
    // Test 3: Binary File Persistence (.wtr) & TraceFileReader Ingestion
    // -------------------------------------------------------------------------
    std::cout << "  Test 3: Binary File Persistence (.wtr) & Reader Validation... " << std::flush;
    const std::filesystem::path test_trace_path = "sysreg_pipeline_test.wtr";
    if (std::filesystem::exists(test_trace_path)) {
        std::filesystem::remove(test_trace_path);
    }

    {
        Memory mem(0x100000);
        TraceManager mgr;
        auto cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 4096;
        cfg.file_output_enabled = true;
        cfg.output_path = test_trace_path;
        mgr.configure(cfg);
        mgr.start();

        CPU cpu;
        cpu.bind_trace_manager(&mgr);
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_vbar_el1(0x80000);

        // Execute 50 pairs of MSR/MRS
        write_instruction(mem, 0x3000, enc_msr(3, 4, 2, 0, 1));
        write_instruction(mem, 0x3004, enc_mrs(2, 3, 4, 2, 0));

        cpu.write_register(1, 0xCULL << 28u);
        for (int iter = 0; iter < 50; ++iter) {
            cpu.set_program_counter(0x3000);
            cpu.step(mem);
            cpu.step(mem);
        }

        // Clean shutdown: flushes and closes .wtr file
        mgr.stop();
    }

    // Verify .wtr file exists and read it with TraceFileReader
    TEST_ASSERT(std::filesystem::exists(test_trace_path));
    {
        TraceFileReader reader;
        bool ok = reader.open(test_trace_path);
        TEST_ASSERT(ok);

        const auto& hdr = reader.header();
        TEST_ASSERT(hdr.magic == TRACE_FILE_MAGIC);
        TEST_ASSERT(hdr.file_format_version == TRACE_FILE_FORMAT_VERSION);

        std::vector<TraceEvent> events;
        std::size_t n = reader.read_all_events(events);
        TEST_ASSERT(n == 200);
        // 50 iterations * 2 instructions * 2 events = 200 events
        TEST_ASSERT(events.size() == 200);

        for (std::size_t i = 0; i < events.size(); i += 4) {
            // MSR event
            TEST_ASSERT(events[i].get_event_type() == TraceEventType::SystemRegisterAccess);
            TEST_ASSERT(events[i].has_flag(TraceEventFlags::IsWrite));
            TEST_ASSERT(events[i].address == 0x5A10ULL); // NZCV key
            TEST_ASSERT(events[i].data == (0xCULL << 28u));
            TEST_ASSERT(events[i].access_size == 8);

            // MSR instruction retirement
            TEST_ASSERT(events[i + 1].get_event_type() == TraceEventType::InstructionRetired);

            // MRS event
            TEST_ASSERT(events[i + 2].get_event_type() == TraceEventType::SystemRegisterAccess);
            TEST_ASSERT(!events[i + 2].has_flag(TraceEventFlags::IsWrite));
            TEST_ASSERT(events[i + 2].address == 0x5A10ULL); // NZCV key
            TEST_ASSERT(events[i + 2].data == (0xCULL << 28u));

            // MRS instruction retirement
            TEST_ASSERT(events[i + 3].get_event_type() == TraceEventType::InstructionRetired);
        }

        reader.close();
        std::filesystem::remove(test_trace_path);
    }
    std::cout << "PASS\n";

    std::cout << "\n=== All System Register Trace Pipeline Tests PASSED Successfully! ===\n";
    return 0;
}
