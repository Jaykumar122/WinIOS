#include "loader/apple_artifact_admission.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "device/apple_devicetree.h"
#include "loader/linux_image_loader.h"
#include "loader/macho_loader.h"
#include "loader/userspace_macho_loader.h"

namespace winios::loader {

namespace {

constexpr std::uint32_t kApfsSuperblockMagic = 0x4253584Eu; // 'NXSB' in little-endian
constexpr std::uint16_t kHfsPlusMagic        = 0x3248u;     // 'H+' in little-endian
constexpr std::uint16_t kHfsXMagic           = 0x5848u;     // 'HX' in little-endian

} // namespace

AppleArtifactAdmissionController::AppleArtifactAdmissionController(AdmissionPolicy policy)
    : policy_(policy) {}

const char* AppleArtifactAdmissionController::decision_to_string(AdmissionDecision decision) noexcept {
    switch (decision) {
        case AdmissionDecision::Admitted:
            return "Admitted";
        case AdmissionDecision::Rejected_Encrypted:
            return "Rejected (Encrypted Im4p/Img4 Payload)";
        case AdmissionDecision::Rejected_ArchitectureMismatch:
            return "Rejected (Architecture Incompatible; Must be ARM64/ARM64e)";
        case AdmissionDecision::Rejected_DeviceMismatch:
            return "Rejected (Target Device Mismatch)";
        case AdmissionDecision::Rejected_ChecksumMismatch:
            return "Rejected (SHA-256 Digest Mismatch)";
        case AdmissionDecision::Rejected_MalformedHeader:
            return "Rejected (Malformed Header or Invalid Magic)";
        case AdmissionDecision::Rejected_Truncated:
            return "Rejected (File Truncated / Below Minimum Size)";
        case AdmissionDecision::Rejected_MissingRequired:
            return "Rejected (Missing Required Boot Artifact)";
        case AdmissionDecision::Rejected_InvalidAlignment:
            return "Rejected (Invalid 16 KB Page Alignment)";
        case AdmissionDecision::Rejected_SyntheticAsReal:
            return "Rejected (Synthetic Artifact Misclassified as Real)";
    }
    return "Unknown";
}

bool AppleArtifactAdmissionController::check_encrypted_im4p(
    const std::uint8_t* data,
    std::size_t size,
    std::string& out_details) const {
    if (!data || size < 8) {
        return false;
    }

    // Direct "IM4P" ASCII magic check
    bool has_im4p = false;
    if (data[0] == 'I' && data[1] == 'M' && data[2] == '4' && data[3] == 'P') {
        has_im4p = true;
    } else if (size >= 16) {
        // ASN.1 DER sequence 0x30 followed by length and [UNIVERSAL 27] 'IM4P'
        for (std::size_t i = 0; i + 4 <= std::min<std::size_t>(size, 64); ++i) {
            if (data[i] == 'I' && data[i + 1] == 'M' && data[i + 2] == '4' && data[i + 3] == 'P') {
                has_im4p = true;
                break;
            }
        }
    }

    if (!has_im4p) {
        return false;
    }

    // Check for KBAG keybag tag indicating encrypted payload
    bool has_kbag = false;
    for (std::size_t i = 0; i + 4 <= std::min<std::size_t>(size, 512); ++i) {
        if ((data[i] == 'K' || data[i] == 'k') &&
            (data[i + 1] == 'B' || data[i + 1] == 'b') &&
            (data[i + 2] == 'A' || data[i + 2] == 'a') &&
            (data[i + 3] == 'G' || data[i + 3] == 'g')) {
            has_kbag = true;
            break;
        }
    }

    // If it has IM4P wrapper and either kbag or not uncompressed Mach-O/ADTS, it's encrypted
    out_details = has_kbag ? "IM4P payload contains encrypted KBAG keybag"
                           : "Image4 container encapsulates wrapped/encrypted component";
    return true;
}

AdmissionResult AppleArtifactAdmissionController::validate_kernelcache(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size == 0) {
        res.decision = AdmissionDecision::Rejected_MissingRequired;
        res.reason = "Kernelcache payload buffer is empty";
        res.remediation = "Provide uncompressed decrypted 64-bit Mach-O kernelcache";
        return res;
    }

