#include "packaging/canonical_path_validator.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace winios::packaging {

bool CanonicalPathValidator::is_windows_reserved_device_name(const std::string& segment) {
    if (segment.empty()) return false;

    // Extract base name before extension (e.g. "CON" from "CON.txt")
    std::string base = segment;
    auto dot_pos = base.find('.');
    if (dot_pos != std::string::npos) {
        base = base.substr(0, dot_pos);
    }

    std::string upper_base = base;
    std::transform(upper_base.begin(), upper_base.end(), upper_base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper_base == "CON" || upper_base == "PRN" || upper_base == "AUX" || upper_base == "NUL") {
        return true;
    }

    if (upper_base.size() == 4) {
        if ((upper_base.rfind("COM", 0) == 0 || upper_base.rfind("LPT", 0) == 0) &&
            upper_base[3] >= '1' && upper_base[3] <= '9') {
            return true;
        }
    }

    return false;
}

bool CanonicalPathValidator::contains_device_namespace(const std::string& path) noexcept {
    if (path.size() < 4) return false;
    // Check \\?\, //?/, \\.\, //./, \??\, /??/
    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/')) {
        if ((path[2] == '?' || path[2] == '.') &&
            (path[3] == '\\' || path[3] == '/')) {
            return true;
        }
    }
    if ((path[0] == '\\' || path[0] == '/') &&
        path[1] == '?' && path[2] == '?' &&
        (path[3] == '\\' || path[3] == '/')) {
        return true;
    }
    return false;
}

bool CanonicalPathValidator::contains_short_name_alias(const std::string& segment) noexcept {
    auto tilde_pos = segment.find('~');
    if (tilde_pos == std::string::npos || tilde_pos == 0) return false;
    // If tilde followed by digits: e.g. PROGRA~1
    if (tilde_pos + 1 < segment.size() && std::isdigit(static_cast<unsigned char>(segment[tilde_pos + 1]))) {
        return true;
    }
    return false;
}

bool CanonicalPathValidator::is_reparse_point(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)path;
    return false;
#endif
}

bool CanonicalPathValidator::resolve_canonical_physical_path(
    const std::string& path,
    std::string& out_canonical) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    char buffer[1024];
    DWORD len = GetFinalPathNameByHandleA(
        hFile,
        buffer,
        sizeof(buffer),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS
    );
    CloseHandle(hFile);

    if (len == 0 || len >= sizeof(buffer)) {
        return false;
    }

    std::string result(buffer, len);
    if (result.rfind("\\\\?\\", 0) == 0) {
        result = result.substr(4);
    }
    std::replace(result.begin(), result.end(), '\\', '/');
    out_canonical = std::move(result);
    return true;
#else
    (void)path;
    (void)out_canonical;
    return false;
#endif
}

