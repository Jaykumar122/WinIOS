#include <iostream>
#include <vector>
#include <cstdint>

#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_macho_header_validation() {
    std::cout << "[Test 1] Stage 211.9: Mach-O Header Parsing & Rejection Rules...\n";

    // Valid ARM64 Mach-O header
    std::uint8_t hdr[32]{0};
    const std::uint32_t magic = kMhMagic64;
    const std::int32_t cputype = kCpuTypeArm64;
    std::memcpy(hdr, &magic, 4);
    std::memcpy(hdr + 4, &cputype, 4);

    std::string err;
    TEST_CHECK(MachoUserLoader::validate_header(hdr, sizeof(hdr), &err), "Valid ARM64 Mach-O header accepted");

    // Too small (< 32 bytes)
    TEST_CHECK(!MachoUserLoader::validate_header(hdr, 16, &err), "Truncated header (< 32 bytes) rejected");

    // Invalid magic
    std::uint8_t bad_magic[32]{0};
    std::memcpy(bad_magic + 4, &cputype, 4);
    TEST_CHECK(!MachoUserLoader::validate_header(bad_magic, sizeof(bad_magic), &err), "Invalid magic rejected");

    // Unsupported cputype (e.g. x86_64 = 0x01000007)
    std::uint8_t x86_hdr[32]{0};
    const std::int32_t x86_cpu = 0x01000007;
    std::memcpy(x86_hdr, &magic, 4);
    std::memcpy(x86_hdr + 4, &x86_cpu, 4);
    TEST_CHECK(!MachoUserLoader::validate_header(x86_hdr, sizeof(x86_hdr), &err), "x86_64 cputype rejected");
}

void test_macho_loading_into_user_address_space() {
    std::cout << "[Test 2] Stage 211.9: Loading Genuine ARM64 Mach-O Executable Into UserAddressSpace...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "macho_load_test");

    const auto bin = MachoUserLoader::get_embedded_first_app();
    TEST_CHECK(!bin.empty(), "Embedded user_first_app.macho binary present");

    auto loaded = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(loaded.valid, "Mach-O successfully loaded: " + loaded.error_message);
    TEST_CHECK(loaded.text_vmaddr == 0x10000ULL, "__TEXT mapped at 0x10000");
    TEST_CHECK(loaded.entry_point >= 0x10000ULL, "Entry point is within valid virtual address space");
    TEST_CHECK(loaded.segments_loaded >= 1, "At least 1 segment loaded");

    // Verify code was populated in guest memory at entry point
    const std::uint64_t entry_pa = uas.translate(loaded.entry_point);
    TEST_CHECK(entry_pa != 0, "Entry point translates to physical page");

    const std::uint32_t insn0 = mem.read_u32(static_cast<std::size_t>(entry_pa));
    TEST_CHECK(insn0 == 0xD2800540u, "First instruction is mov x0, #42 (0xD2800540)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.9 Mach-O User Loader Tests ===\n";
    test_macho_header_validation();
    test_macho_loading_into_user_address_space();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.9: All Mach-O user loader tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.9: " << g_failures << " tests failed.\n";
        return 1;
    }
}
