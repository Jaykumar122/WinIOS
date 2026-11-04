#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_file_writer.h"
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
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":"   \
                      << __LINE__ << std::endl;                                \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

// Drain all events currently available in the TraceManager ring buffer
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

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_daif(0); // Unmask all interrupts
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, enc_nop());
    }
}

// -----------------------------------------------------------------------------
// Group A: Basic Delivery
// -----------------------------------------------------------------------------

void test_timer_irq_delivery() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    // Fire physical timer (CNTP, INTID 30)
    cpu.set_phys_timer(TimerState{true, false, true, 0}); // fired, unmasked, enabled

    // Step CPU at instruction boundary
    cpu.step(mem);

    // Verify CPU entered EL1h IRQ vector (offset 0x280)
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2); // InterruptDelivered + ExceptionEntered

    const auto& deliv = events[0];
    TEST_ASSERT(deliv.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(deliv.pc == 0x1000);
    TEST_ASSERT(deliv.address == 0x10280);
    TEST_ASSERT(deliv.data == 30); // CNTP INTID 30
    TEST_ASSERT(deliv.core_id == 0);
    TEST_ASSERT(deliv.exception_level == 1);
    TEST_ASSERT((deliv.opcode_or_syndrome & 1u) == 0); // is_fiq = false
    TEST_ASSERT((deliv.opcode_or_syndrome & 2u) == 0); // is_virtual = false

    const auto flags = static_cast<TraceEventFlags>(deliv.flags);
    TEST_ASSERT((flags & TraceEventFlags::IsInterrupt) != TraceEventFlags::None);
    TEST_ASSERT((flags & TraceEventFlags::HasVirtualAddress) != TraceEventFlags::None);
    TEST_ASSERT((flags & TraceEventFlags::HasData) != TraceEventFlags::None);
    TEST_ASSERT((flags & TraceEventFlags::IsPrivileged) != TraceEventFlags::None);

    const auto& exc = events[1];
    TEST_ASSERT(exc.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(exc.sequence_number == deliv.sequence_number + 1);

    mgr.stop();
    std::cout << "[PASS] test_timer_irq_delivery\n";
}

void test_fiq_delivery() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);

    // Setup GIC with Group 0 (FIQ) interrupt: INTID 1, priority 0x30
    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4); // Enable Grp0 and Grp1
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    gic.write_mmio(0x080B0080, 0, 4); // Group 0 for SGI/PPI
    gic.write_mmio(0x080B0400 + 1, 0x30, 1); // priority 0x30
    gic.write_mmio(0x080B0100, 1u << 1, 4); // enable INTID 1
    gic.write_mmio(0x080B0200, 1u << 1, 4); // assert pending INTID 1

    cpu.step(mem);

    // FIQ enters EL1h at offset 0x300
    TEST_ASSERT(cpu.get_program_counter() == 0x10300);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);

    const auto& deliv = events[0];
    TEST_ASSERT(deliv.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(deliv.pc == 0x2000);
    TEST_ASSERT(deliv.address == 0x10300);
    TEST_ASSERT(deliv.data == 1);
    TEST_ASSERT((deliv.opcode_or_syndrome & 1u) == 1u); // is_fiq = true
    TEST_ASSERT(((deliv.opcode_or_syndrome >> 8u) & 0xFFu) == 0x30u); // priority

    mgr.stop();
    std::cout << "[PASS] test_fiq_delivery\n";
}

void test_virt_timer_and_cnthp_intid() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x3000);

    // Fire Virtual Timer (INTID 27)
    cpu.set_virt_timer(TimerState{true, false, true, 0});
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].data == 27); // CNTV INTID 27

    // Clear and fire CNTHP (INTID 26) with EL2 routing
    cpu.set_virt_timer(TimerState{false, false, false, 0});
    cpu.set_cnthp_timer(TimerState{true, false, true, 0});
    cpu.set_daif(0);
    cpu.set_program_counter(0x4000);

    cpu.step(mem);
    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].data == 26); // CNTHP INTID 26

    mgr.stop();
    std::cout << "[PASS] test_virt_timer_and_cnthp_intid\n";
}

void test_pmu_overflow_intid() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x5000);

    // Setup GIC to accept PPI 23 (PMU)
    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);
    gic.write_mmio(0x080B0080, 1u << 23, 4); // Group 1
    gic.write_mmio(0x080B0400 + 23, 0x20, 1); // priority 0x20
    gic.write_mmio(0x080B0100, 1u << 23, 4); // enable PPI 23

    // Configure PMU cycle counter overflow
    cpu.get_pmu().reset();
    cpu.get_pmu().write_pmintenset(1u << 31u); // Cycle counter interrupt enable
    cpu.get_pmu().write_pmovsset(1u << 31u);   // Trigger overflow!

    // Step instruction boundary: interrupt is accepted
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_pmu = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered)) {
            TEST_ASSERT(ev.data == 23); // PMU INTID 23
            TEST_ASSERT(((ev.opcode_or_syndrome >> 8u) & 0xFFu) == 0x20u); // priority
            found_pmu = true;
            break;
        }
    }
    TEST_ASSERT(found_pmu);

    mgr.stop();
    std::cout << "[PASS] test_pmu_overflow_intid\n";
}

