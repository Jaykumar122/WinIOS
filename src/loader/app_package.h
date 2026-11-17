#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

enum class AppCapability : std::uint32_t {
    None    = 0,
    Display = 1u << 0,
    Input   = 1u << 1,
    Storage = 1u << 2,
    Audio   = 1u << 3
};

inline constexpr AppCapability operator|(AppCapability a, AppCapability b) noexcept {
    return static_cast<AppCapability>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline constexpr bool operator&(AppCapability a, AppCapability b) noexcept {
    return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}

struct AppManifest {
    std::uint32_t package_version{1};
    std::string app_id;
    std::string display_name;
    std::string binary_path;
    std::string binary_sha256;
    std::uint32_t min_runtime_version{0x00021400};
    AppCapability capabilities{AppCapability::Display | AppCapability::Input};
    std::size_t max_memory_bytes{8 * 1024 * 1024}; // 8 MB default
    std::uint32_t max_surfaces{4};
    std::uint32_t target_width{800};
    std::uint32_t target_height{1200};
};

enum class PackageValidationResult : std::uint8_t {
    Valid,
    InvalidJsonOrFormat,
    MissingRequiredField,
    InvalidAppId,
    BinaryHashMismatch,
    RuntimeVersionIncompatible,
    ResourceBudgetExceeded
};

class AppPackageValidator {
public:
    static PackageValidationResult validate_manifest(
        const AppManifest& manifest,
        std::uint32_t current_runtime_version = 0x00021400);

    static PackageValidationResult validate_binary_hash(
        const AppManifest& manifest,
        const std::uint8_t* binary_data,
        std::size_t binary_size);

    static std::string result_to_string(PackageValidationResult res) noexcept;
};

} // namespace winios::loader
