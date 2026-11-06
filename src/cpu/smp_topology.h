#pragma once

#include "cpu/smp_core_state.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios {

// -----------------------------------------------------------------------------
// SmpTopologyValidationResult: Result of topology validation checks
// -----------------------------------------------------------------------------
enum class SmpTopologyValidationResult : std::uint32_t {
    Success = 0,
    ZeroCoreCount,
    ExcessiveCoreCount,       // > 8 cores
    UnsupportedCoreCount,     // Non power-of-two (e.g., 3, 5, 6, 7)
    DuplicateCoreId,
    NonContiguousCoreId,
    NullCpuInstance,
    ReplayUnsupported         // 8 cores is valid for architecture but unsupported for replay
};

[[nodiscard]] constexpr std::string_view smp_topology_validation_result_name(SmpTopologyValidationResult res) noexcept {
    switch (res) {
    case SmpTopologyValidationResult::Success:              return "Success";
    case SmpTopologyValidationResult::ZeroCoreCount:        return "ZeroCoreCount";
    case SmpTopologyValidationResult::ExcessiveCoreCount:   return "ExcessiveCoreCount";
    case SmpTopologyValidationResult::UnsupportedCoreCount: return "UnsupportedCoreCount";
    case SmpTopologyValidationResult::DuplicateCoreId:      return "DuplicateCoreId";
    case SmpTopologyValidationResult::NonContiguousCoreId:  return "NonContiguousCoreId";
    case SmpTopologyValidationResult::NullCpuInstance:      return "NullCpuInstance";
    case SmpTopologyValidationResult::ReplayUnsupported:    return "ReplayUnsupported";
    default:                                                return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// SmpTopologyConfig: Configuration descriptor for cluster topology instantiation
// -----------------------------------------------------------------------------
struct SmpTopologyConfig {
    static constexpr std::uint32_t kMaxSupportedCores = 8;

    std::uint32_t core_count{1};
    std::uint64_t default_quantum{10000};
    bool allow_experimental_smp{false};
    bool allow_four_core_replay{false};
    bool allow_eight_core_replay{false};

    [[nodiscard]] bool is_valid_architecture_core_count() const noexcept {
        return core_count == 1 || core_count == 2 || core_count == 4 || core_count == 8;
    }

    [[nodiscard]] bool is_replay_authorized() const noexcept {
        if (core_count == 1) return true;
        if (core_count == 2) return allow_experimental_smp;
        if (core_count == 4) return allow_experimental_smp && allow_four_core_replay;
        if (core_count == 8) return allow_experimental_smp && allow_eight_core_replay;
        return false; // arbitrary topologies strictly rejected
    }
};

// -----------------------------------------------------------------------------
// SmpTopology: Formal multi-core topology descriptor and state container
// -----------------------------------------------------------------------------
struct SmpTopology {
    std::uint32_t core_count{1};
    std::vector<SmpCoreState> cores;

    [[nodiscard]] static bool is_valid_architecture_core_count(std::uint32_t count) noexcept {
        return count == 1 || count == 2 || count == 4 || count == 8;
    }

    [[nodiscard]] static bool is_valid_replay_core_count(std::uint32_t count,
                                                         bool allow_experimental_smp,
                                                         bool allow_four_core,
                                                         bool allow_eight_core = false) noexcept {
        if (count == 1) return true;
        if (count == 2) return allow_experimental_smp;
        if (count == 4) return allow_experimental_smp && allow_four_core;
        if (count == 8) return allow_experimental_smp && allow_eight_core;
        return false; // arbitrary topologies rejected
    }

    [[nodiscard]] SmpTopologyValidationResult validate() const noexcept;
    void reset_to_defaults(std::uint32_t count);
};

} // namespace winios
