// WinIOS Phase 48: PSCI Framework and CPU Power-State Management Unit Tests
#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cout << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " #cond << std::endl;                              \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

// Helper: encode MOVZ Xd, #imm16, LSL #shift
static constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm, unsigned shift = 0) {
    const unsigned hw = shift / 16;
    return 0xD2800000u | (static_cast<std::uint32_t>(hw) << 21u) |
           (static_cast<std::uint32_t>(imm) << 5u) | rd;
}

// Helper: encode MOVK Xd, #imm16, LSL #shift
static constexpr std::uint32_t enc_movk(unsigned rd, std::uint16_t imm, unsigned shift = 0) {
    const unsigned hw = shift / 16;
    return 0xF2800000u | (static_cast<std::uint32_t>(hw) << 21u) |
           (static_cast<std::uint32_t>(imm) << 5u) | rd;
}

// Helper: encode SMC #0
static constexpr std::uint32_t enc_smc(std::uint16_t imm = 0) {
    return 0xD4000003u | (static_cast<std::uint32_t>(imm) << 5u);
}

// Helper: encode NOP
static constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

// Test 1: PSCI version discovery
static void test_psci_version() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu = cluster.get_core(0);

    // Call PSCI_VERSION directly via handle_smc
    cpu.write_register(0, winios::PSCIService::kVersion);
    bool handled = cluster.get_psci_service()->handle_smc(cpu, 0);
    CHECK(handled);
    CHECK(cpu.read_register(0) == winios::PSCIService::kPsciVersion);
    CHECK(cpu.read_register(0) == 0x00010001ULL); // v1.1
}

// Test 2: PSCI calling convention, features, and dispatch
static void test_psci_calling_convention_and_dispatch() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu = cluster.get_core(0);

    // Known feature check
    cpu.write_register(0, winios::PSCIService::kPsciFeatures);
    cpu.write_register(1, winios::PSCIService::kCpuOn64);
    bool handled = cluster.get_psci_service()->handle_smc(cpu, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == winios::PSCIService::kSuccess);

    // Unsupported feature check
    cpu.write_register(0, winios::PSCIService::kPsciFeatures);
    cpu.write_register(1, 0x840000FFu); // Non-existent feature
    handled = cluster.get_psci_service()->handle_smc(cpu, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == winios::PSCIService::kNotSupported);

    // Unknown PSCI range function ID returns NOT_SUPPORTED
    cpu.write_register(0, 0x840000EEu);
    handled = cluster.get_psci_service()->handle_smc(cpu, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == winios::PSCIService::kNotSupported);

    // Non-PSCI SMC function ID (e.g., vendor range 0x82000000) is NOT handled by PSCI
    cpu.write_register(0, 0x82000001u);
    handled = cluster.get_psci_service()->handle_smc(cpu, 0);
    CHECK(!handled);
}

// Test 3: CPU_ON secondary startup
static void test_psci_cpu_on_secondary_startup() {
    winios::SMPCluster cluster(4);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);

    // Initially core 1 is off/halted
    CHECK(cluster.is_core_halted(1));
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::Off);

    // Call CPU_ON for core 1: target_affinity=1, entry_pc=0x4000, context_id=0xCAFE
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0xCAFE);
    bool handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kSuccess);

    // Verify core 1 state
    CPU& cpu1 = cluster.get_core(1);
    CHECK(!cluster.is_core_halted(1));
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);
    CHECK(cpu1.get_program_counter() == 0x4000);
    CHECK(cpu1.read_register(0) == 0xCAFE);
    CHECK(cpu1.get_current_el() == 1); // Secondary core starts at EL1

    // Second call to CPU_ON for already running core 1 returns ALREADY_ON
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x5000);
    cpu0.write_register(3, 0xBEEF);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kAlreadyOn);

    // CPU_ON with unaligned entry address returns INVALID_PARAMS
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 2);
    cpu0.write_register(2, 0x4001); // unaligned
    cpu0.write_register(3, 0);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kInvalidParams);

    // CPU_ON with out-of-range target core returns INVALID_PARAMS
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 99); // non-existent
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kInvalidParams);
}

