#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::loader {

// -----------------------------------------------------------------------------
// AppleArtifactKind: Classification of boot-critical Apple iOS components
// -----------------------------------------------------------------------------
enum class AppleArtifactKind {
    Bootloader_iBoot,   // Stage 2 bootloader (iBoot.d73.RELEASE.im4p)
    KernelCollection,   // 64-bit XNU fileset kernelcache (kernelcache.release.iphone15)
    DeviceTree,         // Apple DeviceTree ADTS (DeviceTree.d73ap.im4p)
    Ramdisk,            // Restore / Boot ramdisk (RestoreRamDisk.dmg)
    ApfsSystemVolume,   // Sealed System APFS container volume
    ApfsDataVolume,     // iOS Data partition container volume
    BuildManifest,      // BuildManifest.plist specifying component identity & digests
    TrustCache,         // Code signature trust cache (loadable/static trustcache)
    SepFirmware,        // Secure Enclave Processor firmware (sep-firmware.d73.RELEASE.im4p)
    AopFirmware,        // Always-On Processor firmware (aop.d73.RELEASE.im4p)
    LaunchdBinary,      // /sbin/launchd Mach-O executable
    DyldSharedCache,    // dyld shared cache / dylinker
    DarwinServiceBinary,// syslogd, notifyd, securityd
    WindowServerBinary, // WindowServer Mach-O
    SpringBoardBundle,  // SpringBoard Mach-O executable
    NativeAppBinary     // Native iOS application Mach-O
};

enum class ArtifactProvenance {
    SyntheticHarness,   // Autonomous synthetic test mockups
    DecryptedReal,      // Lawful, authentic decrypted Apple binary
    EncryptedApple      // Encrypted Apple proprietary artifact (blocked)
};

// -----------------------------------------------------------------------------
// AppleBootArtifact: Identity, provenance, and validation status of an artifact
// -----------------------------------------------------------------------------
struct AppleBootArtifact {
    std::string artifact_id;
    AppleArtifactKind kind{AppleArtifactKind::KernelCollection};
    ArtifactProvenance provenance{ArtifactProvenance::SyntheticHarness};
    std::string target_device{"iPhone15,4"};
    std::string target_os_version{"iOS 17.5.1 (Build 21F90)"};
    std::string file_path;
    std::size_t file_size{0};
    std::string sha256_hex;
    bool is_encrypted{false};
    bool is_synthetic{false};
    bool is_present{false};
    bool is_admitted{false};
    std::string notes;
};

// -----------------------------------------------------------------------------
// AppleBootArtifactInventory: Registry and validation manager for iOS artifacts
// -----------------------------------------------------------------------------
class AppleBootArtifactInventory {
public:
    AppleBootArtifactInventory();
    ~AppleBootArtifactInventory() = default;

    // Artifact Registration & Ingestion
    void register_artifact(const AppleBootArtifact& artifact);
    void clear() noexcept;

    // Queries
    [[nodiscard]] const AppleBootArtifact* find_by_kind(AppleArtifactKind kind) const noexcept;
    [[nodiscard]] const AppleBootArtifact* find_by_id(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<AppleBootArtifact>& all_artifacts() const noexcept { return artifacts_; }
    [[nodiscard]] std::size_t count() const noexcept { return artifacts_.size(); }
    [[nodiscard]] std::size_t count_present() const noexcept;

    // Readiness & Security Validation
    [[nodiscard]] bool verify_boot_readiness(std::string& out_report) const;
    [[nodiscard]] std::string dump_inventory_report() const;
    [[nodiscard]] static const char* kind_to_string(AppleArtifactKind kind) noexcept;

    // Factory with standard iPhone 15 iOS 17.5.1 boot set template
    [[nodiscard]] static AppleBootArtifactInventory create_iphone15_template(
        const std::string& base_path = "");

private:
    std::vector<AppleBootArtifact> artifacts_;
};

} // namespace winios::loader
