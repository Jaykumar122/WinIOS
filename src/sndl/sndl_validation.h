#pragma once

#include "sndl/sndl_format.h"
#include "sndl/sndl_types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// SndlInspectionInfo: Complete Diagnostic Inspection Metadata
// -----------------------------------------------------------------------------
struct SndlInspectionInfo {
    WSndlHeader header{};
    WSndlFooter footer{};
    std::vector<WSndlBlockHeader> blocks;
    std::vector<SndlRecord> sample_records;
    std::uint64_t file_size{0};
    std::uint64_t event_type_counts[16]{};
    bool footer_present{false};
    bool cleanly_finalized{false};
};

// -----------------------------------------------------------------------------
// SndlValidation: Non-Mutating File Integrity & Diagnostic Verifier
// -----------------------------------------------------------------------------
class SndlValidation {
public:
    // Validates an .sndl file from disk with optional baseline snapshot association check
    [[nodiscard]] static SndlValidationResult validate_file(
        const std::filesystem::path& file_path,
        const std::optional<std::filesystem::path>& snapshot_path = std::nullopt);

    // Validates an in-memory buffer
    [[nodiscard]] static SndlValidationResult validate_buffer(
        const std::uint8_t* data, std::size_t size,
        std::uint64_t expected_snapshot_hash = 0);

    // Deep inspection extracting all block headers, footer, and event statistics
    [[nodiscard]] static SndlResult inspect_file(
        const std::filesystem::path& file_path,
        SndlInspectionInfo& out_info);

    // Human-readable summary formatter
    [[nodiscard]] static std::string format_summary(const SndlInspectionInfo& info);
};

} // namespace winios::sndl
