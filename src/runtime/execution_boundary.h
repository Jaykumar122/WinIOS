#pragma once

#include <cstdint>
#include <string>
#include <atomic>

namespace winios::runtime {

enum class ExecutionOrigin : uint32_t {
    kUnknown = 0,
    kHostSimulation = 1,
    kGuestCpuRetired = 2,
    kKernelSyscallService = 3,
    kHostPresentation = 4
};

[[nodiscard]] inline const char* execution_origin_name(ExecutionOrigin origin) noexcept {
    switch (origin) {
        case ExecutionOrigin::kHostSimulation: return "HostSimulation";
        case ExecutionOrigin::kGuestCpuRetired: return "GuestCpuRetired";
        case ExecutionOrigin::kKernelSyscallService: return "KernelSyscallService";
        case ExecutionOrigin::kHostPresentation: return "HostPresentation";
        default: return "Unknown";
    }
}

class ExecutionBoundaryEnforcer {
public:
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

    [[nodiscard]] bool RecordGuestInstructionRetirement(uint64_t count = 1) noexcept {
        // Strict invariant: only genuine GuestCpuRetired origin is allowed to increment
        // genuine instruction retirement counters. Host callbacks or simulations must be rejected.
        if (current_origin_ != ExecutionOrigin::kGuestCpuRetired) {
            rejected_increments_++;
            return false;
        }
        verified_guest_instructions_retired_ += count;
        return true;
    }

    [[nodiscard]] uint64_t GetVerifiedGuestInstructionsRetired() const noexcept {
        return verified_guest_instructions_retired_.load();
    }

    [[nodiscard]] uint64_t GetRejectedIncrements() const noexcept {
        return rejected_increments_.load();
    }

    void Reset() noexcept {
        current_origin_ = ExecutionOrigin::kUnknown;
        verified_guest_instructions_retired_ = 0;
        rejected_increments_ = 0;
    }

private:
    ExecutionBoundaryEnforcer() = default;

    ExecutionOrigin current_origin_{ExecutionOrigin::kUnknown};
    std::atomic<uint64_t> verified_guest_instructions_retired_{0};
    std::atomic<uint64_t> rejected_increments_{0};
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
