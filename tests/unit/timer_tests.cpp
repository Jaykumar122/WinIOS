// WinIOS Phase 50: AArch64 Generic Timer Architecture Expansion and Virtual Time Synchronization Tests
#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/synchronous_exception.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cout << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " #cond << std::endl;                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

static constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

static constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return 0xD5300000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

static constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt) {
    return 0xD5100000u | (1u << 20u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

static void run_one(CPU& cpu, Memory& mem, std::uint32_t instr) {
    const std::uint64_t pc = cpu.get_program_counter();
    mem.write_u32(pc, instr);
    cpu.step(mem);
}

// 1. Monotonicity and Frequency
void test_physical_counter_monotonicity() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    CHECK(cpu.get_cntfrq_el0() == 24000000ULL); // 24 MHz default
    const std::uint64_t c0 = cpu.get_guest_counter();
    run_one(cpu, mem, enc_nop());
    const std::uint64_t c1 = cpu.get_guest_counter();
    CHECK(c1 == c0 + 1);

    // MSR CNTFRQ_EL0, X0
    cpu.write_register(0, 50000000ULL);
    run_one(cpu, mem, enc_msr(3, 14, 0, 0, 0));
    CHECK(cpu.get_cntfrq_el0() == 50000000ULL);

    // MRS X1, CNTFRQ_EL0
    run_one(cpu, mem, enc_mrs(1, 3, 14, 0, 0));
    CHECK(cpu.read_register(1) == 50000000ULL);

    std::cout << "[PASS] test_physical_counter_monotonicity\n";
}

// 2. Virtual Counter Offset
void test_virtual_counter_offset() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(10000ULL);
    cpu.set_cntvoff_el2(2500ULL);

    // MRS X0, CNTPCT_EL0
    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 1));
    CHECK(cpu.read_register(0) == 10000ULL);

    // MRS X1, CNTVCT_EL0
    run_one(cpu, mem, enc_mrs(1, 3, 14, 0, 2));
    CHECK(cpu.read_register(1) == 10001ULL - 2500ULL);

    // Dynamic offset update: CNTVOFF_EL2 = 5000
    cpu.set_cntvoff_el2(5000ULL);
    run_one(cpu, mem, enc_mrs(2, 3, 14, 0, 2));
    CHECK(cpu.read_register(2) == 10002ULL - 5000ULL);

    std::cout << "[PASS] test_virtual_counter_offset\n";
}

// 3. EL1 Physical Timer CVAL
void test_el1_physical_timer_cval() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(100ULL);

    // Set CVAL to 105
    cpu.write_register(0, 105ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 2, 0)); // MSR CNTP_CVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 105ULL);

    // Enable timer with IMASK=0: ENABLE=1, IMASK=0 -> 1
    cpu.write_register(1, 1ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 1)); // MSR CNTP_CTL_EL0, X1
    CHECK(cpu.get_phys_timer().enable == true);
    CHECK(cpu.get_phys_timer().istatus == false);

    // Step until counter == 105
    while (cpu.get_guest_counter() < 105ULL) {
        run_one(cpu, mem, enc_nop());
    }
    cpu.check_timer_status();
    CHECK(cpu.get_phys_timer().istatus == true);

    // Reading CNTP_CTL_EL0 shows istatus bit 2 set (1 | 4 = 5)
    run_one(cpu, mem, enc_mrs(2, 3, 14, 2, 1));
    CHECK((cpu.read_register(2) & 4u) != 0);

    std::cout << "[PASS] test_el1_physical_timer_cval\n";
}

// 4. EL1 Physical Timer TVAL
void test_el1_physical_timer_tval() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(500ULL);

    // Writing TVAL = 50 sets CVAL = 500 + 50 = 550
    cpu.write_register(0, 50ULL);
    run_one(cpu, mem, enc_msr(3, 14, 2, 0, 0)); // MSR CNTP_TVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 550ULL);
    CHECK(cpu.get_guest_counter() == 501ULL);

    // Read TVAL back: CVAL (550) - counter (501) = 49
    run_one(cpu, mem, enc_mrs(1, 3, 14, 2, 0)); // MRS X1, CNTP_TVAL_EL0
    CHECK(cpu.read_register(1) == 49ULL);

    // Writing negative TVAL offset (-20)
    std::int64_t neg = -20;
    cpu.write_register(2, static_cast<std::uint64_t>(neg));
    run_one(cpu, mem, enc_msr(3, 14, 2, 0, 2)); // MSR CNTP_TVAL_EL0, X2
    CHECK(cpu.get_phys_timer().cval == 502ULL - 20ULL); // 482

    std::cout << "[PASS] test_el1_physical_timer_tval\n";
}