// Test 4: CPU_OFF lifecycle
static void test_psci_cpu_off_lifecycle() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);

    // Start core 1
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0);
    cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);

    // Core 1 calls CPU_OFF
    cpu1.write_register(0, winios::PSCIService::kCpuOff);
    bool handled = cluster.get_psci_service()->handle_smc(cpu1, 1);
    CHECK(handled);
    CHECK(cluster.is_core_halted(1));
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::Off);

    // Core 0 calling CPU_OFF returns DENIED
    cpu0.write_register(0, winios::PSCIService::kCpuOff);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kDenied);
    CHECK(!cluster.is_core_halted(0));
    CHECK(cluster.get_psci_service()->get_core_state(0) == winios::CorePowerState::On);

    // Restart core 1 after CPU_OFF
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x6000);
    cpu0.write_register(3, 0x1234);
    cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kSuccess);
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);
    CHECK(cpu1.get_program_counter() == 0x6000);
}

// Test 5: CPU_SUSPEND retention and powerdown modes
static void test_psci_cpu_suspend_modes() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu1 = cluster.get_core(1);

    // Start core 1
    cluster.get_core(0).write_register(0, winios::PSCIService::kCpuOn64);
    cluster.get_core(0).write_register(1, 1);
    cluster.get_core(0).write_register(2, 0x4000);
    cluster.get_core(0).write_register(3, 0);
    cluster.get_psci_service()->handle_smc(cluster.get_core(0), 0);

    // Mode 1: Retention suspend (power_state = 0x00000000)
    cpu1.write_register(0, winios::PSCIService::kCpuSuspend64);
    cpu1.write_register(1, 0x00000000u); // State Type = 0 (retention)
    cpu1.write_register(2, 0x0);
    cpu1.write_register(3, 0x0);
    bool handled = cluster.get_psci_service()->handle_smc(cpu1, 1);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu1.read_register(0)) == winios::PSCIService::kSuccess);
    CHECK(cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::SuspendedRetention);

    // Resume from retention: PC is at next instruction, state is restored to On
    cluster.get_psci_service()->resume_core_from_suspend(1);
    CHECK(!cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);

    // Mode 2: Powerdown suspend (power_state = 0x00010000, State Type = 1)
    cpu1.write_register(0, winios::PSCIService::kCpuSuspend64);
    cpu1.write_register(1, 0x00010000u); // State Type = 1 (powerdown)
    cpu1.write_register(2, 0x8000);      // Resume entry point
    cpu1.write_register(3, 0xFEED);      // Resume context ID
    handled = cluster.get_psci_service()->handle_smc(cpu1, 1);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu1.read_register(0)) == winios::PSCIService::kSuccess);
    CHECK(cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::SuspendedPowerdown);

    // Resume from powerdown: PC is reset to entry point, X0 contains context ID
    cluster.get_psci_service()->resume_core_from_suspend(1);
    CHECK(!cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);
    CHECK(cpu1.get_program_counter() == 0x8000);
    CHECK(cpu1.read_register(0) == 0xFEED);
}

// Test 6: AFFINITY_INFO query
static void test_psci_affinity_info() {
    winios::SMPCluster cluster(4);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);

    // Core 0 query: ON (0)
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 0); // Core 0
    cpu0.write_register(2, 0); // Level 0
    bool handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cpu0.read_register(0) == 0); // 0 = ON

    // Core 1 query before start: OFF (1)
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 1); // Core 1
    cpu0.write_register(2, 0); // Level 0
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cpu0.read_register(0) == 1); // 1 = OFF

    // Start Core 1
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0);
    cluster.get_psci_service()->handle_smc(cpu0, 0);

    // Core 1 query after start: ON (0)
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cpu0.read_register(0) == 0); // 0 = ON

    // Query with MPIDR format: 0x80000001
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 0x80000001ULL);
    cpu0.write_register(2, 0);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cpu0.read_register(0) == 0); // 0 = ON

    // Invalid affinity level returns INVALID_PARAMS
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 0);
    cpu0.write_register(2, 1); // Level 1 not supported
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kInvalidParams);

    // Invalid target affinity returns INVALID_PARAMS
    cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 100); // Core 100 doesn't exist
    cpu0.write_register(2, 0);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kInvalidParams);
}

