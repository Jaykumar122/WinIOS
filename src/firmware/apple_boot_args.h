#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace winios::firmware {

// -----------------------------------------------------------------------------
// Darwin / XNU ARM64 Boot Arguments Constants
// Reference: Apple XNU platform expert header pexpert/pexpert/arm64/boot.h
// -----------------------------------------------------------------------------
constexpr std::uint16_t kAppleBootArgsRevision1 = 1u;
constexpr std::uint16_t kAppleBootArgsRevision2 = 2u; // Canonical Apple XNU ARM64 revision (added bootFlags)
constexpr std::uint16_t kAppleBootArgsVersion1  = 1u;
constexpr std::uint16_t kAppleBootArgsVersion2  = 2u;

// Standard command line length by Darwin boot_args specification
// BOOT_LINE_LENGTH in pexpert/arm64/boot.h is exactly 608 bytes (0x260)
constexpr std::size_t kAppleBootCommandLineMaxRev2 = 0x260; // 608 bytes (Canonical XNU BOOT_LINE_LENGTH)
constexpr std::size_t kAppleBootCommandLineMax     = kAppleBootCommandLineMaxRev2;

// Experimental downstream extended buffer (encountered in Inferno/emulators, NOT in public Apple XNU)
constexpr std::size_t kAppleBootCommandLineMaxExperimentalRev3 = 0x400; // 1024 bytes (Experimental)
constexpr std::uint16_t kAppleBootArgsExperimentalRevision3 = 3u;      // Non-standard downstream revision

// Compatibility aliases
constexpr std::size_t kAppleBootCommandLineMaxRev3 = kAppleBootCommandLineMaxExperimentalRev3;
constexpr std::uint16_t kAppleBootArgsRevision3     = kAppleBootArgsExperimentalRevision3;

// Standard Apple boot flags (defined in pexpert/arm64/boot.h)
constexpr std::uint64_t kAppleBootFlagDarkBoot   = 1ULL << 0; // kBootFlagsDarkBoot
constexpr std::uint64_t kAppleBootFlagTextMode   = 1ULL << 1;
constexpr std::uint64_t kAppleBootFlagSerial     = 1ULL << 2;
constexpr std::uint64_t kAppleBootFlagSafeBoot   = 1ULL << 3;

// -----------------------------------------------------------------------------
// AppleBootVideo: Display framebuffer metadata passed to XNU
// Standard 64-bit LP64 Darwin Video structure (Boot_Video, 48 bytes)
// Reference: pexpert/pexpert/arm64/boot.h
// -----------------------------------------------------------------------------
struct AppleBootVideo {
    std::uint64_t v_baseAddr{0};   // 0x00 Base physical address of video memory
    std::uint64_t v_display{0};    // 0x08 Display code / index
    std::uint64_t v_rowBytes{0};   // 0x10 Number of bytes per row (stride)
    std::uint64_t v_width{0};      // 0x18 Width in pixels
    std::uint64_t v_height{0};     // 0x20 Height in pixels
    std::uint64_t v_depth{32};     // 0x28 Pixel depth and other display parameters (64-bit)
};

static_assert(sizeof(AppleBootVideo) == 48, "AppleBootVideo must be exactly 48 bytes (0x30)");
static_assert(alignof(AppleBootVideo) == 8, "AppleBootVideo must have 8-byte alignment");
static_assert(offsetof(AppleBootVideo, v_baseAddr) == 0,  "v_baseAddr offset mismatch");
static_assert(offsetof(AppleBootVideo, v_display)  == 8,  "v_display offset mismatch");
static_assert(offsetof(AppleBootVideo, v_rowBytes) == 16, "v_rowBytes offset mismatch");
static_assert(offsetof(AppleBootVideo, v_width)    == 24, "v_width offset mismatch");
static_assert(offsetof(AppleBootVideo, v_height)   == 32, "v_height offset mismatch");
static_assert(offsetof(AppleBootVideo, v_depth)    == 40, "v_depth offset mismatch");

