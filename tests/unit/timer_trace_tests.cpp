#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
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

void setup_manager_with_timer(TraceManager& mgr) {
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::TimerEvent, true);
    cfg.event_types.set(TraceEventType::InterruptDelivered, true);
    cfg.event_types.set(TraceEventType::ExceptionEntered, true);
    cfg.event_types.set(TraceEventType::ExceptionReturned, true);
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
    mgr.configure(cfg);
    mgr.start();
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return 0xD5300000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5100000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

void run_one(CPU& cpu, Memory& mem, std::uint32_t instr) {
    const std::uint64_t pc = cpu.get_program_counter();
    mem.write_u32(pc, instr);
    cpu.step(mem);
}

// -----------------------------------------------------------------------------
// Group A: Timer discovery and basic state (Tests 1–8)
// -----------------------------------------------------------------------------

void test_1_physical_timer_support_inventory() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    const auto& phys = cpu.get_phys_timer();
    TEST_ASSERT(!phys.enable);
    TEST_ASSERT(!phys.imask);
    TEST_ASSERT(!phys.istatus);
    TEST_ASSERT(phys.cval == 0xFFFFFFFFFFFFFFFFULL);
}

void test_2_virtual_timer_support_inventory() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    const auto& virt = cpu.get_virt_timer();
    TEST_ASSERT(!virt.enable);
    TEST_ASSERT(cpu.get_virtual_time().get_cntvoff_el2(0) == 0ULL);
}

void test_3_hypervisor_timer_support_inventory() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    const auto& hp = cpu.get_cnthp_timer();
    TEST_ASSERT(!hp.enable);
}

void test_4_secure_timer_support_inventory() {
    // Audit check: WinIOS deliberately does not implement CNTPS_* (Secure Physical Timer)
    // or CNTHV_* (Hypervisor Virtual Timer). Explicitly document and verify they are unsupported.
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(3);
    cpu.set_architectural_exceptions(false);

    // Secure timer register CNTPS_CTL_EL1 op1=7, crn=14, crm=2, op2=1 is unallocated/unsupported
    bool caught_unsupported = false;
    try {
        run_one(cpu, mem, enc_mrs(0, 7, 14, 2, 1));
    } catch (...) {
        caught_unsupported = true;
    }
    TEST_ASSERT(caught_unsupported);
}

void test_5_per_core_timer_identity() {
    SMPCluster cluster(2, 0x10000);
    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);

    c0.set_phys_timer(TimerState{true, false, false, 1000ULL});
    c1.set_phys_timer(TimerState{false, true, false, 2000ULL});

    TEST_ASSERT(c0.get_phys_timer().cval == 1000ULL);
    TEST_ASSERT(c0.get_phys_timer().enable == true);
    TEST_ASSERT(c1.get_phys_timer().cval == 2000ULL);
    TEST_ASSERT(c1.get_phys_timer().enable == false);
}

void test_6_correct_current_el_metadata() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_current_el(2);

    cpu.set_cnthp_timer(TimerState{true, false, false, 500ULL});

    auto evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            TEST_ASSERT(ev.exception_level == 2);
            TEST_ASSERT(ev.has_flag(TraceEventFlags::IsPrivileged));
            found = true;
        }
    }
    TEST_ASSERT(found);
}

void test_7_timer_initialization() {
    CPU cpu;
    cpu.reset();
    TEST_ASSERT(cpu.get_cntfrq_el0() == 24000000ULL);
    TEST_ASSERT(cpu.get_guest_counter() == 0ULL);
    TEST_ASSERT(!cpu.get_phys_timer().enable);
    TEST_ASSERT(!cpu.get_virt_timer().enable);
}

void test_8_timer_reset_behavior() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_phys_timer(TimerState{true, false, true, 100ULL});
    TEST_ASSERT(cpu.get_phys_timer().enable);

    cpu.reset();
    TEST_ASSERT(!cpu.get_phys_timer().enable);
    TEST_ASSERT(cpu.get_phys_timer().cval == 0xFFFFFFFFFFFFFFFFULL);
}

// -----------------------------------------------------------------------------
// Group B: Counter behavior (Tests 9–14)
// -----------------------------------------------------------------------------

void test_9_physical_counter_read_behavior() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(12345ULL);
    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 1)); // MRS X0, CNTPCT_EL0
    TEST_ASSERT(cpu.read_register(0) == 12345ULL);
}

