#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "loader/linux_image_loader.h"
#include "memory/memory.h"

#define STAGE79_ASSERT(cond, msg) \
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

std::vector<std::uint8_t> create_synthetic_kernel_image(
    std::size_t file_size = 0x10000, // 64 KB
    std::uint64_t text_offset = 0x80000,
    std::uint64_t image_size = 0x200000) {

    std::vector<std::uint8_t> buf(file_size, 0);

    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(buf.data());
    hdr->code0 = 0x1400000Au; // B +0x28
    hdr->code1 = 0xD503201Fu; // NOP
    hdr->text_offset = text_offset;
    hdr->image_size = image_size;
    hdr->flags = 0x2u;        // 4KB granule, LE
    hdr->res2 = 0;
    hdr->res3 = 0;
    hdr->res4 = 0;
    hdr->magic = kLinuxArm64ImageMagic; // 0x644D5241
    hdr->res5 = 0;

    // Fill payload after header with pattern
    for (std::size_t i = sizeof(LinuxArm64ImageHeader); i < file_size; ++i) {
        buf[i] = static_cast<std::uint8_t>(i & 0xFFu);
    }

    return buf;
}

// -----------------------------------------------------------------------------
// Suite 1: Valid Buffer Loading and Entry Point
// -----------------------------------------------------------------------------
bool test_valid_buffer_loading_and_entry_point() {
    std::cout << "[1] test_valid_buffer_loading_and_entry_point...\n";

    Memory mem(0x08000000ULL); // 128 MB RAM
    auto kernel_buf = create_synthetic_kernel_image(0x10000, 0x80000, 0x10000);

    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(res.is_success(), "Loading must succeed");
    STAGE79_ASSERT(res.load_address == 0x40080000ULL, "Load address match");
    STAGE79_ASSERT(res.entry_point == 0x40080000ULL, "Entry point match");
    STAGE79_ASSERT(res.file_bytes_loaded == 0x10000, "File bytes loaded match");

    // Verify first instruction in memory is B opcode at offset 0x80000
    std::uint32_t first_instr = mem.read_u32(0x80000);
    STAGE79_ASSERT(first_instr == 0x1400000Au, "First instruction in RAM mismatch");

    std::cout << "[PASS] Valid Buffer Loading and Entry Point\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Early BSS Zero-Filling
// -----------------------------------------------------------------------------
bool test_early_bss_zero_filling() {
    std::cout << "[2] test_early_bss_zero_filling...\n";

    Memory mem(0x08000000ULL);
    // Pre-poison RAM with 0xAA around 0x80000..0x100000
    for (std::uint64_t addr = 0x80000ULL; addr < 0x100000ULL; addr += 4) {
        mem.write_u32(addr, 0xAAAAAAAAu);
    }

    // Kernel file is 64KB (0x10000), but image_size is 128KB (0x20000)
    auto kernel_buf = create_synthetic_kernel_image(0x10000, 0x80000, 0x20000);

    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(res.is_success(), "Loading must succeed");
    STAGE79_ASSERT(res.effective_size == 0x20000, "Effective size 128KB");

    // Check payload bytes in RAM
    std::uint8_t payload_byte = mem.read_u8(0x80000ULL + 0x40);
    STAGE79_ASSERT(payload_byte == kernel_buf[0x40], "Payload byte must match");

    // Check BSS bytes (from 0x80000 + 0x10000 up to + 0x20000): must be 0x00
    for (std::uint64_t addr = 0x90000ULL; addr < 0xA0000ULL; addr += 1024) {
        STAGE79_ASSERT(mem.read_u8(addr) == 0, "BSS region must be zero-filled");
    }

    std::cout << "[PASS] Early BSS Zero-Filling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: SHA-256 Cryptographic Verification
// -----------------------------------------------------------------------------
bool test_sha256_cryptographic_verification() {
    std::cout << "[3] test_sha256_cryptographic_verification...\n";

    // Known NIST vector: "abc"
    const char* test_input = "abc";
    std::string hash = LinuxImageLoader::compute_sha256(test_input, 3);
    const std::string expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    STAGE79_ASSERT(hash == expected, "NIST test vector 'abc' SHA-256 mismatch");

    Memory mem(0x08000000ULL);
    auto kernel_buf = create_synthetic_kernel_image(0x1000, 0x80000, 0x1000);
    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(res.is_success(), "Load success");
    STAGE79_ASSERT(res.sha256_hex.size() == 64, "SHA-256 string must be 64 hex chars");

    std::cout << "[PASS] SHA-256 Cryptographic Verification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Load from File System
// -----------------------------------------------------------------------------
bool test_load_from_file() {
    std::cout << "[4] test_load_from_file...\n";

    const std::string test_file = "stage79_test_kernel_Image";
    auto kernel_buf = create_synthetic_kernel_image(0x2000, 0x80000, 0x2000);

    {
        std::ofstream out(test_file, std::ios::binary);
        STAGE79_ASSERT(out.is_open(), "Cannot create test kernel file");
        out.write(reinterpret_cast<const char*>(kernel_buf.data()), kernel_buf.size());
    }

    Memory mem(0x08000000ULL);
    auto res = LinuxImageLoader::load_from_file(test_file, mem, 0x40000000ULL, 0x08000000ULL);
    STAGE79_ASSERT(res.is_success(), "File load must succeed");
    STAGE79_ASSERT(res.file_bytes_loaded == 0x2000, "File size match");
    STAGE79_ASSERT(mem.read_u32(0x80000) == 0x1400000Au, "First instruction in RAM");

    std::cout << "[PASS] Load from File System\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: File Not Found Handling
// -----------------------------------------------------------------------------
bool test_file_not_found_handling() {
    std::cout << "[5] test_file_not_found_handling...\n";

    Memory mem(0x08000000ULL);
    auto res = LinuxImageLoader::load_from_file(
        "non_existent_kernel_Image_xyz123", mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(!res.is_success(), "Must fail on non-existent file");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::FileNotFound, "Status must be FileNotFound");
    STAGE79_ASSERT(!res.error_message.empty(), "Error message must be set");

    std::cout << "[PASS] File Not Found Handling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Truncated Kernel File Rejection
// -----------------------------------------------------------------------------
bool test_truncated_kernel_file_rejection() {
    std::cout << "[6] test_truncated_kernel_file_rejection...\n";

    std::vector<std::uint8_t> tiny(32, 0);
    Memory mem(0x08000000ULL);

    auto res = LinuxImageLoader::load_from_buffer(
        tiny.data(), tiny.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(!res.is_success(), "Truncated buffer must fail");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::TruncatedHeader, "Status TruncatedHeader");

    std::cout << "[PASS] Truncated Kernel File Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Invalid Magic Signature Rejection
// -----------------------------------------------------------------------------
bool test_invalid_magic_signature_rejection() {
    std::cout << "[7] test_invalid_magic_signature_rejection...\n";

    auto kernel_buf = create_synthetic_kernel_image();
    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(kernel_buf.data());
    hdr->magic = 0xDEADBEEFu; // invalid

    Memory mem(0x08000000ULL);
    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(!res.is_success(), "Invalid magic must fail");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::InvalidMagic, "Status InvalidMagic");

    std::cout << "[PASS] Invalid Magic Signature Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Big-Endian Kernel Rejection
// -----------------------------------------------------------------------------
bool test_big_endian_kernel_rejection() {
    std::cout << "[8] test_big_endian_kernel_rejection...\n";

    auto kernel_buf = create_synthetic_kernel_image();
    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(kernel_buf.data());
    hdr->flags |= 0x1u; // bit 0 = 1 (big-endian)

    Memory mem(0x08000000ULL);
    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x08000000ULL);

    STAGE79_ASSERT(!res.is_success(), "Big-endian kernel must fail");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::BigEndianUnsupported, "Status BigEndianUnsupported");

    std::cout << "[PASS] Big-Endian Kernel Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Kernel Exceeding RAM Bounds Rejection
// -----------------------------------------------------------------------------
bool test_kernel_exceeding_ram_bounds_rejection() {
    std::cout << "[9] test_kernel_exceeding_ram_bounds_rejection...\n";

    // Kernel image_size = 128 MB in 64 MB RAM
    auto kernel_buf = create_synthetic_kernel_image(0x1000, 0x80000, 0x08000000ULL);

    Memory mem(0x04000000ULL);
    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40000000ULL, 0x04000000ULL); // 64 MB RAM

    STAGE79_ASSERT(!res.is_success(), "Oversized kernel must fail");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::RamCapacityExceeded, "Status RamCapacityExceeded");

    std::cout << "[PASS] Kernel Exceeding RAM Bounds Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Misaligned RAM Base Rejection
// -----------------------------------------------------------------------------
bool test_misaligned_ram_base_rejection() {
    std::cout << "[10] test_misaligned_ram_base_rejection...\n";

    auto kernel_buf = create_synthetic_kernel_image();
    Memory mem(0x08000000ULL);

    // RAM base 0x40001000 is not 2MB aligned
    auto res = LinuxImageLoader::load_from_buffer(
        kernel_buf.data(), kernel_buf.size(), mem, 0x40001000ULL, 0x08000000ULL);

    STAGE79_ASSERT(!res.is_success(), "Misaligned RAM base must fail");
    STAGE79_ASSERT(res.status == LinuxImageLoadStatus::MisalignedRamBase, "Status MisalignedRamBase");

    std::cout << "[PASS] Misaligned RAM Base Rejection\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 79: Linux ARM64 Image Loader Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_valid_buffer_loading_and_entry_point, "test_valid_buffer_loading_and_entry_point");
    run_test(test_early_bss_zero_filling, "test_early_bss_zero_filling");
    run_test(test_sha256_cryptographic_verification, "test_sha256_cryptographic_verification");
    run_test(test_load_from_file, "test_load_from_file");
    run_test(test_file_not_found_handling, "test_file_not_found_handling");
    run_test(test_truncated_kernel_file_rejection, "test_truncated_kernel_file_rejection");
    run_test(test_invalid_magic_signature_rejection, "test_invalid_magic_signature_rejection");
    run_test(test_big_endian_kernel_rejection, "test_big_endian_kernel_rejection");
    run_test(test_kernel_exceeding_ram_bounds_rejection, "test_kernel_exceeding_ram_bounds_rejection");
    run_test(test_misaligned_ram_base_rejection, "test_misaligned_ram_base_rejection");

    std::cout << "\nStage 79 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
