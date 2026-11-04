#pragma once

#include "replay/replay_profiler.h"
#include "replay/replay_types.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ProcessMemorySnapshot: Host process memory probe (Windows process APIs)
// -----------------------------------------------------------------------------
struct ProcessMemorySnapshot {
    bool available{false};
    std::uint64_t working_set_bytes{0};
    std::uint64_t peak_working_set_bytes{0};
    std::uint64_t private_bytes{0};
    std::uint64_t pagefile_usage_bytes{0};
    std::uint64_t handle_count{0};
};

[[nodiscard]] ProcessMemorySnapshot sample_process_memory() noexcept;

// -----------------------------------------------------------------------------
// ReplayMemoryReport: Artifact size, replay counters, and host memory deltas
// -----------------------------------------------------------------------------
struct ReplayMemoryReport {
    std::string label;
    bool replay_success{false};
    ReplayCompletionStatus completion_status{ReplayCompletionStatus::CompletedNormally};
    ReplayErrorCode replay_error{ReplayErrorCode::Success};

    std::uint64_t memory_capacity{0};
    std::uint64_t wsnap_file_size{0};
    std::uint64_t sndl_file_size{0};
    std::uint64_t instructions_executed{0};
    std::uint64_t events_consumed{0};
    std::uint64_t checkpoints_taken{0};
    std::uint64_t checkpoint_interval{0};
    double elapsed_ms{0.0};

    ProcessMemorySnapshot before;
    ProcessMemorySnapshot after;
};

class ReplayMemoryReporter {
public:
    // Runs one replay session and samples process memory immediately before
    // prepare and immediately after run. No guest state is retained afterwards.
    [[nodiscard]] static ReplayMemoryReport measure(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        const ReplayConfig& config,
        const std::string& label = "replay"
    );

    [[nodiscard]] static std::string to_text(const ReplayMemoryReport& report);
    [[nodiscard]] static std::string to_json(const ReplayMemoryReport& report);

    [[nodiscard]] static ReplayResult write_text(
        const std::filesystem::path& path,
        const ReplayMemoryReport& report
    );
    [[nodiscard]] static ReplayResult write_json(
        const std::filesystem::path& path,
        const ReplayMemoryReport& report
    );
};

} // namespace winios::replay
