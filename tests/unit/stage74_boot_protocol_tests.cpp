#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/linux_boot_protocol.h"

#define STAGE74_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;

namespace {

LinuxArm64ImageHeader create_valid_header(std::uint64_t text_offset = 0x80000,
                                          std::uint64_t image_size = 0x1400000) {
    LinuxArm64ImageHeader hdr{};
    hdr.code0 = 0x1400000Au; // B +0x28
    hdr.code1 = 0xD503201Fu; // NOP
    hdr.text_offset = text_offset;
    hdr.image_size = image_size;
    hdr.flags = 0x2u;        // 4KB granule, little-endian
    hdr.res2 = 0;
    hdr.res3 = 0;
    hdr.res4 = 0;
    hdr.magic = kLinuxArm64ImageMagic; // 0x644D5241
    hdr.res5 = 0;
    return hdr;
}

// -----------------------------------------------------------------------------
// Suite 1: Valid Kernel Image Header Parsing
// -----------------------------------------------------------------------------
bool test_valid_kernel_image_header_parsing() {
    std::cout << "[1] test_valid_kernel_image_header_parsing...\n";

    LinuxArm64ImageHeader in_hdr = create_valid_header(0x80000, 0x1000000);
    std::vector<std::uint8_t> buffer(sizeof(LinuxArm64ImageHeader));
    std::memcpy(buffer.data(), &in_hdr, sizeof(in_hdr));

    LinuxArm64ImageHeader parsed{};
    std::string err;
    const bool ok = LinuxBootProtocolValidator::parse_header(buffer.data(), buffer.size(), parsed, err);

    STAGE74_ASSERT(ok, "Header parsed successfully");
    STAGE74_ASSERT(err.empty(), "Error is empty");
    STAGE74_ASSERT(parsed.magic == kLinuxArm64ImageMagic, "Magic matches ARM64 magic");
    STAGE74_ASSERT(parsed.text_offset == 0x80000, "text_offset is 0x80000");
    STAGE74_ASSERT(parsed.image_size == 0x1000000, "image_size is 0x1000000");
    STAGE74_ASSERT(!parsed.is_big_endian(), "Endianness is little-endian");
    STAGE74_ASSERT(parsed.page_size_flag() == 1, "Page size is 4KB");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Invalid Magic Rejection
// -----------------------------------------------------------------------------
bool test_invalid_magic_rejection() {
    std::cout << "[2] test_invalid_magic_rejection...\n";

    LinuxArm64ImageHeader in_hdr = create_valid_header();
    in_hdr.magic = 0x464C457Fu; // '\x7fELF'

    std::vector<std::uint8_t> buffer(sizeof(LinuxArm64ImageHeader));
    std::memcpy(buffer.data(), &in_hdr, sizeof(in_hdr));

    LinuxArm64ImageHeader parsed{};
    std::string err;
    const bool ok = LinuxBootProtocolValidator::parse_header(buffer.data(), buffer.size(), parsed, err);

    STAGE74_ASSERT(!ok, "Invalid magic rejected");
    STAGE74_ASSERT(!err.empty(), "Error message recorded");
    STAGE74_ASSERT(err.find("Invalid Linux ARM64 image magic") != std::string::npos, "Error diagnostic matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Truncated Header Rejection
// -----------------------------------------------------------------------------
bool test_truncated_header_rejection() {
    std::cout << "[3] test_truncated_header_rejection...\n";

    std::vector<std::uint8_t> buffer(32, 0); // Only 32 bytes (header is 64)
    LinuxArm64ImageHeader parsed{};
    std::string err;
    const bool ok = LinuxBootProtocolValidator::parse_header(buffer.data(), buffer.size(), parsed, err);

    STAGE74_ASSERT(!ok, "Truncated header rejected");
    STAGE74_ASSERT(!err.empty(), "Error message recorded");
    STAGE74_ASSERT(err.find("smaller than required") != std::string::npos, "Error diagnostic matches");

    // Null buffer
    const bool null_ok = LinuxBootProtocolValidator::parse_header(nullptr, 64, parsed, err);
    STAGE74_ASSERT(!null_ok, "Null buffer rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Big-Endian Kernel Rejection
// -----------------------------------------------------------------------------
bool test_big_endian_rejection() {
    std::cout << "[4] test_big_endian_rejection...\n";

    LinuxArm64ImageHeader in_hdr = create_valid_header();
    in_hdr.flags |= 0x1u; // Bit 0 = 1 (Big-endian)

    std::vector<std::uint8_t> buffer(sizeof(LinuxArm64ImageHeader));
    std::memcpy(buffer.data(), &in_hdr, sizeof(in_hdr));

    LinuxArm64ImageHeader parsed{};
    std::string err;
    const bool ok = LinuxBootProtocolValidator::parse_header(buffer.data(), buffer.size(), parsed, err);

    STAGE74_ASSERT(!ok, "Big-endian kernel rejected");
    STAGE74_ASSERT(!err.empty(), "Error message recorded");
    STAGE74_ASSERT(err.find("Big-endian") != std::string::npos, "Error diagnostic matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Valid Kernel Placement
// -----------------------------------------------------------------------------
bool test_valid_kernel_placement() {
    std::cout << "[5] test_valid_kernel_placement...\n";

    LinuxArm64ImageHeader hdr = create_valid_header(0x80000, 0x1000000); // 16 MB
    const std::uint64_t ram_base = 0x40000000ULL;
    const std::uint64_t ram_size = 0x08000000ULL; // 128 MB
    const std::uint64_t load_addr = 0x40080000ULL;

    std::string err;
    const bool ok = LinuxBootProtocolValidator::validate_placement(hdr, ram_base, ram_size, load_addr, err);
    STAGE74_ASSERT(ok, "Placement validated successfully");
    STAGE74_ASSERT(err.empty(), "Error empty on success");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Misaligned RAM Base or Load Address Rejection
// -----------------------------------------------------------------------------
bool test_misaligned_ram_base_or_load_addr() {
    std::cout << "[6] test_misaligned_ram_base_or_load_addr...\n";

    LinuxArm64ImageHeader hdr = create_valid_header(0x80000, 0x1000000);
    std::string err;

    // RAM base not 2MB aligned (e.g. 0x40100000 is 1MB offset, 0x40001000 is 4KB)
    const bool bad_base = LinuxBootProtocolValidator::validate_placement(hdr, 0x40001000ULL, 0x08000000ULL, 0x40080000ULL, err);
    STAGE74_ASSERT(!bad_base, "Non-2MB-aligned RAM base rejected");
    STAGE74_ASSERT(err.find("2MB aligned") != std::string::npos, "Error diagnostic matches");

    // Load address not 4-byte aligned (e.g. 0x40080002)
    const bool bad_load = LinuxBootProtocolValidator::validate_placement(hdr, 0x40000000ULL, 0x08000000ULL, 0x40080002ULL, err);
    STAGE74_ASSERT(!bad_load, "Non-4-byte aligned load address rejected");
    STAGE74_ASSERT(err.find("4-byte aligned") != std::string::npos, "Error diagnostic matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Kernel Exceeding RAM Capacity
// -----------------------------------------------------------------------------
bool test_kernel_exceeding_ram_capacity() {
    std::cout << "[7] test_kernel_exceeding_ram_capacity...\n";

    // 200 MB image in 128 MB RAM
    LinuxArm64ImageHeader hdr = create_valid_header(0x80000, 200ULL * 1024 * 1024);
    std::string err;
    const bool ok = LinuxBootProtocolValidator::validate_placement(hdr, 0x40000000ULL, 0x08000000ULL, 0x40080000ULL, err);

    STAGE74_ASSERT(!ok, "Oversized kernel image rejected");
    STAGE74_ASSERT(err.find("extends beyond RAM capacity") != std::string::npos, "Error diagnostic matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: DTB Alignment and Range Validation
// -----------------------------------------------------------------------------
bool test_dtb_alignment_and_range_validation() {
    std::cout << "[8] test_dtb_alignment_and_range_validation...\n";

    const std::uint64_t ram_base = 0x40000000ULL;
    const std::uint64_t ram_size = 0x08000000ULL;
    const std::uint64_t k_load   = 0x40080000ULL;
    const std::uint64_t k_size   = 0x01000000ULL; // Ends at 0x41080000
    const std::uint64_t dtb_size = 0x10000ULL;

    std::string err;

    // 1. Valid DTB at 0x47000000
    const bool valid = LinuxBootProtocolValidator::validate_dtb_placement(
        0x47000000ULL, dtb_size, ram_base, ram_size, k_load, k_size, err);
    STAGE74_ASSERT(valid, "Valid DTB placement passed");

    // 2. Misaligned DTB at 0x47000004 (not 8-byte aligned)
    const bool misaligned = LinuxBootProtocolValidator::validate_dtb_placement(
        0x47000004ULL, dtb_size, ram_base, ram_size, k_load, k_size, err);
    STAGE74_ASSERT(!misaligned, "Misaligned DTB rejected");
    STAGE74_ASSERT(err.find("8-byte aligned") != std::string::npos, "Error diagnostic matches");

    // 3. Out-of-bounds DTB at 0x50000000
    const bool oob = LinuxBootProtocolValidator::validate_dtb_placement(
        0x50000000ULL, dtb_size, ram_base, ram_size, k_load, k_size, err);
    STAGE74_ASSERT(!oob, "Out-of-bounds DTB rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Kernel-DTB Collision Detection
// -----------------------------------------------------------------------------
bool test_kernel_dtb_collision_detection() {
    std::cout << "[9] test_kernel_dtb_collision_detection...\n";

    const std::uint64_t ram_base = 0x40000000ULL;
    const std::uint64_t ram_size = 0x08000000ULL;
    const std::uint64_t k_load   = 0x40080000ULL;
    const std::uint64_t k_size   = 0x01000000ULL; // 0x40080000 .. 0x41080000
    const std::uint64_t dtb_size = 0x20000ULL;

    // DTB placed at 0x40800000 (collides directly inside kernel image)
    std::string err;
    const bool collided = LinuxBootProtocolValidator::validate_dtb_placement(
        0x40800000ULL, dtb_size, ram_base, ram_size, k_load, k_size, err);

    STAGE74_ASSERT(!collided, "Kernel-DTB collision detected");
    STAGE74_ASSERT(err.find("collides with kernel image footprint") != std::string::npos, "Collision diagnostic matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: CPU Handoff Contract Verification
// -----------------------------------------------------------------------------
bool test_cpu_handoff_contract_verification() {
    std::cout << "[10] test_cpu_handoff_contract_verification...\n";

    CPU cpu;
    cpu.reset();
    const std::uint64_t entry = 0x40080000ULL;
    const std::uint64_t dtb   = 0x47000000ULL;

    cpu.set_program_counter(entry);
    cpu.write_register(0, dtb);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0);
    cpu.write_register(3, 0);
    cpu.set_daif(0xFu);        // Masked
    cpu.set_current_el(1);     // EL1

    std::string err;
    STAGE74_ASSERT(LinuxBootProtocolValidator::validate_cpu_handoff(cpu, entry, dtb, err), "Handoff valid");

    // Violation: X1 non-zero
    cpu.write_register(1, 0x1234);
    STAGE74_ASSERT(!LinuxBootProtocolValidator::validate_cpu_handoff(cpu, entry, dtb, err), "X1 non-zero rejected");
    cpu.write_register(1, 0);

    // Violation: DAIF unmasked
    cpu.set_daif(0x0u);
    STAGE74_ASSERT(!LinuxBootProtocolValidator::validate_cpu_handoff(cpu, entry, dtb, err), "DAIF unmasked rejected");
    cpu.set_daif(0xFu);

    // Violation: EL0 privilege level
    cpu.set_current_el(0);
    STAGE74_ASSERT(!LinuxBootProtocolValidator::validate_cpu_handoff(cpu, entry, dtb, err), "EL0 handoff rejected");

    return true;
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 74: Linux ARM64 Boot Protocol Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*func)(), const char* name) {
        if (func()) {
            std::cout << "[PASS] " << name << "\n\n";
            ++passed;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            ++failed;
        }
    };

    run_test(test_valid_kernel_image_header_parsing, "Valid Kernel Image Header Parsing");
    run_test(test_invalid_magic_rejection, "Invalid Magic Rejection");
    run_test(test_truncated_header_rejection, "Truncated Header Rejection");
    run_test(test_big_endian_rejection, "Big-Endian Kernel Rejection");
    run_test(test_valid_kernel_placement, "Valid Kernel Placement");
    run_test(test_misaligned_ram_base_or_load_addr, "Misaligned RAM Base or Load Address Rejection");
    run_test(test_kernel_exceeding_ram_capacity, "Kernel Exceeding RAM Capacity");
    run_test(test_dtb_alignment_and_range_validation, "DTB Alignment and Range Validation");
    run_test(test_kernel_dtb_collision_detection, "Kernel-DTB Collision Detection");
    run_test(test_cpu_handoff_contract_verification, "CPU Handoff Contract Verification");

    std::cout << "Stage 74 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
