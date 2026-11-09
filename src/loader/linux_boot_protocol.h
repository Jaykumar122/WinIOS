#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/cpu.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Linux ARM64 Image Header Constants (arch/arm64/booting.rst)
// -----------------------------------------------------------------------------
constexpr std::uint32_t kLinuxArm64ImageMagic = 0x644D5241u; // "ARM\x64" little-endian
constexpr std::size_t   kLinuxArm64HeaderSize = 64u;         // Exactly 64 bytes
constexpr std::uint64_t kLinuxArm64Align2MB   = 0x200000ULL; // 2 MB alignment

// -----------------------------------------------------------------------------
// LinuxArm64ImageHeader: Standard 64-byte AArch64 Image Header
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct LinuxArm64ImageHeader {
    std::uint32_t code0;        // Executable code / branch instruction
    std::uint32_t code1;        // Executable code
    std::uint64_t text_offset;  // Image load offset from 2MB aligned base of RAM
    std::uint64_t image_size;   // Effective image size (little-endian)
    std::uint64_t flags;        // Kernel flags
    std::uint64_t res2;         // Reserved
    std::uint64_t res3;         // Reserved
    std::uint64_t res4;         // Reserved
    std::uint32_t magic;        // Magic number: 0x644D5241u
    std::uint32_t res5;         // Reserved (PE/COFF offset in EFI images)

    [[nodiscard]] bool is_valid_magic() const noexcept {
        return magic == kLinuxArm64ImageMagic;
    }

    [[nodiscard]] bool is_big_endian() const noexcept {
        return (flags & 0x1u) != 0;
    }

    [[nodiscard]] std::uint8_t page_size_flag() const noexcept {
        return static_cast<std::uint8_t>((flags >> 1u) & 0x3u);
    }
};
#pragma pack(pop)
static_assert(sizeof(LinuxArm64ImageHeader) == 64, "LinuxArm64ImageHeader must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// LinuxBootParams: Handoff parameters and memory locations
// -----------------------------------------------------------------------------
struct LinuxBootParams {
    std::uint64_t ram_base{0x40000000ULL};
    std::uint64_t ram_size{0x08000000ULL};     // 128 MB default
    std::uint64_t kernel_load_addr{0x40080000ULL};
    std::uint64_t kernel_entry{0x40080000ULL};
    std::uint64_t dtb_address{0x47000000ULL};
    std::uint64_t dtb_size{0x10000ULL};
    std::uint64_t initrd_address{0x0ULL};
    std::uint64_t initrd_size{0x0ULL};
    std::string boot_args{"console=ttyAMA0 earlycon=pl011,0x09000000 root=/dev/ram0 rw"};
};

// -----------------------------------------------------------------------------
// LinuxBootProtocolValidator: Validation and verification engine
// -----------------------------------------------------------------------------
class LinuxBootProtocolValidator {
public:
    static bool parse_header(const std::uint8_t* buffer, std::size_t size,
                             LinuxArm64ImageHeader& out_header, std::string& out_error);

    static bool validate_placement(const LinuxArm64ImageHeader& header,
                                   std::uint64_t ram_base,
                                   std::uint64_t ram_size,
                                   std::uint64_t target_load_addr,
                                   std::string& out_error);

    static bool validate_dtb_placement(std::uint64_t dtb_address,
                                       std::uint64_t dtb_size,
                                       std::uint64_t ram_base,
                                       std::uint64_t ram_size,
                                       std::uint64_t kernel_load_addr,
                                       std::uint64_t kernel_effective_size,
                                       std::string& out_error);

    static bool validate_cpu_handoff(const CPU& cpu, std::uint64_t expected_entry,
                                     std::uint64_t expected_dtb, std::string& out_error);
};

} // namespace winios::loader