// -----------------------------------------------------------------------------
// AppleBootArgsRev2: The canonical 64-bit Darwin kernel boot argument structure
// Exact binary match to struct boot_args in Apple XNU pexpert/arm64/boot.h.
// Naturally aligned LP64 ABI (736 bytes total).
// -----------------------------------------------------------------------------
struct AppleBootArgsRev2 {
    std::uint16_t Revision{kAppleBootArgsRevision2}; // 0x000 (0)   Revision of boot_args structure (2)
    std::uint16_t Version{kAppleBootArgsVersion2};   // 0x002 (2)   Version of boot_args structure (2)
    std::uint8_t  reserved1[4]{0};                   // 0x004 (4)   Natural alignment padding to 8-byte boundary
    std::uint64_t virtBase{0};                       // 0x008 (8)   Virtual base of kernel memory mapping
    std::uint64_t physBase{0};                       // 0x010 (16)  Physical base of memory in RAM
    std::uint64_t memSize{0};                        // 0x018 (24)  Size of physical memory
    std::uint64_t topOfKernelData{0};                // 0x020 (32)  Highest physical address used in kernel data area
    AppleBootVideo Video{};                          // 0x028 (40)  Video Information (Boot_Video, 48 B)
    std::uint32_t machineType{0};                    // 0x058 (88)  Machine Type
    std::uint8_t  reserved2[4]{0};                   // 0x05C (92)  Natural alignment padding to 8-byte boundary
    std::uint64_t deviceTreeP{0};                    // 0x060 (96)  Base of flattened device tree
    std::uint32_t deviceTreeLength{0};               // 0x068 (104) Length of flattened tree
    char CommandLine[kAppleBootCommandLineMaxRev2]{0};// 0x06C (108) Passed in command line (BOOT_LINE_LENGTH = 608 B)
    std::uint8_t  reserved3[4]{0};                   // 0x2CC (716) Natural alignment padding to 8-byte boundary
    std::uint64_t bootFlags{0};                      // 0x2D0 (720) Additional flags specified by bootloader
    std::uint64_t memSizeActual{0};                  // 0x2D8 (728) Actual physical memory size
};

static_assert(sizeof(AppleBootArgsRev2) == 736, "AppleBootArgsRev2 must be exactly 736 bytes (0x2E0)");
static_assert(alignof(AppleBootArgsRev2) == 8,  "AppleBootArgsRev2 must have 8-byte alignment");
static_assert(offsetof(AppleBootArgsRev2, Revision)         == 0,   "Revision offset mismatch (expected 0)");
static_assert(offsetof(AppleBootArgsRev2, Version)          == 2,   "Version offset mismatch (expected 2)");
static_assert(offsetof(AppleBootArgsRev2, virtBase)         == 8,   "virtBase offset mismatch (expected 8)");
static_assert(offsetof(AppleBootArgsRev2, physBase)         == 16,  "physBase offset mismatch (expected 16)");
static_assert(offsetof(AppleBootArgsRev2, memSize)          == 24,  "memSize offset mismatch (expected 24)");
static_assert(offsetof(AppleBootArgsRev2, topOfKernelData) == 32,  "topOfKernelData offset mismatch (expected 32)");
static_assert(offsetof(AppleBootArgsRev2, Video)            == 40,  "Video offset mismatch (expected 40)");
static_assert(offsetof(AppleBootArgsRev2, machineType)      == 88,  "machineType offset mismatch (expected 88)");
static_assert(offsetof(AppleBootArgsRev2, deviceTreeP)      == 96,  "deviceTreeP offset mismatch (expected 96)");
static_assert(offsetof(AppleBootArgsRev2, deviceTreeLength) == 104, "deviceTreeLength offset mismatch (expected 104)");
static_assert(offsetof(AppleBootArgsRev2, CommandLine)      == 108, "CommandLine offset mismatch (expected 108)");
static_assert(offsetof(AppleBootArgsRev2, bootFlags)        == 720, "bootFlags offset mismatch (expected 720)");
static_assert(offsetof(AppleBootArgsRev2, memSizeActual)    == 728, "memSizeActual offset mismatch (expected 728)");