PathValidationStatus CanonicalPathValidator::validate_and_normalize_bundle_path(
    const std::string& raw_path,
    std::string& out_normalized) {

    out_normalized.clear();

    if (raw_path.empty()) {
        return PathValidationStatus::EmptyPath;
    }

    if (raw_path.size() > kMaxPathLength) {
        return PathValidationStatus::PathTooLong;
    }

    // 1. Check for null bytes and control characters
    for (unsigned char c : raw_path) {
        if (c == '\0') {
            return PathValidationStatus::NullCharacterForbidden;
        }
        if (c < 0x20) {
            return PathValidationStatus::ControlCharactersForbidden;
        }
    }

    // 2. Check for Windows device namespaces (\\?\, \??\, \\.\)
    if (contains_device_namespace(raw_path)) {
        return PathValidationStatus::DeviceNamespaceForbidden;
    }

    // 3. Check for Drive letters or Alternate Data Streams (colons)
    auto colon_pos = raw_path.find(':');
    if (colon_pos != std::string::npos) {
        if (colon_pos == 1 && std::isalpha(static_cast<unsigned char>(raw_path[0]))) {
            return PathValidationStatus::DriveLetterForbidden;
        }
        return PathValidationStatus::AlternateDataStreamForbidden;
    }

    // 4. Check for UNC network paths
    if ((raw_path.size() >= 2) &&
        ((raw_path[0] == '/' && raw_path[1] == '/') ||
         (raw_path[0] == '\\' && raw_path[1] == '\\'))) {
        return PathValidationStatus::UncPathForbidden;
    }

    // 4. Check for absolute root
    if (raw_path.front() == '/' || raw_path.front() == '\\') {
        return PathValidationStatus::AbsoluteRootForbidden;
    }

    // 5. Parse and normalize segments
    std::vector<std::string> segments;
    std::string current_seg;

    for (std::size_t i = 0; i <= raw_path.size(); ++i) {
        char c = (i < raw_path.size()) ? raw_path[i] : '/';
        if (c == '/' || c == '\\') {
            if (!current_seg.empty()) {
                if (current_seg == ".") {
                    // Current directory: ignore
                } else if (current_seg == "..") {
                    // Parent directory: pop or reject
                    if (segments.empty()) {
                        return PathValidationStatus::DirectoryTraversal;
                    }
                    segments.pop_back();
                } else {
                    // Check trailing dots or spaces in segment
                    if (current_seg.back() == '.' || current_seg.back() == ' ') {
                        return PathValidationStatus::TrailingDotOrSpace;
                    }

                    // Check for reserved Windows device names
                    if (is_windows_reserved_device_name(current_seg)) {
                        return PathValidationStatus::WindowsReservedDevice;
                    }

                    // Check for short name aliases (e.g. PROGRA~1)
                    if (contains_short_name_alias(current_seg)) {
                        return PathValidationStatus::ShortNameForbidden;
                    }

                    segments.push_back(current_seg);
                    if (segments.size() > kMaxPathDepth) {
                        return PathValidationStatus::DepthExceeded;
                    }
                }
                current_seg.clear();
            }
        } else {
            current_seg.push_back(c);
        }
    }

    if (segments.empty()) {
        return PathValidationStatus::EmptyPath;
    }

    // 6. Rebuild canonical normalized path with '/' separators
    std::ostringstream oss;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) oss << '/';
        oss << segments[i];
    }

    out_normalized = oss.str();
    if (out_normalized.size() > kMaxPathLength) {
        out_normalized.clear();
        return PathValidationStatus::PathTooLong;
    }

    return PathValidationStatus::Valid;
}

