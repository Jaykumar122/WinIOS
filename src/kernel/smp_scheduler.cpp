#include "kernel/smp_scheduler.h"

#include <algorithm>

namespace winios::kernel {

SmpScheduler::SmpScheduler(KernelThreadManager& thread_mgr, Memory& mem, std::size_t num_cores)
    : thread_mgr_(thread_mgr),
      mem_(mem),
      num_cores_(std::clamp(num_cores, std::size_t{1}, kMaxSmpCores)),
      cores_(num_cores_) {}

bool SmpScheduler::initialize(std::uint64_t idle_stacks_base,
                              std::uint64_t idle_stack_size,
                              std::uint64_t idle_code_addr) {
    if (idle_code_addr + 8 > mem_.size()) {
        return false;
    }

    // Write idle routine into guest RAM
    const auto idle_code = PriorityScheduler::emit_guest_idle_routine();
    for (std::size_t i = 0; i < idle_code.size(); ++i) {
        mem_.write_u32(static_cast<std::size_t>(idle_code_addr + i * 4), idle_code[i]);
    }

    // Create per-core idle threads
    for (std::size_t c = 0; c < num_cores_; ++c) {
        const std::string name = "idle_cpu_" + std::to_string(c);
        const std::uint64_t stack_base = idle_stacks_base + c * idle_stack_size;
        auto* idle = thread_mgr_.create_thread(name,
                                               idle_code_addr,
                                               0,
                                               stack_base,
                                               idle_stack_size,
                                               kIdleThreadPriority,
                                               false);
        if (!idle) {
            return false;
        }

        idle->set_cpu_affinity(1ULL << c); // Affine strictly to this core
        idle->set_state(ThreadState::Running);

        cores_[c].idle_thread = idle;
        cores_[c].current_thread = idle;
        cores_[c].ready_bitmap = 0;
        for (auto& q : cores_[c].run_queues) {
            q.clear();
        }
        cores_[c].total_context_switches = 0;
        cores_[c].total_ticks = 0;
    }

    stats_ = SmpSchedulerStats{};
    return true;
}

std::size_t SmpScheduler::select_best_core(const KernelThread& thread, int preferred_core) const {
    const std::uint64_t affinity = thread.cpu_affinity();

    // 1. If preferred core is specified and eligible, use it
    if (preferred_core >= 0 && static_cast<std::size_t>(preferred_core) < num_cores_) {
        if ((affinity & (1ULL << preferred_core)) != 0) {
            return static_cast<std::size_t>(preferred_core);
        }
    }

    // 2. Select eligible core with the minimum number of ready threads
    std::size_t best_core = 0;
    std::size_t min_load = static_cast<std::size_t>(-1);

    for (std::size_t c = 0; c < num_cores_; ++c) {
        if ((affinity & (1ULL << c)) != 0) {
            const std::size_t load = core_ready_count(c);
            if (load < min_load) {
                min_load = load;
                best_core = c;
            }
        }
    }

    return best_core;
}

bool SmpScheduler::enqueue_thread(KernelThread* thread, int preferred_core) {
    if (!thread) {
        return false;
    }

    for (std::size_t c = 0; c < num_cores_; ++c) {
        if (thread == cores_[c].idle_thread) {
            return false;
        }
    }

    const std::size_t target_core = select_best_core(*thread, preferred_core);
    const std::uint32_t prio = std::min(thread->priority(), kMaxPriority);

    thread->set_state(ThreadState::Ready);
    cores_[target_core].run_queues[prio].push_back(thread);
    cores_[target_core].ready_bitmap |= (1u << prio);
    return true;
}

KernelThread* SmpScheduler::pick_next_thread(std::size_t core_id) {
    auto& core = cores_[core_id];
    if (core.ready_bitmap == 0) {
        return core.idle_thread;
    }

    for (int p = static_cast<int>(kMaxPriority); p >= 0; --p) {
        const std::uint32_t bit = (1u << static_cast<std::uint32_t>(p));
        if ((core.ready_bitmap & bit) != 0) {
            auto& q = core.run_queues[static_cast<std::size_t>(p)];
            if (!q.empty()) {
                KernelThread* selected = q.front();
                q.pop_front();
                if (q.empty()) {
                    core.ready_bitmap &= ~bit;
                }
                return selected;
            }
        }
    }

    return core.idle_thread;
}

void SmpScheduler::switch_to(std::size_t core_id, KernelThread* next_thread, CPU& cpu) {
    auto& core = cores_[core_id];
    if (next_thread == core.current_thread) {
        return;
    }

    ContextSwitch::switch_threads(core.current_thread, next_thread, cpu);
    core.current_thread = next_thread;
    ++core.total_context_switches;
    ++stats_.total_context_switches;
}

bool SmpScheduler::tick(std::size_t core_id, CPU& cpu) {
    if (core_id >= num_cores_) {
        return false;
    }

    auto& core = cores_[core_id];
    ++core.total_ticks;

    if (core.current_thread == core.idle_thread) {
        if (core.ready_bitmap != 0) {
            KernelThread* next = pick_next_thread(core_id);
            switch_to(core_id, next, cpu);
        }
        return true;
    }

    core.current_thread->tick();

    // Quantum expired
    if (core.current_thread->time_slice() == 0) {
        core.current_thread->set_time_slice(kDefaultTimeSliceTicks);
        KernelThread* outgoing = core.current_thread;
        KernelThread* next = pick_next_thread(core_id);
        if (next != outgoing) {
            enqueue_thread(outgoing, static_cast<int>(core_id));
            switch_to(core_id, next, cpu);
        }
        return true;
    }

    // Check higher-priority preemption on this core
    for (int p = static_cast<int>(kMaxPriority); p > static_cast<int>(core.current_thread->priority()); --p) {
        const std::uint32_t bit = (1u << static_cast<std::uint32_t>(p));
        if ((core.ready_bitmap & bit) != 0) {
            KernelThread* higher = pick_next_thread(core_id);
            enqueue_thread(core.current_thread, static_cast<int>(core_id));
            switch_to(core_id, higher, cpu);
            break;
        }
    }

    return true;
}

bool SmpScheduler::yield(std::size_t core_id, CPU& cpu) {
    if (core_id >= num_cores_) {
        return false;
    }

    auto& core = cores_[core_id];
    if (core.current_thread == core.idle_thread) {
        return true;
    }

    core.current_thread->set_time_slice(kDefaultTimeSliceTicks);
    KernelThread* outgoing = core.current_thread;
    KernelThread* next = pick_next_thread(core_id);
    enqueue_thread(outgoing, static_cast<int>(core_id));
    switch_to(core_id, next, cpu);
    return true;
}

bool SmpScheduler::block_current(std::size_t core_id, WaitReason reason, CPU& cpu) {
    if (core_id >= num_cores_) {
        return false;
    }

    auto& core = cores_[core_id];
    if (core.current_thread == core.idle_thread) {
        return false;
    }

    core.current_thread->block(reason);
    KernelThread* next = pick_next_thread(core_id);
    switch_to(core_id, next, cpu);
    return true;
}

bool SmpScheduler::unblock_thread(KernelThread* thread) {
    if (!thread || thread->state() != ThreadState::Blocked) {
        return false;
    }

    thread->unblock();
    return enqueue_thread(thread);
}

std::size_t SmpScheduler::balance_load() {
    std::size_t migrations = 0;

    for (std::size_t iter = 0; iter < num_cores_; ++iter) {
        // Find maximum loaded and minimum loaded cores
        std::size_t max_core = 0;
        std::size_t min_core = 0;
        std::size_t max_load = 0;
        std::size_t min_load = static_cast<std::size_t>(-1);

        for (std::size_t c = 0; c < num_cores_; ++c) {
            const std::size_t load = core_ready_count(c);
            if (load > max_load) {
                max_load = load;
                max_core = c;
            }
            if (load < min_load) {
                min_load = load;
                min_core = c;
            }
        }

        // Migrate if load imbalance is at least 2 threads
        if (max_load < min_load + 2) {
            break;
        }

        bool migrated = false;
        auto& src_core = cores_[max_core];
        // Scan run queues from lowest priority upwards to find a thread permitted on min_core
        for (std::size_t p = 0; p < kNumPriorityLevels; ++p) {
            auto& q = src_core.run_queues[p];
            for (auto it = q.begin(); it != q.end(); ++it) {
                KernelThread* candidate = *it;
                if ((candidate->cpu_affinity() & (1ULL << min_core)) != 0) {
                    q.erase(it);
                    if (q.empty()) {
                        src_core.ready_bitmap &= ~(1u << p);
                    }

                    // Enqueue to min_core
                    cores_[min_core].run_queues[p].push_back(candidate);
                    cores_[min_core].ready_bitmap |= (1u << p);

                    ++stats_.total_thread_migrations;
                    ++migrations;
                    migrated = true;
                    break;
                }
            }
            if (migrated) break;
        }

        if (!migrated) {
            break; // No further eligible threads can migrate
        }
    }

    return migrations;
}

bool SmpScheduler::send_reschedule_sgi(std::size_t sender_core, std::size_t target_core, GICv3& gic) {
    if (sender_core >= num_cores_ || target_core >= num_cores_) {
        return false;
    }

    // ICC_SGI1R_EL1 format: [27:24] = INTID (kRescheduleSgi = 1), [15:0] = TargetList
    const std::uint64_t sgi1r = (static_cast<std::uint64_t>(kRescheduleSgi) << 24u) | (1ULL << target_core);
    gic.send_sgi(sender_core, sgi1r);
    ++stats_.total_sgi_reschedules;
    return true;
}

KernelThread* SmpScheduler::current_thread(std::size_t core_id) const {
    if (core_id >= num_cores_) return nullptr;
    return cores_[core_id].current_thread;
}

KernelThread* SmpScheduler::idle_thread(std::size_t core_id) const {
    if (core_id >= num_cores_) return nullptr;
    return cores_[core_id].idle_thread;
}

std::size_t SmpScheduler::core_ready_count(std::size_t core_id) const {
    if (core_id >= num_cores_) return 0;
    std::size_t total = 0;
    for (const auto& q : cores_[core_id].run_queues) {
        total += q.size();
    }
    return total;
}

} // namespace winios::kernel