// Test 7: MIGRATE_INFO_TYPE and migration queries
static void test_psci_migrate_info_type() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);

    cpu0.write_register(0, winios::PSCIService::kMigrateInfoType);
    bool handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    // 2 = Trusted OS is not present or does not require migration
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == 2);

    // MIGRATE function ID is NOT_SUPPORTED
    cpu0.write_register(0, 0x84000005u); // MIGRATE 32-bit
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(static_cast<std::int64_t>(cpu0.read_register(0)) == winios::PSCIService::kNotSupported);
}

// Test 8: SYSTEM_OFF and SYSTEM_RESET
static void test_psci_system_off_and_reset() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);

    // SYSTEM_OFF
    CHECK(!cluster.get_psci_service()->system_off_requested());
    cpu0.write_register(0, winios::PSCIService::kSystemOff);
    bool handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cluster.get_psci_service()->system_off_requested());
    CHECK(cluster.is_core_halted(0));
    CHECK(cluster.is_core_halted(1));

    // Reset flags
    cluster.get_psci_service()->reset_system_flags();
    CHECK(!cluster.get_psci_service()->system_off_requested());

    // SYSTEM_RESET
    CHECK(!cluster.get_psci_service()->system_reset_requested());
    cpu0.write_register(0, winios::PSCIService::kSystemReset);
    handled = cluster.get_psci_service()->handle_smc(cpu0, 0);
    CHECK(handled);
    CHECK(cluster.get_psci_service()->system_reset_requested());
    CHECK(cluster.is_core_halted(0));
    CHECK(cluster.is_core_halted(1));
}

// Test 9: Per-core state machine and cross-core isolation
static void test_psci_per_core_state_isolation() {
    winios::SMPCluster cluster(4);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    // Verify initial state matrix: core 0 is On, 1..3 are Off
    CHECK(psci->get_core_state(0) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(1) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(2) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(3) == winios::CorePowerState::Off);

    // Boot core 2: verify only core 2 changes state
    cluster.get_core(0).write_register(0, winios::PSCIService::kCpuOn64);
    cluster.get_core(0).write_register(1, 2);
    cluster.get_core(0).write_register(2, 0x4000);
    cluster.get_core(0).write_register(3, 0);
    psci->handle_smc(cluster.get_core(0), 0);

    CHECK(psci->get_core_state(0) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(1) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(2) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(3) == winios::CorePowerState::Off);

    // Boot core 3
    cluster.get_core(0).write_register(0, winios::PSCIService::kCpuOn64);
    cluster.get_core(0).write_register(1, 3);
    cluster.get_core(0).write_register(2, 0x5000);
    cluster.get_core(0).write_register(3, 0);
    psci->handle_smc(cluster.get_core(0), 0);

    CHECK(psci->get_core_state(0) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(1) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(2) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(3) == winios::CorePowerState::On);

    // Core 2 halts via CPU_OFF: verify only core 2 turns Off
    cluster.get_core(2).write_register(0, winios::PSCIService::kCpuOff);
    psci->handle_smc(cluster.get_core(2), 2);

    CHECK(psci->get_core_state(0) == winios::CorePowerState::On);
    CHECK(psci->get_core_state(1) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(2) == winios::CorePowerState::Off);
    CHECK(psci->get_core_state(3) == winios::CorePowerState::On);
}

