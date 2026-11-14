#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "kernel/context_switch.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::size_t kMaxSmpCores = 8;
inline constexpr std::uint32_t kRescheduleSgi = 1; // SGI 1 used for inter-core reschedule

struct SmpCoreState {
    KernelThread* current_thread{nullptr};
    KernelThread* idle_thread{nullptr};
    std::uint32_t ready_bitmap{0};
    std::array<std::deque<KernelThread*>, kNumPriorityLevels> run_queues;
    std::uint64_t total_context_switches{0};
    std::uint64_t total_ticks{0};
};

struct SmpSchedulerStats {
    std::uint64_t total_sgi_reschedules{0};
    std::uint64_t total_thread_migrations{0};
    std::uint64_t total_context_switches{0};
};

class SmpScheduler {
public:
    SmpScheduler(KernelThreadManager& thread_mgr, Memory& mem, std::size_t num_cores = 4);
    ~SmpScheduler() = default;

    // Initializes SMP scheduler and creates per-core idle threads
    bool initialize(std::uint64_t idle_stacks_base = 0x80010000ULL,
                    std::uint64_t idle_stack_size = 0x4000ULL,
                    std::uint64_t idle_code_addr = 0x80002000ULL);

    // Enqueue thread respecting its CPU affinity mask
    bool enqueue_thread(KernelThread* thread, int preferred_core = -1);

    // Execute clock tick for a specific core
    bool tick(std::size_t core_id, CPU& cpu);

    // Voluntary yield by thread executing on core_id
    bool yield(std::size_t core_id, CPU& cpu);

    // Block thread executing on core_id
    bool block_current(std::size_t core_id, WaitReason reason, CPU& cpu);

    // Unblock thread and re-enqueue to best affine core
    bool unblock_thread(KernelThread* thread);

    // Load balancer: migrates threads from overloaded cores to underloaded cores
    std::size_t balance_load();

    // Sends an inter-processor interrupt (SGI 1) to notify target core to reschedule
    bool send_reschedule_sgi(std::size_t sender_core, std::size_t target_core, GICv3& gic);

    // State inspection
    [[nodiscard]] std::size_t num_cores() const noexcept { return num_cores_; }
    [[nodiscard]] KernelThread* current_thread(std::size_t core_id) const;
    [[nodiscard]] KernelThread* idle_thread(std::size_t core_id) const;
    [[nodiscard]] std::size_t core_ready_count(std::size_t core_id) const;
    [[nodiscard]] const SmpSchedulerStats& stats() const noexcept { return stats_; }

private:
    KernelThreadManager& thread_mgr_;
    Memory& mem_;
    std::size_t num_cores_;
    std::vector<SmpCoreState> cores_;
    SmpSchedulerStats stats_;

    [[nodiscard]] std::size_t select_best_core(const KernelThread& thread, int preferred_core) const;
    [[nodiscard]] KernelThread* pick_next_thread(std::size_t core_id);
    void switch_to(std::size_t core_id, KernelThread* next_thread, CPU& cpu);
};

} // namespace winios::kernel
