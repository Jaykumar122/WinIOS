#include "packaging/canonical_path_validator.h"

#include <algorithm>
#include <cctype>
#include <sstream>

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

    // 2. Check for Drive letters or Alternate Data Streams (colons)
    auto colon_pos = raw_path.find(':');
    if (colon_pos != std::string::npos) {
        if (colon_pos == 1 && std::isalpha(static_cast<unsigned char>(raw_path[0]))) {
            return PathValidationStatus::DriveLetterForbidden;
        }
        return PathValidationStatus::AlternateDataStreamForbidden;
    }

    // 3. Check for UNC network paths
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

    // Normalize separators in both
    std::string norm_root = root_dir;
    std::replace(norm_root.begin(), norm_root.end(), '\\', '/');
    while (!norm_root.empty() && norm_root.back() == '/') {
        norm_root.pop_back();
    }
    norm_root.push_back('/'); // Ensure trailing slash for prefix matching

    std::string norm_target = target_path;
    std::replace(norm_target.begin(), norm_target.end(), '\\', '/');

    // Check if target starts with root
    return (norm_target.rfind(norm_root, 0) == 0);
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

    // 2. Check for Drive letters or Alternate Data Streams (colons)
    auto colon_pos = raw_path.find(':');
    if (colon_pos != std::string::npos) {
        if (colon_pos == 1 && std::isalpha(static_cast<unsigned char>(raw_path[0]))) {
            return PathValidationStatus::DriveLetterForbidden;
        }
        return PathValidationStatus::AlternateDataStreamForbidden;
    }

    // 3. Check for UNC network paths
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