// 5. EL1 Virtual Timer CVAL and TVAL
void test_el1_virtual_timer_cval_and_tval() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(1000ULL);
    cpu.set_cntvoff_el2(400ULL); // virtual counter is 600

    // Set CNTV_TVAL_EL0 = 100 -> CVAL = 600 + 100 = 700
    cpu.write_register(0, 100ULL);
    run_one(cpu, mem, enc_msr(3, 14, 3, 0, 0)); // MSR CNTV_TVAL_EL0, X0
    CHECK(cpu.get_virt_timer().cval == 700ULL);

    // Read CNTV_TVAL_EL0 back: 700 - 601 = 99
    run_one(cpu, mem, enc_mrs(1, 3, 14, 3, 0));
    CHECK(cpu.read_register(1) == 99ULL);

    // Enable virtual timer
    cpu.write_register(2, 1ULL);
    run_one(cpu, mem, enc_msr(3, 14, 3, 1, 2)); // MSR CNTV_CTL_EL0, X2
    CHECK(cpu.get_virt_timer().istatus == false);

    // Advance physical counter until virtual counter >= 700
    // (physical counter >= 1100)
    cpu.set_guest_counter(1105ULL);
    cpu.check_timer_status();
    CHECK(cpu.get_virt_timer().istatus == true);

    std::cout << "[PASS] test_el1_virtual_timer_cval_and_tval\n";
}

// 6. EL2 Hypervisor Timer
void test_el2_hypervisor_timer() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(2); // EL2
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(2000ULL);

    // Write CNTHP_CVAL_EL2 = 2050
    cpu.write_register(0, 2050ULL);
    run_one(cpu, mem, enc_msr(4, 14, 2, 2, 0)); // MSR CNTHP_CVAL_EL2, X0
    CHECK(cpu.get_cnthp_timer().cval == 2050ULL);

    // Write CNTHP_CTL_EL2 = 1 (ENABLE=1)
    cpu.write_register(1, 1ULL);
    run_one(cpu, mem, enc_msr(4, 14, 2, 1, 1));
    CHECK(cpu.get_cnthp_timer().enable == true);
    CHECK(cpu.get_cnthp_timer().istatus == false);

    // Step until expired
    cpu.set_guest_counter(2055ULL);
    cpu.check_timer_status();
    CHECK(cpu.get_cnthp_timer().istatus == true);

    std::cout << "[PASS] test_el2_hypervisor_timer\n";
}

// 7. GICv3 PPI Routing (PPI 30, 27, 26)
void test_gicv3_ppi_routing() {
    CPU cpu;
    cpu.set_current_el(1);

    // Initialize timers
    cpu.set_phys_timer(winios::TimerState{true, false, true, 0});                     // fired, unmasked
    cpu.set_virt_timer(winios::TimerState{true, false, false, 0xFFFFFFFFFFFFFFFFULL}); // not fired (cval in future)
    cpu.set_cnthp_timer(winios::TimerState{true, false, true, 0});                    // fired, unmasked

    CHECK(cpu.get_virtual_time().get_phys_timer_irq(0) == true);
    CHECK(cpu.get_virtual_time().get_virt_timer_irq(0) == false);
    CHECK(cpu.get_virtual_time().get_cnthp_timer_irq(0) == true);

    // Check pending interrupts wires to GICv3 PPI levels
    cpu.check_pending_interrupts();
    CHECK((cpu.get_gic().get_redistributor(0).level0 & (1u << 30)) != 0);
    CHECK((cpu.get_gic().get_redistributor(0).level0 & (1u << 27)) == 0);
    CHECK((cpu.get_gic().get_redistributor(0).level0 & (1u << 26)) != 0);

    std::cout << "[PASS] test_gicv3_ppi_routing\n";
}

