#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

// 16-byte packed structure matching guest memory layout for a kernel spinlock
#pragma pack(push, 1)
struct GuestSpinlockData {
    std::uint32_t lock{0};       // 0 = unlocked, 1 = locked
    std::uint32_t owner_tid{0};  // TID of current holder, 0 if unlocked
    std::uint32_t lock_count{0}; // Recursion count or contention telemetry
    std::uint32_t flags{0};      // Attribute flags
};
#pragma pack(pop)
static_assert(sizeof(GuestSpinlockData) == 16, "GuestSpinlockData must be 16 bytes");

class KernelSpinlock {
public:
    KernelSpinlock(Memory& mem, std::uint64_t guest_addr);
    ~KernelSpinlock() = default;

    // Reset lock state in guest memory
    void initialize();

    // Host-managed acquisition: returns true if acquired, false if contended.
    // If recursive acquisition detected, out_deadlock is set to true.
    bool try_acquire_host(std::uint64_t tid, bool* out_deadlock = nullptr);
    bool release_host(std::uint64_t tid);

    // Query lock status
    [[nodiscard]] bool is_locked() const;
    [[nodiscard]] std::uint32_t owner_tid() const;
    [[nodiscard]] std::uint64_t guest_address() const noexcept { return guest_addr_; }
    [[nodiscard]] std::uint64_t acquire_count() const noexcept { return acquire_count_; }
    [[nodiscard]] std::uint64_t contention_count() const noexcept { return contention_count_; }

    // Guest machine-code emitter routines
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_spinlock_acquire();
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_spinlock_try_acquire();
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_spinlock_release();

private:
    Memory& mem_;
    std::uint64_t guest_addr_;
    std::uint64_t acquire_count_{0};
    std::uint64_t contention_count_{0};
};

enum class WakePolicy : std::uint32_t {
    Fifo = 0,
    PriorityOrder = 1
};

class WaitQueue {
public:
    explicit WaitQueue(WakePolicy policy = WakePolicy::Fifo);
    ~WaitQueue() = default;

    // Add thread to wait queue without blocking yet
    void add_waiter(KernelThread* thread);

    // Remove thread from wait queue (e.g. timeout or cancelled)
    bool remove_waiter(KernelThread* thread);

    // Block current thread and put it into this wait queue
    bool wait(KernelThread* thread, PriorityScheduler& sched, CPU& cpu);

    // Wake one waiting thread and return it
    KernelThread* wake_one(PriorityScheduler& sched);

    // Wake all waiting threads and return count woken
    std::size_t wake_all(PriorityScheduler& sched);

    [[nodiscard]] bool empty() const noexcept { return waiters_.empty(); }
    [[nodiscard]] std::size_t count() const noexcept { return waiters_.size(); }
    [[nodiscard]] bool contains(const KernelThread* thread) const noexcept;
    [[nodiscard]] WakePolicy policy() const noexcept { return policy_; }

private:
    WakePolicy policy_{WakePolicy::Fifo};
    std::deque<KernelThread*> waiters_;
};

} // namespace winios::kernel
