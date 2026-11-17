#include "loader/app_package.h"
#include "loader/linux_image_loader.h"
#include <algorithm>
#include <cctype>

namespace winios::loader {

static bool is_valid_bundle_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    // Bundle ID should contain at least one dot (e.g. "com.example.app")
    bool has_dot = false;
    for (char c : id) {
        if (c == '.') {
            has_dot = true;
        } else if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            return false;
        }
    }
    return has_dot;
}

PackageValidationResult AppPackageValidator::validate_manifest(
    const AppManifest& manifest,
    std::uint32_t current_runtime_version) {

    if (!is_valid_bundle_id(manifest.app_id)) {
        return PackageValidationResult::InvalidAppId;
    }

    if (manifest.display_name.empty() || manifest.binary_path.empty()) {
        return PackageValidationResult::MissingRequiredField;
    }

    if (manifest.min_runtime_version > current_runtime_version) {
        return PackageValidationResult::RuntimeVersionIncompatible;
    }

    // Check resource limits
    if (manifest.max_memory_bytes == 0 || manifest.max_memory_bytes > 64 * 1024 * 1024) {
        return PackageValidationResult::ResourceBudgetExceeded;
    }

    if (manifest.max_surfaces == 0 || manifest.max_surfaces > 16) {
        return PackageValidationResult::ResourceBudgetExceeded;
    }

    return PackageValidationResult::Valid;
}

PackageValidationResult AppPackageValidator::validate_binary_hash(
    const AppManifest& manifest,
    const std::uint8_t* binary_data,
    std::size_t binary_size) {

    if (!binary_data || binary_size == 0) {
        return PackageValidationResult::MissingRequiredField;
    }

    if (!manifest.binary_sha256.empty()) {
        std::string computed = LinuxImageLoader::compute_sha256(binary_data, binary_size);
        if (computed != manifest.binary_sha256) {
            return PackageValidationResult::BinaryHashMismatch;
        }
    }

    return PackageValidationResult::Valid;
}

std::string AppPackageValidator::result_to_string(PackageValidationResult res) noexcept {
    switch (res) {
    case PackageValidationResult::Valid: return "Valid";
    case PackageValidationResult::InvalidJsonOrFormat: return "InvalidJsonOrFormat";
    case PackageValidationResult::MissingRequiredField: return "MissingRequiredField";
    case PackageValidationResult::InvalidAppId: return "InvalidAppId";
    case PackageValidationResult::BinaryHashMismatch: return "BinaryHashMismatch";
    case PackageValidationResult::RuntimeVersionIncompatible: return "RuntimeVersionIncompatible";
    case PackageValidationResult::ResourceBudgetExceeded: return "ResourceBudgetExceeded";
    default: return "Unknown";
    }
}

} // namespace winios::loader
