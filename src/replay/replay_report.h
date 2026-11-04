#pragma once

#include "replay/replay_scheduler.h"
#include "replay/replay_types.h"

#include <filesystem>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayReport: Generates Structured JSON and Human Summaries
// -----------------------------------------------------------------------------
class ReplayReport {
public:
    [[nodiscard]] static std::string to_json(
        const ReplayExecutionStats& stats,
        const std::string& session_id = "replay_session"
    );

    [[nodiscard]] static ReplayResult write_json(
        const std::filesystem::path& path,
        const ReplayExecutionStats& stats,
        const std::string& session_id = "replay_session"
    );

    [[nodiscard]] static std::string to_summary_string(
        const ReplayExecutionStats& stats
    );
};

} // namespace winios::replay