void test_target_core_id() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.set_core_id(3);
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x6000);

    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(!events.empty());
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].core_id == 3);

    mgr.stop();
    std::cout << "[PASS] test_target_core_id\n";
}

void test_target_el_routing() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    // 1. EL2 Hypervisor Routing via HCR_EL2.IMO (bit 4)
    cpu.set_program_counter(0x7000);
    cpu.set_hcr_el2(1ULL << 4u); // IMO = 1
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 2);
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].exception_level == 2);
    TEST_ASSERT(events[0].address == 0x20480); // vbar_el2 + 0x480 (from lower EL)

    // 2. EL3 Secure Monitor Routing via SCR_EL3.IRQ (bit 1)
    cpu.set_current_el(1);
    cpu.set_daif(0);
    cpu.set_hcr_el2(0);
    cpu.set_scr_el3(cpu.get_scr_el3() | (1ULL << 1u)); // IRQ routed to EL3
    cpu.set_program_counter(0x8000);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 3);
    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].exception_level == 3);
    TEST_ASSERT(events[0].address == 0x30480); // vbar_el3 + 0x480 (from lower EL)

    mgr.stop();
    std::cout << "[PASS] test_target_el_routing\n";
}

void test_virtual_interrupt_delivery() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x9000);

    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // Enable virtual CPU interface at EL2
    gic.write_ich_hcr(1u, 0); // En = 1
    // Set VMCR: PMR = 0xF0 (bits [31:24]), VENG1 = 1 (bit 1)
    gic.write_ich_vmcr((0xF0ULL << 24u) | 0x2ULL, 0);

    // Program ICH_LR0_EL2: Pending (bit 62), Group 1 (bit 60), Priority 0x30 (bits [55:48]), vINTID 42 (bits [31:0])
    std::uint64_t lr_val = (1ULL << 62u) | (1ULL << 60u) | (0x30ULL << 48u) | 42ULL;
    gic.write_ich_lr(0, lr_val, 0);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    const auto& deliv = events[0];
    TEST_ASSERT(deliv.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(deliv.data == 42); // Virtual INTID 42
    TEST_ASSERT((deliv.opcode_or_syndrome & 2u) == 2u); // is_virtual = true
    TEST_ASSERT(((deliv.opcode_or_syndrome >> 8u) & 0xFFu) == 0x30u); // priority

    mgr.stop();
    std::cout << "[PASS] test_virtual_interrupt_delivery\n";
}

// -----------------------------------------------------------------------------
// Group B: Masking
// -----------------------------------------------------------------------------

void test_daif_i_masks_irq() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_daif(0x2u); // Mask IRQ (DAIF.I = 1)

    // Assert physical timer IRQ
    cpu.set_phys_timer(TimerState{true, false, true, 0});

    // Step CPU: Instruction should execute normally (NOP)
    cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == 0x1004); // Advanced past NOP

    auto events = pop_all_events(mgr);
    // There should only be InstructionRetired, ZERO InterruptDelivered
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    }

    mgr.stop();
    std::cout << "[PASS] test_daif_i_masks_irq\n";
}

void test_daif_f_masks_fiq() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.set_daif(0x1u); // Mask FIQ (DAIF.F = 1)

    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_pmr(0xF0, 0);
    gic.write_mmio(0x080B0080, 0, 4); // Group 0
    gic.write_mmio(0x080B0400 + 1, 0x30, 1);
    gic.write_mmio(0x080B0100, 1u << 1, 4);
    gic.write_mmio(0x080B0200, 1u << 1, 4); // Assert Group 0 FIQ

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2004);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    }

    mgr.stop();
    std::cout << "[PASS] test_daif_f_masks_fiq\n";
}

void test_lower_el_unmasked() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 0, 0x10000); // Executing at EL0
    cpu.set_program_counter(0x3000);
    cpu.set_daif(0); // Unmasked at EL0

    // Assert physical timer (targets EL1)
    cpu.set_phys_timer(TimerState{true, false, true, 0});

    // Step CPU: interrupt targeting EL1 from EL0 is taken to EL1
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1);
    TEST_ASSERT(cpu.get_program_counter() == 0x10480); // EL1 lower EL AArch64 vector

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].exception_level == 1);

    mgr.stop();
    std::cout << "[PASS] test_lower_el_unmasked\n";
}