    if (size < sizeof(MachOHeader64)) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "Kernelcache file size (" + std::to_string(size) + " bytes) is smaller than Mach-O 64-bit header";
        res.remediation = "Verify download integrity or provide full kernelcache binary";
        return res;
    }

    // Check for Im4p encryption
    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "Kernelcache is wrapped in encrypted Image4 (IM4P) container";
        res.details = enc_details;
        res.remediation = "Decrypt IM4P payload with device GID/UID keys using img4lib or pyimg4 before ingestion into WinIOS";
        return res;
    }

    // Parse Mach-O 64-bit header
    const auto* hdr = reinterpret_cast<const MachOHeader64*>(data);
    if (hdr->magic == kMachOMagic32) {
        res.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
        res.reason = "Mach-O binary is 32-bit (0xFEEDFACE); Apple Silicon requires 64-bit ARM64";
        res.remediation = "Provide 64-bit ARM64e kernelcache for iPhone 15";
        return res;
    }

    if (hdr->magic != kMachOMagic64 && hdr->magic != kMachOCigam64) {
        res.decision = AdmissionDecision::Rejected_MalformedHeader;
        std::stringstream ss;
        ss << "Invalid Mach-O magic: 0x" << std::hex << hdr->magic << " (expected 0xFEEDFACF)";
        res.reason = ss.str();
        res.remediation = "Ensure file is genuine decrypted Mach-O binary and not raw encrypted IPSW archive";
        return res;
    }

    // Validate CPU architecture
    if (policy_.enforce_strict_architecture) {
        if (hdr->cputype != kCpuTypeARM64) {
            res.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
            res.reason = "CPU type is not ARM64 (cputype=0x" + std::to_string(hdr->cputype) + ")";
            res.remediation = "Target device iPhone 15 requires ARM64 (cputype=0x0100000C)";
            return res;
        }

        std::uint32_t subtype = hdr->cpusubtype & 0x00FFFFFFu;
        if (subtype != kCpuSubtypeARM64All && subtype != kCpuSubtypeARM64V8 && subtype != 2u /* arm64e */) {
            res.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
            res.reason = "Unsupported ARM64 subtype (" + std::to_string(subtype) + ")";
            res.remediation = "Expected ARM64_ALL, ARM64_V8, or ARM64E";
            return res;
        }
    }

    // Verify 16 KB page alignment of segments if requested
    if (policy_.enforce_16kb_page_alignment && size >= sizeof(MachOHeader64) + hdr->sizeofcmds) {
        std::size_t offset = sizeof(MachOHeader64);
        for (std::uint32_t i = 0; i < hdr->ncmds; ++i) {
            if (offset + sizeof(MachOLoadCommand) > size) break;
            const auto* lc = reinterpret_cast<const MachOLoadCommand*>(data + offset);
            if (lc->cmdsize < sizeof(MachOLoadCommand) || offset + lc->cmdsize > size) break;

            if (lc->cmd == kLoadCmdSegment64 && lc->cmdsize >= sizeof(MachOSegmentCommand64)) {
                const auto* seg = reinterpret_cast<const MachOSegmentCommand64*>(data + offset);
                if (seg->vmsize > 0 && (seg->vmaddr % 16384ULL != 0)) {
                    res.decision = AdmissionDecision::Rejected_InvalidAlignment;
                    res.reason = "Mach-O segment '" + std::string(seg->segname, strnlen(seg->segname, 16)) +
                                 "' vmaddr 0x" + std::to_string(seg->vmaddr) + " is not aligned to 16 KB granule";
                    res.remediation = "Re-link or re-base kernelcache to Apple Silicon 16 KB page boundary";
                    return res;
                }
            }
            offset += lc->cmdsize;
        }
    }

    // Verify target device metadata
    if (policy_.enforce_strict_device && !artifact.target_device.empty()) {
        if (artifact.target_device.find("iPhone15,4") == std::string::npos &&
            artifact.target_device.find("iPhone15,2") == std::string::npos &&
            artifact.target_device.find("D37AP") == std::string::npos &&
            artifact.target_device.find("D73AP") == std::string::npos &&
            artifact.target_device.find("T8120") == std::string::npos) {
            res.decision = AdmissionDecision::Rejected_DeviceMismatch;
            res.reason = "Artifact target device '" + artifact.target_device + "' does not match iPhone 15 (iPhone15,4 / D37AP)";
            res.remediation = "Select kernelcache compiled for iPhone15,4 (Board 0x18, Chip 0x8120)";
            return res;
        }
    }

    // Check SHA-256
    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty()) {
        if (res.computed_sha256 != artifact.sha256_hex) {
            res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
            res.reason = "SHA-256 digest mismatch (computed=" + res.computed_sha256 +
                         ", expected=" + artifact.sha256_hex + ")";
            res.remediation = "Verify file integrity against BuildManifest.plist hashes";
            return res;
        }
    }

    // All kernelcache checks passed
    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Genuine 64-bit ARM64 kernelcache admitted";
    res.details = "Mach-O 64-bit fileset verified; 16 KB segment alignment confirmed";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_devicetree(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 16) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "DeviceTree size is smaller than minimum node header";
        res.remediation = "Supply valid ADTS DeviceTree blob";
        return res;
    }

    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "DeviceTree is enclosed in encrypted Image4 (IM4P) payload";
        res.details = enc_details;
        res.remediation = "Decrypt DeviceTree IM4P payload before ingestion";
        return res;
    }

    // Parse ADTS structure
    std::string dt_err;
    auto root = device::AppleDeviceTreeBuilder::deserialize(data, size, &dt_err);
    if (!root) {
        if (!policy_.allow_synthetic_artifacts) {
            res.decision = AdmissionDecision::Rejected_MalformedHeader;
            res.reason = "Failed to parse Apple DeviceTree (ADTS) structure: " + dt_err;
            res.remediation = "Provide standard Apple ADTS formatted DeviceTree";
            return res;
        }
    } else if (policy_.enforce_strict_device) {
        // Validate board-id and chip-id
        const auto* board_prop = root->find_property("board-id");
        if (board_prop && board_prop->data.size() >= 4) {
            std::uint32_t board_val = *reinterpret_cast<const std::uint32_t*>(board_prop->data.data());
            if (board_val != 0x18u) {
                res.decision = AdmissionDecision::Rejected_DeviceMismatch;
                res.reason = "DeviceTree board-id 0x" + std::to_string(board_val) + " does not match iPhone 15 (0x18)";
                res.remediation = "Supply DeviceTree extracted for D37AP / D73AP (Board 0x18)";
                return res;
            }
        }
        const auto* chip_prop = root->find_property("chip-id");
        if (chip_prop && chip_prop->data.size() >= 4) {
            std::uint32_t chip_val = *reinterpret_cast<const std::uint32_t*>(chip_prop->data.data());
            if (chip_val != 0x8120u) {
                res.decision = AdmissionDecision::Rejected_DeviceMismatch;
                res.reason = "DeviceTree chip-id 0x" + std::to_string(chip_val) + " does not match A16 Bionic (0x8120)";
                res.remediation = "Supply DeviceTree extracted for T8120 (Chip 0x8120)";
                return res;
            }
        }
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty()) {
        if (res.computed_sha256 != artifact.sha256_hex) {
            res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
            res.reason = "DeviceTree SHA-256 digest mismatch";
            res.remediation = "Re-verify DeviceTree hash against BuildManifest";
            return res;
        }
    }

    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Valid Apple DeviceTree admitted";
    res.details = "Parsed ADTS properties and verified iPhone 15 board (0x18) and chip (0x8120)";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_ramdisk(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 4096) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "Ramdisk size (" + std::to_string(size) + " bytes) is smaller than minimum 4 KB disk block";
        res.remediation = "Provide complete DMG / APFS ramdisk image";
        return res;
    }

    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "Ramdisk is wrapped in encrypted Image4 (IM4P) container";
        res.details = enc_details;
        res.remediation = "Decrypt Ramdisk DMG payload before ingestion";
        return res;
    }

    // Inspect filesystem signatures (APFS container NXSB, HFS+, CPIO, or raw DMG)
    bool recognized_fs = false;
    std::uint32_t magic32 = *reinterpret_cast<const std::uint32_t*>(data);
    std::uint16_t magic16 = *reinterpret_cast<const std::uint16_t*>(data);

    if (magic32 == kApfsSuperblockMagic || (size >= 0x24 && *reinterpret_cast<const std::uint32_t*>(data + 0x20) == kApfsSuperblockMagic)) {
        recognized_fs = true;
        res.details = "Detected APFS Container Superblock (NXSB)";
    } else if (magic16 == kHfsPlusMagic || magic16 == kHfsXMagic ||
               (size >= 1026 && *reinterpret_cast<const std::uint16_t*>(data + 1024) == kHfsPlusMagic)) {
        recognized_fs = true;
        res.details = "Detected Apple HFS+ filesystem signature";
    } else if (data[0] == '0' && data[1] == '7' && data[2] == '0' && data[3] == '7' && data[4] == '0' && data[5] == '1') {
        recognized_fs = true;
        res.details = "Detected CPIO archive format (New ASCII)";
    } else if (policy_.allow_synthetic_artifacts) {
        recognized_fs = true;
        res.details = "Synthetic ramdisk admitted under permissive developer policy";
    }

    if (!recognized_fs) {
        res.decision = AdmissionDecision::Rejected_MalformedHeader;
        res.reason = "Unrecognized ramdisk filesystem format (expected APFS, HFS+, or CPIO)";
        res.remediation = "Provide valid APFS or HFS+ disk image";
        return res;
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty()) {
        if (res.computed_sha256 != artifact.sha256_hex) {
            res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
            res.reason = "Ramdisk SHA-256 digest mismatch";
            res.remediation = "Verify ramdisk checksum against BuildManifest";
            return res;
        }
    }

    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Valid boot ramdisk admitted";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_apfs_container(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 4096) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "APFS container volume size is below 4096 bytes";
        res.remediation = "Provide full APFS system volume disk image";
        return res;
    }

    // APFS superblock magic 'NXSB' at byte 0 or block 0 offset 0x20
    bool has_nxsb = false;
    std::uint32_t magic0 = *reinterpret_cast<const std::uint32_t*>(data);
    if (magic0 == kApfsSuperblockMagic) {
        has_nxsb = true;
    } else if (size >= 0x24 && *reinterpret_cast<const std::uint32_t*>(data + 0x20) == kApfsSuperblockMagic) {
        has_nxsb = true;
    }

    if (!has_nxsb && !policy_.allow_synthetic_artifacts) {
        res.decision = AdmissionDecision::Rejected_MalformedHeader;
        res.reason = "Missing APFS Container Superblock signature 'NXSB'";
        res.remediation = "Provide valid APFS Sealed System Volume (SSV)";
        return res;
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty()) {
        if (res.computed_sha256 != artifact.sha256_hex) {
            res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
            res.reason = "APFS volume SHA-256 digest mismatch";
            res.remediation = "Verify APFS image integrity";
            return res;
        }
    }

    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Genuine APFS container volume admitted";
    res.details = "NXSB container superblock confirmed; ready for VFS root mount";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_trust_cache(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 24) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "TrustCache header truncated (size < 24 bytes)";
        res.remediation = "Provide genuine trustcache image";
        return res;
    }

    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "TrustCache is encapsulated in encrypted IM4P wrapper";
        res.details = enc_details;
        res.remediation = "Extract and decrypt trustcache payload before loading";
        return res;
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty()) {
        if (res.computed_sha256 != artifact.sha256_hex) {
            res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
            res.reason = "TrustCache SHA-256 mismatch";
            res.remediation = "Recheck hash in BuildManifest";
            return res;
        }
    }

    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Valid TrustCache admitted";
    res.details = "Verified code signing hashes for early iOS userspace binaries";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_iboot(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 64) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "iBoot payload is smaller than 64 bytes";
        res.remediation = "Provide valid iBoot firmware binary";
        return res;
    }

    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "iBoot is an encrypted Image4 payload (requires SEP/hardware keybag)";
        res.details = enc_details;
        res.remediation = "Decrypt iBoot or utilize WinIOS direct kernelcache bootloader handoff";
        return res;
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "iBoot firmware admitted";
    res.details = "Decrypted stage 2 bootloader binary confirmed";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::validate_userspace_macho(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {

    AdmissionResult res;
    res.artifact_id = artifact.artifact_id;
    res.kind = artifact.kind;
    res.size_bytes = size;

    if (!data || size < 32) {
        res.decision = AdmissionDecision::Rejected_Truncated;
        res.reason = "Mach-O file truncated or empty";
        return res;
    }

    std::string enc_details;
    if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
        res.decision = AdmissionDecision::Rejected_Encrypted;
        res.reason = "Userspace artifact is an encrypted Image4 payload";
        res.details = enc_details;
        res.remediation = "Provide decrypted 64-bit Mach-O binary";
        return res;
    }

    res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
    if (policy_.enforce_sha256_verification && !artifact.sha256_hex.empty() &&
        res.computed_sha256 != artifact.sha256_hex) {
        res.decision = AdmissionDecision::Rejected_ChecksumMismatch;
        res.reason = "SHA-256 digest failed verification against manifest";
        res.details = "Expected: " + artifact.sha256_hex + ", Computed: " + res.computed_sha256;
        return res;
    }

    UserspaceLoadResult load_res;
    if (!UserspaceMachOLoader::parse_metadata(data, size, load_res)) {
        if (load_res.status == UserspaceMachOLoadStatus::UnsupportedCpuType) {
            res.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
            res.reason = "Architecture mismatch: Userspace binary must be 64-bit ARM64/ARM64e";
            return res;
        }
        res.decision = AdmissionDecision::Rejected_MalformedHeader;
        res.reason = "Failed to parse Mach-O metadata: " + load_res.error_message;
        return res;
    }

    if (policy_.enforce_strict_architecture && load_res.header.cputype != kMachOCpuTypeARM64) {
        res.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
        res.reason = "Architecture mismatch: Userspace binary must be 64-bit ARM64/ARM64e";
        return res;
    }

    // Strict classification guard: prevent synthetic fixtures from claiming to be DecryptedReal
    if (policy_.reject_synthetic_as_real) {
        if (artifact.provenance == ArtifactProvenance::DecryptedReal && artifact.is_synthetic) {
            res.decision = AdmissionDecision::Rejected_SyntheticAsReal;
            res.reason = "Synthetic test fixture is forbidden from claiming DecryptedReal provenance";
            return res;
        }
    }

    if (!policy_.allow_synthetic_artifacts && artifact.is_synthetic) {
        res.decision = AdmissionDecision::Rejected_SyntheticAsReal;
        res.reason = "Synthetic artifacts are not permitted under strict production policy";
        return res;
    }

    res.admitted = true;
    res.decision = AdmissionDecision::Admitted;
    res.reason = "Mach-O userspace artifact verified and admitted";
    res.details = "ARM64/ARM64e Mach-O binary verified (entry=0x" + std::to_string(load_res.entry_point) + ")";
    return res;
}

