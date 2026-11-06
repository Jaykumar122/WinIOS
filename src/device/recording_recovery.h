#pragma once

#include "device/dma_payload_store.h"
#include "device/virtual_dma_types.h"
#include "sndl/sndl_v2_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// RecoveryOptions: Configuration for conservative interrupted recording salvage
// -----------------------------------------------------------------------------
struct RecoveryOptions {
    std::filesystem::path input_sndl_path;
    std::filesystem::path input_dma_path;          // Inferred if empty (input_sndl_path + ".dma")
    std::filesystem::path output_directory;        // Isolated destination directory (MANDATORY)
    bool trim_incomplete_trailing_dma{true};       // If true, discard partial trailing uncompleted DMA
};

// -----------------------------------------------------------------------------
// RecoveryReport: Structured outcome of recovery operation
// -----------------------------------------------------------------------------
struct RecoveryReport {
    bool success{false};
    std::string error_message;
    std::filesystem::path output_sndl_path;
    std::filesystem::path output_dma_path;
    std::uint64_t salvaged_event_count{0};
    std::uint64_t salvaged_block_count{0};
    std::uint64_t salvaged_payload_count{0};
    std::uint64_t discarded_bytes{0};
    std::uint64_t last_valid_global_sequence{0};
    std::uint64_t last_valid_retirement_sequence{0};
    std::uint64_t session_id{0};
    std::uint64_t base_snapshot_hash{0};
    sndl::SndlV2CompletionStatus final_completion_status{sndl::SndlV2CompletionStatus::RecoveredIncomplete};

    [[nodiscard]] std::string format_summary() const;
};

// -----------------------------------------------------------------------------
// RecordingRecoveryScanner: Crash-consistent salvage and isolation engine
// -----------------------------------------------------------------------------
class RecordingRecoveryScanner {
public:
    RecordingRecoveryScanner() = default;
    ~RecordingRecoveryScanner() = default;

    // Scans damaged or interrupted recording and salvages intact records into output_directory
    [[nodiscard]] RecoveryReport recover(const RecoveryOptions& options);
};

} // namespace winios::device