void test_10_virtual_counter_read_behavior() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(20000ULL);
    cpu.set_cntvoff_el2(5000ULL);
    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 2)); // MRS X0, CNTVCT_EL0
    TEST_ASSERT(cpu.read_register(0) == 15000ULL);
}

void test_11_counter_frequency_behavior() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    TEST_ASSERT(cpu.get_cntfrq_el0() == 24000000ULL);
    cpu.write_register(0, 50000000ULL);
    run_one(cpu, mem, enc_msr(3, 14, 0, 0, 0)); // MSR CNTFRQ_EL0, X0
    TEST_ASSERT(cpu.get_cntfrq_el0() == 50000000ULL);
}

void test_12_counter_monotonicity() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    std::uint64_t prev = cpu.get_guest_counter();
    for (int i = 0; i < 20; ++i) {
        run_one(cpu, mem, enc_nop());
        std::uint64_t curr = cpu.get_guest_counter();
        TEST_ASSERT(curr > prev);
        prev = curr;
    }
}

void test_13_virtual_offset_behavior() {
    CPU cpu;
    cpu.set_guest_counter(10000ULL);
    cpu.set_cntvoff_el2(2000ULL);
    TEST_ASSERT(cpu.get_virtual_time().get_virtual_counter(0) == 8000ULL);

    cpu.set_cntvoff_el2(4000ULL);
    TEST_ASSERT(cpu.get_virtual_time().get_virtual_counter(0) == 6000ULL);
}

void test_14_counter_read_filtering() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    // Reading CNTPCT_EL0 should NOT emit TimerEvent (avoids flood on guest spinloops)
    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 1));
    auto evs = pop_all_events(mgr);
    for (const auto& ev : evs) {
        TEST_ASSERT(ev.get_event_type() != TraceEventType::TimerEvent);
    }
}

// -----------------------------------------------------------------------------
// Group C: Compare and control registers (Tests 15–22)
// -----------------------------------------------------------------------------

void test_15_physical_compare_value_update() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.write_register(0, 5000ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 2, 0)); // MSR CNTP_CVAL_EL0, X0

    auto evs = pop_all_events(mgr);
    bool found_compare_update = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            auto type = static_cast<TimerType>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (op == TimerOp::CompareUpdate && type == TimerType::Physical) {
                TEST_ASSERT(ev.address == 5000ULL);
                TEST_ASSERT(ev.has_flag(TraceEventFlags::IsWrite));
                found_compare_update = true;
            }
        }
    }
    TEST_ASSERT(found_compare_update);
}

void test_16_virtual_compare_value_update() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.write_register(0, 7500ULL);
    run_one(cpu, mem, enc_msr(3, 14, 3, 2, 0)); // MSR CNTV_CVAL_EL0, X0

    auto evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            auto type = static_cast<TimerType>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (op == TimerOp::CompareUpdate && type == TimerType::Virtual) {
                TEST_ASSERT(ev.address == 7500ULL);
                found = true;
            }
        }
    }
    TEST_ASSERT(found);
}

void test_17_timer_enable() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.write_register(0, 1ULL); // ENABLE=1, IMASK=0
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 0)); // MSR CNTP_CTL_EL0, X0

    auto evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::ControlUpdate) {
                TEST_ASSERT((ev.opcode_or_syndrome & (1u << 16u)) != 0); // enable bit
                found = true;
            }
        }
    }
    TEST_ASSERT(found);
}

void test_18_timer_disable() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.write_register(0, 1ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 0)); // enable
    pop_all_events(mgr);

    cpu.write_register(0, 0ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 0)); // disable

    auto evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::ControlUpdate) {
                TEST_ASSERT((ev.opcode_or_syndrome & (1u << 16u)) == 0); // enable cleared
                found = true;
            }
        }
    }
    TEST_ASSERT(found);
}

void test_19_timer_mask_update() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.write_register(0, 3ULL); // ENABLE=1, IMASK=1
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 0));

    auto evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::ControlUpdate) {
                TEST_ASSERT((ev.opcode_or_syndrome & (1u << 17u)) != 0); // imask set
                found = true;
            }
        }
    }
    TEST_ASSERT(found);
}

