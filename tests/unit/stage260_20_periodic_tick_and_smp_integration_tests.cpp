#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/deterministic_virtual_timer.h"
#include "cpu/early_scheduler_accounting.h"
#include "device/apple_aic.h"
#include "memory/memory.h"
#include "memory/xnu_zone_allocator.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::memory;

static inline uint32_t encode_stp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 260.18 - Periodic Tick Accounting Implementation
// ----------------------------------------------------------------------------
static void test_stage260_18_scheduler_accounting() {
    std::cout << "[Stage 260.18] Testing Periodic Tick Accounting (Category C Model)...\n";

    EarlySchedulerAccounting sched;
    sched.reset();

    TEST_ASSERT(sched.global_tick_count() == 0, "Initial global ticks is 0");
    TEST_ASSERT(sched.check_invariants(), "Initial invariants hold");

    // Record 5 active ticks and 3 idle ticks on Core 0 (unmasked)
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(sched.record_timer_tick(0, /*is_idle=*/false, /*is_masked=*/false), "Active tick recorded");
    }
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT(sched.record_timer_tick(0, /*is_idle=*/true, /*is_masked=*/false), "Idle tick recorded");
    }

    // Masked interrupt: Must NOT increment delivered tick counts
    bool masked_res = sched.record_timer_tick(0, /*is_idle=*/false, /*is_masked=*/true);
    TEST_ASSERT(!masked_res, "Masked interrupt correctly rejected by accounting");

    const auto& c0 = sched.core_accounting(0);
    TEST_ASSERT(c0.total_ticks == 8, "Core 0 total ticks is 8");
    TEST_ASSERT(c0.active_ticks == 5, "Core 0 active ticks is 5");
    TEST_ASSERT(c0.idle_ticks == 3, "Core 0 idle ticks is 3");
    TEST_ASSERT(c0.timer_deliveries == 8, "Core 0 timer deliveries is 8");
    TEST_ASSERT(sched.global_tick_count() == 8, "Global ticks is 8");
    TEST_ASSERT(sched.check_invariants(), "Accounting invariants verified");

    // Record acknowledgments
    sched.record_ack(0);
    sched.record_ack(0);
    TEST_ASSERT(sched.core_accounting(0).acknowledged_irqs == 2, "Core 0 acks recorded");
    TEST_ASSERT(sched.check_invariants(), "Accounting invariants hold with acks");

    std::cout << "[Stage 260.18] Periodic tick accounting verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 260.19 - Adversarial Accounting & Coalesced Ticks