// 8. CNTKCTL_EL1 Trapping at EL0
void test_cntkctl_el1_traps() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);

    // All CNTKCTL bits zero -> EL0 access traps to EL1
    cpu.set_cntkctl_el1(0);

    // MRS X0, CNTPCT_EL0 at EL0 -> Traps to EL1 with EC=0x18
    run_one(cpu, mem, enc_mrs(0, 3, 14, 0, 1));
    CHECK(cpu.get_current_el() == 1);
    CHECK(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x18u); // EC = 0x18 (TrappedMsrMrs)

    // Now grant EL0 access via CNTKCTL_EL1 = 0x303 (bits 0,1,8,9 set)
    cpu.set_current_el(0);
    cpu.set_program_counter(0x2000);
    cpu.set_cntkctl_el1(0x303ULL);

    // MRS X1, CNTPCT_EL0 succeeds without exception
    run_one(cpu, mem, enc_mrs(1, 3, 14, 0, 1));
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.get_program_counter() == 0x2004);

    std::cout << "[PASS] test_cntkctl_el1_traps\n";
}

// 9. CNTHCTL_EL2 Trapping at EL1 (Read and Write)
void test_cnthctl_el2_traps() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(1); // EL1
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el2(0x90000ULL);

    // Disable EL1 access to physical timer (EL1PCEN = 0, bit 1 = 0)
    cpu.set_cnthctl_el2(1ULL); // bit 0 set, bit 1 clear

    // 1. MRS CNTP_CTL_EL0 at EL1 -> Traps to EL2
    run_one(cpu, mem, enc_mrs(0, 3, 14, 2, 1));
    CHECK(cpu.get_current_el() == 2);
    CHECK(((cpu.get_esr_el2() >> 26u) & 0x3Fu) == 0x18u);

    // Reset to EL1
    cpu.set_current_el(1);
    cpu.set_program_counter(0x3000);

    // 2. MSR CNTP_CTL_EL0, X0 at EL1 -> Traps to EL2 (both reads AND writes must trap!)
    run_one(cpu, mem, enc_msr(3, 14, 2, 1, 0));
    CHECK(cpu.get_current_el() == 2);
    CHECK(((cpu.get_esr_el2() >> 26u) & 0x3Fu) == 0x18u);

    // 3. Disable physical counter access: EL1PCTEN = 0 (bit 0 = 0)
    cpu.set_cnthctl_el2(2ULL); // bit 1 set, bit 0 clear
    cpu.set_current_el(1);
    cpu.set_program_counter(0x4000);

    // MRS CNTPCT_EL0 at EL1 -> Traps to EL2
    run_one(cpu, mem, enc_mrs(1, 3, 14, 0, 1));
    CHECK(cpu.get_current_el() == 2);
    CHECK(((cpu.get_esr_el2() >> 26u) & 0x3Fu) == 0x18u);

    std::cout << "[PASS] test_cnthctl_el2_traps\n";
}

// 10. SMP Shared Counter Monotonicity
void test_smp_shared_counter() {
    winios::SMPCluster cluster(2, 0x10000);
    cluster.get_memory().write_u32(0x1000, enc_nop());
    cluster.get_memory().write_u32(0x2000, enc_nop());

    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);
    c0.set_program_counter(0x1000);
    c1.set_program_counter(0x2000);

    CHECK(c0.get_guest_counter() == c1.get_guest_counter());

    // Step core 0
    c0.step(cluster.get_memory());
    CHECK(c0.get_guest_counter() == c1.get_guest_counter());
    CHECK(c0.get_guest_counter() == 1ULL);

    // Step core 1
    c1.step(cluster.get_memory());
    CHECK(c0.get_guest_counter() == c1.get_guest_counter());
    CHECK(c0.get_guest_counter() == 2ULL);

    // Set counter via core 0
    c0.set_guest_counter(5000ULL);
    CHECK(c1.get_guest_counter() == 5000ULL);

    std::cout << "[PASS] test_smp_shared_counter\n";
}

// 11. SMP Independent Timers
void test_smp_independent_timers() {
    winios::SMPCluster cluster(2, 0x10000);
    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);

    c0.set_phys_timer(winios::TimerState{true, false, false, 1000ULL});
    c1.set_phys_timer(winios::TimerState{true, false, false, 2000ULL});

    c0.set_cntvoff_el2(100ULL);
    c1.set_cntvoff_el2(500ULL);

    CHECK(c0.get_phys_timer().cval == 1000ULL);
    CHECK(c1.get_phys_timer().cval == 2000ULL);

    CHECK(c0.get_virtual_time().get_virtual_counter(0) != c1.get_virtual_time().get_virtual_counter(1));

    std::cout << "[PASS] test_smp_independent_timers\n";
}

