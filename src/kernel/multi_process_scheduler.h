#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/user_address_space.h"

namespace winios::kernel {

struct MultiProcessSchedulerStats {
    std::size_t total_context_switches{0};
    std::size_t total_address_space_switches{0};
    std::size_t same_process_context_switches{0};
    std::size_t cross_process_context_switches{0};
    std::size_t total_preemptions{0};
    std::size_t total_yields{0};
};

class MultiProcessScheduler {
public:
    static constexpr std::uint32_t kDefaultTimeSliceQuantum = 4; // 4 ticks quantum

    MultiProcessScheduler(
        KernelThreadManager& thread_mgr,
        BsdProcessManager& proc_mgr,
        Memory& mem);
    ~MultiProcessScheduler() = default;

    // Register UserAddressSpace for a given process PID
    void register_process_address_space(pid_t pid, memory::UserAddressSpace* uas);
    void unregister_process_address_space(pid_t pid);

    // Enqueue thread for execution
    bool enqueue_thread(KernelThread* thread);

    // Schedule and dispatch the next ready thread onto the virtual CPU
    bool schedule_next(CPU& cpu);

    // Timer tick: decrements remaining quantum and preempts when expired
    bool tick(CPU& cpu);

    // Voluntary yield
    bool yield(CPU& cpu);

    // Thread lifecycle & state management
    void set_time_slice_quantum(std::uint32_t quantum) noexcept { default_quantum_ = quantum; }
    [[nodiscard]] std::uint32_t time_slice_quantum() const noexcept { return default_quantum_; }
    bool block_thread(KernelThread* thread, WaitReason reason);
    bool unblock_thread(KernelThread* thread);
    bool terminate_thread(KernelThread* thread, std::int32_t code);
    std::size_t reap_zombies();

    // Invariant consistency checks
    [[nodiscard]] bool check_invariants() const noexcept;

    // State inspection
    [[nodiscard]] KernelThread* current_thread() const noexcept { return current_thread_; }
    [[nodiscard]] pid_t current_pid() const noexcept;
    [[nodiscard]] std::uint64_t current_ttbr0() const noexcept { return current_ttbr0_; }
    [[nodiscard]] const MultiProcessSchedulerStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t ready_count() const noexcept;
    [[nodiscard]] memory::UserAddressSpace* get_address_space(pid_t pid) const;

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    KernelThreadManager&                            thread_mgr_;
    BsdProcessManager&                              proc_mgr_;
    Memory&                                         mem_;
    KernelThread*                                   current_thread_{nullptr};
    std::uint64_t                                   current_ttbr0_{0};
    std::uint32_t                                   default_quantum_{kDefaultTimeSliceQuantum};
    std::uint32_t                                   quantum_remaining_{kDefaultTimeSliceQuantum};
    std::deque<KernelThread*>                       ready_queue_;
    std::unordered_map<pid_t, memory::UserAddressSpace*> uas_map_;
    MultiProcessSchedulerStats                      stats_{};
    mutable std::mutex                              mutex_;

    bool perform_context_switch(CPU& cpu, KernelThread* prev_thread, KernelThread* next_thread);
};

} // namespace winios::kernel
