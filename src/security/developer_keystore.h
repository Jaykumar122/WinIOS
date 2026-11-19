#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "security/publisher_trust_store.h"
#include "crypto/ed25519.h"

namespace winios::security {

enum class KeystoreProtection : std::uint32_t {
    None = 0,
    DPAPI = 1,
    Passphrase = 2,
    DPAPIAndPassphrase = 3
};

inline const char* keystore_protection_to_string(KeystoreProtection p) noexcept {
    switch (p) {
    case KeystoreProtection::None: return "NONE";
    case KeystoreProtection::DPAPI: return "DPAPI";
    case KeystoreProtection::Passphrase: return "PASSPHRASE";
    case KeystoreProtection::DPAPIAndPassphrase: return "DPAPI_AND_PASSPHRASE";
    default: return "UNKNOWN";
    }
}

inline KeystoreProtection keystore_protection_from_string(const std::string& str) noexcept {
    if (str == "DPAPI") return KeystoreProtection::DPAPI;
    if (str == "PASSPHRASE") return KeystoreProtection::Passphrase;
    if (str == "DPAPI_AND_PASSPHRASE") return KeystoreProtection::DPAPIAndPassphrase;
    return KeystoreProtection::None;
}

struct DeveloperIdentity {
    std::string publisher_id;
    std::string organization_name;
    std::string key_id;
    PublisherRole role{PublisherRole::Developer};
    SignatureAlgorithm algorithm{SignatureAlgorithm::Ed25519};
    std::uint64_t created_at{0};
    std::uint64_t valid_from{0};
    std::uint64_t valid_until{0}; // 0 = no expiration

    // Private key seed: 32 bytes (must be protected and never exported in public artifacts)
    std::vector<std::uint8_t> private_key_seed;
    // Public key: 32 bytes Ed25519 public key
    std::vector<std::uint8_t> public_key;

    // Zero out private key seed upon destruction or clearing
    DeveloperIdentity() = default;
    ~DeveloperIdentity() noexcept {
        wipe_private_key();
    }
    DeveloperIdentity(const DeveloperIdentity&) = default;
    DeveloperIdentity& operator=(const DeveloperIdentity& other) {
        if (this != &other) {
            wipe_private_key();
            publisher_id = other.publisher_id;
            organization_name = other.organization_name;
            key_id = other.key_id;
            role = other.role;
            algorithm = other.algorithm;
            created_at = other.created_at;
            valid_from = other.valid_from;
            valid_until = other.valid_until;
            private_key_seed = other.private_key_seed;
            public_key = other.public_key;
        }
        return *this;
    }

    DeveloperIdentity(DeveloperIdentity&& other) noexcept
        : publisher_id(std::move(other.publisher_id)),
          organization_name(std::move(other.organization_name)),
          key_id(std::move(other.key_id)),
          role(other.role),
          algorithm(other.algorithm),
          created_at(other.created_at),
          valid_from(other.valid_from),
          valid_until(other.valid_until),
          private_key_seed(std::move(other.private_key_seed)),
          public_key(std::move(other.public_key)) {
        other.wipe_private_key();
    }

    DeveloperIdentity& operator=(DeveloperIdentity&& other) noexcept {
        if (this != &other) {
            wipe_private_key();
            publisher_id = std::move(other.publisher_id);
            organization_name = std::move(other.organization_name);
            key_id = std::move(other.key_id);
            role = other.role;
            algorithm = other.algorithm;
            created_at = other.created_at;
            valid_from = other.valid_from;
            valid_until = other.valid_until;
            private_key_seed = std::move(other.private_key_seed);
            public_key = std::move(other.public_key);
            other.wipe_private_key();
        }
        return *this;
    }

    void wipe_private_key() noexcept;
    bool is_valid() const noexcept;
};

class DeveloperKeyStore {
public:
    // Generate a fresh Ed25519 developer identity
    static DeveloperIdentity generate_identity(
        const std::string& publisher_id,
        const std::string& organization_name,
        const std::string& key_id,
        PublisherRole role = PublisherRole::Developer,
        std::uint64_t valid_from = 0,
        std::uint64_t valid_until = 0);

    // Export public verification key (suitable for runtime PublisherTrustStore)
    // CRITICAL: Guaranteed to contain ONLY the public key; private seed is never copied
    static PublisherKey export_public_key(const DeveloperIdentity& identity);

    // Export public verification key in PublisherTrustStore configuration text format
    static std::string export_public_key_text(const DeveloperIdentity& identity);

    // Save public verification key to a `.pub` file
    static bool save_public_key_file(const std::string& file_path,
                                     const DeveloperIdentity& identity);

    // Save developer identity to keystore file (.devkey) with specified protection
    static bool save_to_file(const std::string& file_path,
                             const DeveloperIdentity& identity,
                             KeystoreProtection protection = KeystoreProtection::DPAPI,
                             const std::string& passphrase = "");

    // Load developer identity from keystore file (.devkey)
    static std::optional<DeveloperIdentity> load_from_file(
        const std::string& file_path,
        const std::string& passphrase = "");

    // Windows DPAPI encryption / decryption
    static bool dpapi_protect(const std::vector<std::uint8_t>& plaintext,
                              std::vector<std::uint8_t>& ciphertext);
    static bool dpapi_unprotect(const std::vector<std::uint8_t>& ciphertext,
                                std::vector<std::uint8_t>& plaintext);

    // Passphrase PBKDF2 + HMAC authenticated stream encryption
    static bool encrypt_with_passphrase(const std::vector<std::uint8_t>& plaintext,
                                        const std::string& passphrase,
                                        const std::vector<std::uint8_t>& salt,
                                        std::vector<std::uint8_t>& ciphertext,
                                        std::vector<std::uint8_t>& mac_tag);

    static bool decrypt_with_passphrase(const std::vector<std::uint8_t>& ciphertext,
                                        const std::string& passphrase,
                                        const std::vector<std::uint8_t>& salt,
                                        const std::vector<std::uint8_t>& mac_tag,
                                        std::vector<std::uint8_t>& plaintext);

    // PBKDF2-HMAC-SHA256 key derivation function
    static void pbkdf2_hmac_sha256(const std::string& passphrase,
                                  const std::uint8_t* salt, std::size_t salt_len,
                                  std::uint32_t iterations,
                                  std::uint8_t* out, std::size_t out_len) noexcept;
};

} // namespace winios::security
