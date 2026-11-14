#include <iostream>
#include <vector>
#include <cstdint>
#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "kernel/sync.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

// Helper to write instructions
static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// -----------------------------------------------------------------------------
// Test D1: Spinlock Mutual Exclusion & Deadlock Detection (Acceptance Test D)
// -----------------------------------------------------------------------------
static bool test_spinlock_mutual_exclusion() {
    Memory mem(0x10000);
    constexpr std::uint64_t kLockAddr = 0x8000;
    KernelSpinlock lock(mem, kLockAddr);

    TEST_ASSERT(!lock.is_locked(), "Spinlock initially unlocked");
    TEST_ASSERT(lock.owner_tid() == 0, "Initial owner TID is 0");

    // Thread 100 acquires lock
    bool deadlock = false;
    bool acquired = lock.try_acquire_host(100, &deadlock);
    TEST_ASSERT(acquired, "Thread 100 acquired lock");
    TEST_ASSERT(!deadlock, "No deadlock on first acquire");
    TEST_ASSERT(lock.is_locked(), "Lock is now held");
    TEST_ASSERT(lock.owner_tid() == 100, "Owner TID is 100");

    // Thread 200 attempts to acquire lock (contention)
    bool acquired2 = lock.try_acquire_host(200, &deadlock);
    TEST_ASSERT(!acquired2, "Thread 200 blocked by existing holder");
    TEST_ASSERT(!deadlock, "No recursive deadlock between different threads");
    TEST_ASSERT(lock.contention_count() == 1, "Contention telemetry incremented");

    // Thread 100 attempts recursive acquisition -> Deadlock detected!
    bool acquired_rec = lock.try_acquire_host(100, &deadlock);
    TEST_ASSERT(!acquired_rec, "Recursive acquire rejected");
    TEST_ASSERT(deadlock, "Recursive acquire flagged as deadlock");

    // Thread 200 cannot release Thread 100's lock
    bool bad_release = lock.release_host(200);
    TEST_ASSERT(!bad_release, "Non-owner cannot release lock");
    TEST_ASSERT(lock.is_locked(), "Lock remains held");

    // Thread 100 releases lock
    bool released = lock.release_host(100);
    TEST_ASSERT(released, "Thread 100 released lock");
    TEST_ASSERT(!lock.is_locked(), "Lock is now free");
    TEST_ASSERT(lock.owner_tid() == 0, "Owner TID reset to 0");

    // Now Thread 200 can acquire lock
    bool acquired_after = lock.try_acquire_host(200, &deadlock);
    TEST_ASSERT(acquired_after, "Thread 200 acquired freed lock");
    TEST_ASSERT(lock.owner_tid() == 200, "Owner TID is now 200");
    lock.release_host(200);
    return true;
}

// -----------------------------------------------------------------------------
// Test D2: Guest-Executed Machine Code Spinlock Emitter
// Real ARM64 guest instructions (LDAXR/STLXR/STLR) executed by CPU
// -----------------------------------------------------------------------------
static bool test_guest_spinlock_machine_code() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);
    constexpr std::uint64_t kLockAddr = 0x8000;
    constexpr std::uint64_t kCodeAddr = 0x1000;

    mem.write_u32(kLockAddr, 0); // unlocked
    mem.write_u32(kLockAddr + 4, 0);

    // Emit guest spinlock acquire:
    auto acquire_code = KernelSpinlock::emit_guest_spinlock_acquire();
    for (std::size_t i = 0; i < acquire_code.size(); ++i) {
        write_u32(mem, kCodeAddr + i * 4, acquire_code[i]);
    }

    // Set parameters: X0 = kLockAddr, W1 = 0x42 (TID), LR = 0x2000
    cpu.write_register(0, kLockAddr);
    cpu.write_register(1, 0x42);
    cpu.write_register(30, 0x2000);
    cpu.set_program_counter(kCodeAddr);

    // Step through acquire function until RET (returns to 0x2000)
    while (cpu.get_program_counter() != 0x2000) {
        cpu.step(mem);
    }

    TEST_ASSERT(cpu.read_register(0) == 0, "Acquire returned status 0 (success)");
    TEST_ASSERT(mem.read_u32(kLockAddr) == 1, "Guest memory lock word is 1 (locked)");
    TEST_ASSERT(mem.read_u32(kLockAddr + 4) == 0x42, "Guest memory owner TID is 0x42");

    // Emit guest spinlock release:
    constexpr std::uint64_t kReleaseCodeAddr = 0x3000;
    auto release_code = KernelSpinlock::emit_guest_spinlock_release();
    for (std::size_t i = 0; i < release_code.size(); ++i) {
        write_u32(mem, kReleaseCodeAddr + i * 4, release_code[i]);
    }

    cpu.write_register(0, kLockAddr);
    cpu.write_register(30, 0x4000);
    cpu.set_program_counter(kReleaseCodeAddr);

    while (cpu.get_program_counter() != 0x4000) {
        cpu.step(mem);
    }

    TEST_ASSERT(mem.read_u32(kLockAddr) == 0, "Guest memory lock word is 0 (released)");
    TEST_ASSERT(mem.read_u32(kLockAddr + 4) == 0, "Guest memory owner TID cleared");
    return true;
}

