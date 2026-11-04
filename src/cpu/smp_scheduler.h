#pragma once

#include "cpu/cpu.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_shared_memory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace winios {

namespace sndl {
class SndlV2Recorder;
}

// -----------------------------------------------------------------------------
// QuantumTruncationReason: Deterministic cause of quantum termination
// -----------------------------------------------------------------------------
enum class QuantumTruncationReason : std::uint32_t {
    QuantumExhausted        = 0, // Core executed its full instruction budget Q
    CoreHalted              = 1, // Core entered Halted state (HLT instruction or termination)
    CoreWaitingForInterrupt = 2, // Core executed WFI instruction
    CoreWaitingForEvent     = 3, // Core executed WFE instruction
    SynchronizationBarrier  = 4, // Core reached synchronization boundary
    InterProcessorInterrupt = 5, // Cross-core SGI interrupt generated
    ExceptionRaised         = 6, // Architectural synchronous exception taken
    AllCoresQuiescent       = 7  // No runnable cores available to schedule
};

[[nodiscard]] constexpr std::string_view quantum_truncation_reason_name(QuantumTruncationReason r) noexcept {
    switch (r) {
    case QuantumTruncationReason::QuantumExhausted:        return "QuantumExhausted";
    case QuantumTruncationReason::CoreHalted:              return "CoreHalted";
    case QuantumTruncationReason::CoreWaitingForInterrupt: return "CoreWaitingForInterrupt";
    case QuantumTruncationReason::CoreWaitingForEvent:     return "CoreWaitingForEvent";
    case QuantumTruncationReason::SynchronizationBarrier:  return "SynchronizationBarrier";
    case QuantumTruncationReason::InterProcessorInterrupt: return "InterProcessorInterrupt";
    case QuantumTruncationReason::ExceptionRaised:         return "ExceptionRaised";
    case QuantumTruncationReason::AllCoresQuiescent:       return "AllCoresQuiescent";
    default:                                               return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// QuantumTransitionRecord: Audit history of a scheduler slice execution
// -----------------------------------------------------------------------------
struct QuantumTransitionRecord {
    std::size_t core_id{0};
    std::uint64_t start_global_sequence{0};
    std::uint64_t end_global_sequence{0};
    std::uint64_t instructions_retired{0};
    QuantumTruncationReason reason{QuantumTruncationReason::QuantumExhausted};
};

// -----------------------------------------------------------------------------
// SmpSchedulerSlot: Scheduling slot representing a virtual core
// -----------------------------------------------------------------------------
struct SmpSchedulerSlot {
    std::size_t core_id{0};
    std::shared_ptr<CPU> cpu;
    SmpCoreState state;
    std::uint64_t total_instructions_retired{0};
    std::uint64_t quantums_allocated{0};
    std::uint64_t early_truncations{0};
    bool active{false};
};

// -----------------------------------------------------------------------------
// SmpScheduler: Single-Host-Thread Deterministic Round-Robin Multicore Scheduler
//
// Key Architectural Guarantees:
// 1. Single Host Thread Execution: Eliminates all OS thread scheduling jitter and races.
// 2. Strict Round-Robin Ordering: Cores are scheduled in ascending core_id order.
// 3. Fixed Instruction Quantum (Q): Configurable instruction budget per slice.
// 4. Deterministic Truncation: Clear architectural triggers (WFI, WFE, HLT, Exception).
// 5. Monotonic Global Sequence: Single continuous retirement clock across all cores.
// 6. Multicore Canonical Hash: Exact cryptographic identity of complete SMP state.
// -----------------------------------------------------------------------------
class SmpScheduler {
public:
    static constexpr std::uint64_t kDefaultQuantumInstructions = 10000;

    explicit SmpScheduler(std::shared_ptr<SmpSharedMemory> shared_memory,
                          std::size_t max_cores = 16,
                          std::uint64_t quantum_instructions = kDefaultQuantumInstructions);

    // Register a virtual core into the scheduler
    void register_core(std::size_t core_id, std::shared_ptr<CPU> cpu,
                       CoreLifecycleState initial_state = CoreLifecycleState::Runnable);

    [[nodiscard]] std::size_t core_count() const noexcept { return registered_core_count_; }
    [[nodiscard]] std::uint64_t quantum_instructions() const noexcept { return quantum_instructions_; }
    void set_quantum_instructions(std::uint64_t q) noexcept { quantum_instructions_ = q; }

    [[nodiscard]] std::uint64_t global_retirement_sequence() const noexcept { return global_retirement_sequence_; }
    void set_global_retirement_sequence(std::uint64_t seq) noexcept { global_retirement_sequence_ = seq; }

    [[nodiscard]] std::size_t current_core_index() const noexcept { return current_core_index_; }
    void set_current_core_index(std::size_t idx) noexcept { current_core_index_ = idx; }

    [[nodiscard]] const std::vector<SmpSchedulerSlot>& slots() const noexcept { return slots_; }
    [[nodiscard]] std::vector<SmpSchedulerSlot>& slots() noexcept { return slots_; }

    [[nodiscard]] SmpSharedMemory& shared_memory() noexcept { return *shared_memory_; }
    [[nodiscard]] const SmpSharedMemory& shared_memory() const noexcept { return *shared_memory_; }

    [[nodiscard]] SmpExclusiveMonitorManager& exclusive_monitor() noexcept { return exclusive_monitor_; }
    [[nodiscard]] const SmpExclusiveMonitorManager& exclusive_monitor() const noexcept { return exclusive_monitor_; }

    [[nodiscard]] SmpInterruptRouter& interrupt_router() noexcept { return interrupt_router_; }
    [[nodiscard]] const SmpInterruptRouter& interrupt_router() const noexcept { return interrupt_router_; }

    [[nodiscard]] const SmpSchedulerSlot& get_slot(std::size_t core_id) const;
    [[nodiscard]] SmpSchedulerSlot& get_slot(std::size_t core_id);

    // Advances the schedule by one quantum for the next runnable core
    QuantumTransitionRecord step_quantum();

    // Runs until max_total_instructions are retired across all cores or all cores quiesce/halt
    std::uint64_t run_deterministic(std::uint64_t max_total_instructions);

    // Runs for a fixed number of quantum ticks
    std::uint64_t run_deterministic_ticks(std::size_t num_ticks);

    // Resets the scheduler and registered cores to clean initial state
    void reset();

    // Multicore canonical state hash
    [[nodiscard]] std::uint64_t compute_canonical_multicore_hash() const noexcept;

    // Transition audit history
    [[nodiscard]] const std::vector<QuantumTransitionRecord>& transition_history() const noexcept {
        return history_;
    }
    void clear_history() noexcept { history_.clear(); }

    // Optional SNDL v2 recorder integration
    void attach_sndl_recorder(sndl::SndlV2Recorder* recorder) noexcept { sndl_recorder_ = recorder; }
    [[nodiscard]] sndl::SndlV2Recorder* sndl_recorder() const noexcept { return sndl_recorder_; }

private:
    std::shared_ptr<SmpSharedMemory> shared_memory_;
    SmpExclusiveMonitorManager exclusive_monitor_;
    SmpInterruptRouter interrupt_router_;

    std::vector<SmpSchedulerSlot> slots_;
    std::size_t registered_core_count_{0};
    std::size_t current_core_index_{0};
    std::uint64_t quantum_instructions_{kDefaultQuantumInstructions};
    std::uint64_t global_retirement_sequence_{0};
    std::vector<QuantumTransitionRecord> history_;
    sndl::SndlV2Recorder* sndl_recorder_{nullptr};
};

} // namespace winios