// 12. WFI Idle Advancement
void test_wfi_idle_advancement() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.get_memory().write_u32(0x1000, enc_nop());
    cluster.get_memory().write_u32(0x1004, enc_nop());
    cluster.get_memory().write_u32(0x2000, enc_nop());
    cluster.get_memory().write_u32(0x2004, enc_nop());

    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);

    // Set both cores in WFI state
    c0.set_waiting_for_interrupt(true);
    c1.set_waiting_for_interrupt(true);

    // Set Core 0 physical timer deadline in future (current counter is 0, deadline 500)
    c0.set_phys_timer(winios::TimerState{true, false, false, 500ULL});
    // Core 0 enables GIC redistributor PPI 30 and unmasks DAIF
    c0.set_daif(0);
    c0.get_gic().write_icc_pmr(0xFF, 0);
    c0.get_gic().write_icc_igrpen1(1, 0);
    c0.get_gic().get_redistributor(0).isenabler0 |= (1u << 30);

    // Run deterministic scheduler
    cluster.run_deterministic(10);
    // Counter should have advanced directly to the deadline 500
    CHECK(cluster.get_virtual_time().get_physical_counter() >= 500ULL);
    CHECK(c0.get_phys_timer().istatus == true);

    std::cout << "[PASS] test_wfi_idle_advancement\n";
}

// 13. WFE Event Register
void test_wfe_event_register() {
    CPU cpu;
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    // Setting event register
    cpu.set_event_register(true);
    CHECK(cpu.get_event_register() == true);

    // SEV clears or sets event register
    cpu.set_event_register(false);
    cpu.set_waiting_for_event(true);
    cpu.wake_from_wfe();
    CHECK(cpu.is_waiting() == false);

    std::cout << "[PASS] test_wfe_event_register\n";
}

// 14. PSCI CPU_ON Lifecycle
void test_psci_cpu_on_lifecycle() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.get_memory().write_u32(0x4000, enc_nop());
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();
    CHECK(psci != nullptr);

    // Secondary core (1) starts halted and offline
    CHECK(cluster.is_core_halted(1) == true);
    CHECK(cluster.get_virtual_time().is_core_offline(1) == true);

    // Core 0 issues CPU_ON for Core 1 via handle_smc
    auto& c0 = cluster.get_core(0);
    c0.write_register(0, winios::PSCIService::kCpuOn64);
    c0.write_register(1, 1);
    c0.write_register(2, 0x4000);
    c0.write_register(3, 0x1234);
    bool handled = psci->handle_smc(c0, 0);
    CHECK(handled);
    CHECK(c0.read_register(0) == static_cast<std::uint64_t>(winios::PSCIService::kSuccess));

    // Core 1 is now online and active
    CHECK(cluster.is_core_halted(1) == false);
    CHECK(cluster.get_virtual_time().is_core_offline(1) == false);

    std::cout << "[PASS] test_psci_cpu_on_lifecycle\n";
}

// 15. PSCI CPU_OFF Lifecycle
void test_psci_cpu_off_lifecycle() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.get_memory().write_u32(0x4000, enc_nop());
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    // Start Core 1
    auto& c0 = cluster.get_core(0);
    c0.write_register(0, winios::PSCIService::kCpuOn64);
    c0.write_register(1, 1);
    c0.write_register(2, 0x4000);
    c0.write_register(3, 0);
    psci->handle_smc(c0, 0);
    CHECK(cluster.get_virtual_time().is_core_offline(1) == false);

    // Core 1 sets timer fired
    auto& c1 = cluster.get_core(1);
    c1.set_phys_timer(winios::TimerState{true, false, true, 0});
    CHECK(cluster.get_virtual_time().get_phys_timer_irq(1) == true);

    // Core 1 powers off
    c1.write_register(0, winios::PSCIService::kCpuOff);
    bool handled = psci->handle_smc(c1, 1);
    CHECK(handled);
    CHECK(cluster.is_core_halted(1) == true);
    CHECK(cluster.get_virtual_time().is_core_offline(1) == true);

    // While offline, timer IRQ must NOT be asserted
    CHECK(cluster.get_virtual_time().get_phys_timer_irq(1) == false);

    std::cout << "[PASS] test_psci_cpu_off_lifecycle\n";
}