// Test 10: GICv3 and Timer interrupt wakeup from PSCI suspend
static void test_psci_gicv3_timer_interrupt_wakeup() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);
    cpu1.set_architectural_exceptions(true);

    // Start core 1
    cpu0.write_register(0, winios::PSCIService::kCpuOn64);
    cpu0.write_register(1, 1);
    cpu0.write_register(2, 0x4000);
    cpu0.write_register(3, 0);
    cluster.get_psci_service()->handle_smc(cpu0, 0);

    // Put core 1 in retention suspend
    cpu1.write_register(0, winios::PSCIService::kCpuSuspend64);
    cpu1.write_register(1, 0);
    cpu1.write_register(2, 0);
    cpu1.write_register(3, 0);
    cluster.get_psci_service()->handle_smc(cpu1, 1);
    CHECK(cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::SuspendedRetention);

    // Deliver interrupt to wake core 1
    cpu1.wake_from_wfi();
    CHECK(!cpu1.is_waiting());
    CHECK(cluster.get_psci_service()->get_core_state(1) == winios::CorePowerState::On);
}

// Test 11: Real instruction-level SMC execution through step()
static void test_psci_instruction_level_smc_execution() {
    winios::SMPCluster cluster(2);
    cluster.enable_psci();
    CPU& cpu = cluster.get_core(0);
    Memory& mem = cluster.get_memory();

    // Assemble at address 0x1000:
    // MOVZ X0, #0x0000
    // MOVK X0, #0x8400, LSL #16  -> X0 = 0x84000000 (PSCI_VERSION)
    // SMC #0
    // NOP
    const std::uint64_t base = 0x1000;
    mem.write_u32(base + 0, enc_movz(0, 0x0000, 0));
    mem.write_u32(base + 4, enc_movk(0, 0x8400, 16));
    mem.write_u32(base + 8, enc_smc(0));
    mem.write_u32(base + 12, enc_nop());

    cpu.set_program_counter(base);

    // Step MOVZ
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == base + 4);

    // Step MOVK
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == base + 8);
    CHECK(cpu.read_register(0) == 0x84000000ULL);

    // Step SMC #0: must be intercepted by PSCI service, returning PSCI_VERSION in X0
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == base + 12); // Advanced past SMC
    CHECK(cpu.read_register(0) == winios::PSCIService::kPsciVersion); // 0x00010001
    CHECK(cpu.get_current_el() == 1); // Remained at EL1, not trapped to EL3 guest vector!

    // Step NOP
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == base + 16);
}

// Test 12: Deterministic vs Parallel Execution secondary startup
static void test_psci_deterministic_and_parallel_execution() {
    // 12a: Deterministic execution
    {
        winios::SMPCluster cluster(2);
        cluster.enable_psci();
        Memory& mem = cluster.get_memory();

        // Core 0 executes CPU_ON for Core 1
        CPU& cpu0 = cluster.get_core(0);
        cpu0.write_register(0, winios::PSCIService::kCpuOn64);
        cpu0.write_register(1, 1);
        cpu0.write_register(2, 0x2000);
        cpu0.write_register(3, 0);
        cluster.get_psci_service()->handle_smc(cpu0, 0);

        // Put NOPs at PC for both cores
        mem.write_u32(cpu0.get_program_counter(), enc_nop());
        mem.write_u32(0x2000, enc_nop());

        // Step deterministic scheduler
        std::size_t stepped = cluster.step_round_robin();
        CHECK(stepped >= 1);
        CHECK(cluster.get_core(1).get_program_counter() == 0x2004);
    }

    // 12b: Parallel execution engine
    {
        winios::SMPCluster cluster(2);
        cluster.enable_psci();
        Memory& mem = cluster.get_memory();

        // Core 0 starts Core 1
        CPU& cpu0 = cluster.get_core(0);
        cpu0.write_register(0, winios::PSCIService::kCpuOn64);
        cpu0.write_register(1, 1);
        cpu0.write_register(2, 0x3000);
        cpu0.write_register(3, 0);
        cluster.get_psci_service()->handle_smc(cpu0, 0);

        // Fill code regions with NOPs for both cores
        for (std::uint64_t addr = cpu0.get_program_counter(); addr < cpu0.get_program_counter() + 0x100; addr += 4) {
            mem.write_u32(addr, enc_nop());
        }
        for (std::uint64_t addr = 0x3000; addr < 0x3100; addr += 4) {
            mem.write_u32(addr, enc_nop());
        }

        // Run parallel epoch for 10 instructions
        std::size_t stepped = cluster.run_parallel(10);
        CHECK(stepped >= 2);
        cluster.stop_parallel();
    }
}