// -----------------------------------------------------------------------------
// AppleBootArgsExperimentalRev3: Downstream / Experimental 1152-byte layout
// NOTE: This structure is NOT part of public Apple XNU source code. It originated
// in downstream emulator/research projects (such as Inferno) with a 1024-byte
// command line buffer. It is retained strictly as an experimental option and
// is NOT claimed to be the official Apple standard for iOS 17+ or iPhone 15.
// -----------------------------------------------------------------------------
struct AppleBootArgsExperimentalRev3 {
    std::uint16_t Revision{kAppleBootArgsExperimentalRevision3}; // 0x000 (0)
    std::uint16_t Version{kAppleBootArgsVersion2};               // 0x002 (2)
    std::uint8_t  reserved1[4]{0};                               // 0x004 (4)
    std::uint64_t virtBase{0};                                   // 0x008 (8)
    std::uint64_t physBase{0};                                   // 0x010 (16)
    std::uint64_t memSize{0};                                    // 0x018 (24)
    std::uint64_t topOfKernelData{0};                            // 0x020 (32)
    AppleBootVideo Video{};                                      // 0x028 (40)
    std::uint32_t machineType{0};                                // 0x058 (88)
    std::uint8_t  reserved2[4]{0};                               // 0x05C (92)
    std::uint64_t deviceTreeP{0};                                // 0x060 (96)
    std::uint32_t deviceTreeLength{0};                           // 0x068 (104)
    char CommandLine[kAppleBootCommandLineMaxExperimentalRev3]{0};// 0x06C (108) 1024 B
    std::uint8_t  reserved3[4]{0};                               // 0x46C (1132)
    std::uint64_t bootFlags{0};                                  // 0x470 (1136)
    std::uint64_t memSizeActual{0};                              // 0x478 (1144)
};

static_assert(sizeof(AppleBootArgsExperimentalRev3) == 1152, "AppleBootArgsExperimentalRev3 must be exactly 1152 bytes (0x480)");
static_assert(alignof(AppleBootArgsExperimentalRev3) == 8,   "AppleBootArgsExperimentalRev3 must have 8-byte alignment");

// Compatibility alias for existing code
using AppleBootArgsRev3 = AppleBootArgsExperimentalRev3;

// Canonical alias: Revision 2 is the sole default boot ABI across WinIOS
using AppleBootArgs = AppleBootArgsRev2;

// -----------------------------------------------------------------------------
// AppleBootArgsValidator: Comprehensive validation of the Darwin boot contract
// -----------------------------------------------------------------------------
class AppleBootArgsValidator {
public:
    // Validates raw buffer size, alignment, revision, and version
    static bool validate_buffer_layout(const void* buffer, std::size_t size, std::string* out_error = nullptr) {
        if (!buffer) {
            if (out_error) *out_error = "Boot arguments buffer pointer is null";
            return false;
        }
        if ((reinterpret_cast<std::uintptr_t>(buffer) & 0x7u) != 0) {
            if (out_error) *out_error = "Boot arguments buffer address is not 8-byte aligned (LP64 natural alignment requirement)";
            return false;
        }
        if (size < sizeof(AppleBootArgsRev2)) {
            if (out_error) *out_error = "Boot arguments buffer truncated (got " + std::to_string(size) +
                                         ", minimum required " + std::to_string(sizeof(AppleBootArgsRev2)) + " bytes)";
            return false;
        }

        const auto* raw16 = reinterpret_cast<const std::uint16_t*>(buffer);
        const std::uint16_t rev = raw16[0];
        const std::uint16_t ver = raw16[1];

        if (rev != kAppleBootArgsRevision2 && rev != kAppleBootArgsRevision3) {
            if (out_error) *out_error = "Unsupported boot_args Revision: " + std::to_string(rev) +
                                         " (expected canonical Revision 2 or experimental Rev 3)";
            return false;
        }
        if (ver != kAppleBootArgsVersion2) {
            if (out_error) *out_error = "Unsupported boot_args Version: " + std::to_string(ver) +
                                         " (expected Version 2)";
            return false;
        }
        if (rev == kAppleBootArgsRevision3 && size < sizeof(AppleBootArgsRev3)) {
            if (out_error) *out_error = "Boot arguments buffer truncated for Experimental Rev 3 (got " + std::to_string(size) +
                                         ", required " + std::to_string(sizeof(AppleBootArgsRev3)) + " bytes)";
            return false;
        }
        return true;
    }

