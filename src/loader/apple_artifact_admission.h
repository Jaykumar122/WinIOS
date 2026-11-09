#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "loader/apple_boot_artifact_inventory.h"
#include "device/apple_hardware_profile.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// AdmissionDecision: Final disposition of an artifact admission check
// -----------------------------------------------------------------------------
enum class AdmissionDecision {
    Admitted,                       // Passed all checks and admitted for boot execution
    Rejected_Encrypted,             // Encrypted payload (e.g. Im4p/Img4 with kbag)
    Rejected_ArchitectureMismatch,  // Incompatible CPU architecture (must be ARM64 / ARM64e)
    Rejected_DeviceMismatch,        // Device target mismatch (not iPhone15,4 / D73AP)
    Rejected_ChecksumMismatch,      // SHA-256 digest failed verification
    Rejected_MalformedHeader,       // Corrupt or invalid file header/magic
    Rejected_Truncated,             // File truncated or smaller than minimum required header
    Rejected_MissingRequired,       // Required artifact is missing or not provided
    Rejected_InvalidAlignment,      // Misaligned segment or page boundary (< 16 KB)
    Rejected_SyntheticAsReal        // Synthetic test fixture improperly claiming authentic real status
};

// -----------------------------------------------------------------------------
// AdmissionPolicy: Rules governing the admission engine
// -----------------------------------------------------------------------------
struct AdmissionPolicy {
    bool enforce_strict_architecture{true};  // Enforce ARM64 / ARM64e
    bool enforce_strict_device{true};        // Enforce iPhone15,4 / D73AP
    bool enforce_sha256_verification{true};  // Check SHA256 if expected hash is non-empty
    bool reject_encrypted_im4p{true};        // Reject encrypted Im4p payloads
    bool allow_synthetic_artifacts{false};   // In developer mode, permit synthetic test mockups
    bool enforce_16kb_page_alignment{true};  // Enforce 16 KB segment/RAM alignment
    bool reject_synthetic_as_real{true};     // Strictly reject synthetic artifacts claiming genuine provenance
};

// -----------------------------------------------------------------------------
// AdmissionResult: Detailed report on an artifact evaluation
// -----------------------------------------------------------------------------
struct AdmissionResult {
    std::string artifact_id;
    AppleArtifactKind kind{AppleArtifactKind::KernelCollection};
    AdmissionDecision decision{AdmissionDecision::Rejected_MissingRequired};
    bool admitted{false};
    std::string reason;
    std::string details;
    std::string remediation;
    std::string computed_sha256;
    std::size_t size_bytes{0};
};

// -----------------------------------------------------------------------------
// AppleArtifactAdmissionController: Validates genuine iOS boot artifacts
// -----------------------------------------------------------------------------
class AppleArtifactAdmissionController {
public:
    explicit AppleArtifactAdmissionController(AdmissionPolicy policy = AdmissionPolicy{});
    ~AppleArtifactAdmissionController() = default;

    // Policy accessors
    [[nodiscard]] const AdmissionPolicy& policy() const noexcept { return policy_; }
    void set_policy(const AdmissionPolicy& policy) noexcept { policy_ = policy; }

    // Single artifact admission evaluation
    [[nodiscard]] AdmissionResult evaluate_artifact(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    // Evaluate whole inventory against loaded data blobs (map: artifact_id -> buffer)
    [[nodiscard]] std::vector<AdmissionResult> evaluate_inventory(
        AppleBootArtifactInventory& inventory,
        const std::map<std::string, std::vector<std::uint8_t>>& loaded_data) const;

    // Check overall inventory admission verdict
    [[nodiscard]] bool is_inventory_bootable(
        const std::vector<AdmissionResult>& results,
        std::string& out_summary) const;

    // Generate formatted markdown admission report
    [[nodiscard]] std::string generate_admission_report(
        const std::vector<AdmissionResult>& results) const;

    // Format helpers
    [[nodiscard]] static const char* decision_to_string(AdmissionDecision decision) noexcept;

private:
    // Format-specific validators
    [[nodiscard]] AdmissionResult validate_kernelcache(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_devicetree(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_ramdisk(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_apfs_container(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_trust_cache(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_iboot(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] AdmissionResult validate_userspace_macho(
        const AppleBootArtifact& artifact,
        const std::uint8_t* data,
        std::size_t size) const;

    [[nodiscard]] bool check_encrypted_im4p(
        const std::uint8_t* data,
        std::size_t size,
        std::string& out_details) const;

    AdmissionPolicy policy_;
};

} // namespace winios::loader