void test_20_negative_past_compare_value() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(1000ULL);
    // Write TVAL = -50 -> CVAL becomes 1000 - 50 = 950 (past!)
    std::int32_t neg = -50;
    cpu.write_register(0, static_cast<std::uint64_t>(static_cast<std::uint32_t>(neg)));
    run_one(cpu, mem, enc_msr(3, 14, 2, 0, 0)); // MSR CNTP_TVAL_EL0, X0

    TEST_ASSERT(cpu.get_phys_timer().cval == 950ULL);
}

void test_21_invalid_trapped_timer_register_access() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x50000);
    cpu.set_cntkctl_el1(0); // Trap physical counter

    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 1)); // MRS CNTPCT_EL0
    TEST_ASSERT(cpu.get_current_el() == 1); // Trapped to EL1!

    // Zero TimerEvents emitted for trapped instruction
    auto evs = pop_all_events(mgr);
    for (const auto& ev : evs) {
        TEST_ASSERT(ev.get_event_type() != TraceEventType::TimerEvent);
    }
}

void test_22_duplicate_event_prevention() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    // Initial configuration
    cpu.set_phys_timer(TimerState{true, false, false, 500ULL});
    pop_all_events(mgr);

    // Writing identical state again
    cpu.set_phys_timer(TimerState{true, false, false, 500ULL});
    auto evs = pop_all_events(mgr);
    TEST_ASSERT(evs.empty()); // Exactly zero duplicate events emitted
}

// -----------------------------------------------------------------------------
// Group D: Expiration and interrupts (Tests 23–34)
// -----------------------------------------------------------------------------

void test_23_physical_timer_expiration() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL});
    pop_all_events(mgr);

    // Advance counter to 200 (hits CVAL)
    cpu.set_guest_counter(200ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_expired = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::Expired) {
                found_expired = true;
            }
        }
    }
    TEST_ASSERT(found_expired);
}

void test_24_virtual_timer_expiration() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(1000ULL);
    cpu.set_cntvoff_el2(200ULL); // virtual counter is 800
    cpu.set_virt_timer(TimerState{true, false, false, 900ULL});
    pop_all_events(mgr);

    // Advance physical counter to 1105 (virtual counter is 905 >= 900)
    cpu.set_guest_counter(1105ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_expired = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            auto type = static_cast<TimerType>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (op == TimerOp::Expired && type == TimerType::Virtual) {
                found_expired = true;
            }
        }
    }
    TEST_ASSERT(found_expired);
}

void test_25_hypervisor_timer_expiration() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(1000ULL);
    cpu.set_cnthp_timer(TimerState{true, false, false, 1500ULL});
    pop_all_events(mgr);

    cpu.set_guest_counter(1550ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_expired = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            auto type = static_cast<TimerType>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (op == TimerOp::Expired && type == TimerType::HypervisorPhysical) {
                found_expired = true;
            }
        }
    }
    TEST_ASSERT(found_expired);
}

void test_26_timer_interrupt_assertion() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL}); // unmasked
    pop_all_events(mgr);

    cpu.set_guest_counter(200ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_irq = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::InterruptAsserted) {
                TEST_ASSERT(ev.has_flag(TraceEventFlags::IsInterrupt));
                found_irq = true;
            }
        }
    }
    TEST_ASSERT(found_irq);
}

void test_27_no_duplicate_expiration_during_polling() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(250ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL});
    pop_all_events(mgr);

    // Call check_timer_status 5 times without state change
    for (int i = 0; i < 5; ++i) {
        cpu.check_timer_status();
    }
    auto evs = pop_all_events(mgr);
    TEST_ASSERT(evs.empty()); // Zero duplicate events during polling!
}

void test_28_no_expiration_when_condition_not_satisfied() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 5000ULL});
    pop_all_events(mgr);

    // Advance counter but remain below CVAL (400 < 5000)
    cpu.set_guest_counter(400ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            TEST_ASSERT(op != TimerOp::Expired);
            TEST_ASSERT(op != TimerOp::InterruptAsserted);
        }
    }
}

void test_29_timer_pending_state() {
    CPU cpu;
    cpu.set_guest_counter(600ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 500ULL});
    cpu.check_timer_status();
    TEST_ASSERT(cpu.get_phys_timer().istatus == true);
}

void test_30_masked_timer_interrupt() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(100ULL);
    // IMASK = 1 (masked)
    cpu.set_phys_timer(TimerState{true, true, false, 200ULL});
    pop_all_events(mgr);

    cpu.set_guest_counter(250ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    bool found_expired = false;
    bool found_irq = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::Expired) found_expired = true;
            if (op == TimerOp::InterruptAsserted) found_irq = true;
        }
    }
    TEST_ASSERT(found_expired);
    TEST_ASSERT(!found_irq); // Must NOT assert interrupt while masked!
}

