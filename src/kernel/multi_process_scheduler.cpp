#include "kernel/multi_process_scheduler.h"

namespace winios::kernel {

MultiProcessScheduler::MultiProcessScheduler(
    KernelThreadManager& thread_mgr,
    BsdProcessManager& proc_mgr,
    Memory& mem)
    : thread_mgr_(thread_mgr),
      proc_mgr_(proc_mgr),
      mem_(mem) {}

void MultiProcessScheduler::register_process_address_space(pid_t pid, memory::UserAddressSpace* uas) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uas) {
        uas_map_[pid] = uas;
    }
}

void MultiProcessScheduler::unregister_process_address_space(pid_t pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    uas_map_.erase(pid);
}

bool MultiProcessScheduler::enqueue_thread(KernelThread* thread) {
    if (!thread) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    thread->set_state(ThreadState::Ready);
    ready_queue_.push_back(thread);
    return true;
}

bool MultiProcessScheduler::schedule_next(CPU& cpu) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_queue_.empty()) {
        return false;
    }

    KernelThread* next_thread = ready_queue_.front();
    ready_queue_.pop_front();

    KernelThread* prev_thread = current_thread_;
    return perform_context_switch(cpu, prev_thread, next_thread);
}

bool MultiProcessScheduler::tick(CPU& cpu) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_thread_) {
        if (!ready_queue_.empty()) {
            KernelThread* next = ready_queue_.front();
            ready_queue_.pop_front();
            return perform_context_switch(cpu, nullptr, next);
        }
        return false;
    }

    if (quantum_remaining_ > 0) {
        --quantum_remaining_;
    }

    if (quantum_remaining_ == 0) {
        if (!ready_queue_.empty()) {
            ++stats_.total_preemptions;
            current_thread_->set_state(ThreadState::Ready);
            ready_queue_.push_back(current_thread_);

            KernelThread* next = ready_queue_.front();
            ready_queue_.pop_front();
            return perform_context_switch(cpu, current_thread_, next);
        } else {
            // No other thread ready: recharge quantum
            quantum_remaining_ = default_quantum_;
        }
    }

    return true;
}

bool MultiProcessScheduler::yield(CPU& cpu) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.total_yields;

    if (current_thread_) {
        current_thread_->set_state(ThreadState::Ready);
        ready_queue_.push_back(current_thread_);
    }

    if (!ready_queue_.empty()) {
        KernelThread* next = ready_queue_.front();
        ready_queue_.pop_front();
        return perform_context_switch(cpu, current_thread_, next);
    }

    return false;
}

bool MultiProcessScheduler::perform_context_switch(
    CPU& cpu,
    KernelThread* prev_thread,
    KernelThread* next_thread) {
    if (!next_thread) return false;

    // 1. Save state of previous thread
    if (prev_thread) {
        prev_thread->save_context_from_cpu(cpu);
    }

    // 2. Switch address space if moving to a different process
    const pid_t next_pid = next_thread->owner_pid();
    auto it = uas_map_.find(next_pid);
    if (it != uas_map_.end() && it->second) {
        const std::uint64_t target_ttbr0 = it->second->root_table_pa_with_asid();
        if (target_ttbr0 != current_ttbr0_) {
            cpu.get_mmu().set_ttbr0_el1(target_ttbr0);
            cpu.get_mmu().invalidate_tlb_all();
            current_ttbr0_ = target_ttbr0;
            ++stats_.total_address_space_switches;
        }
    }

    // 3. Restore architectural register state of next thread
    next_thread->load_context_to_cpu(cpu);

    next_thread->set_state(ThreadState::Running);
    if (prev_thread && prev_thread->owner_pid() == next_thread->owner_pid()) {
        ++stats_.same_process_context_switches;
    } else {
        ++stats_.cross_process_context_switches;
    }
    current_thread_ = next_thread;
    quantum_remaining_ = default_quantum_;
    ++stats_.total_context_switches;
    return true;
}

pid_t MultiProcessScheduler::current_pid() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_thread_ ? current_thread_->owner_pid() : 0;
}

std::size_t MultiProcessScheduler::ready_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_queue_.size();
}

memory::UserAddressSpace* MultiProcessScheduler::get_address_space(pid_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = uas_map_.find(pid);
    return it != uas_map_.end() ? it->second : nullptr;
}