    // Validates memory ranges, alignment, address overflow, and boundaries
    static bool validate_memory_contract(
        std::uint64_t phys_base,
        std::uint64_t mem_size,
        std::uint64_t top_of_kernel,
        std::uint64_t virt_base,
        std::uint64_t ram_base = 0x40000000ULL,
        std::uint64_t ram_size = 0x40000000ULL,
        std::string* out_error = nullptr) {

        // 16 KB translation granule alignment for Apple Silicon
        if ((phys_base & 0x3FFFULL) != 0) {
            if (out_error) *out_error = "physBase is not aligned to Apple Silicon 16 KB page granule (0x4000)";
            return false;
        }
        if (mem_size == 0) {
            if (out_error) *out_error = "Physical memory size is zero";
            return false;
        }
        // Address overflow check
        if (phys_base + mem_size < phys_base) {
            if (out_error) *out_error = "Integer overflow in physical memory range: physBase + memSize wrapped";
            return false;
        }
        // RAM bounds check
        if (phys_base < ram_base || (phys_base + mem_size) > (ram_base + ram_size)) {
            if (out_error) *out_error = "Physical memory range outside backed physical RAM boundary";
            return false;
        }
        // Kernel binary boundaries
        if (top_of_kernel < phys_base) {
            if (out_error) *out_error = "topOfKernelData cannot be less than physBase";
            return false;
        }
        if (top_of_kernel > phys_base + mem_size) {
            if (out_error) *out_error = "topOfKernelData exceeds physical memory boundary";
            return false;
        }
        // Canonical virtual address check (must be high-half 64-bit kernel mapping)
        if (virt_base < 0xFFFF000000000000ULL) {
            if (out_error) *out_error = "virtBase is not a canonical high-half 64-bit kernel virtual address";
            return false;
        }
        return true;
    }

    // Validates DeviceTree staging pointer, size, and bounds
    static bool validate_devicetree_contract(
        std::uint64_t dtb_phys,
        std::uint32_t dtb_len,
        std::uint64_t ram_base = 0x40000000ULL,
        std::uint64_t ram_size = 0x40000000ULL,
        std::string* out_error = nullptr) {

        if (dtb_phys == 0 || dtb_len == 0) {
            if (out_error) *out_error = "DeviceTree pointer or length is uninitialized/zero";
            return false;
        }
        if (dtb_len < 16) {
            if (out_error) *out_error = "DeviceTree length smaller than minimum header size (16 bytes)";
            return false;
        }
        if (dtb_phys + dtb_len < dtb_phys) {
            if (out_error) *out_error = "Integer overflow in DeviceTree physical range";
            return false;
        }
        if (dtb_phys < ram_base || (dtb_phys + dtb_len) > (ram_base + ram_size)) {
            if (out_error) *out_error = "DeviceTree physical memory range outside backed physical RAM boundary";
            return false;
        }
        if ((dtb_phys & 0x7ULL) != 0) {
            if (out_error) *out_error = "DeviceTree physical pointer is not 8-byte aligned";
            return false;
        }
        return true;
    }

