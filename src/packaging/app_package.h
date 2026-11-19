#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::security {
class PublisherTrustStore;
}

namespace winios::packaging {

enum class PackageValidationResult : std::uint32_t {
    Valid = 0,
    InvalidMagic = 1,
    CorruptedHeader = 2,
    ManifestTooLarge = 3,
    InvalidManifest = 4,
    FileCountExceeded = 5,
    PayloadTruncated = 6,
    PathTraversalAttempt = 7,
    DuplicateFileEntry = 8,
    ExecutableNotFound = 9,
    ExecutableInvalid = 10,
    ChecksumMismatch = 11,
    IntegerOverflow = 12,
    UnsupportedArchitecture = 13,
    EntrySizeMismatch = 14,
    OverlappingEntries = 15,
    UntrustedSignature = 16,
    QuotaExceeded = 17,
    InvalidEntryType = 18
};

inline const char* package_validation_result_to_string(PackageValidationResult res) noexcept {
    switch (res) {
    case PackageValidationResult::Valid: return "Valid";
    case PackageValidationResult::InvalidMagic: return "InvalidMagic";
    case PackageValidationResult::CorruptedHeader: return "CorruptedHeader";
    case PackageValidationResult::ManifestTooLarge: return "ManifestTooLarge";
    case PackageValidationResult::InvalidManifest: return "InvalidManifest";
    case PackageValidationResult::FileCountExceeded: return "FileCountExceeded";
    case PackageValidationResult::PayloadTruncated: return "PayloadTruncated";
    case PackageValidationResult::PathTraversalAttempt: return "PathTraversalAttempt";
    case PackageValidationResult::DuplicateFileEntry: return "DuplicateFileEntry";
    case PackageValidationResult::ExecutableNotFound: return "ExecutableNotFound";
    case PackageValidationResult::ExecutableInvalid: return "ExecutableInvalid";
    case PackageValidationResult::ChecksumMismatch: return "ChecksumMismatch";
    case PackageValidationResult::IntegerOverflow: return "IntegerOverflow";
    case PackageValidationResult::UnsupportedArchitecture: return "UnsupportedArchitecture";
    case PackageValidationResult::EntrySizeMismatch: return "EntrySizeMismatch";
    case PackageValidationResult::OverlappingEntries: return "OverlappingEntries";
    case PackageValidationResult::UntrustedSignature: return "UntrustedSignature";
    case PackageValidationResult::QuotaExceeded: return "QuotaExceeded";
    case PackageValidationResult::InvalidEntryType: return "InvalidEntryType";
    default: return "UnknownValidationResult";
    }
}

enum class PackageTrustLevel : std::uint32_t {
    Unsigned = 0,
    DeveloperLocal = 1,
    PlatformSigned = 2
};

struct PackageTrustInfo {
    PackageTrustLevel trust_level{PackageTrustLevel::Unsigned};
    bool integrity_verified{false};
    std::string digest_hex;
    std::string signature_hex;
};

struct ResourceLimits {
    std::uint64_t max_memory_bytes{32 * 1024 * 1024};
    std::uint64_t max_instruction_budget{10000000ULL};
    std::uint32_t max_open_handles{64};
};

struct FileEntry {
    std::string path; // Relative path inside bundle, e.g. "bin/calc", "res/icon.png"
    std::uint64_t size{0};
    std::string sha256_hex;
    std::vector<std::uint8_t> data;
};

struct PackageManifest {
    std::string format_version{"2.0"};
    std::string bundle_id;
    std::string name;
    std::string display_name;
    std::string version{"1.0.0"};
    std::string min_os_version{"221.0"};
    std::string architecture{"arm64"};
    std::string min_abi_version{"1.5"};
    std::uint32_t min_sdk_version_u32{0x00020300};
    std::string target_abi_version{"2.4"};
    std::string executable_name;
    std::uint64_t storage_quota_bytes{10 * 1024 * 1024}; // 10 MB default
    ResourceLimits resource_limits{};
    std::vector<std::string> supported_doc_types;
    std::string entry_symbol{"_main"};
    std::uint32_t entry_stack_size{65536};
    std::string category{"Utilities"};
    std::string author;
    std::string description;
    std::string signature_sha256;
    std::vector<std::string> permissions;
    std::vector<std::string> optional_permissions;
    std::string icon_path;
    std::string startup_config;
    std::vector<FileEntry> files;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::string serialize() const;
    static bool deserialize(const std::string& text, PackageManifest& out_manifest);
};

struct AppPackage {
    PackageManifest manifest;

    // Helper to add a file to package
    void add_file(const std::string& rel_path, std::vector<std::uint8_t> data);

    // Serialization to single package payload (.wios container)
    [[nodiscard]] std::vector<std::uint8_t> pack() const;
    [[nodiscard]] std::vector<std::uint8_t> pack_v3() const;

    // Deserialization from package payload
    static bool unpack(const std::uint8_t* data, std::size_t size, AppPackage& out_pkg);

    // Strict validation with detailed error diagnosis
    static PackageValidationResult validate_package_data(const std::uint8_t* data,
                                                         std::size_t size,
                                                         AppPackage* out_pkg = nullptr);

    // Evaluate trust policy
    static PackageTrustInfo evaluate_trust(const AppPackage& pkg, bool allow_developer_trust = true);
    static PackageTrustInfo evaluate_trust(const std::uint8_t* raw_pkg_data,
                                           std::size_t raw_pkg_size,
                                           const security::PublisherTrustStore& store,
                                           bool allow_developer_trust = true);
};

} // namespace winios::packaging