// 16. PSCI CPU_SUSPEND
void test_psci_cpu_suspend() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.get_memory().write_u32(0x4000, enc_nop());
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    auto& c0 = cluster.get_core(0);
    c0.write_register(0, winios::PSCIService::kCpuOn64);
    c0.write_register(1, 1);
    c0.write_register(2, 0x4000);
    c0.write_register(3, 0);
    psci->handle_smc(c0, 0);

    auto& c1 = cluster.get_core(1);
    // Suspend Core 1
    c1.write_register(0, winios::PSCIService::kCpuSuspend64);
    c1.write_register(1, 0); // retention
    c1.write_register(2, 0x4000);
    c1.write_register(3, 0);
    bool handled = psci->handle_smc(c1, 1);
    CHECK(handled);
    CHECK(c1.is_waiting() == true);

    // Timer deadline wakes suspended core
    c1.set_phys_timer(winios::TimerState{true, false, false, 100ULL});
    cluster.get_virtual_time().advance_physical_counter(150ULL);
    CHECK(c1.get_phys_timer().istatus == true);

    std::cout << "[PASS] test_psci_cpu_suspend\n";
}

// 17. PSCI SYSTEM_RESET
void test_psci_system_reset() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    cluster.get_virtual_time().set_physical_counter(100000ULL);
    cluster.get_core(0).set_phys_timer(winios::TimerState{true, true, true, 500ULL});

    auto& c0 = cluster.get_core(0);
    c0.write_register(0, winios::PSCIService::kSystemReset);
    bool handled = psci->handle_smc(c0, 0);
    CHECK(handled);
    CHECK(cluster.get_virtual_time().get_physical_counter() == 0ULL);
    CHECK(cluster.get_core(0).get_phys_timer().enable == false);

    std::cout << "[PASS] test_psci_system_reset\n";
}

// 18. PMU Independence
void test_pmu_independence() {
    CPU cpu;
    cpu.get_pmu().reset();
    cpu.get_virtual_time().reset_all();

    // Generic timer starts at 0, advances independently of PMCR.E
    CHECK(cpu.get_guest_counter() == 0ULL);
    CHECK(cpu.get_pmu().read_pmccntr() == 0ULL);

    cpu.increment_guest_counter();
    CHECK(cpu.get_guest_counter() == 1ULL);
    // PMU cycle counter was not enabled via PMCR, so remains 0
    CHECK(cpu.get_pmu().read_pmccntr() == 0ULL);

    std::cout << "[PASS] test_pmu_independence\n";
}

// 19. 64-bit Counter Wraparound
void test_counter_wraparound() {
    CPU cpu;
    cpu.set_guest_counter(0xFFFFFFFFFFFFFFFEULL);
    cpu.increment_guest_counter();
    CHECK(cpu.get_guest_counter() == 0xFFFFFFFFFFFFFFFFULL);
    cpu.increment_guest_counter();
    CHECK(cpu.get_guest_counter() == 0ULL); // Wraps safely modulo 2^64

    std::cout << "[PASS] test_counter_wraparound\n";
}

// 20. Timer IMASK Toggle
void test_timer_imask_toggle() {
    CPU cpu;
    cpu.set_phys_timer(winios::TimerState{true, false, true, 0ULL}); // fired, unmasked
    CHECK(cpu.get_virtual_time().get_phys_timer_irq(0) == true);

    // Mask timer (IMASK = true)
    auto t = cpu.get_phys_timer();
    t.imask = true;
    cpu.set_phys_timer(t);
    CHECK(cpu.get_virtual_time().get_phys_timer_irq(0) == false);
    CHECK(cpu.get_phys_timer().istatus == true); // istatus still true

    // Unmask timer (IMASK = false)
    t.imask = false;
    cpu.set_phys_timer(t);
    CHECK(cpu.get_virtual_time().get_phys_timer_irq(0) == true);

    std::cout << "[PASS] test_timer_imask_toggle\n";
}

