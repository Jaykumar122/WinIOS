#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "loader/apple_artifact_admission.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Real Artifact Inspection Metadata
// -----------------------------------------------------------------------------
struct RealArtifactMetadata {
    std::string       file_path;
    std::string       sha256;
    std::size_t       file_size{0};
    bool              is_macho{false};
    bool              is_arm64{false};
    bool              is_arm64e{false};
    bool              is_encrypted{false};
    std::uint32_t     cryptid{0};
    std::uint32_t     cryptoff{0};
    std::uint32_t     cryptsize{0};
    bool              is_img4_container{false};
    bool              requires_sep_gid{false};
    AdmissionDecision decision{AdmissionDecision::Rejected_MissingRequired};
    std::string       status_message;

    [[nodiscard]] bool is_admitted() const noexcept {
        return decision == AdmissionDecision::Admitted;
    }

    [[nodiscard]] bool can_execute_in_userspace() const noexcept {
        return is_macho && (is_arm64 || is_arm64e) && !is_encrypted && !requires_sep_gid;
    }
};

// -----------------------------------------------------------------------------
// RealArtifactHarness: Evaluates Real Apple Binaries and Enforces Cryptographic Isolation
// -----------------------------------------------------------------------------
class RealArtifactHarness {
public:
    RealArtifactHarness() = default;
    ~RealArtifactHarness() = default;

    // Inspect an in-memory buffer representing an authentic or user-provided binary
    [[nodiscard]] static RealArtifactMetadata inspect_buffer(
        const std::uint8_t* data,
        std::size_t size,
        std::string_view file_path = "");

    // Computes SHA-256 hex string from byte buffer
    [[nodiscard]] static std::string compute_sha256(
        const std::uint8_t* data,
        std::size_t size);

    // Determines if userspace execution can proceed without Apple SEP hardware keys
    [[nodiscard]] static bool can_execute_without_sep_keys(
        const RealArtifactMetadata& meta) noexcept;
};

} // namespace winios::loader