void test_31_unmasked_interrupt_delivery() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_guest_counter(250ULL);
    cpu.set_phys_timer(TimerState{true, true, true, 200ULL}); // already expired, masked
    pop_all_events(mgr);

    // Unmask timer (IMASK = false)
    cpu.set_phys_timer(TimerState{true, false, true, 200ULL});

    auto evs = pop_all_events(mgr);
    bool found_irq = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(ev.opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::InterruptAsserted) {
                found_irq = true;
            }
        }
    }
    TEST_ASSERT(found_irq);
}

void test_32_correlation_with_interrupt_delivered() {
    CPU cpu;
    Memory mem(0x20000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_daif(0); // Unmask IRQ
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.set_guest_counter(100ULL);
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL});
    pop_all_events(mgr);

    // Step CPU: trigger expiration
    cpu.set_guest_counter(250ULL);
    cpu.step(mem); // accepted!

    auto evs = pop_all_events(mgr);
    std::size_t timer_irq_idx = 0;
    std::size_t delivered_idx = 0;
    bool found_timer_irq = false;
    bool found_delivered = false;

    for (std::size_t i = 0; i < evs.size(); ++i) {
        if (evs[i].get_event_type() == TraceEventType::TimerEvent) {
            auto op = static_cast<TimerOp>(evs[i].opcode_or_syndrome & 0xFFu);
            if (op == TimerOp::InterruptAsserted) {
                timer_irq_idx = i;
                found_timer_irq = true;
            }
        }
        if (evs[i].get_event_type() == TraceEventType::InterruptDelivered) {
            delivered_idx = i;
            found_delivered = true;
        }
    }
    TEST_ASSERT(found_timer_irq);
    TEST_ASSERT(found_delivered);
    TEST_ASSERT(timer_irq_idx < delivered_idx); // Timer assertion strictly precedes delivery!
}

void test_33_correlation_with_exception_entered() {
    CPU cpu;
    Memory mem(0x20000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_daif(0);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    cpu.set_guest_counter(250ULL);
    cpu.set_phys_timer(TimerState{true, false, true, 200ULL});
    pop_all_events(mgr);

    cpu.step(mem);

    auto evs = pop_all_events(mgr);
    bool found_delivered = false;
    bool found_entered = false;
    std::size_t del_idx = 0;
    std::size_t ent_idx = 0;

    for (std::size_t i = 0; i < evs.size(); ++i) {
        if (evs[i].get_event_type() == TraceEventType::InterruptDelivered) {
            del_idx = i;
            found_delivered = true;
        }
        if (evs[i].get_event_type() == TraceEventType::ExceptionEntered) {
            ent_idx = i;
            found_entered = true;
        }
    }
    TEST_ASSERT(found_delivered);
    TEST_ASSERT(found_entered);
    TEST_ASSERT(del_idx < ent_idx);
}

void test_34_correct_interrupt_return_behavior() {
    CPU cpu;
    Memory mem(0x20000);
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_daif(0);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_nop());

    // Write handler at EL1h IRQ offset (0x10280): ERET
    constexpr std::uint32_t enc_eret = 0xD69F03E0u;
    mem.write_u32(0x10280, enc_eret);

    cpu.set_guest_counter(250ULL);
    cpu.set_phys_timer(TimerState{true, false, true, 200ULL});
    cpu.step(mem); // enters vector at 0x10280

    TEST_ASSERT(cpu.get_program_counter() == 0x10280);

    // Disable timer before return so it doesn't immediately re-interrupt
    cpu.set_phys_timer(TimerState{false, false, false, 0ULL});
    pop_all_events(mgr);

    // Execute ERET
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2000); // Back at interrupted instruction!

    auto evs = pop_all_events(mgr);
    bool found_ret = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::ExceptionReturned) {
            found_ret = true;
        }
    }
    TEST_ASSERT(found_ret);
}

// -----------------------------------------------------------------------------
// Group E: SMP and filtering (Tests 35–42)
// -----------------------------------------------------------------------------