void test_masked_remains_pending_then_unmasked_delivers() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x4000);
    cpu.set_daif(0x2u); // Masked

    cpu.set_phys_timer(TimerState{true, false, true, 0});

    // Step 3 instructions while masked
    for (int i = 0; i < 3; ++i) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x400C);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 3); // 3 InstructionRetired events
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    }

    // Now unmask DAIF.I
    cpu.set_daif(0);

    // Next step takes interrupt immediately
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].pc == 0x400C);

    mgr.stop();
    std::cout << "[PASS] test_masked_remains_pending_then_unmasked_delivers\n";
}

// -----------------------------------------------------------------------------
// Group C: GIC Behavior
// -----------------------------------------------------------------------------

void test_disabled_interrupt_no_delivery() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // SPI 34: Priority 0x40, asserted, but NOT enabled in GICD_ISENABLER
    gic.write_mmio(GICv3::kGicdBase + 0x0084, 1u << 2, 4); // Group 1
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 34, 0x40, 1);
    gic.set_spi_level(34, true);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    }

    mgr.stop();
    std::cout << "[PASS] test_disabled_interrupt_no_delivery\n";
}

void test_pmr_priority_filtering() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);

    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    // Set PMR to 0x50 (only priority < 0x50 can be delivered)
    gic.write_icc_pmr(0x50, 0);

    // SPI 35: Priority 0x60 (lower priority than PMR)
    gic.write_mmio(GICv3::kGicdBase + 0x0084, 1u << 3, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 35, 0x60, 1);
    gic.write_mmio(GICv3::kGicdBase + 0x0104, 1u << 3, 4); // Enabled
    gic.set_spi_level(35, true);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2004);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    }

    // Now raise PMR to 0x80 (allowing 0x60 through)
    gic.write_icc_pmr(0x80, 0);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[0].data == 35);

    mgr.stop();
    std::cout << "[PASS] test_pmr_priority_filtering\n";
}

void test_gic_cpu_routing() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu0;
    cpu0.set_core_id(0);
    cpu0.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu0, mem, 1, 0x10000);
    cpu0.set_program_counter(0x3000);

    auto& gic = cpu0.get_gic();
    gic.set_core_count(2);
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // Route SPI 36 specifically to Core 1 (IRM=0, Aff0=1)
    gic.write_mmio(GICv3::kGicdBase + 0x0084, 1u << 4, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 36, 0x30, 1);
    gic.write_mmio(GICv3::kGicdBase + 0x0104, 1u << 4, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x6100 + (36 - 32) * 8, 0x1ULL, 8); // Aff0 = 1
    gic.set_spi_level(36, true);

    // Step Core 0: Should NOT receive interrupt
    cpu0.step(mem);
    TEST_ASSERT(cpu0.get_program_counter() == 0x3004);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    }

    mgr.stop();
    std::cout << "[PASS] test_gic_cpu_routing\n";
}

void test_spurious_1023_no_delivery() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);

    auto& gic = cpu.get_gic();
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_pmr(0xF0, 0);

    // No interrupts asserted -> reading IAR returns 1023 (spurious)
    std::uint32_t iar = gic.acknowledge_group1(1, 0);
    TEST_ASSERT(iar == 1023u);

    // Step CPU
    cpu.set_program_counter(0x4000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x4004);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    }

    mgr.stop();
    std::cout << "[PASS] test_spurious_1023_no_delivery\n";
}

// -----------------------------------------------------------------------------
// Group D: Event Ordering & Boundary Semantics
// -----------------------------------------------------------------------------

void test_interrupted_instruction_does_not_retire() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 100);

    // At 0x1000: ADD X0, X0, #5
    mem.write_u32(0x1000, enc_add_imm(0, 0, 5));

    // Fire interrupt at boundary
    cpu.set_phys_timer(TimerState{true, false, true, 0});

    cpu.step(mem);

    // Register X0 must NOT have changed!
    TEST_ASSERT(cpu.read_register(0) == 100);
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    // EXACTLY 0 InstructionRetired events
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    }
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

    mgr.stop();
    std::cout << "[PASS] test_interrupted_instruction_does_not_retire\n";
}

void test_causal_ordering_and_monotonicity() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    // Step 1: Normal instruction (ADD X1, X1, #1) retires
    mem.write_u32(0x1000, enc_add_imm(1, 1, 1));
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    // Step 2: Fire interrupt at next boundary (0x1004)
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 3);

    // Event 0: InstructionRetired (seq 1)
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[0].pc == 0x1000);
    TEST_ASSERT(events[0].sequence_number == 1);

    // Event 1: InterruptDelivered (seq 2)
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[1].pc == 0x1004);
    TEST_ASSERT(events[1].address == 0x10280);
    TEST_ASSERT(events[1].sequence_number == 2);

    // Event 2: ExceptionEntered (seq 3)
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(events[2].pc == 0x1004);
    TEST_ASSERT(events[2].address == 0x10280);
    TEST_ASSERT(events[2].sequence_number == 3);

    mgr.stop();
    std::cout << "[PASS] test_causal_ordering_and_monotonicity\n";
}