    // Validates that kernel data, devicetree, and framebuffer regions do not overlap
    static bool validate_non_overlapping_regions(
        std::uint64_t kernel_start,
        std::uint64_t kernel_end,
        std::uint64_t dtb_start,
        std::uint64_t dtb_len,
        std::uint64_t fb_start = 0,
        std::uint64_t fb_len = 0,
        std::string* out_error = nullptr) {

        const std::uint64_t dtb_end = dtb_start + dtb_len;

        // Check Kernel vs DeviceTree overlap
        if (std::max(kernel_start, dtb_start) < std::min(kernel_end, dtb_end)) {
            if (out_error) *out_error = "Kernel image region overlaps DeviceTree blob memory";
            return false;
        }

        // Check Framebuffer overlaps if active
        if (fb_start != 0 && fb_len != 0) {
            const std::uint64_t fb_end = fb_start + fb_len;
            if (std::max(kernel_start, fb_start) < std::min(kernel_end, fb_end)) {
                if (out_error) *out_error = "Kernel image region overlaps Framebuffer memory";
                return false;
            }
            if (std::max(dtb_start, fb_start) < std::min(dtb_end, fb_end)) {
                if (out_error) *out_error = "DeviceTree blob memory overlaps Framebuffer memory";
                return false;
            }
        }
        return true;
    }

    // Validates command line string (null-termination and printable ASCII characters)
    static bool validate_command_line(const char* cmdline, std::size_t max_len, std::string* out_error = nullptr) {
        if (!cmdline) {
            if (out_error) *out_error = "CommandLine pointer is null";
            return false;
        }
        bool null_terminated = false;
        for (std::size_t i = 0; i < max_len; ++i) {
            if (cmdline[i] == '\0') {
                null_terminated = true;
                break;
            }
            const unsigned char c = static_cast<unsigned char>(cmdline[i]);
            if (c < 0x20 || c > 0x7E) {
                if (out_error) *out_error = "CommandLine contains non-printable character at index " + std::to_string(i);
                return false;
            }
        }
        if (!null_terminated) {
            if (out_error) *out_error = "CommandLine is not null-terminated within allocated buffer size";
            return false;
        }
        return true;
    }

    // Full contract validation for AppleBootArgsRev2
    static bool validate(const AppleBootArgsRev2& args,
                         std::uint64_t ram_base = 0x40000000ULL,
                         std::uint64_t ram_size = 0x40000000ULL,
                         std::string* out_error = nullptr) {

        if (!validate_buffer_layout(&args, sizeof(args), out_error)) return false;
        if (!validate_memory_contract(args.physBase, args.memSize, args.topOfKernelData, args.virtBase, ram_base, ram_size, out_error)) return false;
        if (!validate_devicetree_contract(args.deviceTreeP, args.deviceTreeLength, ram_base, ram_size, out_error)) return false;
        
        const std::uint64_t fb_len = args.Video.v_height * args.Video.v_rowBytes;
        if (!validate_non_overlapping_regions(args.physBase, args.topOfKernelData, args.deviceTreeP, args.deviceTreeLength, args.Video.v_baseAddr, fb_len, out_error)) return false;
        if (!validate_command_line(args.CommandLine, sizeof(args.CommandLine), out_error)) return false;

        return true;
    }

    // Full contract validation for AppleBootArgsRev3
    static bool validate_rev3(const AppleBootArgsRev3& args,
                              std::uint64_t ram_base = 0x40000000ULL,
                              std::uint64_t ram_size = 0x40000000ULL,
                              std::string* out_error = nullptr) {

        if (!validate_buffer_layout(&args, sizeof(args), out_error)) return false;
        if (!validate_memory_contract(args.physBase, args.memSize, args.topOfKernelData, args.virtBase, ram_base, ram_size, out_error)) return false;
        if (!validate_devicetree_contract(args.deviceTreeP, args.deviceTreeLength, ram_base, ram_size, out_error)) return false;

        const std::uint64_t fb_len = args.Video.v_height * args.Video.v_rowBytes;
        if (!validate_non_overlapping_regions(args.physBase, args.topOfKernelData, args.deviceTreeP, args.deviceTreeLength, args.Video.v_baseAddr, fb_len, out_error)) return false;
        if (!validate_command_line(args.CommandLine, sizeof(args.CommandLine), out_error)) return false;

        return true;
    }
};