void test_35_per_core_timer_identity_smp() {
    SMPCluster cluster(2, 0x10000);
    TraceManager mgr0;
    TraceManager mgr1;
    setup_manager_with_timer(mgr0);
    setup_manager_with_timer(mgr1);

    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);
    c0.set_trace_manager(&mgr0);
    c1.set_trace_manager(&mgr1);

    c0.set_phys_timer(TimerState{true, false, false, 500ULL});
    c1.set_phys_timer(TimerState{true, false, false, 900ULL});

    auto evs0 = pop_all_events(mgr0);
    auto evs1 = pop_all_events(mgr1);

    for (const auto& ev : evs0) {
        TEST_ASSERT(ev.core_id == 0);
    }
    for (const auto& ev : evs1) {
        TEST_ASSERT(ev.core_id == 1);
    }
}

void test_36_independent_timers_across_cores() {
    SMPCluster cluster(2, 0x10000);
    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);

    c0.set_phys_timer(TimerState{true, false, false, 500ULL});
    c1.set_phys_timer(TimerState{true, false, false, 1500ULL});

    cluster.get_virtual_time().set_physical_counter(600ULL);
    c0.check_timer_status();
    c1.check_timer_status();

    TEST_ASSERT(c0.get_phys_timer().istatus == true);
    TEST_ASSERT(c1.get_phys_timer().istatus == false);
}

void test_37_shared_counter_behavior() {
    SMPCluster cluster(2, 0x10000);
    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);

    cluster.get_virtual_time().set_physical_counter(9999ULL);
    TEST_ASSERT(c0.get_guest_counter() == 9999ULL);
    TEST_ASSERT(c1.get_guest_counter() == 9999ULL);
}

void test_38_correct_sequence_numbering() {
    CPU cpu;
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    cpu.set_trace_manager(&mgr);

    cpu.set_phys_timer(TimerState{true, false, false, 100ULL});
    cpu.set_phys_timer(TimerState{true, false, false, 200ULL});
    cpu.set_phys_timer(TimerState{true, false, false, 300ULL});

    auto evs = pop_all_events(mgr);
    TEST_ASSERT(evs.size() >= 3);
    for (std::size_t i = 1; i < evs.size(); ++i) {
        TEST_ASSERT(evs[i].sequence_number == evs[i - 1].sequence_number + 1);
    }
}

void test_39_timer_event_filtering() {
    CPU cpu;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::TimerEvent, false); // Filtered out!
    mgr.configure(cfg);
    mgr.start();
    cpu.set_trace_manager(&mgr);

    cpu.set_phys_timer(TimerState{true, false, false, 100ULL});
    cpu.set_guest_counter(200ULL);
    cpu.check_timer_status();

    auto evs = pop_all_events(mgr);
    for (const auto& ev : evs) {
        TEST_ASSERT(ev.get_event_type() != TraceEventType::TimerEvent);
    }
}

void test_40_dynamic_filter_reconfiguration() {
    CPU cpu;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::TimerEvent, false);
    mgr.configure(cfg);
    mgr.start();
    cpu.set_trace_manager(&mgr);

    cpu.set_phys_timer(TimerState{true, false, false, 500ULL});
    auto evs = pop_all_events(mgr);
    TEST_ASSERT(evs.empty());

    // Reconfigure to enable TimerEvent
    mgr.stop();
    cfg.event_types.set(TraceEventType::TimerEvent, true);
    mgr.configure(cfg);
    mgr.start();

    cpu.set_phys_timer(TimerState{true, false, false, 600ULL});
    evs = pop_all_events(mgr);
    bool found = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::TimerEvent) {
            found = true;
        }
    }
    TEST_ASSERT(found);
}

void test_41_other_event_filters_remain_independent() {
    CPU cpu;
    Memory mem(0x10000);
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::TimerEvent, false);
    cfg.event_types.set(TraceEventType::InstructionRetired, true);
    mgr.configure(cfg);
    mgr.start();
    cpu.set_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    run_one(cpu, mem, enc_nop());
    auto evs = pop_all_events(mgr);
    bool found_ret = false;
    for (const auto& ev : evs) {
        if (ev.get_event_type() == TraceEventType::InstructionRetired) {
            found_ret = true;
        }
        TEST_ASSERT(ev.get_event_type() != TraceEventType::TimerEvent);
    }
    TEST_ASSERT(found_ret);
}