void test_eret_from_isr_emits_returned() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    // Put ERET at handler address 0x10280
    mem.write_u32(0x10280, enc_eret());

    // Step 1: Interrupt accepted, vectors to 0x10280
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    // Clear timer line so it doesn't immediately refire
    cpu.set_phys_timer(TimerState{false, false, false, 0});

    // Step 2: Execute ERET
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1000); // Returned to interrupted PC!

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 4);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired)); // ERET retired
    TEST_ASSERT(events[3].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionReturned));
    TEST_ASSERT(events[3].pc == 0x10280);
    TEST_ASSERT(events[3].address == 0x1000);

    mgr.stop();
    std::cout << "[PASS] test_eret_from_isr_emits_returned\n";
}

// -----------------------------------------------------------------------------
// Group E: Filtering & Binary Persistence
// -----------------------------------------------------------------------------

void test_filter_disabled() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    // Disable InterruptDelivered in filter
    cfg.event_types.set(TraceEventType::InterruptDelivered, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    // Only ExceptionEntered is emitted!
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

    mgr.stop();
    std::cout << "[PASS] test_filter_disabled\n";
}

void test_dynamic_filter_reconfiguration() {
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::InterruptDelivered, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    Memory mem(0x40000);
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);

    // First interrupt: filtered out
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);
    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 1);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered));

    // Stop and reconfigure filter dynamically
    mgr.stop();
    cfg.event_types.set(TraceEventType::InterruptDelivered, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Second interrupt: now captured
    cpu.set_daif(0);
    cpu.set_program_counter(0x2000);
    cpu.step(mem);
    events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered));

    mgr.stop();
    std::cout << "[PASS] test_dynamic_filter_reconfiguration\n";
}

void test_binary_persistence_roundtrip() {
    const std::filesystem::path trace_path = "test_interrupt_trace.wtr";
    if (std::filesystem::exists(trace_path)) {
        std::filesystem::remove(trace_path);
    }

    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.file_output_enabled = true;
        cfg.output_path = trace_path;
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());

        CPU cpu;
        cpu.bind_trace_manager(&mgr);
        Memory mem(0x40000);
        setup_test_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x1000);

        cpu.set_phys_timer(TimerState{true, false, true, 0});
        cpu.step(mem);

        mgr.flush();
        mgr.stop();
    }

    // Read back file with TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_path));
    TEST_ASSERT(reader.header().magic == TRACE_FILE_MAGIC);

    std::vector<TraceEvent> file_events;
    TraceEvent ev{};
    while (reader.read_event(ev)) {
        file_events.push_back(ev);
    }
    reader.close();

    bool found_deliv = false;
    for (const auto& event : file_events) {
        if (event.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered)) {
            TEST_ASSERT(event.pc == 0x1000);
            TEST_ASSERT(event.address == 0x10280);
            TEST_ASSERT(event.data == 30);
            TEST_ASSERT(event.core_id == 0);
            TEST_ASSERT(event.exception_level == 1);
            found_deliv = true;
            break;
        }
    }
    TEST_ASSERT(found_deliv);

    if (std::filesystem::exists(trace_path)) {
        std::filesystem::remove(trace_path);
    }
    std::cout << "[PASS] test_binary_persistence_roundtrip\n";
}

} // namespace

int main() {
    std::cout << "Starting Interrupt Trace Unit Tests...\n";

    test_timer_irq_delivery();
    test_fiq_delivery();
    test_virt_timer_and_cnthp_intid();
    test_pmu_overflow_intid();
    test_target_core_id();
    test_target_el_routing();
    test_virtual_interrupt_delivery();

    test_daif_i_masks_irq();
    test_daif_f_masks_fiq();
    test_lower_el_unmasked();
    test_masked_remains_pending_then_unmasked_delivers();

    test_disabled_interrupt_no_delivery();
    test_pmr_priority_filtering();
    test_gic_cpu_routing();
    test_spurious_1023_no_delivery();

    test_interrupted_instruction_does_not_retire();
    test_causal_ordering_and_monotonicity();
    test_eret_from_isr_emits_returned();

    test_filter_disabled();
    test_dynamic_filter_reconfiguration();
    test_binary_persistence_roundtrip();

    std::cout << "All 18 Interrupt Trace Unit Tests Passed Successfully!\n";
    return 0;
}