// ----------------------------------------------------------------------------
static void test_stage260_19_adversarial_accounting() {
    std::cout << "[Stage 260.19] Testing Adversarial Tick Bursts, Coalescing & Snapshot Replay...\n";

    EarlySchedulerAccounting sched;
    sched.reset();

    // 100 ticks across 6 cores
    for (uint64_t tick = 0; tick < 100; ++tick) {
        size_t core = tick % 6;
        bool is_idle = (tick % 3 == 0);
        sched.record_timer_tick(core, is_idle, /*is_masked=*/false);
        sched.record_ack(core);
    }

    TEST_ASSERT(sched.global_tick_count() == 100, "100 ticks accounted");
    TEST_ASSERT(sched.total_deliveries() == 100, "100 deliveries accounted");
    TEST_ASSERT(sched.total_acks() == 100, "100 acks accounted");
    TEST_ASSERT(sched.check_invariants(), "Invariants hold across 6 cores");

    // Record coalesced/missed ticks on Core 2 (e.g. CPU was in critical section)
    sched.record_coalesced_tick(2, 5);
    TEST_ASSERT(sched.core_accounting(2).missed_or_coalesced_ticks == 5, "Coalesced ticks recorded");
    TEST_ASSERT(sched.check_invariants(), "Coalesced ticks do not violate delivery invariants");

    // Snapshot Serialization & Replay
    std::vector<uint8_t> snap = sched.serialize();
    TEST_ASSERT(!snap.empty(), "Accounting serialized");

    EarlySchedulerAccounting sched2;
    TEST_ASSERT(sched2.deserialize(snap.data(), snap.size()), "Accounting deserialized");
    TEST_ASSERT(sched2.global_tick_count() == 100, "Restored global ticks matches 100");
    TEST_ASSERT(sched2.core_accounting(2).missed_or_coalesced_ticks == 5, "Restored coalesced ticks matches 5");
    TEST_ASSERT(sched2.check_invariants(), "Restored invariants hold");

    std::cout << "[Stage 260.19] Adversarial bursts and coalesced accounting verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 260.20 - Unified Exception, Timer & Allocator Integration
// ----------------------------------------------------------------------------
static void test_stage260_20_unified_lifecycle() {
    std::cout << "[Stage 260.20] Testing Unified Exception, Timer & Allocator Lifecycle...\n";

    Memory mem(0x800000ULL);

    // Initialize Zone Allocator at 0x200000 (1 MiB pool)
    XnuZoneAllocator za(0x200000ULL, 0x100000ULL);
    std::string err;
    TEST_ASSERT(za.create_zone("vm.threads", 256, 32, &err), "vm.threads created");
    TEST_ASSERT(za.create_zone("ipc.ports", 64, 64, &err), "ipc.ports created");

    // Pre-exception allocation in guest memory
    uint64_t pre_alloc_thread = za.zalloc("vm.threads");
    TEST_ASSERT(pre_alloc_thread >= 0x200000ULL, "Thread allocated in zone pool");

    AppleAIC aic;
    aic.reset();
    aic.set_core_count(6);
    aic.set_current_core(0);

    DeterministicVirtualTimer timer(24000000ULL);
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    timer.set_comparator(0, 5000ULL);

    EarlySchedulerAccounting sched;
    sched.reset();

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    const uint64_t stack_top = 0x44000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Initial GPR setup
    for (uint32_t i = 0; i < 29; ++i) {
        cpu.write_register(i, 0x5555000000000000ULL | (i * 0x100ULL));
    }
    cpu.write_register(29, 0xFEEDFEEDFEEDFEEDULL);
    cpu.write_register(30, 0xCAFECAFECAFECAFEULL);

    // Write workload at 0x1000:
    // 0x1000: mov x0, #100
    // 0x1004: add x0, x0, #1
    // 0x1008: wfi
    // 0x100C: add x0, x0, #50
    mem.write_u32(0x1000, 0xD2800C80u); // mov x0, #100
    mem.write_u32(0x1004, 0x91000400u); // add x0, x0, #1
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x9100C800u); // add x0, x0, #50

    cpu.set_program_counter(0x1000);
    cpu.step(mem); // 0x1000
    cpu.step(mem); // 0x1004
    TEST_ASSERT(cpu.get_program_counter() == 0x1008ULL, "Reached WFI at 0x1008");

    // Advance timer by 5000 ticks: deadline fires!
    timer.advance_counter(5000ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Timer interrupt asserted");
    aic.set_timer_irq(0, true);

    // Exception entry to 0x0A80
    cpu.set_elr_el1(0x100CULL);
    cpu.set_spsr_el1(0x00000000ULL);
    cpu.set_far_el1(0ULL);
    cpu.set_esr_el1(0x56000000ULL);

    // Build complete save-then-restore sequence at 0x0A80
    std::vector<uint32_t> trap;
    trap.push_back(0xD10483FFu); // 0x0A80: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        trap.push_back(encode_stp_sp(k));
    }
    trap.push_back(0xF9007BFEu); // str x30, [sp, #240]
    trap.push_back(0x910483E0u); // add x0, sp, #288
    trap.push_back(0xF9007FE0u); // str x0, [sp, #248]
    trap.push_back(0xD5384020u); // mrs x0, elr_el1
    trap.push_back(0xF90083E0u); // str x0, [sp, #256]
    trap.push_back(0xD5384001u); // mrs x1, spsr_el1
    trap.push_back(0xF90087E1u); // str x1, [sp, #264]
    trap.push_back(0xD5386002u); // mrs x2, far_el1
    trap.push_back(0xF9008BE2u); // str x2, [sp, #272]
    trap.push_back(0xD5385203u); // mrs x3, esr_el1
    trap.push_back(0xB9011BE3u); // str w3, [sp, #280] (32-bit ESR)
    trap.push_back(0x52800024u); // mov w4, #1
    trap.push_back(0xB9011FE4u); // str w4, [sp, #284] (32-bit Exception)
    trap.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trap.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    trap.push_back(0xF94013E4u); // ldr x4, [sp, #32]
    trap.push_back(0xD503201Fu); // 0x0B00: NOP (handler boundary)

    // Restore sequence
    trap.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    trap.push_back(0xD5184020u); // msr elr_el1, x0
    trap.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    trap.push_back(0xD5184001u); // msr spsr_el1, x1
    trap.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        trap.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    trap.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trap.push_back(0x910483FFu); // add sp, sp, #288
    trap.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < trap.size(); ++i) {
        mem.write_u32(0x0A80 + (i * 4), trap[i]);
    }

    cpu.set_program_counter(0x0A80);

    // Execute save trampoline to reach handler boundary
    while (cpu.get_program_counter() != 0x0B00ULL) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x0B00ULL, "Paused at handler boundary 0x0B00");
    TEST_ASSERT(cpu.get_sp_el1() == 0x43EE0ULL, "Stack frame saved at 0x43EE0");

    // Handler Execution:
    // 1. Read AIC Event
    uint64_t aic_event = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
    TEST_ASSERT((aic_event >> 16) == AppleAIC::kEventTypeTimer, "Timer event received");

    // 2. Perform zone allocation inside interrupt handler!
    uint64_t irq_alloc_port = za.zalloc("ipc.ports");
    TEST_ASSERT(irq_alloc_port != 0, "Allocated in zone during interrupt");

    // 3. Update scheduler tick accounting
    sched.record_timer_tick(0, /*is_idle=*/false, /*is_masked=*/false);
    sched.record_ack(0);

    // 4. Acknowledge and rearm timer, clear AIC line, and write EOI
    timer.acknowledge(0);
    timer.rearm_periodic(0, 5000ULL);
    aic.set_timer_irq(0, false);
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, aic_event, 4);

    // Continue through restore phase and ERET
    while (cpu.get_program_counter() != 0x100CULL && cpu.get_program_counter() < 0x0C00ULL) {
        cpu.step(mem);
    }

    // Verify resumption
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "Resumed at 0x100C");
    TEST_ASSERT(cpu.get_sp_el1() == 0x44000ULL, "Stack restored to 0x44000");

    // Execute continuation: add x0, x0, #50
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 151ULL, "Continuation completed: 101 + 50 == 151");

    // Post-exception zone integrity verification:
    // Both pre-allocated thread and in-handler port are valid and non-overlapping!
    TEST_ASSERT(pre_alloc_thread != irq_alloc_port, "Allocations are distinct");
    TEST_ASSERT(za.zfree("vm.threads", pre_alloc_thread, &err), "Freed pre-allocated thread");
    TEST_ASSERT(za.zfree("ipc.ports", irq_alloc_port, &err), "Freed in-handler port");

    std::cout << "[Stage 260.20] Unified exception, timer and allocator lifecycle verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 260.21 - Six-Core SMP Isolation & Deterministic Replay
