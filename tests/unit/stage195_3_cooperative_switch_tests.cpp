// ============================================================================
// Stage 195.3: Cooperative Context-Switch Verification Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/context_switch.h"
#include "kernel/cpu_context.h"
#include "kernel/thread.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

static bool test_three_thread_cooperative_ring_guest_execution() {
    Memory mem(16 * 1024 * 1024);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL); // Enable FP/SIMD at EL1/EL0

    // 1. Load compiled Mach-O artifact
    const std::string bin_path = "D:/WinISO26/WinIOS/tests/artifacts/bin/guest_cooperative_3threads";
    const auto load_result = loader::MachOLoader::load_from_file(bin_path, mem, 0, mem.size());
    CHECK(load_result.is_success(), "Mach-O load_from_file must succeed");

    const std::uint64_t entry_point = load_result.entry_point;
    std::cout << "Loaded entry_point: 0x" << std::hex << entry_point << std::dec << std::endl;
    CHECK(entry_point != 0ULL, "Entry point must be non-zero");

    // 2. Set up 3 GuestTcbs and stacks in guest RAM:
    // TCB1 at 0x40010000, Stack1 at 0x40020000
    // TCB2 at 0x40010200, Stack2 at 0x40030000
    // TCB3 at 0x40010400, Stack3 at 0x40040000
    constexpr std::uint64_t kTcb1Addr = 0x40000ULL;
    constexpr std::uint64_t kTcb2Addr = 0x41000ULL;
    constexpr std::uint64_t kTcb3Addr = 0x42000ULL;
    constexpr std::uint64_t kStatusAddr = 0x43000ULL;

    constexpr std::uint64_t kStack1Top = 0x50000ULL;
    constexpr std::uint64_t kStack2Top = 0x60000ULL;
    constexpr std::uint64_t kStack3Top = 0x70000ULL;

    GuestTcb tcb1{};
    tcb1.tid = 1;
    tcb1.sp_el1 = kStack1Top;
    tcb1.pc = entry_point; // _start (0x13b0)
    tcb1.state = static_cast<std::uint32_t>(ThreadState::Running);

    GuestTcb tcb2{};
    tcb2.tid = 2;
    tcb2.sp_el1 = kStack2Top;
    tcb2.pc = 0x1480ULL; // _thread2_entry
    tcb2.state = static_cast<std::uint32_t>(ThreadState::Ready);

    GuestTcb tcb3{};
    tcb3.tid = 3;
    tcb3.sp_el1 = kStack3Top;
    tcb3.pc = 0x153CULL; // _thread3_entry
    tcb3.state = static_cast<std::uint32_t>(ThreadState::Ready);

    // Write TCBs to memory
    std::memcpy(mem.raw_data() + kTcb1Addr, &tcb1, sizeof(GuestTcb));
    std::memcpy(mem.raw_data() + kTcb2Addr, &tcb2, sizeof(GuestTcb));
    std::memcpy(mem.raw_data() + kTcb3Addr, &tcb3, sizeof(GuestTcb));

    // Clear status flags at kStatusAddr
    mem.write_u32(static_cast<std::size_t>(kStatusAddr + 0), 0);
    mem.write_u32(static_cast<std::size_t>(kStatusAddr + 4), 0);
    mem.write_u32(static_cast<std::size_t>(kStatusAddr + 8), 0);

    // 3. Initialize CPU for Thread 1 execution
    cpu.set_program_counter(entry_point);
    cpu.set_sp_el1(kStack1Top);

    // Step CPU until Thread 1 marks completion or step limit reached
    std::size_t steps = 0;
    constexpr std::size_t kMaxSteps = 300;
    while (steps < kMaxSteps) {
        cpu.step(mem);
        ++steps;
        if (mem.read_u32(static_cast<std::size_t>(kStatusAddr + 0)) == 1 &&
            mem.read_u32(static_cast<std::size_t>(kStatusAddr + 4)) == 1 &&
            mem.read_u32(static_cast<std::size_t>(kStatusAddr + 8)) == 1) {
            break;
        }
    }

    CHECK(mem.read_u32(static_cast<std::size_t>(kStatusAddr + 0)) == 1u, "Thread 1 stack canary and register integrity pass");
    CHECK(mem.read_u32(static_cast<std::size_t>(kStatusAddr + 4)) == 1u, "Thread 2 stack canary and register integrity pass");
    CHECK(mem.read_u32(static_cast<std::size_t>(kStatusAddr + 8)) == 1u, "Thread 3 stack canary and register integrity pass");
    CHECK(steps > 20u, "Step count must be meaningful");

    std::cout << "[PASS] test_three_thread_cooperative_ring_guest_execution (retired " << steps << " instructions)" << std::endl;
    return true;
}