void test_42_spsc_producer_ownership() {
    SMPCluster cluster(2, 0x10000);
    TraceManager mgr0;
    TraceManager mgr1;
    setup_manager_with_timer(mgr0);
    setup_manager_with_timer(mgr1);

    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);
    c0.set_trace_manager(&mgr0);
    c1.set_trace_manager(&mgr1);

    // Each core writes only to its own ring buffer
    c0.set_phys_timer(TimerState{true, false, false, 100ULL});
    c1.set_phys_timer(TimerState{true, false, false, 200ULL});

    auto evs0 = pop_all_events(mgr0);
    auto evs1 = pop_all_events(mgr1);

    TEST_ASSERT(!evs0.empty());
    TEST_ASSERT(!evs1.empty());
    for (const auto& ev : evs0) TEST_ASSERT(ev.core_id == 0);
    for (const auto& ev : evs1) TEST_ASSERT(ev.core_id == 1);
}

// -----------------------------------------------------------------------------
// Group F: Persistence and regression (Tests 43–48)
// -----------------------------------------------------------------------------

void test_43_timer_event_serialization() {
    const std::filesystem::path test_file = "test_timer_event.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(test_file.string()));

    TraceEvent ev{};
    ev.sequence_number = 1;
    ev.event_type = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
    ev.core_id = 0;
    ev.address = 5000ULL;
    ev.data = 1200ULL;
    ev.opcode_or_syndrome = static_cast<std::uint32_t>(TimerOp::Expired);
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasData | TraceEventFlags::IsPrivileged);

    TEST_ASSERT(writer.write_event(ev));
    writer.close();

    TEST_ASSERT(std::filesystem::exists(test_file));
    std::filesystem::remove(test_file);
}

void test_44_trace_file_reader_roundtrip() {
    const std::filesystem::path test_file = "test_timer_roundtrip.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(test_file.string()));

    TraceEvent orig{};
    orig.sequence_number = 42;
    orig.virtual_time = 1000;
    orig.pc = 0x8000;
    orig.address = 5000ULL;
    orig.data = 2500ULL;
    orig.opcode_or_syndrome = static_cast<std::uint32_t>(TimerOp::CompareUpdate) | (static_cast<std::uint32_t>(TimerType::Physical) << 8u);
    orig.flags = static_cast<std::uint32_t>(TraceEventFlags::HasData | TraceEventFlags::IsWrite | TraceEventFlags::IsPrivileged);
    orig.event_type = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
    orig.core_id = 1;
    orig.exception_level = 1;

    TEST_ASSERT(writer.write_event(orig));
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(test_file.string()));
    TraceEvent read_ev{};
    TEST_ASSERT(reader.read_event(read_ev));
    TEST_ASSERT(read_ev.sequence_number == 42);
    TEST_ASSERT(read_ev.get_event_type() == TraceEventType::TimerEvent);
    TEST_ASSERT(read_ev.address == 5000ULL);
    TEST_ASSERT(read_ev.data == 2500ULL);
    TEST_ASSERT(read_ev.core_id == 1);
    TEST_ASSERT(read_ev.exception_level == 1);
    reader.close();

    std::filesystem::remove(test_file);
}

void test_45_crc32c_validation() {
    const std::filesystem::path test_file = "test_timer_crc.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    writer.open(test_file.string());
    for (int i = 0; i < 50; ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
        ev.data = i * 100;
        writer.write_event(ev);
    }
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(test_file.string()));
    TEST_ASSERT(reader.is_cleanly_finalized());
    reader.close();

    std::filesystem::remove(test_file);
}

void test_46_mixed_14_event_type_stream() {
    const std::filesystem::path test_file = "test_mixed_14.wtr";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    TraceFileWriter writer;
    writer.open(test_file.string());

    // Write all 13 architectural types + diagnostic markers
    const TraceEventType all_types[] = {
        TraceEventType::InstructionRetired,
        TraceEventType::BranchTaken,
        TraceEventType::ExceptionEntered,
        TraceEventType::ExceptionReturned,
        TraceEventType::InterruptDelivered,
        TraceEventType::SystemRegisterAccess,
        TraceEventType::MemoryRead,
        TraceEventType::MemoryWrite,
        TraceEventType::DebugEvent,
        TraceEventType::CPUStarted,
        TraceEventType::CPUStopped,
        TraceEventType::ContextChanged,
        TraceEventType::TimerEvent,
        TraceEventType::DroppedMarker
    };

    for (std::size_t i = 0; i < sizeof(all_types)/sizeof(all_types[0]); ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.event_type = static_cast<std::uint16_t>(all_types[i]);
        writer.write_event(ev);
    }
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(test_file.string()));
    for (std::size_t i = 0; i < sizeof(all_types)/sizeof(all_types[0]); ++i) {
        TraceEvent read_ev{};
        TEST_ASSERT(reader.read_event(read_ev));
        TEST_ASSERT(read_ev.get_event_type() == all_types[i]);
    }
    reader.close();

    std::filesystem::remove(test_file);
}

