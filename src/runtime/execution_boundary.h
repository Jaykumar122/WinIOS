#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <array>

namespace winios::runtime {

enum class ExecutionOrigin : uint32_t {
    kUnknown = 0,
    kHostSimulation = 1,
    kGuestCpuRetired = 2,
    kKernelSyscallService = 3,
    kHostPresentation = 4,
    kHostSchedulerOperation = 5,
    kTestInstrumentation = 6
};

[[nodiscard]] inline const char* execution_origin_name(ExecutionOrigin origin) noexcept {
    switch (origin) {
        case ExecutionOrigin::kHostSimulation: return "HostSimulation";
        case ExecutionOrigin::kGuestCpuRetired: return "GuestCpuRetired";
        case ExecutionOrigin::kKernelSyscallService: return "KernelSyscallService";
        case ExecutionOrigin::kHostPresentation: return "HostPresentation";
        case ExecutionOrigin::kHostSchedulerOperation: return "HostSchedulerOperation";
        case ExecutionOrigin::kTestInstrumentation: return "TestInstrumentation";
        default: return "Unknown";
    }
}

struct ProvenanceEvent {
    uint64_t sequence_number{0};
    ExecutionOrigin origin{ExecutionOrigin::kUnknown};
    int32_t pid{0};
    uint64_t tid{0};
    uint64_t instruction_count{0};
    uint64_t guest_pc{0};
};

class ExecutionBoundaryEnforcer {
public:
    static constexpr size_t kMaxRecentEvents = 256;

    static ExecutionBoundaryEnforcer& Get() {
        static ExecutionBoundaryEnforcer instance;
        return instance;
    }

    void SetCurrentOrigin(ExecutionOrigin origin) noexcept {
        current_origin_ = origin;
    }

    [[nodiscard]] ExecutionOrigin GetCurrentOrigin() const noexcept {
        return current_origin_;
    }

    void SetActiveContext(int32_t pid, uint64_t tid, uint64_t pc = 0) noexcept {
        active_pid_ = pid;
        active_tid_ = tid;
        active_pc_ = pc;
    }

    [[nodiscard]] int32_t GetActivePid() const noexcept { return active_pid_; }
    [[nodiscard]] uint64_t GetActiveTid() const noexcept { return active_tid_; }
    [[nodiscard]] uint64_t GetActivePc() const noexcept { return active_pc_; }

    [[nodiscard]] bool RecordGuestInstructionRetirement(uint64_t count = 1) noexcept {
        return RecordGuestInstructionRetirementExplicit(count, active_pid_, active_tid_, active_pc_);
    }

    [[nodiscard]] bool RecordGuestInstructionRetirementExplicit(
        uint64_t count, int32_t pid, uint64_t tid, uint64_t pc) noexcept {
        // Strict invariant: only genuine GuestCpuRetired origin is allowed to increment
        // genuine instruction retirement counters. Host callbacks, schedulers, or simulations must be rejected.
        if (current_origin_ != ExecutionOrigin::kGuestCpuRetired) {
            rejected_increments_++;
            record_event(current_origin_, pid, tid, count, pc);
            return false;
        }

        verified_guest_instructions_retired_ += count;
        record_event(ExecutionOrigin::kGuestCpuRetired, pid, tid, count, pc);
        return true;
    }

    void RecordSchedulerOperation(int32_t pid = 0, uint64_t tid = 0) noexcept {
        total_scheduler_ops_++;
        record_event(ExecutionOrigin::kHostSchedulerOperation, pid, tid, 0, 0);
    }

    void RecordSyscallService(int32_t pid = 0, uint64_t tid = 0) noexcept {
        total_syscall_services_++;
        record_event(ExecutionOrigin::kKernelSyscallService, pid, tid, 0, 0);
    }

    [[nodiscard]] uint64_t GetVerifiedGuestInstructionsRetired() const noexcept {
        return verified_guest_instructions_retired_.load();
    }

    [[nodiscard]] uint64_t GetRejectedIncrements() const noexcept {
        return rejected_increments_.load();
    }

    [[nodiscard]] uint64_t GetTotalSchedulerOperations() const noexcept {
        return total_scheduler_ops_.load();
    }

    [[nodiscard]] uint64_t GetTotalSyscallServices() const noexcept {
        return total_syscall_services_.load();
    }

    [[nodiscard]] uint64_t GetCurrentSequenceNumber() const noexcept {
        return sequence_counter_.load();
    }

    [[nodiscard]] std::vector<ProvenanceEvent> GetRecentEvents() const {
        std::lock_guard<std::mutex> lock(event_mutex_);
        std::vector<ProvenanceEvent> result;
        const size_t count = event_count_ < kMaxRecentEvents ? event_count_ : kMaxRecentEvents;
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (event_head_ + kMaxRecentEvents - count + i) % kMaxRecentEvents;
            result.push_back(events_[idx]);
        }
        return result;
    }

    void Reset() noexcept {
        current_origin_ = ExecutionOrigin::kUnknown;
        active_pid_ = 0;
        active_tid_ = 0;
        active_pc_ = 0;
        verified_guest_instructions_retired_ = 0;
        rejected_increments_ = 0;
        total_scheduler_ops_ = 0;
        total_syscall_services_ = 0;
        sequence_counter_ = 0;
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_head_ = 0;
            event_count_ = 0;
        }
    }

private:
    ExecutionBoundaryEnforcer() = default;

    void record_event(ExecutionOrigin origin, int32_t pid, uint64_t tid, uint64_t count, uint64_t pc) noexcept {
        const uint64_t seq = ++sequence_counter_;
        std::lock_guard<std::mutex> lock(event_mutex_);
        events_[event_head_] = ProvenanceEvent{seq, origin, pid, tid, count, pc};
        event_head_ = (event_head_ + 1) % kMaxRecentEvents;
        if (event_count_ < kMaxRecentEvents) {
            event_count_++;
        }
    }

    ExecutionOrigin current_origin_{ExecutionOrigin::kUnknown};
    int32_t active_pid_{0};
    uint64_t active_tid_{0};
    uint64_t active_pc_{0};

    std::atomic<uint64_t> verified_guest_instructions_retired_{0};
    std::atomic<uint64_t> rejected_increments_{0};
    std::atomic<uint64_t> total_scheduler_ops_{0};
    std::atomic<uint64_t> total_syscall_services_{0};
    std::atomic<uint64_t> sequence_counter_{0};

    mutable std::mutex event_mutex_;
    std::array<ProvenanceEvent, kMaxRecentEvents> events_{};
    size_t event_head_{0};
    size_t event_count_{0};
};

// RAII Origin Scope helper
class ExecutionOriginScope {
public:
    explicit ExecutionOriginScope(ExecutionOrigin new_origin) noexcept
        : previous_origin_(ExecutionBoundaryEnforcer::Get().GetCurrentOrigin()) {
        ExecutionBoundaryEnforcer::Get().SetCurrentOrigin(new_origin);
    }

    ~ExecutionOriginScope() noexcept {
        ExecutionBoundaryEnforcer::Get().SetCurrentOrigin(previous_origin_);
    }

    ExecutionOriginScope(const ExecutionOriginScope&) = delete;
    ExecutionOriginScope& operator=(const ExecutionOriginScope&) = delete;

private:
    ExecutionOrigin previous_origin_;
};

} // namespace winios::runtime
