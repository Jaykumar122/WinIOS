#include "cpu/smp_topology.h"

#include <vector>

namespace winios {

SmpTopologyValidationResult SmpTopology::validate() const noexcept {
    if (core_count == 0) {
        return SmpTopologyValidationResult::ZeroCoreCount;
    }
    if (core_count > SmpTopologyConfig::kMaxSupportedCores) {
        return SmpTopologyValidationResult::ExcessiveCoreCount;
    }
    if (!is_valid_architecture_core_count(core_count)) {
        return SmpTopologyValidationResult::UnsupportedCoreCount;
    }
    if (cores.size() != core_count) {
        return SmpTopologyValidationResult::NonContiguousCoreId;
    }

    std::vector<bool> seen_core(core_count, false);
    for (std::size_t i = 0; i < cores.size(); ++i) {
        const auto& c = cores[i];
        if (c.core_id >= core_count) {
            return SmpTopologyValidationResult::NonContiguousCoreId;
        }
        if (seen_core[c.core_id]) {
            return SmpTopologyValidationResult::DuplicateCoreId;
        }
        seen_core[c.core_id] = true;
    }

    for (std::size_t i = 0; i < core_count; ++i) {
        if (!seen_core[i]) {
            return SmpTopologyValidationResult::NonContiguousCoreId;
        }
    }

    return SmpTopologyValidationResult::Success;
}

void SmpTopology::reset_to_defaults(std::uint32_t count) {
    core_count = count;
    cores.clear();
    cores.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        cores[i].reset(i, (i == 0) ? CoreLifecycleState::Runnable : CoreLifecycleState::Halted);
    }
}

} // namespace winios