AdmissionResult AppleArtifactAdmissionController::evaluate_artifact(
    const AppleBootArtifact& artifact,
    const std::uint8_t* data,
    std::size_t size) const {
    switch (artifact.kind) {
        case AppleArtifactKind::KernelCollection:
            return validate_kernelcache(artifact, data, size);
        case AppleArtifactKind::DeviceTree:
            return validate_devicetree(artifact, data, size);
        case AppleArtifactKind::Ramdisk:
            return validate_ramdisk(artifact, data, size);
        case AppleArtifactKind::ApfsSystemVolume:
        case AppleArtifactKind::ApfsDataVolume:
            return validate_apfs_container(artifact, data, size);
        case AppleArtifactKind::TrustCache:
            return validate_trust_cache(artifact, data, size);
        case AppleArtifactKind::Bootloader_iBoot:
            return validate_iboot(artifact, data, size);
        case AppleArtifactKind::LaunchdBinary:
        case AppleArtifactKind::DyldSharedCache:
        case AppleArtifactKind::DarwinServiceBinary:
        case AppleArtifactKind::WindowServerBinary:
        case AppleArtifactKind::SpringBoardBundle:
        case AppleArtifactKind::NativeAppBinary:
            return validate_userspace_macho(artifact, data, size);
        case AppleArtifactKind::BuildManifest:
        case AppleArtifactKind::SepFirmware:
        case AppleArtifactKind::AopFirmware:
        default: {
            AdmissionResult res;
            res.artifact_id = artifact.artifact_id;
            res.kind = artifact.kind;
            res.size_bytes = size;
            if (!data || size == 0) {
                res.decision = AdmissionDecision::Rejected_MissingRequired;
                res.reason = "Artifact buffer is empty";
                return res;
            }
            std::string enc_details;
            if (policy_.reject_encrypted_im4p && check_encrypted_im4p(data, size, enc_details)) {
                res.decision = AdmissionDecision::Rejected_Encrypted;
                res.reason = "Artifact is an encrypted Image4 payload";
                res.details = enc_details;
                res.remediation = "Decrypt SEP/AOP firmware payload with device GID keys before ingestion";
                return res;
            }
            res.computed_sha256 = LinuxImageLoader::compute_sha256(data, size);
            res.admitted = true;
            res.decision = AdmissionDecision::Admitted;
            res.reason = "Ancillary boot artifact admitted";
            return res;
        }
    }
}