// 21. Parallel Mode Counter Atomic Advance
void test_parallel_mode_counter_atomic() {
    winios::SMPCluster cluster(2, 0x20000);
    cluster.get_virtual_time().set_physical_counter(0);

    // Verify atomic increment across threads
    cluster.shared_counter_increment();
    cluster.shared_counter_increment();
    CHECK(cluster.shared_counter_load() == 2ULL);

    std::cout << "[PASS] test_parallel_mode_counter_atomic\n";
}

// 22. Instruction Stepping Time
void test_instruction_stepping_time() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);

    const std::uint64_t before = cpu.get_guest_counter();
    for (int i = 0; i < 50; ++i) {
        run_one(cpu, mem, enc_nop());
    }
    const std::uint64_t after = cpu.get_guest_counter();
    CHECK(after == before + 50ULL);

    std::cout << "[PASS] test_instruction_stepping_time\n";
}

// 23. TVAL Signed Negative Offset
void test_tval_signed_negative_overflow() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_architectural_exceptions(false);

    cpu.set_guest_counter(1000ULL);

    // Negative offset -100
    std::int32_t offset = -100;
    cpu.write_register(0, static_cast<std::uint64_t>(static_cast<std::uint32_t>(offset)));
    run_one(cpu, mem, enc_msr(3, 14, 2, 0, 0)); // MSR CNTP_TVAL_EL0, X0
    CHECK(cpu.get_phys_timer().cval == 900ULL);

    std::cout << "[PASS] test_tval_signed_negative_overflow\n";
}

// 24. Read-Only Counter MSR Exception
void test_read_only_counter_msr_exception() {
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000ULL);

    // MSR CNTPCT_EL0, X0 -> Undefined instruction
    run_one(cpu, mem, enc_msr(3, 14, 0, 1, 0));
    CHECK(cpu.get_program_counter() == 0x10200ULL); // Synch exception EL1h

    std::cout << "[PASS] test_read_only_counter_msr_exception\n";
}

// 25. Multicore Earliest Deadline Selection
void test_multicore_earliest_deadline_selection() {
    winios::VirtualTime vt(24000000ULL, 4);
    vt.set_physical_counter(1000ULL);

    // Core 0 deadline at 1500
    vt.set_phys_timer(0, winios::TimerState{true, false, false, 1500ULL});
    // Core 1 deadline at 1200
    vt.set_phys_timer(1, winios::TimerState{true, false, false, 1200ULL});
    // Core 2 deadline at 2000
    vt.set_phys_timer(2, winios::TimerState{true, false, false, 2000ULL});

    std::vector<bool> active_mask = {true, true, true, false};
    const std::uint64_t earliest = vt.get_earliest_future_deadline(active_mask);
    CHECK(earliest == 1200ULL);

    // Advance to earliest
    const bool advanced = vt.advance_to_earliest_deadline(active_mask);
    CHECK(advanced == true);
    CHECK(vt.get_physical_counter() == 1200ULL);
    CHECK(vt.get_phys_timer(1).istatus == true);
    CHECK(vt.get_phys_timer(0).istatus == false);

    std::cout << "[PASS] test_multicore_earliest_deadline_selection\n";
}

int main() {
    try {
        std::cout << "Running WinIOS Phase 50 Generic Timer and Virtual Time Tests..." << std::endl;

        test_physical_counter_monotonicity();
        test_virtual_counter_offset();
        test_el1_physical_timer_cval();
        test_el1_physical_timer_tval();
        test_el1_virtual_timer_cval_and_tval();
        test_el2_hypervisor_timer();
        test_gicv3_ppi_routing();
        test_cntkctl_el1_traps();
        test_cnthctl_el2_traps();
        test_smp_shared_counter();
        test_smp_independent_timers();
        test_wfi_idle_advancement();
        test_wfe_event_register();
        test_psci_cpu_on_lifecycle();
        test_psci_cpu_off_lifecycle();
        test_psci_cpu_suspend();
        test_psci_system_reset();
        test_pmu_independence();
        test_counter_wraparound();
        test_timer_imask_toggle();
        test_parallel_mode_counter_atomic();
        test_instruction_stepping_time();
        test_tval_signed_negative_overflow();
        test_read_only_counter_msr_exception();
        test_multicore_earliest_deadline_selection();

        std::cout << "All 25/25 Phase 50 Generic Timer tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
}
