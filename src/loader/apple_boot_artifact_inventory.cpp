#include "loader/apple_boot_artifact_inventory.h"

#include <iomanip>
#include <sstream>

namespace winios::loader {

AppleBootArtifactInventory::AppleBootArtifactInventory() = default;

void AppleBootArtifactInventory::register_artifact(const AppleBootArtifact& artifact) {
    for (auto& a : artifacts_) {
        if (a.artifact_id == artifact.artifact_id) {
            a = artifact;
            return;
        }
    }
    artifacts_.push_back(artifact);
}

void AppleBootArtifactInventory::clear() noexcept {
    artifacts_.clear();
}

const AppleBootArtifact* AppleBootArtifactInventory::find_by_kind(AppleArtifactKind kind) const noexcept {
    for (const auto& a : artifacts_) {
        if (a.kind == kind) {
            return &a;
        }
    }
    return nullptr;
}

const AppleBootArtifact* AppleBootArtifactInventory::find_by_id(std::string_view id) const noexcept {
    for (const auto& a : artifacts_) {
        if (a.artifact_id == id) {
            return &a;
        }
    }
    return nullptr;
}

std::size_t AppleBootArtifactInventory::count_present() const noexcept {
    std::size_t n = 0;
    for (const auto& a : artifacts_) {
        if (a.is_present) n++;
    }
    return n;
}

const char* AppleBootArtifactInventory::kind_to_string(AppleArtifactKind kind) noexcept {
    switch (kind) {
        case AppleArtifactKind::Bootloader_iBoot: return "Bootloader (iBoot)";
        case AppleArtifactKind::KernelCollection: return "KernelCollection (XNU)";
        case AppleArtifactKind::DeviceTree:       return "DeviceTree (ADTS)";
        case AppleArtifactKind::Ramdisk:          return "Ramdisk (RootFS/DMG)";
        case AppleArtifactKind::ApfsSystemVolume: return "APFS System Volume";
        case AppleArtifactKind::ApfsDataVolume:   return "APFS Data Volume";
        case AppleArtifactKind::BuildManifest:    return "BuildManifest";
        case AppleArtifactKind::TrustCache:       return "TrustCache";
        case AppleArtifactKind::SepFirmware:      return "SEP Firmware";
        case AppleArtifactKind::AopFirmware:      return "AOP Firmware";
        case AppleArtifactKind::LaunchdBinary:    return "Launchd Binary (/sbin/launchd)";
        case AppleArtifactKind::DyldSharedCache:  return "dyld Shared Cache";
        case AppleArtifactKind::DarwinServiceBinary: return "Darwin Service Binary";
        case AppleArtifactKind::WindowServerBinary: return "WindowServer Binary";
        case AppleArtifactKind::SpringBoardBundle: return "SpringBoard Bundle";
        case AppleArtifactKind::NativeAppBinary:  return "Native iOS App Binary";
    }
    return "Unknown";
}

bool AppleBootArtifactInventory::verify_boot_readiness(std::string& out_report) const {
    std::ostringstream ss;
    bool ready = true;

    ss << "=== Apple Boot Artifact Readiness Verification ===\n";

    // 1. Kernel Collection check
    const auto* kernel = find_by_kind(AppleArtifactKind::KernelCollection);
    if (!kernel || !kernel->is_present) {
        ss << "[MISSING] KernelCollection is required for execution\n";
        ready = false;
    } else if (kernel->is_encrypted) {
        ss << "[BLOCKED] KernelCollection is encrypted (requires lawful decrypted binary)\n";
        ready = false;
    } else {
        ss << "[READY] KernelCollection present: " << kernel->artifact_id << "\n";
    }

    // 2. DeviceTree check
    const auto* dt = find_by_kind(AppleArtifactKind::DeviceTree);
    if (!dt || !dt->is_present) {
        ss << "[MISSING] DeviceTree (ADTS) is required for hardware discovery\n";
        ready = false;
    } else {
        ss << "[READY] DeviceTree present: " << dt->artifact_id << "\n";
    }

    // 3. Root Filesystem check (Ramdisk or APFS System Volume)
    const auto* rd = find_by_kind(AppleArtifactKind::Ramdisk);
    const auto* sys = find_by_kind(AppleArtifactKind::ApfsSystemVolume);
    if ((!rd || !rd->is_present) && (!sys || !sys->is_present)) {
        ss << "[MISSING] Root filesystem (Ramdisk or APFS Volume) is required to execute launchd\n";
        ready = false;
    } else {
        ss << "[READY] Root filesystem volume verified present\n";
    }

    out_report = ss.str();
    return ready;
}

std::string AppleBootArtifactInventory::dump_inventory_report() const {
    std::ostringstream ss;
    ss << "# Apple iOS Boot Artifact Inventory\n\n";
    ss << "| Artifact ID | Kind | Target Device | OS Build | Size | Status |\n";
    ss << "| :--- | :--- | :---: | :---: | :---: | :---: |\n";

    for (const auto& a : artifacts_) {
        ss << "| `" << a.artifact_id << "` | "
           << kind_to_string(a.kind) << " | "
           << a.target_device << " | "
           << a.target_os_version << " | "
           << (a.file_size > 0 ? std::to_string(a.file_size / 1024) + " KB" : "-") << " | "
           << (a.is_admitted ? "**ADMITTED**" : (a.is_present ? "PRESENT" : "PENDING")) << " |\n";
    }

    return ss.str();
}

AppleBootArtifactInventory AppleBootArtifactInventory::create_iphone15_template(
    const std::string& base_path) {

    AppleBootArtifactInventory inv;
    const std::string pfx = base_path.empty() ? "" : base_path + "/";

    AppleBootArtifact kernel;
    kernel.artifact_id = "kernelcache.release.iphone15";
    kernel.kind = AppleArtifactKind::KernelCollection;
    kernel.target_device = "iPhone15,4";
    kernel.target_os_version = "iOS 17.5.1 (Build 21F90)";
    kernel.file_path = pfx + "kernelcache.release.iphone15";
    kernel.notes = "64-bit ARM64e Mach-O fileset containing xnu core and kernel extensions";
    inv.register_artifact(kernel);

    AppleBootArtifact dt;
    dt.artifact_id = "DeviceTree.d37ap.im4p";
    dt.kind = AppleArtifactKind::DeviceTree;
    dt.target_device = "iPhone15,4";
    dt.target_os_version = "iOS 17.5.1 (Build 21F90)";
    dt.file_path = pfx + "DeviceTree.d37ap.im4p";
    dt.notes = "Apple DeviceTree (ADTS format) for D37AP board ID 0x18";
    inv.register_artifact(dt);

    AppleBootArtifact dt_compat;
    dt_compat.artifact_id = "DeviceTree.d73ap.im4p";
    dt_compat.kind = AppleArtifactKind::DeviceTree;
    dt_compat.target_device = "iPhone15,4";
    dt_compat.target_os_version = "iOS 17.5.1 (Build 21F90)";
    dt_compat.file_path = pfx + "DeviceTree.d73ap.im4p";
    dt_compat.notes = "Apple DeviceTree (ADTS format) for D73AP board ID 0x18 (iPhone 14 Pro fixture)";
    inv.register_artifact(dt_compat);

    AppleBootArtifact ramdisk;
    ramdisk.artifact_id = "RestoreRamDisk.dmg";
    ramdisk.kind = AppleArtifactKind::Ramdisk;
    ramdisk.target_device = "iPhone15,4";
    ramdisk.target_os_version = "iOS 17.5.1 (Build 21F90)";
    ramdisk.file_path = pfx + "RestoreRamDisk.dmg";
    ramdisk.notes = "APFS container ramdisk image containing early userspace tools and /sbin/launchd";
    inv.register_artifact(ramdisk);

    AppleBootArtifact sys_vol;
    sys_vol.artifact_id = "SystemOS.dmg";
    sys_vol.kind = AppleArtifactKind::ApfsSystemVolume;
    sys_vol.target_device = "iPhone15,4";
    sys_vol.target_os_version = "iOS 17.5.1 (Build 21F90)";
    sys_vol.file_path = pfx + "SystemOS.dmg";
    sys_vol.notes = "Sealed APFS system volume containing full iOS root, SpringBoard, and frameworks";
    inv.register_artifact(sys_vol);

    AppleBootArtifact manifest;
    manifest.artifact_id = "BuildManifest.plist";
    manifest.kind = AppleArtifactKind::BuildManifest;
    manifest.target_device = "iPhone15,4";
    manifest.target_os_version = "iOS 17.5.1 (Build 21F90)";
    manifest.file_path = pfx + "BuildManifest.plist";
    manifest.notes = "Cryptographic build manifest containing component digests and versions";
    inv.register_artifact(manifest);

    AppleBootArtifact tc;
    tc.artifact_id = "StaticTrustCache.img4";
    tc.kind = AppleArtifactKind::TrustCache;
    tc.target_device = "iPhone15,4";
    tc.target_os_version = "iOS 17.5.1 (Build 21F90)";
    tc.file_path = pfx + "StaticTrustCache.img4";
    tc.notes = "TrustCache holding pre-approved SHA256 hashes of iOS system binaries";
    inv.register_artifact(tc);

    AppleBootArtifact sep;
    sep.artifact_id = "sep-firmware.d73.RELEASE.im4p";
    sep.kind = AppleArtifactKind::SepFirmware;
    sep.target_device = "iPhone15,4";
    sep.target_os_version = "iOS 17.5.1 (Build 21F90)";
    sep.file_path = pfx + "sep-firmware.d73.RELEASE.im4p";
    sep.is_encrypted = true;
    sep.notes = "Secure Enclave Processor firmware binary";
    inv.register_artifact(sep);

    return inv;
}

} // namespace winios::loader