// -----------------------------------------------------------------------------
// AppleBootArgsBuilder: Constructs Darwin boot_args structures
// -----------------------------------------------------------------------------
class AppleBootArgsBuilder {
public:
    static AppleBootArgsRev2 create(
        std::uint64_t phys_base,
        std::uint64_t virt_base,
        std::uint64_t mem_size,
        std::uint64_t top_of_kernel,
        std::uint64_t dtb_phys_addr,
        std::uint32_t dtb_len,
        const std::string& cmdline = "-v debug=0x144 serial=3",
        const AppleBootVideo& video = {},
        std::uint64_t boot_flags = kAppleBootFlagDarkBoot) {

        AppleBootArgsRev2 args{};
        args.Revision = kAppleBootArgsRevision2;
        args.Version = kAppleBootArgsVersion2;
        args.physBase = phys_base;
        args.virtBase = virt_base;
        args.memSize = mem_size;
        args.memSizeActual = mem_size;
        args.topOfKernelData = top_of_kernel;
        args.deviceTreeP = dtb_phys_addr;
        args.deviceTreeLength = dtb_len;
        args.Video = video;
        args.machineType = 0x4150504Cu; // 'APPL'
        args.bootFlags = boot_flags;

        const std::size_t copy_len = std::min(cmdline.size(), sizeof(args.CommandLine) - 1);
        std::memcpy(args.CommandLine, cmdline.data(), copy_len);
        args.CommandLine[copy_len] = '\0';

        return args;
    }

    // Experimental builder: creates downstream 1152-byte layout (Inferno/emulator)
    static AppleBootArgsExperimentalRev3 create_experimental_rev3(
        std::uint64_t phys_base,
        std::uint64_t virt_base,
        std::uint64_t mem_size,
        std::uint64_t top_of_kernel,
        std::uint64_t dtb_phys_addr,
        std::uint32_t dtb_len,
        const std::string& cmdline = "-v debug=0x144 serial=3",
        const AppleBootVideo& video = {},
        std::uint64_t boot_flags = kAppleBootFlagDarkBoot) {

        AppleBootArgsExperimentalRev3 args{};
        args.Revision = kAppleBootArgsExperimentalRevision3;
        args.Version = kAppleBootArgsVersion2;
        args.physBase = phys_base;
        args.virtBase = virt_base;
        args.memSize = mem_size;
        args.memSizeActual = mem_size;
        args.topOfKernelData = top_of_kernel;
        args.deviceTreeP = dtb_phys_addr;
        args.deviceTreeLength = dtb_len;
        args.Video = video;
        args.machineType = 0x4150504Cu; // 'APPL'
        args.bootFlags = boot_flags;

        const std::size_t copy_len = std::min(cmdline.size(), sizeof(args.CommandLine) - 1);
        std::memcpy(args.CommandLine, cmdline.data(), copy_len);
        args.CommandLine[copy_len] = '\0';

        return args;
    }

    // Compatibility alias for existing code
    static AppleBootArgsExperimentalRev3 create_rev3(
        std::uint64_t phys_base,
        std::uint64_t virt_base,
        std::uint64_t mem_size,
        std::uint64_t top_of_kernel,
        std::uint64_t dtb_phys_addr,
        std::uint32_t dtb_len,
        const std::string& cmdline = "-v debug=0x144 serial=3",
        const AppleBootVideo& video = {},
        std::uint64_t boot_flags = kAppleBootFlagDarkBoot) {
        return create_experimental_rev3(phys_base, virt_base, mem_size, top_of_kernel, dtb_phys_addr, dtb_len, cmdline, video, boot_flags);
    }

    static bool validate(const AppleBootArgsRev2& args, std::string* out_error = nullptr) {
        const std::uint64_t effective_ram = (args.memSize > 0) ? std::max(args.memSize, 0x40000000ULL) : 0x40000000ULL;
        return AppleBootArgsValidator::validate(args, args.physBase ? args.physBase : 0x40000000ULL, effective_ram, out_error);
    }
};

} // namespace winios::firmware