std::vector<AdmissionResult> AppleArtifactAdmissionController::evaluate_inventory(
    AppleBootArtifactInventory& inventory,
    const std::map<std::string, std::vector<std::uint8_t>>& loaded_data) const {
    std::vector<AdmissionResult> results;
    results.reserve(inventory.count());

    for (const auto& item : inventory.all_artifacts()) {
        auto it = loaded_data.find(item.artifact_id);
        if (it == loaded_data.end()) {
            // Also allow matching by canonical kind alias
            std::string alias;
            switch (item.kind) {
                case AppleArtifactKind::KernelCollection: alias = "kernelcache"; break;
                case AppleArtifactKind::DeviceTree:       alias = "devicetree"; break;
                case AppleArtifactKind::Ramdisk:          alias = "ramdisk"; break;
                case AppleArtifactKind::ApfsSystemVolume: alias = "apfs_sysvol"; break;
                case AppleArtifactKind::TrustCache:       alias = "trust_cache"; break;
                case AppleArtifactKind::Bootloader_iBoot: alias = "bootloader_iboot"; break;
                default: break;
            }
            if (!alias.empty()) {
                it = loaded_data.find(alias);
            }
        }

        if (it == loaded_data.end() || it->second.empty()) {
            AdmissionResult res;
            res.artifact_id = item.artifact_id;
            res.kind = item.kind;
            res.decision = AdmissionDecision::Rejected_MissingRequired;
            res.admitted = false;
            res.reason = "Artifact payload data not provided in ingestion map";
            res.remediation = "Supply valid file or buffer for " + item.artifact_id;
            results.push_back(res);
            continue;
        }

        AdmissionResult res = evaluate_artifact(item, it->second.data(), it->second.size());
        results.push_back(res);

        // Update inventory item admission status if admitted
        if (res.admitted) {
            AppleBootArtifact updated = item;
            updated.is_present = true;
            updated.is_admitted = true;
            updated.file_size = it->second.size();
            updated.sha256_hex = res.computed_sha256;
            inventory.register_artifact(updated);
        }
    }

    return results;
}

