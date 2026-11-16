#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "snapshot/smp_snapshot_manager.h"

using namespace winios;
using namespace winios::snapshot;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_boot_pre_handoff_snapshot_and_replay() {
    std::cout << "[Test 1] Kernel Boot Pre-Handoff Snapshot and Bit-Exact Replay..." << std::endl;

    constexpr std::size_t kCores = 6;
    constexpr std::size_t kRamSize = 1024 * 1024 * 4; // 4 MB RAM
    auto mem = std::make_shared<SmpSharedMemory>(kRamSize, kCores);
    SmpScheduler sched(mem, kCores, 25);

    constexpr std::uint64_t kEntryPC = 0x1000ULL;
    constexpr std::uint64_t kBootArgsPA = 0x40000ULL;

    // Early Kernel Code at 0x1000:
    // Insn 0: MSR DAIFSet, #0xF (0xD5034FDF)
    // Insn 1: MOV X19, X0       (0xAA0003F3)
    // Insn 2: ADD X2, X2, #1    (0x91000442)
    // Insn 3: STR X2, [X19]     (0xF9000262)
    mem->raw_memory().write_u32(0x1000, 0xD5034FDFU);
    mem->raw_memory().write_u32(0x1004, 0xAA0003F3U);
    mem->raw_memory().write_u32(0x1008, 0x91000442U);
    mem->raw_memory().write_u32(0x100C, 0xF9000262U);

    // Register cores with scheduler FIRST
    auto cpu0 = std::make_shared<CPU>();
    sched.register_core(0, cpu0, CoreLifecycleState::Runnable);

    for (std::size_t c = 1; c < kCores; ++c) {
        auto cpux = std::make_shared<CPU>();
        sched.register_core(c, cpux, CoreLifecycleState::Halted);
    }

    // Configure Core 0 post-registration
    cpu0->set_core_id(0);
    cpu0->set_current_el(1);
    cpu0->set_sp_sel(true);
    cpu0->set_stack_pointer(0x20000);
    cpu0->set_program_counter(kEntryPC);
    cpu0->write_register(0, kBootArgsPA);
    cpu0->write_register(19, kBootArgsPA);
    cpu0->set_vbar_el1(0x8000);
    sched.get_slot(0).state.capture_from_cpu(*cpu0);

    // 1. Capture Pre-Handoff Snapshot
    std::vector<std::uint8_t> snap_buffer;
    SmpSnapshotCaptureOptions opt;
    opt.description = "WinIOS Kernel Boot Pre-Handoff Snapshot";
    opt.cluster_topology = "Apple A16 Bionic 6-Core (2P+4E)";

    const auto cap_res = SmpSnapshotManager::capture_to_buffer(sched, snap_buffer, opt);
    TEST_CHECK(cap_res.success, "Pre-handoff snapshot capture must succeed");
    TEST_CHECK(!snap_buffer.empty(), "Snapshot buffer must not be empty");

    const auto val_res = SmpSnapshotManager::validate_buffer(snap_buffer.data(), snap_buffer.size());
    TEST_CHECK(val_res.valid, "Snapshot buffer validation must pass");

    // 2. Execute Pass 1 on Core 0 (4 instructions)
    for (int i = 0; i < 4; ++i) {
        cpu0->step(mem->raw_memory());
    }

    const std::uint64_t pass1_pc = cpu0->get_program_counter();
    const std::uint64_t pass1_x19 = cpu0->read_register(19);
    const std::uint64_t pass1_x2 = cpu0->read_register(2);
    const std::uint64_t pass1_mem_val = mem->raw_memory().read_u64(static_cast<std::size_t>(kBootArgsPA));

    TEST_CHECK(pass1_pc == 0x1010, "Pass 1 PC must advance to 0x1010");
    TEST_CHECK(pass1_x19 == kBootArgsPA, "Pass 1 X19 must retain boot_args PA");
    TEST_CHECK(pass1_x2 == 1, "Pass 1 X2 must be incremented to 1");
    TEST_CHECK(pass1_mem_val == 1, "Pass 1 memory write must store 1 to boot_args PA");

    // 3. Restore back to Pre-Handoff Snapshot
    const auto rest_res = SmpSnapshotManager::restore_from_buffer(snap_buffer.data(), snap_buffer.size(), sched);
    TEST_CHECK(rest_res.success, "Snapshot restoration must succeed");

    // Verify restored state before re-running
    const auto& restored_cpu0 = *sched.get_slot(0).cpu;
    TEST_CHECK(restored_cpu0.get_program_counter() == kEntryPC, "Restored PC must be at entry point");
    TEST_CHECK(restored_cpu0.read_register(19) == kBootArgsPA, "Restored X19 must be boot_args PA");
    TEST_CHECK(restored_cpu0.read_register(2) == 0, "Restored X2 must be reset to 0");
    TEST_CHECK(mem->raw_memory().read_u64(static_cast<std::size_t>(kBootArgsPA)) == 0, "Restored memory must be reset to 0");

    // 4. Replay Pass 2
    for (int i = 0; i < 4; ++i) {
        sched.get_slot(0).cpu->step(mem->raw_memory());
    }

    TEST_CHECK(restored_cpu0.get_program_counter() == pass1_pc, "Pass 2 PC must match Pass 1 bit-for-bit");
    TEST_CHECK(restored_cpu0.read_register(19) == pass1_x19, "Pass 2 X19 must match Pass 1 bit-for-bit");
    TEST_CHECK(restored_cpu0.read_register(2) == pass1_x2, "Pass 2 X2 must match Pass 1 bit-for-bit");
    TEST_CHECK(mem->raw_memory().read_u64(static_cast<std::size_t>(kBootArgsPA)) == pass1_mem_val,
               "Pass 2 memory write must match Pass 1 bit-for-bit");
}

