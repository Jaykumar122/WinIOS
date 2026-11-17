#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/context_switch.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::size_t kNumPriorityLevels = 32;
inline constexpr std::uint32_t kIdleThreadPriority = 0;
inline constexpr std::uint32_t kMaxPriority = 31;

struct SchedulerStats {
    std::uint64_t total_context_switches{0};
    std::uint64_t total_scheduler_ticks{0};
    std::uint64_t total_idle_ticks{0};
    std::uint64_t total_yield_operations{0};
    std::uint64_t total_preemptions{0};
};

class PriorityScheduler {
public:
    PriorityScheduler(KernelThreadManager& thread_mgr, Memory& mem);
    ~PriorityScheduler() = default;

    // Initializes scheduler and creates the system idle thread
    bool initialize(std::uint64_t idle_stack_base = 0x10000ULL,
                    std::uint64_t idle_stack_size = 0x4000ULL,
                    std::uint64_t idle_code_addr = 0x2000ULL);

    // Enqueue a ready thread into the appropriate priority run queue
    bool enqueue_thread(KernelThread* thread);

    // Pick next thread to run based on highest priority
    [[nodiscard]] KernelThread* pick_next_thread();

    // Dispatch next ready thread to CPU immediately
    bool dispatch_next(CPU& cpu);

    // Advance scheduler timer tick, manage quantum, preempt if expired
    bool tick(CPU& cpu);

    // Voluntarily yield the remaining time slice of the currently running thread
    bool yield(CPU& cpu);

    // Block the currently running thread with the specified reason
    bool block_current(WaitReason reason, CPU& cpu);

    // Unblock a thread and put it back in the ready queue
    bool unblock_thread(KernelThread* thread);

    // Query state
    [[nodiscard]] KernelThread* current_thread() const noexcept { return current_thread_; }
    [[nodiscard]] KernelThread* idle_thread() const noexcept { return idle_thread_; }
    [[nodiscard]] std::uint32_t ready_bitmap() const noexcept { return ready_bitmap_; }
    [[nodiscard]] std::size_t ready_thread_count() const;
    [[nodiscard]] const SchedulerStats& stats() const noexcept { return stats_; }

    // Generates an authentic ARM64 guest idle loop routine (WFI in loop)
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_idle_routine();

private:
    KernelThreadManager& thread_mgr_;
    Memory& mem_;
    KernelThread* current_thread_{nullptr};
    KernelThread* idle_thread_{nullptr};

    std::uint32_t ready_bitmap_{0};
    std::array<std::deque<KernelThread*>, kNumPriorityLevels> run_queues_;
    SchedulerStats stats_;

    void switch_to(KernelThread* next_thread, CPU& cpu);
};

} // namespace winios::kernel
