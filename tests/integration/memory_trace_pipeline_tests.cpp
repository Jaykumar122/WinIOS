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

// STRB Wt, [Xn]
constexpr std::uint32_t enc_strb(unsigned rt, unsigned rn) {
    return 0x39000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// LDRB Wt, [Xn]
constexpr std::uint32_t enc_ldrb(unsigned rt, unsigned rn) {
    return 0x39400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// STRH Wt, [Xn]
constexpr std::uint32_t enc_strh(unsigned rt, unsigned rn) {
    return 0x79000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// LDRH Wt, [Xn]
constexpr std::uint32_t enc_ldrh(unsigned rt, unsigned rn) {
    return 0x79400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// STR Wt, [Xn]
constexpr std::uint32_t enc_str32(unsigned rt, unsigned rn) {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// LDR Wt, [Xn]
constexpr std::uint32_t enc_ldr32(unsigned rt, unsigned rn) {
    return 0xB9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// STR Xt, [Xn]
constexpr std::uint32_t enc_str64(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// LDR Xt, [Xn]
constexpr std::uint32_t enc_ldr64(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// STP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_stp64(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9000000u | ((rt2 & 0x1Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt1 & 0x1Fu);
}

// LDP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_ldp64(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9400000u | ((rt2 & 0x1Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt1 & 0x1Fu);
}

// LDADD Xt, Xs, [Xn] (atomic add)
constexpr std::uint32_t enc_ldadd64(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8200000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// SWP Xt, Xs, [Xn] (atomic swap)
constexpr std::uint32_t enc_swp64(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8208000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
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
    std::cout << "=== WinIOS Phase 52 Stage 10: Memory Trace Pipeline & Integration Tests ===\n\n";

    // -------------------------------------------------------------------------
    // Test 1: Differential Validation (Run A: Disabled vs Run B: Tracing Enabled)
    // -------------------------------------------------------------------------
    std::cout << "  Test 1: Differential Validation (Run A: Disabled vs Run B: Enabled)... " << std::flush;
    CpuSnapshot snapshot_untraced{};
    CpuSnapshot snapshot_traced{};
    std::vector<std::uint8_t> ram_untraced(0x2000);
    std::vector<std::uint8_t> ram_traced(0x2000);

    auto setup_workload = [](Memory& mem) {
        // Base address for data: 0x4000
        // Instructions at 0x1000:
        // 0x1000: STRB W1, [X0]           (byte write: 0x4000)
        // 0x1004: LDRB W2, [X0]           (byte read)
        // 0x1008: STRH W3, [X0]           (halfword write: 0x4000)
        // 0x100C: LDRH W4, [X0]           (halfword read)
        // 0x1010: STR W5, [X0]            (word write: 0x4000)
        // 0x1014: LDR W6, [X0]            (word read)
        // 0x1018: STR X7, [X0]            (doubleword write: 0x4000)
        // 0x101C: LDR X8, [X0]            (doubleword read)
        // 0x1020: STP X9, X10, [X0]       (pair write: 0x4000, 0x4008)
        // 0x1024: LDP X11, X12, [X0]      (pair read)
        // 0x1028: LDADD X13, X14, [X0]    (atomic add: X14 gets old, mem gets +X13)
        // 0x102C: SWP X15, X16, [X0]      (atomic swap: X16 gets old, mem gets X15)
        // 0x1030: NOP
        write_instruction(mem, 0x1000, enc_strb(1, 0));
        write_instruction(mem, 0x1004, enc_ldrb(2, 0));
        write_instruction(mem, 0x1008, enc_strh(3, 0));
        write_instruction(mem, 0x100C, enc_ldrh(4, 0));
        write_instruction(mem, 0x1010, enc_str32(5, 0));
        write_instruction(mem, 0x1014, enc_ldr32(6, 0));
        write_instruction(mem, 0x1018, enc_str64(7, 0));
        write_instruction(mem, 0x101C, enc_ldr64(8, 0));
        write_instruction(mem, 0x1020, enc_stp64(9, 10, 0));
        write_instruction(mem, 0x1024, enc_ldp64(11, 12, 0));
        write_instruction(mem, 0x1028, enc_ldadd64(13, 14, 0));
        write_instruction(mem, 0x102C, enc_swp64(15, 16, 0));
        write_instruction(mem, 0x1030, enc_nop());
    };

    // Run A: Tracing Disabled
    {
        Memory memA(0x100000);
        setup_workload(memA);
        CPU cpuA;
        cpuA.set_architectural_exceptions(true);
        cpuA.set_current_el(1);
        cpuA.set_vbar_el1(0x80000);

        cpuA.write_register(0, 0x4000ULL);
        cpuA.write_register(1, 0x7Au);
        cpuA.write_register(3, 0x1234u);
        cpuA.write_register(5, 0xDEADBEEFu);
        cpuA.write_register(7, 0x0123456789ABCDEFULL);
        cpuA.write_register(9, 0x1111222233334444ULL);
        cpuA.write_register(10, 0x5555666677778888ULL);
        cpuA.write_register(13, 100ULL);
        cpuA.write_register(15, 0x9999999999999999ULL);
        cpuA.set_program_counter(0x1000);

        for (int i = 0; i < 13; ++i) {
            cpuA.step(memA);
        }
        snapshot_untraced = capture_snapshot(cpuA);
        for (std::size_t i = 0; i < ram_untraced.size(); ++i) {
            ram_untraced[i] = memA.read_u8(0x4000 + i);
        }
    }

    // Run B: Full Tracing Enabled (Retirement, Branch, Exception, SysReg, MemoryRead, MemoryWrite)
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

        cpuB.write_register(0, 0x4000ULL);
        cpuB.write_register(1, 0x7Au);
        cpuB.write_register(3, 0x1234u);
        cpuB.write_register(5, 0xDEADBEEFu);
        cpuB.write_register(7, 0x0123456789ABCDEFULL);
        cpuB.write_register(9, 0x1111222233334444ULL);
        cpuB.write_register(10, 0x5555666677778888ULL);
        cpuB.write_register(13, 100ULL);
        cpuB.write_register(15, 0x9999999999999999ULL);
        cpuB.set_program_counter(0x1000);

        for (int i = 0; i < 13; ++i) {
            cpuB.step(memB);
        }
        snapshot_traced = capture_snapshot(cpuB);
        for (std::size_t i = 0; i < ram_traced.size(); ++i) {
            ram_traced[i] = memB.read_u8(0x4000 + i);
        }
        mgr.stop();
    }

    // Differential Verification: 100% Bit-Exact Equivalence across CPU and Memory
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
    for (std::size_t i = 0; i < ram_untraced.size(); ++i) {
        TEST_ASSERT(ram_untraced[i] == ram_traced[i]);
    }
    std::cout << "PASS (100% Bit-Exact Equivalence)\n";

    // -------------------------------------------------------------------------
    // Test 2: Stream Monotonicity & Event Correlation
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
        cpu.set_core_id(2);

        // Sequence:
        // 0x2000: STR X1, [X0]          -> MemoryWrite (seq 0) + InstructionRetired (seq 1)
        // 0x2004: LDR X2, [X0]          -> MemoryRead (seq 2) + InstructionRetired (seq 3)
        // 0x2008: STP X3, X4, [X0]      -> MemoryWrite (seq 4) + MemoryWrite (seq 5) + InstructionRetired (seq 6)
        // 0x200C: LDADD X5, X6, [X0]    -> MemoryRead (seq 7) + MemoryWrite (seq 8) + InstructionRetired (seq 9)
        write_instruction(mem, 0x2000, enc_str64(1, 0));
        write_instruction(mem, 0x2004, enc_ldr64(2, 0));
        write_instruction(mem, 0x2008, enc_stp64(3, 4, 0));
        write_instruction(mem, 0x200C, enc_ldadd64(5, 6, 0));

        cpu.write_register(0, 0x5000ULL);
        cpu.write_register(1, 0xAAAA5555AAAA5555ULL);
        cpu.write_register(3, 0x1111111111111111ULL);
        cpu.write_register(4, 0x2222222222222222ULL);
        cpu.write_register(5, 50ULL);
        cpu.set_program_counter(0x2000);

        cpu.step(mem); // STR
        cpu.step(mem); // LDR
        cpu.step(mem); // STP
        cpu.step(mem); // LDADD

        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 10);

        // 1. Monotonicity & Core ID verification
        for (std::size_t i = 0; i < evs.size(); ++i) {
            TEST_ASSERT(evs[i].core_id == 2);
            if (i > 0) {
                TEST_ASSERT(evs[i].sequence_number == evs[i - 1].sequence_number + 1);
                TEST_ASSERT(evs[i].virtual_time >= evs[i - 1].virtual_time);
            }
        }

        // 2. Event ordering & correlation:
        // evs[0]: MemoryWrite (STR)
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::MemoryWrite);
        TEST_ASSERT(evs[0].pc == 0x2000ULL);
        TEST_ASSERT(evs[0].address == 0x5000ULL);
        TEST_ASSERT(evs[0].data == 0xAAAA5555AAAA5555ULL);
        TEST_ASSERT(evs[0].access_size == 8);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsAtomic));

        // evs[1]: InstructionRetired (STR)
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].pc == 0x2000ULL);

        // evs[2]: MemoryRead (LDR)
        TEST_ASSERT(evs[2].get_event_type() == TraceEventType::MemoryRead);
        TEST_ASSERT(evs[2].pc == 0x2004ULL);
        TEST_ASSERT(evs[2].address == 0x5000ULL);
        TEST_ASSERT(evs[2].data == 0xAAAA5555AAAA5555ULL);
        TEST_ASSERT(evs[2].access_size == 8);
        TEST_ASSERT(!evs[2].has_flag(TraceEventFlags::IsWrite));

        // evs[3]: InstructionRetired (LDR)
        TEST_ASSERT(evs[3].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[3].pc == 0x2004ULL);

        // evs[4]: MemoryWrite 1 (STP first word)
        TEST_ASSERT(evs[4].get_event_type() == TraceEventType::MemoryWrite);
        TEST_ASSERT(evs[4].pc == 0x2008ULL);
        TEST_ASSERT(evs[4].address == 0x5000ULL);
        TEST_ASSERT(evs[4].data == 0x1111111111111111ULL);
        TEST_ASSERT(evs[4].access_size == 8);

        // evs[5]: MemoryWrite 2 (STP second word)
        TEST_ASSERT(evs[5].get_event_type() == TraceEventType::MemoryWrite);
        TEST_ASSERT(evs[5].pc == 0x2008ULL);
        TEST_ASSERT(evs[5].address == 0x5008ULL);
        TEST_ASSERT(evs[5].data == 0x2222222222222222ULL);
        TEST_ASSERT(evs[5].access_size == 8);

        // evs[6]: InstructionRetired (STP)
        TEST_ASSERT(evs[6].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[6].pc == 0x2008ULL);

        // evs[7]: MemoryRead (LDADD read old)
        TEST_ASSERT(evs[7].get_event_type() == TraceEventType::MemoryRead);
        TEST_ASSERT(evs[7].pc == 0x200CULL);
        TEST_ASSERT(evs[7].address == 0x5000ULL);
        TEST_ASSERT(evs[7].data == 0x1111111111111111ULL);
        TEST_ASSERT(evs[7].has_flag(TraceEventFlags::IsAtomic));

        // evs[8]: MemoryWrite (LDADD write new)
        TEST_ASSERT(evs[8].get_event_type() == TraceEventType::MemoryWrite);
        TEST_ASSERT(evs[8].pc == 0x200CULL);
        TEST_ASSERT(evs[8].address == 0x5000ULL);
        TEST_ASSERT(evs[8].data == 0x1111111111111111ULL + 50ULL);
        TEST_ASSERT(evs[8].has_flag(TraceEventFlags::IsAtomic));

        // evs[9]: InstructionRetired (LDADD)
        TEST_ASSERT(evs[9].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[9].pc == 0x200CULL);

        mgr.stop();
    }
    std::cout << "PASS\n";

    // -------------------------------------------------------------------------
    // Test 3: Binary File Persistence (.wtr) & TraceFileReader Ingestion
    // -------------------------------------------------------------------------
    std::cout << "  Test 3: Binary File Persistence (.wtr) & Reader Validation... " << std::flush;
    const std::filesystem::path test_trace_path = "memory_pipeline_test.wtr";
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

        // Execute 50 pairs of STR/LDR:
        // 0x3000: STR X1, [X0]
        // 0x3004: LDR X2, [X0]
        write_instruction(mem, 0x3000, enc_str64(1, 0));
        write_instruction(mem, 0x3004, enc_ldr64(2, 0));

        cpu.write_register(0, 0x6000ULL);
        cpu.write_register(1, 0xCAFEBABE12345678ULL);

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
        // 50 iterations * (STR [write + retired] + LDR [read + retired]) = 50 * 4 = 200 events
        TEST_ASSERT(events.size() == 200);

        for (std::size_t i = 0; i < events.size(); i += 4) {
            // STR memory write
            TEST_ASSERT(events[i].get_event_type() == TraceEventType::MemoryWrite);
            TEST_ASSERT(events[i].has_flag(TraceEventFlags::IsWrite));
            TEST_ASSERT(events[i].has_flag(TraceEventFlags::HasData));
            TEST_ASSERT(events[i].address == 0x6000ULL);
            TEST_ASSERT(events[i].data == 0xCAFEBABE12345678ULL);
            TEST_ASSERT(events[i].access_size == 8);
            TEST_ASSERT(events[i].pc == 0x3000ULL);

            // STR instruction retirement
            TEST_ASSERT(events[i + 1].get_event_type() == TraceEventType::InstructionRetired);
            TEST_ASSERT(events[i + 1].pc == 0x3000ULL);

            // LDR memory read
            TEST_ASSERT(events[i + 2].get_event_type() == TraceEventType::MemoryRead);
            TEST_ASSERT(!events[i + 2].has_flag(TraceEventFlags::IsWrite));
            TEST_ASSERT(events[i + 2].has_flag(TraceEventFlags::HasData));
            TEST_ASSERT(events[i + 2].address == 0x6000ULL);
            TEST_ASSERT(events[i + 2].data == 0xCAFEBABE12345678ULL);
            TEST_ASSERT(events[i + 2].access_size == 8);
            TEST_ASSERT(events[i + 2].pc == 0x3004ULL);

            // LDR instruction retirement
            TEST_ASSERT(events[i + 3].get_event_type() == TraceEventType::InstructionRetired);
            TEST_ASSERT(events[i + 3].pc == 0x3004ULL);
        }

        reader.close();
        std::filesystem::remove(test_trace_path);
    }
    std::cout << "PASS\n";

    std::cout << "\n=== All Memory Trace Pipeline Tests PASSED Successfully! ===\n";
    return 0;
}