void test_kernel_boot_exception_boundary_snapshot_and_restore() {
    std::cout << "[Test 2] Kernel Exception Boundary Snapshot & Restore..." << std::endl;

    constexpr std::size_t kCores = 6;
    constexpr std::size_t kRamSize = 1024 * 1024 * 4;
    auto mem = std::make_shared<SmpSharedMemory>(kRamSize, kCores);
    SmpScheduler sched(mem, kCores, 25);

    constexpr std::uint64_t kVbarBase = 0x8000ULL;
    constexpr std::uint64_t kFaultAddr = 0x23B100000ULL; // AIC MMIO out of RAM

    // Main code at 0x1000:
    // Insn 0: LDR X26, [X27]             (0xF940037AU) -> Faults on unmapped PA!
    // Handler at VBAR + 0x200 (0x8200):
    // Insn 0: MRS X28, ESR_EL1           (0xD538521CU)
    // Insn 1: MRS X29, FAR_EL1           (0xD538601DU)
    // Insn 2: MOVZ X30, #0xB007          (0xD29600FEU)
    mem->raw_memory().write_u32(0x1000, 0xF940037AU);

    mem->raw_memory().write_u32(0x8200, 0xD538521CU);
    mem->raw_memory().write_u32(0x8204, 0xD538601DU);
    mem->raw_memory().write_u32(0x8208, 0xD29600FEU);

    auto cpu0 = std::make_shared<CPU>();
    sched.register_core(0, cpu0, CoreLifecycleState::Runnable);

    for (std::size_t c = 1; c < kCores; ++c) {
        auto cpux = std::make_shared<CPU>();
        sched.register_core(c, cpux, CoreLifecycleState::Halted);
    }

    // Configure Core 0 post-registration
    cpu0->set_core_id(0);
    cpu0->set_architectural_exceptions(true);
    cpu0->set_current_el(1);
    cpu0->set_sp_sel(true);
    cpu0->set_stack_pointer(0x20000);
    cpu0->set_program_counter(0x1000);
    cpu0->set_vbar_el1(kVbarBase);
    cpu0->write_register(27, kFaultAddr); // Pre-load fault address in X27
    sched.get_slot(0).state.capture_from_cpu(*cpu0);

    // Step 1: LDR X26, [X27] -> Abort! PC redirects to 0x8200
    cpu0->step(mem->raw_memory());
    TEST_CHECK(cpu0->get_program_counter() == 0x8200, "PC must vector to 0x8200 on abort");

    // Step 2: MRS X28, ESR_EL1
    cpu0->step(mem->raw_memory());
    // Step 3: MRS X29, FAR_EL1
    cpu0->step(mem->raw_memory());
    // Step 4: MOVZ X30, #0xB007
    cpu0->step(mem->raw_memory());

    TEST_CHECK(cpu0->read_register(29) == kFaultAddr, "FAR_EL1 captured fault address");
    TEST_CHECK(cpu0->read_register(30) == 0xB007, "X30 set to magic 0xB007");

    // Capture Snapshot at Exception State
    std::vector<std::uint8_t> exc_snap;
    auto cap_exc = SmpSnapshotManager::capture_to_buffer(sched, exc_snap);
    TEST_CHECK(cap_exc.success, "Exception state snapshot capture must succeed");

    // Create fresh scheduler & restore exception state
    auto fresh_mem = std::make_shared<SmpSharedMemory>(kRamSize, kCores);
    SmpScheduler fresh_sched(fresh_mem, kCores, 25);
    for (std::size_t c = 0; c < kCores; ++c) {
        auto fresh_cpu = std::make_shared<CPU>();
        fresh_sched.register_core(c, fresh_cpu, CoreLifecycleState::Halted);
    }

    const auto rest_exc = SmpSnapshotManager::restore_from_buffer(exc_snap.data(), exc_snap.size(), fresh_sched);
    TEST_CHECK(rest_exc.success, "Exception state snapshot restore must succeed");

    const auto& restored_exc_cpu = *fresh_sched.get_slot(0).cpu;
    TEST_CHECK(restored_exc_cpu.get_current_el() == 1, "Restored EL must be 1");
    TEST_CHECK(restored_exc_cpu.get_program_counter() == 0x820C, "Restored PC must be 0x820C");
    TEST_CHECK(restored_exc_cpu.read_register(29) == kFaultAddr, "Restored FAR_EL1 must match fault address");
    TEST_CHECK(restored_exc_cpu.read_register(30) == 0xB007, "Restored X30 must match 0xB007");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.22 Kernel Snapshot and Replay Tests ===" << std::endl;

    test_kernel_boot_pre_handoff_snapshot_and_replay();
    test_kernel_boot_exception_boundary_snapshot_and_restore();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.22 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.22 Tests FAILED ===" << std::endl;
        return 1;
    }
}