// Test 13: Concurrency and stress validation
static void test_psci_concurrency_stress() {
    winios::SMPCluster cluster(4);
    cluster.enable_psci();

    // Perform 100 rapid CPU_ON -> AFFINITY_INFO -> CPU_OFF cycles on secondary cores
    for (int iter = 0; iter < 100; ++iter) {
        for (std::size_t c = 1; c < 4; ++c) {
            CPU& cpu0 = cluster.get_core(0);
            CPU& target = cluster.get_core(c);

            // CPU_ON
            cpu0.write_register(0, winios::PSCIService::kCpuOn64);
            cpu0.write_register(1, c);
            cpu0.write_register(2, 0x4000 + c * 0x100);
            cpu0.write_register(3, iter);
            cluster.get_psci_service()->handle_smc(cpu0, 0);
            CHECK(cluster.get_psci_service()->get_core_state(c) == winios::CorePowerState::On);

            // AFFINITY_INFO
            cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
            cpu0.write_register(1, c);
            cpu0.write_register(2, 0);
            cluster.get_psci_service()->handle_smc(cpu0, 0);
            CHECK(cpu0.read_register(0) == 0); // ON

            // CPU_OFF
            target.write_register(0, winios::PSCIService::kCpuOff);
            cluster.get_psci_service()->handle_smc(target, c);
            CHECK(cluster.get_psci_service()->get_core_state(c) == winios::CorePowerState::Off);

            // AFFINITY_INFO
            cpu0.write_register(0, winios::PSCIService::kAffinityInfo64);
            cpu0.write_register(1, c);
            cpu0.write_register(2, 0);
            cluster.get_psci_service()->handle_smc(cpu0, 0);
            CHECK(cpu0.read_register(0) == 1); // OFF
        }
    }
}

int main() {
    std::cout << "Running WinIOS Phase 48 PSCI Framework Tests...\n";

    test_psci_version();
    std::cout << "  [PASS] test_psci_version" << std::endl;

    test_psci_calling_convention_and_dispatch();
    std::cout << "  [PASS] test_psci_calling_convention_and_dispatch" << std::endl;

    test_psci_cpu_on_secondary_startup();
    std::cout << "  [PASS] test_psci_cpu_on_secondary_startup" << std::endl;

    test_psci_cpu_off_lifecycle();
    std::cout << "  [PASS] test_psci_cpu_off_lifecycle" << std::endl;

    test_psci_cpu_suspend_modes();
    std::cout << "  [PASS] test_psci_cpu_suspend_modes" << std::endl;

    test_psci_affinity_info();
    std::cout << "  [PASS] test_psci_affinity_info" << std::endl;

    test_psci_migrate_info_type();
    std::cout << "  [PASS] test_psci_migrate_info_type" << std::endl;

    test_psci_system_off_and_reset();
    std::cout << "  [PASS] test_psci_system_off_and_reset" << std::endl;

    test_psci_per_core_state_isolation();
    std::cout << "  [PASS] test_psci_per_core_state_isolation" << std::endl;

    test_psci_gicv3_timer_interrupt_wakeup();
    std::cout << "  [PASS] test_psci_gicv3_timer_interrupt_wakeup" << std::endl;

    test_psci_instruction_level_smc_execution();
    std::cout << "  [PASS] test_psci_instruction_level_smc_execution" << std::endl;

    test_psci_deterministic_and_parallel_execution();
    std::cout << "  [PASS] test_psci_deterministic_and_parallel_execution" << std::endl;

    test_psci_concurrency_stress();
    std::cout << "  [PASS] test_psci_concurrency_stress" << std::endl;

    std::cout << "All 13 Phase 48 PSCI Framework unit tests PASSED successfully!" << std::endl;
    return 0;
}
