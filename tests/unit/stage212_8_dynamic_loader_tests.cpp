#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/guest_dynamic_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

// Helper to generate a minimal valid ARM64 Mach-O dylib with exported symbols
std::vector<std::uint8_t> make_test_dylib(
    const std::string& exported_func_name,
    std::uint64_t func_offset,
    bool violate_wx = false) {
    std::vector<std::uint8_t> bin(0x200, 0);

    // 1. mach_header_64 (32 bytes)
    const std::uint32_t magic = 0xFEEDFACFu;
    const std::int32_t cputype = 0x0100000Cu;
    const std::uint32_t filetype = 6u; // MH_DYLIB
    const std::uint32_t ncmds = 2u;
    const std::uint32_t sizeofcmds = 72u + 24u;

    std::memcpy(bin.data() + 0, &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 12, &filetype, 4);
    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);

    // 2. LC_SEGMENT_64 for __TEXT (offset 32, size 72)
    const std::uint32_t cmd_seg = 0x19u;
    const std::uint32_t cmdsize_seg = 72u;
    char segname[16] = "__TEXT";
    const std::uint64_t vmaddr = 0x0ULL;
    const std::uint64_t vmsize = 0x4000ULL; // 16 KB
    const std::uint64_t fileoff = 0x0ULL;
    const std::uint64_t filesize = 0x200ULL;
    const std::int32_t maxprot = violate_wx ? 7 : 5; // 7 = RWX
    const std::int32_t initprot = violate_wx ? 7 : 5; // 5 = R-X, 7 = RWX

    std::size_t off = 32;
    std::memcpy(bin.data() + off + 0, &cmd_seg, 4);
    std::memcpy(bin.data() + off + 4, &cmdsize_seg, 4);
    std::memcpy(bin.data() + off + 8, segname, 16);
    std::memcpy(bin.data() + off + 24, &vmaddr, 8);
    std::memcpy(bin.data() + off + 32, &vmsize, 8);
    std::memcpy(bin.data() + off + 40, &fileoff, 8);
    std::memcpy(bin.data() + off + 48, &filesize, 8);
    std::memcpy(bin.data() + off + 56, &maxprot, 4);
    std::memcpy(bin.data() + off + 60, &initprot, 4);

    // 3. LC_SYMTAB (offset 104, size 24)
    off = 104;
    const std::uint32_t cmd_sym = 0x02u;
    const std::uint32_t cmdsize_sym = 24u;
    const std::uint32_t symoff = 0x100u;
    const std::uint32_t nsyms = 1u;
    const std::uint32_t stroff = 0x120u;
    const std::uint32_t strsize = 0x40u;

    std::memcpy(bin.data() + off + 0, &cmd_sym, 4);
    std::memcpy(bin.data() + off + 4, &cmdsize_sym, 4);
    std::memcpy(bin.data() + off + 8, &symoff, 4);
    std::memcpy(bin.data() + off + 12, &nsyms, 4);
    std::memcpy(bin.data() + off + 16, &stroff, 4);
    std::memcpy(bin.data() + off + 20, &strsize, 4);

    // 4. nlist_64 entry at 0x100
    const std::uint32_t strx = 1u;
    const std::uint8_t type = 0x0Fu; // N_SECT | N_EXT
    const std::uint8_t sect = 1u;
    const std::uint16_t desc = 0u;
    const std::uint64_t val = func_offset;

    std::memcpy(bin.data() + symoff + 0, &strx, 4);
    std::memcpy(bin.data() + symoff + 4, &type, 1);
    std::memcpy(bin.data() + symoff + 5, &sect, 1);
    std::memcpy(bin.data() + symoff + 6, &desc, 2);
    std::memcpy(bin.data() + symoff + 8, &val, 8);

    // 5. String table at 0x120
    bin[stroff] = '\0';
    std::memcpy(bin.data() + stroff + 1, exported_func_name.c_str(), exported_func_name.size() + 1);

    return bin;
}

void test_dynamic_library_loading() {
    std::cout << "[Test 1] Dynamic Library Loading & Non-Overlapping Mapping...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "dyn_uas");

    GuestDynamicLoader loader(uas, ppm, mem);

    auto dylib_a = make_test_dylib("_service_init", 0x100);
    auto dylib_b = make_test_dylib("_service_dispatch", 0x200);

    LoadedDylibInfo info_a{};
    LoadedDylibInfo info_b{};
    std::string err;

    TEST_CHECK(loader.load_library("libA.dylib", dylib_a, &info_a, &err), "Loaded libA.dylib");
    TEST_CHECK(loader.load_library("libB.dylib", dylib_b, &info_b, &err), "Loaded libB.dylib");

    TEST_CHECK(info_a.base_va != 0, "libA mapped at non-zero base VA");
    TEST_CHECK(info_b.base_va != 0, "libB mapped at non-zero base VA");
    TEST_CHECK(info_a.base_va != info_b.base_va, "libA and libB have non-overlapping base VAs");

    // Symbol resolution
    const std::uint64_t sym_init_va = loader.find_exported_symbol("_service_init");
    const std::uint64_t sym_disp_va = loader.find_exported_symbol("_service_dispatch");

    TEST_CHECK(sym_init_va == info_a.base_va + 0x100, "Symbol _service_init resolved to libA + 0x100");
    TEST_CHECK(sym_disp_va == info_b.base_va + 0x200, "Symbol _service_dispatch resolved to libB + 0x200");
}

void test_wx_enforcement() {
    std::cout << "[Test 2] Strict W^X Protection Enforcement...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "wx_uas");

    GuestDynamicLoader loader(uas, ppm, mem);

    // Create dylib with simultaneous Write + Execute permissions
    auto bad_dylib = make_test_dylib("_exploit", 0x100, true);

    std::string err;
    TEST_CHECK(!loader.load_library("libBad.dylib", bad_dylib, nullptr, &err), "RWX segment rejected");
    TEST_CHECK(err.find("W^X") != std::string::npos, "Error indicates W^X violation");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.8 Guest Dynamic Library Loader Tests ===\n";
    test_dynamic_library_loading();
    test_wx_enforcement();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.8: All dynamic library loader tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.8: " << g_failures << " tests failed.\n";
        return 1;
    }
}
