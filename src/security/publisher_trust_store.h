#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>

#include "security/security_utils.h"

namespace winios::security {

enum class SignatureAlgorithm : std::uint32_t {
    HmacSha256 = 1,
    Ed25519 = 2
};

enum class PublisherRole : std::uint32_t {
    Developer = 1,
    Platform = 2,
    RootAuthority = 3
};

enum class SignatureStatus : std::uint32_t {
    Unsigned = 0,
    DeveloperSigned = 1,
    PlatformSigned = 2,
    RevokedPublisher = 3,
    CorruptSignature = 4,
    ExpiredSignature = 5,
    UnknownKey = 6,
    AlgorithmMismatch = 7
};

inline const char* signature_status_to_string(SignatureStatus s) noexcept {
    switch (s) {
    case SignatureStatus::Unsigned: return "Unsigned";
    case SignatureStatus::DeveloperSigned: return "DeveloperSigned";
    case SignatureStatus::PlatformSigned: return "PlatformSigned";
    case SignatureStatus::RevokedPublisher: return "RevokedPublisher";
    case SignatureStatus::CorruptSignature: return "CorruptSignature";
    case SignatureStatus::ExpiredSignature: return "ExpiredSignature";
    case SignatureStatus::UnknownKey: return "UnknownKey";
    case SignatureStatus::AlgorithmMismatch: return "AlgorithmMismatch";
    default: return "Unknown";
    }
}

enum class TrustPolicy : std::uint32_t {
    Development = 1,      // Allow DeveloperSigned and PlatformSigned (and optional Unsigned)
    StrictProduction = 2  // Require PlatformSigned only
};

struct PublisherKey {
    std::string publisher_id;
    std::string organization_name;
    std::string key_id;
    PublisherRole role{PublisherRole::Developer};
    SignatureAlgorithm algorithm{SignatureAlgorithm::HmacSha256};
    std::vector<std::uint8_t> key_bytes;
    std::uint64_t valid_from{0};
    std::uint64_t valid_until{0}; // 0 = no expiration
    bool is_revoked{false};
};

struct SignatureVerificationResult {
    SignatureStatus status{SignatureStatus::Unsigned};
    bool is_trusted{false};
    std::string publisher_id;
    std::string key_id;
    PublisherRole role{PublisherRole::Developer};
    std::uint32_t trailer_version{0};
    std::uint32_t trailer_flags{0};
    std::string failure_reason;
    std::string digest_hex;
    std::string signature_hex;
};

// Compute RFC 2104 HMAC-SHA256
void hmac_sha256(const void* key, std::size_t key_len,
                 const void* msg, std::size_t msg_len,
                 std::uint8_t out[32]) noexcept;

struct DeveloperIdentity;

class PublisherTrustStore {
public:
    PublisherTrustStore() = default;

    // Key management
    bool register_key(const PublisherKey& key);
    bool register_root_anchor(const PublisherKey& key);
    bool revoke_key(const std::string& key_id);
    bool revoke_publisher(const std::string& publisher_id);

    bool is_key_revoked(const std::string& key_id) const;
    bool is_publisher_revoked(const std::string& publisher_id) const;
    bool is_root_anchor(const std::string& key_id) const;

    std::optional<PublisherKey> get_key(const std::string& key_id) const;
    std::optional<PublisherKey> get_root_anchor() const;
    std::vector<PublisherKey> get_keys_for_publisher(const std::string& publisher_id) const;
    bool rotate_key(const std::string& old_key_id, const PublisherKey& new_key);

    std::size_t total_keys() const noexcept { return keys_.size(); }
    const std::unordered_map<std::string, PublisherKey>& all_keys() const noexcept { return keys_; }

    // Serialization / config loading
    std::string serialize() const;
    bool deserialize(const std::string& text);

    // Default test fixture
    static PublisherTrustStore create_default_test_store();
    static PublisherKey get_test_platform_key();
    static PublisherKey get_test_developer_key();
    static PublisherKey get_test_revoked_key();
    static PublisherKey get_test_platform_ed25519_key();
    static PublisherKey get_test_developer_ed25519_key();
    static PublisherKey get_test_root_anchor_key();

    // Sign a package in-place by appending a WIOSAUTH trailer block (HMAC-SHA256)
    static bool sign_package(std::vector<std::uint8_t>& pkg_data,
                             const PublisherKey& key,
                             std::uint64_t valid_from = 0,
                             std::uint64_t valid_until = 0);

    // Sign a package in-place by appending a WIOSAUTH trailer block (Ed25519 asymmetric)
    static bool sign_package_ed25519(std::vector<std::uint8_t>& pkg_data,
                                     const DeveloperIdentity& identity,
                                     std::uint64_t valid_from = 0,
                                     std::uint64_t valid_until = 0);

    // Sign a package in-place with WIOSAUTH v2 trailer (Ed25519 asymmetric)
    static bool sign_package_v2(std::vector<std::uint8_t>& pkg_data,
                                const DeveloperIdentity& identity,
                                std::uint32_t flags = 0,
                                std::uint64_t valid_from = 0,
                                std::uint64_t valid_until = 0);

    // Sign a package in-place with WIOSAUTH v2 trailer (HMAC-SHA256)
    static bool sign_package_hmac_v2(std::vector<std::uint8_t>& pkg_data,
                                     const PublisherKey& key,
                                     std::uint32_t flags = 0,
                                     std::uint64_t valid_from = 0,
                                     std::uint64_t valid_until = 0);

    // Verify package signature against this trust store
    SignatureVerificationResult verify_package_signature(
        const std::uint8_t* pkg_data,
        std::size_t pkg_size,
        TrustPolicy policy,
        std::uint64_t current_time = 0) const;

private:
    std::unordered_map<std::string, PublisherKey> keys_;
    std::unordered_set<std::string> revoked_keys_;
    std::unordered_set<std::string> revoked_publishers_;
    std::optional<std::string> root_anchor_key_id_;
};

} // namespace winios::security
