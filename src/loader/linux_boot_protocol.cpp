#include "loader/linux_boot_protocol.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::loader {

bool LinuxBootProtocolValidator::parse_header(const std::uint8_t* buffer, std::size_t size,
                                             LinuxArm64ImageHeader& out_header, std::string& out_error) {
    if (!buffer) {
        out_error = "Null buffer supplied for Linux ARM64 image header";
        return false;
    }

    if (size < kLinuxArm64HeaderSize) {
        std::ostringstream ss;
        ss << "Image size (" << size << " bytes) is smaller than required ARM64 header size (64 bytes)";
        out_error = ss.str();
        return false;
    }

    std::memcpy(&out_header, buffer, sizeof(LinuxArm64ImageHeader));

    if (!out_header.is_valid_magic()) {
        std::ostringstream ss;
        ss << "Invalid Linux ARM64 image magic 0x" << std::hex << out_header.magic
           << " (expected 0x" << kLinuxArm64ImageMagic << " 'ARM\\x64')";
        out_error = ss.str();
        return false;
    }

    if (out_header.is_big_endian()) {
        out_error = "Big-endian ARM64 Linux kernels are not supported by the little-endian virtual platform";
        return false;
    }

    out_error.clear();
    return true;
}

bool LinuxBootProtocolValidator::validate_placement(const LinuxArm64ImageHeader& header,
                                                   std::uint64_t ram_base,
                                                   std::uint64_t ram_size,
                                                   std::uint64_t target_load_addr,
                                                   std::string& out_error) {
    if ((ram_base & (kLinuxArm64Align2MB - 1u)) != 0) {
        out_error = "RAM base address must be 2MB aligned per Linux ARM64 boot protocol";
        return false;
    }

    if (target_load_addr < ram_base) {
        out_error = "Target kernel load address is below RAM base address";
        return false;
    }

    // Kernel entry must be 4-byte aligned
    if ((target_load_addr & 0x3u) != 0) {
        out_error = "Kernel load address must be at least 4-byte aligned";
        return false;
    }

    const std::uint64_t effective_size = (header.image_size > 0) ? header.image_size : 0x200000ULL;
    const std::uint64_t ram_end = ram_base + ram_size;

    // Check 64-bit integer wrap-around
    if (target_load_addr > UINT64_MAX - effective_size) {
        out_error = "Kernel image footprint causes 64-bit address overflow";
        return false;
    }

    if (target_load_addr + effective_size > ram_end) {
        std::ostringstream ss;
        ss << "Kernel image footprint (load 0x" << std::hex << target_load_addr
           << " + size 0x" << effective_size << ") extends beyond RAM capacity (0x" << ram_end << ")";
        out_error = ss.str();
        return false;
    }

    out_error.clear();
    return true;
}

bool LinuxBootProtocolValidator::validate_dtb_placement(std::uint64_t dtb_address,
                                                       std::uint64_t dtb_size,
                                                       std::uint64_t ram_base,
                                                       std::uint64_t ram_size,
                                                       std::uint64_t kernel_load_addr,
                                                       std::uint64_t kernel_effective_size,
                                                       std::string& out_error) {
    // DTB must be 8-byte aligned per Linux ARM64 boot protocol
    if ((dtb_address & 0x7u) != 0) {
        out_error = "Device Tree Blob (DTB) address must be 8-byte aligned";
        return false;
    }

    if (dtb_size == 0) {
        out_error = "Device Tree Blob size cannot be zero";
        return false;
    }

    if (dtb_address < ram_base) {
        out_error = "Device Tree Blob address is below RAM base";
        return false;
    }

    const std::uint64_t ram_end = ram_base + ram_size;
    if (dtb_address > UINT64_MAX - dtb_size || dtb_address + dtb_size > ram_end) {
        out_error = "Device Tree Blob extends beyond RAM capacity";
        return false;
    }

    // Overlap validation between kernel image memory footprint and DTB
    const std::uint64_t k_start = kernel_load_addr;
    const std::uint64_t k_end   = kernel_load_addr + kernel_effective_size;
    const std::uint64_t d_start = dtb_address;
    const std::uint64_t d_end   = dtb_address + dtb_size;

    if (std::max(k_start, d_start) < std::min(k_end, d_end)) {
        std::ostringstream ss;
        ss << "Device Tree Blob [0x" << std::hex << d_start << "..0x" << d_end
           << ") collides with kernel image footprint [0x" << k_start << "..0x" << k_end << ")";
        out_error = ss.str();
        return false;
    }

    out_error.clear();
    return true;
}

bool LinuxBootProtocolValidator::validate_cpu_handoff(const CPU& cpu, std::uint64_t expected_entry,
                                                     std::uint64_t expected_dtb, std::string& out_error) {
    if (cpu.get_program_counter() != expected_entry) {
        std::ostringstream ss;
        ss << "CPU PC (0x" << std::hex << cpu.get_program_counter()
           << ") does not match expected kernel entry point (0x" << expected_entry << ")";
        out_error = ss.str();
        return false;
    }

    if (cpu.read_register(0) != expected_dtb) {
        std::ostringstream ss;
        ss << "CPU X0 (0x" << std::hex << cpu.read_register(0)
           << ") does not contain expected DTB pointer (0x" << expected_dtb << ")";
        out_error = ss.str();
        return false;
    }

    if (cpu.read_register(1) != 0 || cpu.read_register(2) != 0 || cpu.read_register(3) != 0) {
        out_error = "CPU registers X1, X2, and X3 must be 0 (reserved by Linux ARM64 boot protocol)";
        return false;
    }

    if ((cpu.get_daif() & 0xFu) != 0xFu) {
        out_error = "CPU DAIF interrupts must all be masked (D=1, A=1, I=1, F=1) at kernel handoff";
        return false;
    }

    if (cpu.get_current_el() != 1 && cpu.get_current_el() != 2) {
        out_error = "CPU CurrentEL must be EL1 or EL2 at Linux kernel handoff";
        return false;
    }

    out_error.clear();
    return true;
}

} // namespace winios::loader
