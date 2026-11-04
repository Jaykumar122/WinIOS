#pragma once

#include "sndl/sndl_v2_types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace winios::sndl {

class SndlV2Validator {
public:
    // Streaming full validation of SNDL v2 recording against optional baseline snapshot
    [[nodiscard]] static SndlV2ValidationReport validate_file(
        const std::filesystem::path& sndl_path,
        const std::optional<std::filesystem::path>& baseline_snapshot_path = std::nullopt,
        const SndlV2ValidationOptions& options = {});

    // Fast inspection of header and footer
    [[nodiscard]] static SndlV2Result inspect_file(
        const std::filesystem::path& sndl_path,
        SndlV2InspectionInfo& out_info);

    // Extract records for tests and timeline analysis
    [[nodiscard]] static SndlV2Result extract_events(
        const std::filesystem::path& sndl_path,
        std::vector<SndlV2Record>& out_records,
        std::size_t max_records = 0);

    // Format human-readable summary of SNDL v2 recording
    [[nodiscard]] static std::string format_summary(const SndlV2InspectionInfo& info);

    // Safely recover an incomplete/aborted recording up to the last valid block
    [[nodiscard]] static SndlV2Result recover_incomplete_recording(
        const std::filesystem::path& input_path,
        const std::filesystem::path& recovered_path);
};

} // namespace winios::sndl
