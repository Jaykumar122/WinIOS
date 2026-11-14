#include "kernel/scheduler.h"

#include <algorithm>

namespace winios::kernel {

PriorityScheduler::PriorityScheduler(KernelThreadManager& thread_mgr, Memory& mem)
    : thread_mgr_(thread_mgr), mem_(mem) {}

std::vector<std::uint32_t> PriorityScheduler::emit_guest_idle_routine() {
    return {
        0xD503207Fu, // [00] wfi
        0x17FFFFFFu  // [04] b . - 4 (loop back to wfi)
    };
}

bool PriorityScheduler::initialize(std::uint64_t idle_stack_base,
                                   std::uint64_t idle_stack_size,
                                   std::uint64_t idle_code_addr) {
    // Write idle routine into guest memory
    const auto idle_code = emit_guest_idle_routine();
    for (std::size_t i = 0; i < idle_code.size(); ++i) {
        mem_.write_u32(static_cast<std::size_t>(idle_code_addr + i * 4), idle_code[i]);
    }

    // Create system idle thread
    idle_thread_ = thread_mgr_.create_thread("idle_thread",
                                             idle_code_addr,
                                             0,
                                             idle_stack_base,
                                             idle_stack_size,
                                             kIdleThreadPriority,
                                             false);
    if (!idle_thread_) {
        return false;
    }

    current_thread_ = idle_thread_;
    current_thread_->set_state(ThreadState::Running);

    ready_bitmap_ = 0;
    for (auto& q : run_queues_) {
        q.clear();
    }
    stats_ = SchedulerStats{};
    return true;
}

bool PriorityScheduler::enqueue_thread(KernelThread* thread) {
    if (!thread || thread == idle_thread_) {
        return false;
    }

    const std::uint32_t prio = std::min(thread->priority(), kMaxPriority);
    thread->set_state(ThreadState::Ready);
    run_queues_[prio].push_back(thread);
    ready_bitmap_ |= (1u << prio);
    return true;
}

KernelThread* PriorityScheduler::pick_next_thread() {
    if (ready_bitmap_ == 0) {
        return idle_thread_;
    }

    for (int p = static_cast<int>(kMaxPriority); p >= 0; --p) {
        const std::uint32_t bit = (1u << static_cast<std::uint32_t>(p));
        if ((ready_bitmap_ & bit) != 0) {
            auto& q = run_queues_[static_cast<std::size_t>(p)];
            if (!q.empty()) {
                KernelThread* selected = q.front();
                q.pop_front();
                if (q.empty()) {
                    ready_bitmap_ &= ~bit;
                }
                return selected;
            }
        }
    }

    return idle_thread_;
}

void PriorityScheduler::switch_to(KernelThread* next_thread, CPU& cpu) {
    if (next_thread == current_thread_) {
        return;
    }

    ContextSwitch::switch_threads(current_thread_, next_thread, cpu);
    current_thread_ = next_thread;
    ++stats_.total_context_switches;
}

bool PriorityScheduler::tick(CPU& cpu) {
    ++stats_.total_scheduler_ticks;

    if (current_thread_ == idle_thread_) {
        ++stats_.total_idle_ticks;
        if (ready_bitmap_ != 0) {
            KernelThread* next = pick_next_thread();
            switch_to(next, cpu);
        }
        return true;
    }

    current_thread_->tick();

    // Check if time-slice quantum expired
    if (current_thread_->time_slice() == 0) {
        ++stats_.total_preemptions;
        current_thread_->set_time_slice(kDefaultTimeSliceTicks);

        KernelThread* outgoing = current_thread_;
        KernelThread* next = pick_next_thread();
        if (next != outgoing) {
            enqueue_thread(outgoing);
            switch_to(next, cpu);
        }
        return true;
    }

    // Check if a strictly higher-priority thread became ready
    for (int p = static_cast<int>(kMaxPriority); p > static_cast<int>(current_thread_->priority()); --p) {
        const std::uint32_t bit = (1u << static_cast<std::uint32_t>(p));
        if ((ready_bitmap_ & bit) != 0) {
            ++stats_.total_preemptions;
            KernelThread* higher = pick_next_thread();
            enqueue_thread(current_thread_);
            switch_to(higher, cpu);
            break;
        }
    }

    return true;
}

bool PriorityScheduler::yield(CPU& cpu) {
    ++stats_.total_yield_operations;
    if (current_thread_ == idle_thread_) {
        return true;
    }

    current_thread_->set_time_slice(kDefaultTimeSliceTicks);
    KernelThread* outgoing = current_thread_;
    KernelThread* next = pick_next_thread();
    enqueue_thread(outgoing);
    switch_to(next, cpu);
    return true;
}

bool PriorityScheduler::block_current(WaitReason reason, CPU& cpu) {
    if (current_thread_ == idle_thread_) {
        return false;
    }

    current_thread_->block(reason);
    KernelThread* next = pick_next_thread();
    switch_to(next, cpu);
    return true;
}

bool PriorityScheduler::unblock_thread(KernelThread* thread) {
    if (!thread || thread->state() != ThreadState::Blocked) {
        return false;
    }

    thread->unblock();
    return enqueue_thread(thread);
}

std::size_t PriorityScheduler::ready_thread_count() const {
    std::size_t count = 0;
    for (const auto& q : run_queues_) {
        count += q.size();
    }
    return count;
}

} // namespace winios::kernel
