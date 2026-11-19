#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::packaging {

enum class PathValidationStatus : std::uint32_t {
    Valid = 0,
    EmptyPath = 1,
    PathTooLong = 2,
    DepthExceeded = 3,
    AbsoluteRootForbidden = 4,
    DriveLetterForbidden = 5,
    UncPathForbidden = 6,
    DirectoryTraversal = 7,
    WindowsReservedDevice = 8,
    AlternateDataStreamForbidden = 9,
    TrailingDotOrSpace = 10,
    ControlCharactersForbidden = 11,
    NullCharacterForbidden = 12,
    EscapesSandboxRoot = 13
};

inline const char* path_validation_status_to_string(PathValidationStatus status) noexcept {
    switch (status) {
    case PathValidationStatus::Valid: return "Valid";
    case PathValidationStatus::EmptyPath: return "EmptyPath";
    case PathValidationStatus::PathTooLong: return "PathTooLong";
    case PathValidationStatus::DepthExceeded: return "DepthExceeded";
    case PathValidationStatus::AbsoluteRootForbidden: return "AbsoluteRootForbidden";
    case PathValidationStatus::DriveLetterForbidden: return "DriveLetterForbidden";
    case PathValidationStatus::UncPathForbidden: return "UncPathForbidden";
    case PathValidationStatus::DirectoryTraversal: return "DirectoryTraversal";
    case PathValidationStatus::WindowsReservedDevice: return "WindowsReservedDevice";
    case PathValidationStatus::AlternateDataStreamForbidden: return "AlternateDataStreamForbidden";
    case PathValidationStatus::TrailingDotOrSpace: return "TrailingDotOrSpace";
    case PathValidationStatus::ControlCharactersForbidden: return "ControlCharactersForbidden";
    case PathValidationStatus::NullCharacterForbidden: return "NullCharacterForbidden";
    case PathValidationStatus::EscapesSandboxRoot: return "EscapesSandboxRoot";
    default: return "UnknownPathStatus";
    }
}

class CanonicalPathValidator {
public:
    static constexpr std::size_t kMaxPathLength = 256;
    static constexpr std::size_t kMaxPathDepth = 16;

    // Validates a relative bundle-internal path (e.g. "bin/calc", "res/icon.png").
    // Normalizes separators to '/', resolves relative '.' and '..', checks device names,
    // ADS, trailing dots/spaces, control characters, and length/depth boundaries.
    static PathValidationStatus validate_and_normalize_bundle_path(
        const std::string& raw_path,
        std::string& out_normalized);

    // Validates an absolute VFS path (e.g. "/tmp/pkg.wios", "/Applications/app").
    // Normalizes separators to '/', resolves relative '.' and '..', checks traversal out of root,
    // device names, ADS, trailing dots/spaces, control characters, and length/depth boundaries.
    static PathValidationStatus validate_and_normalize_vfs_path(
        const std::string& raw_path,
        std::string& out_normalized);

    // Checks if raw_path is valid according to bundle security rules.
    static bool is_valid_bundle_path(const std::string& raw_path) {
        std::string norm;
        return validate_and_normalize_bundle_path(raw_path, norm) == PathValidationStatus::Valid;
    }

    // Checks if raw_path is valid according to VFS security rules.
    static bool is_valid_vfs_path(const std::string& raw_path) {
        std::string norm;
        return validate_and_normalize_vfs_path(raw_path, norm) == PathValidationStatus::Valid;
    }

    // Validates that target_path resides strictly inside root_dir without escaping.
    static bool is_strictly_inside_root(
        const std::string& root_dir,
        const std::string& target_path);

    // Checks if a name is a Windows reserved device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9).
    static bool is_windows_reserved_device_name(const std::string& segment);
};

} // namespace winios::packaging