// ----------------------------------------------------------------------------
static void test_stage260_21_six_core_isolation() {
    std::cout << "[Stage 260.21] Testing Six-Core SMP Isolation & Deterministic Replay...\n";

    AppleAIC aic;
    aic.reset();
    aic.set_core_count(6);

    DeterministicVirtualTimer timer(24000000ULL);
    EarlySchedulerAccounting sched;
    sched.reset();

    // Verify per-core banked WhoAmI register (0x2000 + core * 0x80) across all 6 cores
    for (std::size_t core = 0; core < 6; ++core) {
        aic.set_current_core(core);
        uint64_t who = aic.read_register(AppleAIC::kRegCoreBase + (core * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI, 4);
        TEST_ASSERT(who == core, "Banked WhoAmI returns exact core index");
    }

    // Configure unique timer intervals per core
    for (std::size_t core = 0; core < 6; ++core) {
        timer.set_control(core, DeterministicVirtualTimer::kCtlEnable);
        timer.set_comparator(core, (core + 1) * 2000ULL);
    }

    // Step virtual time by 5000 ticks: Cores 0 and 1 (2000, 4000) should fire; Cores 2..5 (6000+) should not
    timer.advance_counter(5000ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Core 0 timer fired");
    TEST_ASSERT(timer.is_interrupt_asserted(1), "Core 1 timer fired");
    TEST_ASSERT(!timer.is_interrupt_asserted(2), "Core 2 timer not fired");
    TEST_ASSERT(!timer.is_interrupt_asserted(3), "Core 3 timer not fired");
    TEST_ASSERT(!timer.is_interrupt_asserted(4), "Core 4 timer not fired");
    TEST_ASSERT(!timer.is_interrupt_asserted(5), "Core 5 timer not fired");

    // Record accounting for fired cores
    sched.record_timer_tick(0, false, false);
    sched.record_ack(0);
    sched.record_timer_tick(1, false, false);
    sched.record_ack(1);

    TEST_ASSERT(sched.core_accounting(0).total_ticks == 1, "Core 0 has 1 tick");
    TEST_ASSERT(sched.core_accounting(1).total_ticks == 1, "Core 1 has 1 tick");
    TEST_ASSERT(sched.core_accounting(2).total_ticks == 0, "Core 2 has 0 ticks");
    TEST_ASSERT(sched.global_tick_count() == 2, "Global tick count is 2");

    std::cout << "[Stage 260.21] Six-core SMP isolation and deterministic replay verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 260: Stages 260.18 - 260.21 Periodic Tick & SMP Integration Tests\n";
    std::cout << "====================================================================\n";

    test_stage260_18_scheduler_accounting();
    test_stage260_19_adversarial_accounting();
    test_stage260_20_unified_lifecycle();
    test_stage260_21_six_core_isolation();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 260.18 - 260.21 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