static bool test_host_thread_manager_cooperative_switching() {
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    KernelThreadManager mgr;
    auto* t1 = mgr.create_thread("task1", 0x1000, 0x111, 0x50000, 0x4000, 16);
    auto* t2 = mgr.create_thread("task2", 0x2000, 0x222, 0x60000, 0x4000, 16);
    auto* t3 = mgr.create_thread("task3", 0x3000, 0x333, 0x70000, 0x4000, 16);

    CHECK(t1 != nullptr && t2 != nullptr && t3 != nullptr, "Threads created successfully");

    // Set unique registers for task 1
    cpu.write_register(19, 0xAAAA);
    cpu.write_register(20, 0xBBBB);
    cpu.set_program_counter(0x1020);
    t1->set_state(ThreadState::Running);

    // Switch task 1 -> task 2
    CHECK(ContextSwitch::switch_threads(t1, t2, cpu), "Switch t1 -> t2");
    CHECK(t1->state() == ThreadState::Ready, "t1 state Ready");
    CHECK(t2->state() == ThreadState::Running, "t2 state Running");
    CHECK(cpu.get_program_counter() == 0x2000, "PC is t2 entry");

    // Modify registers in task 2
    cpu.write_register(19, 0xCCCC);
    cpu.write_register(20, 0xDDDD);
    cpu.set_program_counter(0x2040);

    // Switch task 2 -> task 3
    CHECK(ContextSwitch::switch_threads(t2, t3, cpu), "Switch t2 -> t3");
    CHECK(t2->state() == ThreadState::Ready, "t2 state Ready");
    CHECK(t3->state() == ThreadState::Running, "t3 state Running");
    CHECK(cpu.get_program_counter() == 0x3000, "PC is t3 entry");

    // Switch task 3 -> task 1
    CHECK(ContextSwitch::switch_threads(t3, t1, cpu), "Switch t3 -> t1");
    CHECK(t3->state() == ThreadState::Ready, "t3 state Ready");
    CHECK(t1->state() == ThreadState::Running, "t1 state Running");
    CHECK(cpu.get_program_counter() == 0x1020, "PC restored to t1");
    CHECK(cpu.read_register(19) == 0xAAAA, "X19 restored for t1");
    CHECK(cpu.read_register(20) == 0xBBBB, "X20 restored for t1");

    std::cout << "[PASS] test_host_thread_manager_cooperative_switching" << std::endl;
    return true;
}

static bool test_architectural_context_capture_and_restore() {
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(0x00300000ULL); // Enable FP/SIMD access

    // Initialize arbitrary live state across all GPRs, SIMD, flags, TLS
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1000000000000000ULL + i * 0x1111ULL);
    }
    cpu.set_program_counter(0x80004000ULL);
    cpu.set_sp_el0(0x70002000ULL);
    cpu.set_sp_el1(0x60001000ULL);
    cpu.set_nzcv(0x5); // N=0, Z=1, C=0, V=1
    cpu.set_daif(0x2); // IRQ unmasked
    cpu.set_tpidr_el0(0xCAFED00DULL);
    cpu.set_tpidr_el1(0xDEADBEEFULL);
    cpu.set_ttbr0_el1(0x0001000040000000ULL); // ASID = 1
    cpu.set_fpcr(0x01000000);
    cpu.set_fpsr(0x00000008);

    for (std::size_t i = 0; i < 32; ++i) {
        cpu.write_vector_register(i, 0xAAAA0000ULL + i, 0xBBBB0000ULL + i);
    }

    ArchitecturalCpuContext ctx1{};
    ctx1.capture(cpu);

    // Corrupt CPU state with task 2 values
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xFFFFFFFFFFFFFFFFULL);
    }
    cpu.set_program_counter(0x1111ULL);
    cpu.set_nzcv(0xA);
    for (std::size_t i = 0; i < 32; ++i) {
        cpu.write_vector_register(i, 0, 0);
    }

    // Restore ctx1 and verify 100% preservation
    ctx1.restore(cpu);

    ArchitecturalCpuContext ctx2{};
    ctx2.capture(cpu);

    CHECK(ctx1.equals(ctx2), "ArchitecturalCpuContext must equal original after restore");

    // Test writing and reading exception frame in memory
    Memory mem(1024 * 1024);
    constexpr std::uint64_t kFrameAddr = 0x2000ULL;
    CHECK(ctx1.write_frame_to_memory(mem, kFrameAddr), "Write exception frame to RAM");

    ArchitecturalCpuContext ctx3{};
    CHECK(ctx3.read_frame_from_memory(mem, kFrameAddr), "Read exception frame from RAM");
    CHECK(ctx1.equals(ctx3), "ExceptionFrame in RAM must match original context");

    std::cout << "[PASS] test_architectural_context_capture_and_restore" << std::endl;
    return true;
}

int main() {
    std::cout << "Running Stage 195.3 Cooperative Context-Switch Verification Tests..." << std::endl;

    try {
        std::cout << "Starting test 1..." << std::endl;
        if (!test_three_thread_cooperative_ring_guest_execution()) {
            std::cout << "test_three_thread_cooperative_ring_guest_execution failed!" << std::endl;
            return 1;
        }
        std::cout << "Starting test 2..." << std::endl;
        if (!test_host_thread_manager_cooperative_switching()) {
            std::cout << "test_host_thread_manager_cooperative_switching failed!" << std::endl;
            return 1;
        }
        std::cout << "Starting test 3..." << std::endl;
        if (!test_architectural_context_capture_and_restore()) {
            std::cout << "test_architectural_context_capture_and_restore failed!" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "Caught unknown exception!" << std::endl;
        return 1;
    }

    std::cout << "\nAll Stage 195.3 tests PASSED successfully." << std::endl;
    return 0;
}