bool CanonicalPathValidator::is_strictly_inside_root(
    const std::string& root_dir,
    const std::string& target_path) {

    if (root_dir.empty() || target_path.empty()) {
        return false;
    }

    std::string root_to_check = root_dir;
    std::string target_to_check = target_path;

    std::string physical_root;
    if (resolve_canonical_physical_path(root_dir, physical_root)) {
        root_to_check = physical_root;
    }

    std::string physical_target;
    if (resolve_canonical_physical_path(target_path, physical_target)) {
        target_to_check = physical_target;
    } else {
        // Target does not exist yet on disk; walk parent chain to verify no parent is a reparse point or junction escaping root
        std::filesystem::path cur = std::filesystem::path(target_path).lexically_normal();
        auto parent = cur.parent_path();
        while (!parent.empty() && parent != cur) {
            std::string canon_parent;
            if (resolve_canonical_physical_path(parent.string(), canon_parent)) {
                std::string norm_r = root_to_check;
                std::replace(norm_r.begin(), norm_r.end(), '\\', '/');
                while (!norm_r.empty() && norm_r.back() == '/') norm_r.pop_back();
                norm_r.push_back('/');

                std::string norm_p = canon_parent;
                std::replace(norm_p.begin(), norm_p.end(), '\\', '/');
                while (!norm_p.empty() && norm_p.back() == '/') norm_p.pop_back();
                norm_p.push_back('/');

                std::string lower_r = norm_r;
                std::string lower_p = norm_p;
                std::transform(lower_r.begin(), lower_r.end(), lower_r.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(lower_p.begin(), lower_p.end(), lower_p.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (lower_p.rfind(lower_r, 0) != 0 && lower_p != lower_r) {
                    return false; // Parent escaped sandbox via junction or symlink!
                }
                break;
            }
            cur = parent;
            parent = cur.parent_path();
        }
    }

    // Normalize separators in both
    std::string norm_root = root_to_check;
    std::replace(norm_root.begin(), norm_root.end(), '\\', '/');
    while (!norm_root.empty() && norm_root.back() == '/') {
        norm_root.pop_back();
    }
    norm_root.push_back('/'); // Ensure trailing slash for prefix matching

    std::string norm_target = target_to_check;
    std::replace(norm_target.begin(), norm_target.end(), '\\', '/');

    // Case-insensitive comparison on Windows
    std::string lower_root = norm_root;
    std::string lower_target = norm_target;
    std::transform(lower_root.begin(), lower_root.end(), lower_root.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return (lower_target.rfind(lower_root, 0) == 0);
}

PathValidationStatus CanonicalPathValidator::validate_and_normalize_vfs_path(
    const std::string& raw_path,
    std::string& out_normalized) {

    out_normalized.clear();

    if (raw_path.empty()) {
        return PathValidationStatus::EmptyPath;
    }

    if (raw_path.size() > kMaxPathLength) {
        return PathValidationStatus::PathTooLong;
    }

    // 1. Check for null bytes and control characters
    for (unsigned char c : raw_path) {
        if (c == '\0') {
            return PathValidationStatus::NullCharacterForbidden;
        }
        if (c < 0x20) {
            return PathValidationStatus::ControlCharactersForbidden;
        }
    }

    // 2. Check for Windows device namespaces (\\?\, \??\, \\.\)
    if (contains_device_namespace(raw_path)) {
        return PathValidationStatus::DeviceNamespaceForbidden;
    }

    // 3. Check for Drive letters or Alternate Data Streams (colons)
    auto colon_pos = raw_path.find(':');
    if (colon_pos != std::string::npos) {
        if (colon_pos == 1 && std::isalpha(static_cast<unsigned char>(raw_path[0]))) {
            return PathValidationStatus::DriveLetterForbidden;
        }
        return PathValidationStatus::AlternateDataStreamForbidden;
    }

    // 4. Check for UNC network paths
    if ((raw_path.size() >= 2) &&
        ((raw_path[0] == '/' && raw_path[1] == '/') ||
         (raw_path[0] == '\\' && raw_path[1] == '\\'))) {
        return PathValidationStatus::UncPathForbidden;
    }

    // 4. VFS absolute paths must start with root separator '/' or '\'
    if (raw_path.front() != '/' && raw_path.front() != '\\') {
        return PathValidationStatus::DirectoryTraversal;
    }

    // 5. Parse and normalize segments
    std::vector<std::string> segments;
    std::string current_seg;

    for (std::size_t i = 1; i <= raw_path.size(); ++i) {
        char c = (i < raw_path.size()) ? raw_path[i] : '/';
        if (c == '/' || c == '\\') {
            if (!current_seg.empty()) {
                if (current_seg == ".") {
                    // Current directory: ignore
                } else if (current_seg == "..") {
                    // Parent directory: pop or reject if already at root
                    if (segments.empty()) {
                        return PathValidationStatus::DirectoryTraversal;
                    }
                    segments.pop_back();
                } else {
                    // Check trailing dots or spaces in segment
                    if (current_seg.back() == '.' || current_seg.back() == ' ') {
                        return PathValidationStatus::TrailingDotOrSpace;
                    }

                    // Check for reserved Windows device names
                    if (is_windows_reserved_device_name(current_seg)) {
                        return PathValidationStatus::WindowsReservedDevice;
                    }

                    // Check for short name aliases (e.g. PROGRA~1)
                    if (contains_short_name_alias(current_seg)) {
                        return PathValidationStatus::ShortNameForbidden;
                    }

                    segments.push_back(current_seg);
                    if (segments.size() > kMaxPathDepth) {
                        return PathValidationStatus::DepthExceeded;
                    }
                }
                current_seg.clear();
            }
        } else {
            current_seg.push_back(c);
        }
    }

    // 6. Rebuild canonical normalized path with leading '/'
    std::ostringstream oss;
    oss << '/';
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) oss << '/';
        oss << segments[i];
    }

    out_normalized = oss.str();
    if (out_normalized.size() > kMaxPathLength) {
        out_normalized.clear();
        return PathValidationStatus::PathTooLong;
    }

    return PathValidationStatus::Valid;
}

} // namespace winios::packaging
