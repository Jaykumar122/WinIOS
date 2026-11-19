#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::crypto {

constexpr std::size_t kEd25519PublicKeySize = 32;
constexpr std::size_t kEd25519PrivateKeySize = 64; // 32-byte seed + 32-byte public key
constexpr std::size_t kEd25519SeedSize = 32;
constexpr std::size_t kEd25519SignatureSize = 64;

using Ed25519PublicKey = std::array<std::uint8_t, kEd25519PublicKeySize>;
using Ed25519PrivateKey = std::array<std::uint8_t, kEd25519PrivateKeySize>;
using Ed25519Seed = std::array<std::uint8_t, kEd25519SeedSize>;
using Ed25519Signature = std::array<std::uint8_t, kEd25519SignatureSize>;

class Ed25519 {
public:
    // Generate an Ed25519 keypair from a 32-byte CSPRNG seed
    static void create_keypair(Ed25519PublicKey& out_pub,
                               Ed25519PrivateKey& out_priv,
                               const Ed25519Seed& seed) noexcept;

    // Generate an Ed25519 keypair using Windows CNG CSPRNG
    static bool generate_keypair(Ed25519PublicKey& out_pub,
                                 Ed25519PrivateKey& out_priv) noexcept;

    // Sign a message using private key (deterministic, RFC 8032)
    static void sign(Ed25519Signature& out_sig,
                     const void* message,
                     std::size_t message_len,
                     const Ed25519PublicKey& public_key,
                     const Ed25519PrivateKey& private_key) noexcept;

    // Verify a message signature against a public key
    // Returns true if valid, false if signature or public key is malformed or invalid
    [[nodiscard]] static bool verify(const Ed25519Signature& signature,
                                     const void* message,
                                     std::size_t message_len,
                                     const Ed25519PublicKey& public_key) noexcept;

    // Helper utilities
    static std::string to_hex(const std::uint8_t* data, std::size_t len);
    static bool from_hex(const std::string& hex, std::uint8_t* out_data, std::size_t expected_len);
};

} // namespace winios::crypto
