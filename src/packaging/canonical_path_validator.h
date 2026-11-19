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
    EscapesSandboxRoot = 13,
    ReparsePointForbidden = 14,
    DeviceNamespaceForbidden = 15,
    ShortNameForbidden = 16
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
    case PathValidationStatus::ReparsePointForbidden: return "ReparsePointForbidden";
    case PathValidationStatus::DeviceNamespaceForbidden: return "DeviceNamespaceForbidden";
    case PathValidationStatus::ShortNameForbidden: return "ShortNameForbidden";
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
    // Physically resolves symlinks and NTFS junctions to prevent directory junction jailbreaks.
    static bool is_strictly_inside_root(
        const std::string& root_dir,
        const std::string& target_path);

    // Checks if a path is an NTFS junction or symlink reparse point.
    static bool is_reparse_point(const std::string& path);

    // Resolves true physical canonical path on Windows using GetFinalPathNameByHandle.
    static bool resolve_canonical_physical_path(const std::string& path, std::string& out_canonical);

    // Verify whether an open file handle is strictly confined within root_dir
    static bool is_handle_strictly_inside_root(void* file_handle, const std::string& root_dir);

    // Verify whether an open file handle is strictly confined within an open root directory handle
    static bool is_handle_strictly_inside_root_handle(void* file_handle, void* root_dir_handle);

    // Verify whether two open handles reside on the exact same physical volume
    static bool check_volume_identity(void* handle1, void* handle2) noexcept;

    // Securely opens a file guaranteed to be confined within root_dir, defeating symlink/junction escapes
    struct ConfinedFileHandle {
        void* handle{nullptr};
        std::string canonical_path;
        bool is_valid{false};
    };

    static ConfinedFileHandle open_confined_file_read(const std::string& root_dir, const std::string& rel_path);
    static ConfinedFileHandle open_confined_file_write(const std::string& root_dir, const std::string& rel_path, bool create_new = true);
    static void close_confined_file(ConfinedFileHandle& confined);

    // Checks if path contains a Windows device namespace prefix (\\?\, \\.\, \??\).
    static bool contains_device_namespace(const std::string& path) noexcept;

    // Checks if segment contains an 8.3 short name alias pattern (e.g. PROGRA~1).
    static bool contains_short_name_alias(const std::string& segment) noexcept;

    // Checks if a name is a Windows reserved device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9).
    static bool is_windows_reserved_device_name(const std::string& segment);
};

} // namespace winios::packaging
