#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "snapshot/snapshot_types.h"
#include "snapshot/wsnap_format.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace winios::snapshot {

struct SnapshotRestoreContext {
    CPU* primary_cpu{nullptr};
    Memory* memory{nullptr};
    VirtualTime* virtual_time{nullptr};
    GICv3* gic{nullptr};
    std::vector<CPU*> secondary_cpus{}; // Optional additional SMP cores
};

// Restores architectural state into the components provided in the context.
// Validates the snapshot buffer completely before mutating any target state.
[[nodiscard]] SnapshotResult restore_snapshot_from_buffer(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotRestoreContext& context
);

} // namespace winios::snapshot