// -----------------------------------------------------------------------------
// Test E1: IRQ-Safe Locking & DAIF Flag Restoration (Acceptance Test E)
// -----------------------------------------------------------------------------
static bool test_irq_safe_locking() {
    Memory mem(0x10000);
    CPU cpu;
    cpu.set_current_el(1);
    constexpr std::uint64_t kLockAddr = 0x8000;
    KernelSpinlock lock(mem, kLockAddr);

    // Initial state: interrupts enabled (DAIF.I = 0)
    cpu.set_daif(0x0u);
    TEST_ASSERT((cpu.get_daif() & 0x2u) == 0, "Interrupts initially unmasked");

    // 1. acquire_irqsave
    std::uint8_t saved_daif = 0;
    bool ok = lock.acquire_irqsave(cpu, 50, saved_daif);
    TEST_ASSERT(ok, "acquire_irqsave succeeded");
    TEST_ASSERT(saved_daif == 0x0u, "Saved DAIF was 0");
    TEST_ASSERT((cpu.get_daif() & 0x2u) != 0, "DAIF.I is now masked (IRQ disabled)");
    TEST_ASSERT(lock.is_locked(), "Lock is held");

    // 2. release_irqrestore
    bool rel = lock.release_irqrestore(cpu, 50, saved_daif);
    TEST_ASSERT(rel, "release_irqrestore succeeded");
    TEST_ASSERT(!lock.is_locked(), "Lock is released");
    TEST_ASSERT((cpu.get_daif() & 0x2u) == 0, "DAIF.I restored to unmasked state");

    // 3. Nested critical sections
    // Outer lock starts with interrupts enabled (DAIF=0)
    std::uint8_t outer_daif = 0;
    bool outer_ok = lock.acquire_irqsave(cpu, 50, outer_daif);
    TEST_ASSERT(outer_ok, "Outer lock acquired");
    TEST_ASSERT(outer_daif == 0x0u, "Outer saved DAIF = 0");
    TEST_ASSERT((cpu.get_daif() & 0x2u) != 0, "DAIF masked in outer critical section");

    // Create a second spinlock for inner critical section
    constexpr std::uint64_t kInnerLockAddr = 0x8020;
    KernelSpinlock inner_lock(mem, kInnerLockAddr);

    std::uint8_t inner_daif = 0;
    bool inner_ok = inner_lock.acquire_irqsave(cpu, 50, inner_daif);
    TEST_ASSERT(inner_ok, "Inner lock acquired");
    TEST_ASSERT((inner_daif & 0x2u) != 0, "Inner saw already-masked DAIF");

    // Release inner lock -> DAIF must REMAIN masked!
    inner_lock.release_irqrestore(cpu, 50, inner_daif);
    TEST_ASSERT((cpu.get_daif() & 0x2u) != 0, "DAIF remains masked while outer lock is still held");

    // Release outer lock -> DAIF is restored to unmasked!
    lock.release_irqrestore(cpu, 50, outer_daif);
    TEST_ASSERT((cpu.get_daif() & 0x2u) == 0, "DAIF unmasked after outermost lock released");

    // 4. RAII IrqSafeSpinlockGuard
    {
        IrqSafeSpinlockGuard guard(lock, cpu, 77);
        TEST_ASSERT(guard.is_acquired(), "Guard acquired lock");
        TEST_ASSERT((cpu.get_daif() & 0x2u) != 0, "Guard masked IRQ");
        TEST_ASSERT(lock.is_locked(), "Lock held inside guard scope");
    }
    TEST_ASSERT(!lock.is_locked(), "Guard released lock upon scope exit");
    TEST_ASSERT((cpu.get_daif() & 0x2u) == 0, "Guard restored unmasked IRQ upon scope exit");
    return true;
}

int main() {
    std::cout << "[STAGE 196.7 & 196.8] Running Kernel Spinlock & IRQ-Safe Locking Tests (Acceptance Tests D & E)..." << std::endl;

    if (!test_spinlock_mutual_exclusion()) {
        std::cerr << "test_spinlock_mutual_exclusion FAILED" << std::endl;
        return 1;
    }
    if (!test_guest_spinlock_machine_code()) {
        std::cerr << "test_guest_spinlock_machine_code FAILED" << std::endl;
        return 1;
    }
    if (!test_irq_safe_locking()) {
        std::cerr << "test_irq_safe_locking FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.7 & 196.8] All Spinlock and IRQ-Safe Tests PASSED (Acceptance Tests D & E VERIFIED)." << std::endl;
    return 0;
}