void MultiProcessScheduler::serialize(std::vector<std::uint8_t>& dst) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Current TTBR0 and quantum
    const auto* p_ttbr0 = reinterpret_cast<const std::uint8_t*>(&current_ttbr0_);
    dst.insert(dst.end(), p_ttbr0, p_ttbr0 + sizeof(current_ttbr0_));

    const auto* p_q = reinterpret_cast<const std::uint8_t*>(&quantum_remaining_);
    dst.insert(dst.end(), p_q, p_q + sizeof(quantum_remaining_));

    // 2. Stats
    const auto* p_stats = reinterpret_cast<const std::uint8_t*>(&stats_);
    dst.insert(dst.end(), p_stats, p_stats + sizeof(stats_));

    // 3. Current thread ID (or 0)
    const std::uint64_t cur_tid = current_thread_ ? current_thread_->tid() : 0;
    const auto* p_ctid = reinterpret_cast<const std::uint8_t*>(&cur_tid);
    dst.insert(dst.end(), p_ctid, p_ctid + sizeof(cur_tid));

    // 4. Ready queue thread IDs
    const std::uint32_t q_size = static_cast<std::uint32_t>(ready_queue_.size());
    const auto* p_qs = reinterpret_cast<const std::uint8_t*>(&q_size);
    dst.insert(dst.end(), p_qs, p_qs + sizeof(q_size));
    for (const auto* t : ready_queue_) {
        const std::uint64_t tid = t ? t->tid() : 0;
        const auto* pt = reinterpret_cast<const std::uint8_t*>(&tid);
        dst.insert(dst.end(), pt, pt + sizeof(tid));
    }
}

bool MultiProcessScheduler::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_queue_.clear();
    current_thread_ = nullptr;

    constexpr std::size_t kHeaderSize = sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(MultiProcessSchedulerStats) + sizeof(std::uint64_t) + sizeof(std::uint32_t);
    if (remaining < kHeaderSize) return false;

    std::memcpy(&current_ttbr0_, src, sizeof(current_ttbr0_));
    src += sizeof(current_ttbr0_);
    remaining -= sizeof(current_ttbr0_);

    std::memcpy(&quantum_remaining_, src, sizeof(quantum_remaining_));
    src += sizeof(quantum_remaining_);
    remaining -= sizeof(quantum_remaining_);

    std::memcpy(&stats_, src, sizeof(stats_));
    src += sizeof(stats_);
    remaining -= sizeof(stats_);

    std::uint64_t cur_tid = 0;
    std::memcpy(&cur_tid, src, sizeof(cur_tid));
    src += sizeof(cur_tid);
    remaining -= sizeof(cur_tid);
    if (cur_tid != 0) {
        current_thread_ = thread_mgr_.get_thread(cur_tid);
    }

    std::uint32_t q_size = 0;
    std::memcpy(&q_size, src, sizeof(q_size));
    src += sizeof(q_size);
    remaining -= sizeof(q_size);

    if (remaining < q_size * sizeof(std::uint64_t)) return false;

    for (std::uint32_t i = 0; i < q_size; ++i) {
        std::uint64_t tid = 0;
        std::memcpy(&tid, src, sizeof(tid));
        src += sizeof(tid);
        remaining -= sizeof(tid);

        KernelThread* t = thread_mgr_.get_thread(tid);
        if (t) {
            ready_queue_.push_back(t);
        }
    }

    return true;
}

bool MultiProcessScheduler::block_thread(KernelThread* thread, WaitReason reason) {
    if (!thread) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    thread->block(reason);
    for (auto it = ready_queue_.begin(); it != ready_queue_.end();) {
        if (*it == thread) {
            it = ready_queue_.erase(it);
        } else {
            ++it;
        }
    }
    if (current_thread_ == thread) {
        current_thread_ = nullptr;
    }
    return true;
}

bool MultiProcessScheduler::unblock_thread(KernelThread* thread) {
    if (!thread) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread->state() != ThreadState::Blocked) return false;
    thread->unblock();
    ready_queue_.push_back(thread);
    return true;
}

bool MultiProcessScheduler::terminate_thread(KernelThread* thread, std::int32_t code) {
    if (!thread) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    thread->terminate(code);
    for (auto it = ready_queue_.begin(); it != ready_queue_.end();) {
        if (*it == thread) {
            it = ready_queue_.erase(it);
        } else {
            ++it;
        }
    }
    if (current_thread_ == thread) {
        current_thread_ = nullptr;
    }
    return true;
}

std::size_t MultiProcessScheduler::reap_zombies() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t reaped = 0;
    for (auto it = ready_queue_.begin(); it != ready_queue_.end();) {
        if ((*it)->state() == ThreadState::Terminated) {
            it = ready_queue_.erase(it);
            ++reaped;
        } else {
            ++it;
        }
    }
    return reaped;
}

bool MultiProcessScheduler::check_invariants() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_thread_ && current_thread_->state() != ThreadState::Running) {
        return false;
    }
    for (const auto* th : ready_queue_) {
        if (!th) return false;
        if (th->state() != ThreadState::Ready) return false;
    }
    return true;
}

} // namespace winios::kernel
