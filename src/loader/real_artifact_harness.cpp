#include "loader/real_artifact_harness.h"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "loader/linux_image_loader.h"
#include "loader/userspace_macho_loader.h"

namespace winios::loader {

// Mach-O Encryption Load Command Constants
constexpr std::uint32_t kCmdEncryptionInfo   = 0x00000021u; // LC_ENCRYPTION_INFO
constexpr std::uint32_t kCmdEncryptionInfo64 = 0x0000002Cu; // LC_ENCRYPTION_INFO_64

#pragma pack(push, 1)
struct MachOEncryptionInfo64Raw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t cryptoff{0};
    std::uint32_t cryptsize{0};
    std::uint32_t cryptid{0};
    std::uint32_t pad{0};
};
#pragma pack(pop)

std::string RealArtifactHarness::compute_sha256(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) {
        return "";
    }
    return LinuxImageLoader::compute_sha256(data, size);
}

bool RealArtifactHarness::can_execute_without_sep_keys(const RealArtifactMetadata& meta) noexcept {
    return meta.can_execute_in_userspace();
}

RealArtifactMetadata RealArtifactHarness::inspect_buffer(
    const std::uint8_t* data,
    std::size_t size,
    std::string_view file_path) {

    RealArtifactMetadata meta{};
    meta.file_path = std::string(file_path);
    meta.file_size = size;

    if (!data || size < 32) {
        meta.decision = AdmissionDecision::Rejected_Truncated;
        meta.status_message = "Buffer truncated or smaller than minimum 32-byte header";
        return meta;
    }

    meta.sha256 = compute_sha256(data, size);

    // 1. Check for Apple IM4P / IMG4 Container
    if (std::memcmp(data, "IM4P", 4) == 0 ||
        (data[0] == 0x30 && size > 64 && (
            std::memcmp(data + 4, "IMG4", 4) == 0 ||
            std::memcmp(data + 8, "IM4P", 4) == 0 ||
            std::memcmp(data + 16, "IM4M", 4) == 0 ||
            std::memcmp(data + 32, "IM4P", 4) == 0))) {

        meta.is_img4_container = true;

        // Check for encrypted payload or KBAG
        bool has_kbag = false;
        const std::string search_blob(reinterpret_cast<const char*>(data), size > 4096 ? 4096 : size);
        if (search_blob.find("KBAG") != std::string::npos ||
            search_blob.find("AES-256") != std::string::npos ||
            search_blob.find("kbag") != std::string::npos) {
            has_kbag = true;
        }

        if (has_kbag) {
            meta.is_encrypted = true;
            meta.requires_sep_gid = true;
            meta.decision = AdmissionDecision::Rejected_Encrypted;
            meta.status_message = "Encrypted IM4P/IMG4 container requiring Apple SEP GID hardware keys (BLOCKED)";
        } else {
            meta.is_encrypted = false;
            meta.requires_sep_gid = false;
            meta.decision = AdmissionDecision::Admitted;
            meta.status_message = "Unencrypted personalized IMG4 container admitted";
        }
        return meta;
    }

    // 2. Check for Mach-O 64-Bit Binary
    std::uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));

    if (magic == kMachO64Magic) {
        meta.is_macho = true;

        MachOHeader64Raw hdr{};
        std::memcpy(&hdr, data, sizeof(hdr));

        if (hdr.cputype == kMachOCpuTypeARM64) {
            meta.is_arm64 = true;
            const std::uint32_t subtype = hdr.cpusubtype & kMachOCpuSubtypeMask;
            if (subtype == kMachOCpuSubtypeArm64E) {
                meta.is_arm64e = true;
            }
        } else {
            meta.decision = AdmissionDecision::Rejected_ArchitectureMismatch;
            meta.status_message = "Architecture mismatch: binary is not ARM64/ARM64e";
            return meta;
        }

        // Iterate load commands to inspect LC_ENCRYPTION_INFO_64
        std::size_t offset = sizeof(MachOHeader64Raw);
        for (std::uint32_t i = 0; i < hdr.ncmds; ++i) {
            if (offset + sizeof(MachOLoadCommandRaw) > size) break;

            MachOLoadCommandRaw cmd{};
            std::memcpy(&cmd, data + offset, sizeof(cmd));

            if (cmd.cmdsize < sizeof(MachOLoadCommandRaw) || offset + cmd.cmdsize > size) break;

            if (cmd.cmd == kCmdEncryptionInfo64 && cmd.cmdsize >= sizeof(MachOEncryptionInfo64Raw)) {
                MachOEncryptionInfo64Raw enc{};
                std::memcpy(&enc, data + offset, sizeof(enc));
                meta.cryptid = enc.cryptid;
                meta.cryptoff = enc.cryptoff;
                meta.cryptsize = enc.cryptsize;
                if (enc.cryptid != 0) {
                    meta.is_encrypted = true;
                }
            } else if (cmd.cmd == kCmdEncryptionInfo && cmd.cmdsize >= 20) {
                // 32-bit encryption info structure fallback
                std::uint32_t cryptid = 0;
                std::memcpy(&cryptid, data + offset + 16, sizeof(cryptid));
                meta.cryptid = cryptid;
                if (cryptid != 0) {
                    meta.is_encrypted = true;
                }
            }

            offset += cmd.cmdsize;
        }

        if (meta.is_encrypted) {
            meta.requires_sep_gid = false; // Decryption does not need SEP hardware, but requires FairPlay keys
            meta.decision = AdmissionDecision::Rejected_Encrypted;
            meta.status_message = "Mach-O contains encrypted segments (cryptid != 0); unencrypted binary required";
        } else {
            meta.decision = AdmissionDecision::Admitted;
            meta.status_message = "Unencrypted 64-bit ARM64/ARM64e Mach-O admitted for userspace execution";
        }

        return meta;
    }

    // 3. Unrecognized or Malformed File
    meta.decision = AdmissionDecision::Rejected_MalformedHeader;
    meta.status_message = "Unrecognized binary format (not 64-bit Mach-O or Apple IMG4 container)";
    return meta;
}

} // namespace winios::loader