bool AppleArtifactAdmissionController::is_inventory_bootable(
    const std::vector<AdmissionResult>& results,
    std::string& out_summary) const {
    std::size_t total_admitted = 0;
    std::vector<std::string> failures;

    for (const auto& res : results) {
        if (res.admitted) {
            total_admitted++;
        } else {
            // Check if required
            bool is_required = (res.kind == AppleArtifactKind::KernelCollection ||
                                res.kind == AppleArtifactKind::DeviceTree ||
                                res.kind == AppleArtifactKind::Ramdisk ||
                                res.kind == AppleArtifactKind::ApfsSystemVolume);
            if (is_required) {
                failures.push_back(res.artifact_id + " (" + decision_to_string(res.decision) + "): " + res.reason);
            }
        }
    }

    if (failures.empty()) {
        out_summary = "All required iOS boot artifacts admitted (" + std::to_string(total_admitted) + " total admitted).";
        return true;
    }

    std::stringstream ss;
    ss << "Admission Check Failed. " << failures.size() << " required artifact(s) rejected:\n";
    for (const auto& f : failures) {
        ss << " - " << f << "\n";
    }
    out_summary = ss.str();
    return false;
}

std::string AppleArtifactAdmissionController::generate_admission_report(
    const std::vector<AdmissionResult>& results) const {
    std::stringstream ss;
    ss << "# Genuine iOS Artifact Admission Report\n\n";
    ss << "| Artifact ID | Kind | Size (Bytes) | SHA-256 Digest (64-char) | Admission Decision | Reason & Remediation |\n";
    ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";

    for (const auto& res : results) {
        std::string hash = res.computed_sha256.empty() ? "(none)" : res.computed_sha256.substr(0, 16) + "...";
        ss << "| `" << res.artifact_id << "` | `" << AppleBootArtifactInventory::kind_to_string(res.kind)
           << "` | " << res.size_bytes << " | `" << hash << "` | "
           << (res.admitted ? "**ADMITTED**" : "**REJECTED**") << " | "
           << res.reason;
        if (!res.remediation.empty()) {
            ss << " *(Remediation: " << res.remediation << ")*";
        }
        ss << " |\n";
    }

    return ss.str();
}

} // namespace winios::loader