void test_47_graceful_pipeline_shutdown() {
    TraceManager mgr;
    setup_manager_with_timer(mgr);
    CPU cpu;
    cpu.set_trace_manager(&mgr);

    cpu.set_phys_timer(TimerState{true, false, false, 1000ULL});
    TEST_ASSERT(mgr.enabled());

    // Flush and reset
    mgr.flush();
    auto evs = pop_all_events(mgr);
    TEST_ASSERT(!evs.empty());
}

void test_48_existing_timer_and_trace_regression() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    // Verify ordinary timer operations execute with zero regressions
    cpu.set_guest_counter(500ULL);
    cpu.write_register(0, 50ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 0, 0)); // MSR CNTP_TVAL_EL0, X0
    TEST_ASSERT(cpu.get_phys_timer().cval == 550ULL);

    run_one(cpu, mem, enc_mrs(1, 3, 14, 2, 0)); // MRS X1, CNTP_TVAL_EL0
    TEST_ASSERT(cpu.read_register(1) == 49ULL);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 14: Timer Trace Unit Tests ===" << std::endl;

    std::cout << "  Group A: Timer discovery and basic state (Tests 1-8)... ";
    test_1_physical_timer_support_inventory();
    test_2_virtual_timer_support_inventory();
    test_3_hypervisor_timer_support_inventory();
    test_4_secure_timer_support_inventory();
    test_5_per_core_timer_identity();
    test_6_correct_current_el_metadata();
    test_7_timer_initialization();
    test_8_timer_reset_behavior();
    std::cout << "PASS\n";

    std::cout << "  Group B: Counter behavior (Tests 9-14)... ";
    test_9_physical_counter_read_behavior();
    test_10_virtual_counter_read_behavior();
    test_11_counter_frequency_behavior();
    test_12_counter_monotonicity();
    test_13_virtual_offset_behavior();
    test_14_counter_read_filtering();
    std::cout << "PASS\n";

    std::cout << "  Group C: Compare and control registers (Tests 15-22)... ";
    test_15_physical_compare_value_update();
    test_16_virtual_compare_value_update();
    test_17_timer_enable();
    test_18_timer_disable();
    test_19_timer_mask_update();
    test_20_negative_past_compare_value();
    test_21_invalid_trapped_timer_register_access();
    test_22_duplicate_event_prevention();
    std::cout << "PASS\n";

    std::cout << "  Group D: Expiration and interrupts (Tests 23-34)... ";
    test_23_physical_timer_expiration();
    test_24_virtual_timer_expiration();
    test_25_hypervisor_timer_expiration();
    test_26_timer_interrupt_assertion();
    test_27_no_duplicate_expiration_during_polling();
    test_28_no_expiration_when_condition_not_satisfied();
    test_29_timer_pending_state();
    test_30_masked_timer_interrupt();
    test_31_unmasked_interrupt_delivery();
    test_32_correlation_with_interrupt_delivered();
    test_33_correlation_with_exception_entered();
    test_34_correct_interrupt_return_behavior();
    std::cout << "PASS\n";

    std::cout << "  Group E: SMP and filtering (Tests 35-42)... ";
    test_35_per_core_timer_identity_smp();
    test_36_independent_timers_across_cores();
    test_37_shared_counter_behavior();
    test_38_correct_sequence_numbering();
    test_39_timer_event_filtering();
    test_40_dynamic_filter_reconfiguration();
    test_41_other_event_filters_remain_independent();
    test_42_spsc_producer_ownership();
    std::cout << "PASS\n";

    std::cout << "  Group F: Persistence and regression (Tests 43-48)... ";
    test_43_timer_event_serialization();
    test_44_trace_file_reader_roundtrip();
    test_45_crc32c_validation();
    test_46_mixed_14_event_type_stream();
    test_47_graceful_pipeline_shutdown();
    test_48_existing_timer_and_trace_regression();
    std::cout << "PASS\n";

    std::cout << "All 48/48 Phase 52 Stage 14 Timer Trace Unit Tests PASSED successfully!" << std::endl;
    return 0;
}
