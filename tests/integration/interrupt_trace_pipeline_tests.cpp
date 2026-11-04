#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
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

constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

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
    std::uint64_t elr_el1;
    std::uint32_t spsr_el1;
    std::uint32_t esr_el1;
    std::uint64_t far_el1;
    std::uint8_t nzcv;
    std::uint8_t daif;
};

CpuSnapshot capture_snapshot(CPU& cpu) {
    CpuSnapshot s{};
    for (unsigned i = 0; i < 31; ++i) {
        s.x[i] = cpu.read_register(i);
    }
    s.pc = cpu.get_program_counter();
    s.sp_el0 = cpu.get_sp_el0();
    s.sp_el1 = cpu.get_sp_el1();
    s.elr_el1 = cpu.get_elr_el1();
    s.spsr_el1 = cpu.get_spsr_el1();
    s.esr_el1 = cpu.get_esr_el1();
    s.far_el1 = cpu.get_far_el1();
    s.nzcv = cpu.get_nzcv();
    s.daif = cpu.get_daif();
    return s;
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_daif(0);
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, enc_nop());
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 12: Interrupt Trace Pipeline & Integration Tests ===\n\n";

    // -------------------------------------------------------------------------
    // Test 1: Differential Validation (Run A: Untraced vs Run B: Traced)
    // -------------------------------------------------------------------------
    std::cout << "  Test 1: Differential Validation (Run A: Disabled vs Run B: Traced)... " << std::flush;
    CpuSnapshot snapshot_untraced{};
    CpuSnapshot snapshot_traced{};
    std::vector<std::uint8_t> ram_untraced(0x2000);
    std::vector<std::uint8_t> ram_traced(0x2000);

    auto setup_workload = [](Memory& mem) {
        // Main thread code:
        // 0x2000: MOVZ X0, #10
        // 0x2004: ADD X1, X0, #20
        // 0x2008: MOVZ X2, #0x5000
        // 0x200C: STR X1, [X2]
        // 0x2010: ADD X6, X3, X4
        write_instruction(mem, 0x2000, enc_movz(0, 10));
        write_instruction(mem, 0x2004, enc_add_imm(1, 0, 20));
        write_instruction(mem, 0x2008, enc_movz(2, 0x5000));
        write_instruction(mem, 0x200C, enc_str_x(1, 2));
        write_instruction(mem, 0x2010, enc_add_imm(6, 3, 5));

        // IRQ vector handler at 0x10280 (EL1h IRQ):
        // 0x10280: ADD X3, X3, #1
        // 0x10284: ERET
        write_instruction(mem, 0x10280, enc_add_imm(3, 3, 1));
        write_instruction(mem, 0x10284, enc_eret());

        // FIQ vector handler at 0x10300 (EL1h FIQ):
        // 0x10300: ADD X5, X5, #2
        // 0x10304: ERET
        write_instruction(mem, 0x10300, enc_add_imm(5, 5, 2));
        write_instruction(mem, 0x10304, enc_eret());
    };

    // Run A: Untraced
    {
        Memory memA(0x40000);
        CPU cpuA;
        setup_test_cpu(cpuA, memA, 1, 0x10000);
        setup_workload(memA);
        cpuA.set_program_counter(0x2000);

        // Step 1: 0x2000 (MOVZ X0, #10)
        cpuA.step(memA);
        // Step 2: 0x2004 (ADD X1, X0, #20)
        cpuA.step(memA);

        // Fire physical timer before step 3
        cpuA.set_phys_timer(TimerState{true, false, true, 0});
        // Step 3: Timer IRQ accepted at boundary 0x2008 -> jumps to 0x10280
        cpuA.step(memA);
        // Clear timer so it doesn't refire in ISR
        cpuA.set_phys_timer(TimerState{false, false, false, 0});
        // Step 4: 0x10280 (ADD X3, X3, #1)
        cpuA.step(memA);
        // Step 5: 0x10284 (ERET) -> returns to 0x2008
        cpuA.step(memA);

        // Step 6: 0x2008 (MOVZ X2, #0x5000)
        cpuA.step(memA);

        // Fire Group 0 FIQ before step 7
        auto& gicA = cpuA.get_gic();
        gicA.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
        gicA.write_icc_sre_el1(1, 0);
        gicA.write_icc_igrpen0(1, 0);
        gicA.write_icc_pmr(0xF0, 0);
        gicA.write_mmio(0x080B0080, 0, 4); // Group 0
        gicA.write_mmio(0x080B0400 + 1, 0x30, 1);
        gicA.write_mmio(0x080B0100, 1u << 1, 4);
        gicA.write_mmio(0x080B0200, 1u << 1, 4); // FIQ asserted

        // Step 7: FIQ accepted at boundary 0x200C -> jumps to 0x10300
        cpuA.step(memA);
        // Clear FIQ in GIC
        gicA.write_mmio(0x080B0280, 1u << 1, 4); // clear pending
        // Step 8: 0x10300 (ADD X5, X5, #2)
        cpuA.step(memA);
        // Step 9: 0x10304 (ERET) -> returns to 0x200C
        cpuA.step(memA);

        // Step 10: 0x200C (STR X1, [X2]) -> stores 30 to 0x5000
        cpuA.step(memA);
        // Step 11: 0x2010 (ADD X6, X3, #5)
        cpuA.step(memA);

        snapshot_untraced = capture_snapshot(cpuA);
        for (std::size_t i = 0; i < ram_untraced.size(); ++i) {
            ram_untraced[i] = memA.read_u8(0x4000 + i);
        }
    }

    // Run B: Traced
    {
        Memory memB(0x40000);
        CPU cpuB;
        setup_test_cpu(cpuB, memB, 1, 0x10000);
        setup_workload(memB);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 2048;
        mgr.configure(cfg);
        mgr.start();

        cpuB.bind_trace_manager(&mgr);
        cpuB.reset_instruction_trace_sequence();
        cpuB.set_program_counter(0x2000);

        // Step 1: 0x2000 (MOVZ X0, #10)
        cpuB.step(memB);
        // Step 2: 0x2004 (ADD X1, X0, #20)
        cpuB.step(memB);

        // Fire physical timer before step 3
        cpuB.set_phys_timer(TimerState{true, false, true, 0});
        // Step 3: Timer IRQ accepted at boundary 0x2008 -> jumps to 0x10280
        cpuB.step(memB);
        // Clear timer so it doesn't refire in ISR
        cpuB.set_phys_timer(TimerState{false, false, false, 0});
        // Step 4: 0x10280 (ADD X3, X3, #1)
        cpuB.step(memB);
        // Step 5: 0x10284 (ERET) -> returns to 0x2008
        cpuB.step(memB);

        // Step 6: 0x2008 (MOVZ X2, #0x5000)
        cpuB.step(memB);

        // Fire Group 0 FIQ before step 7
        auto& gicB = cpuB.get_gic();
        gicB.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
        gicB.write_icc_sre_el1(1, 0);
        gicB.write_icc_igrpen0(1, 0);
        gicB.write_icc_pmr(0xF0, 0);
        gicB.write_mmio(0x080B0080, 0, 4); // Group 0
        gicB.write_mmio(0x080B0400 + 1, 0x30, 1);
        gicB.write_mmio(0x080B0100, 1u << 1, 4);
        gicB.write_mmio(0x080B0200, 1u << 1, 4); // FIQ asserted

        // Step 7: FIQ accepted at boundary 0x200C -> jumps to 0x10300
        cpuB.step(memB);
        // Clear FIQ in GIC
        gicB.write_mmio(0x080B0280, 1u << 1, 4);
        // Step 8: 0x10300 (ADD X5, X5, #2)
        cpuB.step(memB);
        // Step 9: 0x10304 (ERET) -> returns to 0x200C
        cpuB.step(memB);

        // Step 10: 0x200C (STR X1, [X2])
        cpuB.step(memB);
        // Step 11: 0x2010 (ADD X6, X3, #5)
        cpuB.step(memB);

        snapshot_traced = capture_snapshot(cpuB);
        for (std::size_t i = 0; i < ram_traced.size(); ++i) {
            ram_traced[i] = memB.read_u8(0x4000 + i);
        }

        mgr.stop();
    }

    // Compare snapshots bit-for-bit
    for (unsigned i = 0; i < 31; ++i) {
        TEST_ASSERT(snapshot_untraced.x[i] == snapshot_traced.x[i]);
    }
    TEST_ASSERT(snapshot_untraced.pc == snapshot_traced.pc);
    TEST_ASSERT(snapshot_untraced.sp_el0 == snapshot_traced.sp_el0);
    TEST_ASSERT(snapshot_untraced.sp_el1 == snapshot_traced.sp_el1);
    TEST_ASSERT(snapshot_untraced.elr_el1 == snapshot_traced.elr_el1);
    TEST_ASSERT(snapshot_untraced.spsr_el1 == snapshot_traced.spsr_el1);
    TEST_ASSERT(snapshot_untraced.esr_el1 == snapshot_traced.esr_el1);
    TEST_ASSERT(snapshot_untraced.far_el1 == snapshot_traced.far_el1);
    TEST_ASSERT(snapshot_untraced.nzcv == snapshot_traced.nzcv);
    TEST_ASSERT(snapshot_untraced.daif == snapshot_traced.daif);
    TEST_ASSERT(ram_untraced == ram_traced);
    std::cout << "PASS (100% bit-exact equivalence)\n";

    // -------------------------------------------------------------------------
    // Test 2: Stream Monotonicity & Causal Event Correlation
    // -------------------------------------------------------------------------
    std::cout << "  Test 2: Stream Monotonicity & Event Correlation... " << std::flush;
    {
        Memory mem(0x40000);
        CPU cpu;
        setup_test_cpu(cpu, mem, 1, 0x10000);
        setup_workload(mem);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 2048;
        mgr.configure(cfg);
        mgr.start();

        cpu.bind_trace_manager(&mgr);
        cpu.reset_instruction_trace_sequence();
        cpu.set_program_counter(0x2000);

        // Run full sequence
        cpu.step(mem); // 0x2000
        cpu.step(mem); // 0x2004
        cpu.set_phys_timer(TimerState{true, false, true, 0});
        cpu.step(mem); // IRQ delivery to 0x10280
        cpu.set_phys_timer(TimerState{false, false, false, 0});
        cpu.step(mem); // 0x10280
        cpu.step(mem); // 0x10284 ERET -> 0x2008
        cpu.step(mem); // 0x2008

        auto& gic = cpu.get_gic();
        gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
        gic.write_icc_sre_el1(1, 0);
        gic.write_icc_igrpen0(1, 0);
        gic.write_icc_pmr(0xF0, 0);
        gic.write_mmio(0x080B0080, 0, 4);
        gic.write_mmio(0x080B0400 + 1, 0x30, 1);
        gic.write_mmio(0x080B0100, 1u << 1, 4);
        gic.write_mmio(0x080B0200, 1u << 1, 4);

        cpu.step(mem); // FIQ delivery to 0x10300
        gic.write_mmio(0x080B0280, 1u << 1, 4);
        cpu.step(mem); // 0x10300
        cpu.step(mem); // 0x10304 ERET -> 0x200C
        cpu.step(mem); // 0x200C
        cpu.step(mem); // 0x2010

        auto events = pop_all_events(mgr);
        mgr.stop();

        TEST_ASSERT(!events.empty());

        // Verify strictly monotonic sequence numbering
        for (std::size_t i = 1; i < events.size(); ++i) {
            TEST_ASSERT(events[i].sequence_number == events[i - 1].sequence_number + 1);
        }

        // Verify non-decreasing virtual time
        for (std::size_t i = 1; i < events.size(); ++i) {
            TEST_ASSERT(events[i].virtual_time >= events[i - 1].virtual_time);
        }

        // Verify causal chains for each InterruptDelivered event
        std::size_t interrupt_count = 0;
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (events[i].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered)) {
                ++interrupt_count;
                // Immediate successor must be ExceptionEntered
                TEST_ASSERT(i + 1 < events.size());
                const auto& succ = events[i + 1];
                TEST_ASSERT(succ.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
                TEST_ASSERT(succ.sequence_number == events[i].sequence_number + 1);
                TEST_ASSERT(succ.pc == events[i].pc);
                TEST_ASSERT(succ.address == events[i].address);
                const auto succ_flags = static_cast<TraceEventFlags>(succ.flags);
                TEST_ASSERT((succ_flags & TraceEventFlags::IsInterrupt) != TraceEventFlags::None);
            }
        }
        TEST_ASSERT(interrupt_count == 2); // 1 Timer IRQ + 1 GIC FIQ
    }
    std::cout << "PASS (Strict sequence monotonicity & causal pairing)\n";

    // -------------------------------------------------------------------------
    // Test 3: Binary Container Persistence (.wtr) & Deserialization
    // -------------------------------------------------------------------------
    std::cout << "  Test 3: Binary Container Persistence (.wtr)... " << std::flush;
    {
        const std::filesystem::path trace_path = "test_pipeline_interrupt.wtr";
        if (std::filesystem::exists(trace_path)) {
            std::filesystem::remove(trace_path);
        }

        std::vector<TraceEvent> in_memory_events;
        {
            Memory mem(0x40000);
            CPU cpu;
            setup_test_cpu(cpu, mem, 1, 0x10000);
            setup_workload(mem);

            TraceManager mgr;
            TraceConfig cfg = TraceConfig::default_enabled_config();
            cfg.ring_capacity = 2048;
            cfg.file_output_enabled = true;
            cfg.output_path = trace_path;
            mgr.configure(cfg);
            mgr.start();

            cpu.bind_trace_manager(&mgr);
            cpu.reset_instruction_trace_sequence();
            cpu.set_program_counter(0x2000);

            cpu.step(mem);
            cpu.set_phys_timer(TimerState{true, false, true, 0});
            cpu.step(mem); // IRQ delivery

            mgr.flush();
            mgr.stop();
        }

        // Read back from file
        TraceFileReader reader;
        TEST_ASSERT(reader.open(trace_path));
        TEST_ASSERT(reader.header().magic == TRACE_FILE_MAGIC);

        std::vector<TraceEvent> file_events;
        TraceEvent ev{};
        while (reader.read_event(ev)) {
            file_events.push_back(ev);
        }
        reader.close();

        TEST_ASSERT(!file_events.empty());
        bool found_deliv = false;
        for (const auto& e : file_events) {
            if (e.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered)) {
                TEST_ASSERT(e.pc == 0x2004);
                TEST_ASSERT(e.address == 0x10280);
                TEST_ASSERT(e.data == 30);
                found_deliv = true;
                break;
            }
        }
        TEST_ASSERT(found_deliv);

        if (std::filesystem::exists(trace_path)) {
            std::filesystem::remove(trace_path);
        }
    }
    std::cout << "PASS (Binary round-trip verified)\n";

    std::cout << "\nAll Interrupt Trace Pipeline Tests Passed Successfully!\n";
    return 0;
}
