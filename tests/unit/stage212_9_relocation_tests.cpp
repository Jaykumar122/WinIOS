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

// Helper to generate a dylib exporting a symbol
std::vector<std::uint8_t> make_exporting_dylib(const std::string& sym_name, std::uint64_t sym_offset) {
    std::vector<std::uint8_t> bin(0x200, 0);

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

    // LC_SEGMENT_64 __TEXT
    const std::uint32_t cmd_seg = 0x19u;
    const std::uint32_t cmdsize_seg = 72u;
    char segname[16] = "__TEXT";
    const std::uint64_t vmaddr = 0x0ULL;
    const std::uint64_t vmsize = 0x4000ULL;
    const std::uint64_t fileoff = 0x0ULL;
    const std::uint64_t filesize = 0x200ULL;
    const std::int32_t maxprot = 5;
    const std::int32_t initprot = 5;

    std::memcpy(bin.data() + 32 + 0, &cmd_seg, 4);
    std::memcpy(bin.data() + 32 + 4, &cmdsize_seg, 4);
    std::memcpy(bin.data() + 32 + 8, segname, 16);
    std::memcpy(bin.data() + 32 + 24, &vmaddr, 8);
    std::memcpy(bin.data() + 32 + 32, &vmsize, 8);
    std::memcpy(bin.data() + 32 + 40, &fileoff, 8);
    std::memcpy(bin.data() + 32 + 48, &filesize, 8);
    std::memcpy(bin.data() + 32 + 56, &maxprot, 4);
    std::memcpy(bin.data() + 32 + 60, &initprot, 4);

    // LC_SYMTAB
    const std::uint32_t cmd_sym = 0x02u;
    const std::uint32_t cmdsize_sym = 24u;
    const std::uint32_t symoff = 0x100u;
    const std::uint32_t nsyms = 1u;
    const std::uint32_t stroff = 0x120u;
    const std::uint32_t strsize = 0x40u;

    std::memcpy(bin.data() + 104 + 0, &cmd_sym, 4);
    std::memcpy(bin.data() + 104 + 4, &cmdsize_sym, 4);
    std::memcpy(bin.data() + 104 + 8, &symoff, 4);
    std::memcpy(bin.data() + 104 + 12, &nsyms, 4);
    std::memcpy(bin.data() + 104 + 16, &stroff, 4);
    std::memcpy(bin.data() + 104 + 20, &strsize, 4);

    // nlist_64
    const std::uint32_t strx = 1u;
    const std::uint8_t type = 0x0Fu; // N_SECT | N_EXT
    const std::uint8_t sect = 1u;
    const std::uint16_t desc = 0u;
    const std::uint64_t val = sym_offset;

    std::memcpy(bin.data() + symoff + 0, &strx, 4);
    std::memcpy(bin.data() + symoff + 4, &type, 1);
    std::memcpy(bin.data() + symoff + 5, &sect, 1);
    std::memcpy(bin.data() + symoff + 6, &desc, 2);
    std::memcpy(bin.data() + symoff + 8, &val, 8);

    bin[stroff] = '\0';
    std::memcpy(bin.data() + stroff + 1, sym_name.c_str(), sym_name.size() + 1);

    return bin;
}

// Helper to generate a client executable binary importing a symbol via GOT pointer
std::vector<std::uint8_t> make_importing_client(
    const std::string& imported_sym_name,
    std::uint64_t got_va,
    bool is_weak) {
    std::vector<std::uint8_t> bin(0x400, 0);

    const std::uint32_t magic = 0xFEEDFACFu;
    const std::int32_t cputype = 0x0100000Cu;
    const std::uint32_t filetype = 2u; // MH_EXECUTE
    const std::uint32_t ncmds = 3u;
    // LC_SEGMENT_64 (72 + 80 = 152 bytes) + LC_SYMTAB (24 bytes) + LC_DYSYMTAB (80 bytes) = 256 bytes
    const std::uint32_t sizeofcmds = 152u + 24u + 80u;

    std::memcpy(bin.data() + 0, &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 12, &filetype, 4);
    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);

    // 1. LC_SEGMENT_64 with 1 section (__got)
    const std::uint32_t cmd_seg = 0x19u;
    const std::uint32_t cmdsize_seg = 152u;
    char segname[16] = "__DATA";
    const std::uint64_t vmaddr = got_va;
    const std::uint64_t vmsize = 0x4000ULL;
    const std::uint64_t fileoff = 0x0ULL;
    const std::uint64_t filesize = 0x200ULL;
    const std::int32_t maxprot = 3;
    const std::int32_t initprot = 3; // RW
    const std::uint32_t nsects = 1u;

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
    std::memcpy(bin.data() + off + 64, &nsects, 4);

    // Section 1: __got
    std::size_t sect_off = off + 72;
    char sectname[16] = "__got";
    const std::uint64_t s_addr = got_va;
    const std::uint64_t s_size = 8ULL; // 1 pointer (8 bytes)
    const std::uint32_t s_flags = 0x6u; // S_NON_LAZY_SYMBOL_POINTERS
    const std::uint32_t s_reserved1 = 0u; // indirect symbol table index 0

    std::memcpy(bin.data() + sect_off + 0, sectname, 16);
    std::memcpy(bin.data() + sect_off + 16, segname, 16);
    std::memcpy(bin.data() + sect_off + 32, &s_addr, 8);
    std::memcpy(bin.data() + sect_off + 40, &s_size, 8);
    std::memcpy(bin.data() + sect_off + 56, &s_flags, 4);
    std::memcpy(bin.data() + sect_off + 68, &s_reserved1, 4);

    // 2. LC_SYMTAB (offset 184, size 24)
    off = 184;
    const std::uint32_t cmd_sym = 0x02u;
    const std::uint32_t cmdsize_sym = 24u;
    const std::uint32_t symoff = 0x200u;
    const std::uint32_t nsyms = 1u;
    const std::uint32_t stroff = 0x240u;
    const std::uint32_t strsize = 0x40u;

    std::memcpy(bin.data() + off + 0, &cmd_sym, 4);
    std::memcpy(bin.data() + off + 4, &cmdsize_sym, 4);
    std::memcpy(bin.data() + off + 8, &symoff, 4);
    std::memcpy(bin.data() + off + 12, &nsyms, 4);
    std::memcpy(bin.data() + off + 16, &stroff, 4);
    std::memcpy(bin.data() + off + 20, &strsize, 4);

    // 3. LC_DYSYMTAB (offset 208, size 80)
    off = 208;
    const std::uint32_t cmd_dysym = 0x0Bu;
    const std::uint32_t cmdsize_dysym = 80u;
    const std::uint32_t indirect_off = 0x220u;
    const std::uint32_t nindirect = 1u;

    std::memcpy(bin.data() + off + 0, &cmd_dysym, 4);
    std::memcpy(bin.data() + off + 4, &cmdsize_dysym, 4);
    std::memcpy(bin.data() + off + 64, &indirect_off, 4);
    std::memcpy(bin.data() + off + 68, &nindirect, 4);

    // nlist_64 at 0x200 (imported undefined symbol)
    const std::uint32_t strx = 1u;
    const std::uint8_t type = 0x01u; // N_UNDF | N_EXT
    const std::uint8_t sect = 0u;
    const std::uint16_t desc = is_weak ? 0x0080u /* N_WEAK_REF */ : 0x0000u;
    const std::uint64_t val = 0ULL;

    std::memcpy(bin.data() + symoff + 0, &strx, 4);
    std::memcpy(bin.data() + symoff + 4, &type, 1);
    std::memcpy(bin.data() + symoff + 5, &sect, 1);
    std::memcpy(bin.data() + symoff + 6, &desc, 2);
    std::memcpy(bin.data() + symoff + 8, &val, 8);

    // Indirect table at 0x220: maps index 0 to symbol index 0
    const std::uint32_t sym_index = 0u;
    std::memcpy(bin.data() + indirect_off, &sym_index, 4);

    // String table at 0x240
    bin[stroff] = '\0';
    std::memcpy(bin.data() + stroff + 1, imported_sym_name.c_str(), imported_sym_name.size() + 1);

    return bin;
}

void test_symbol_binding() {
    std::cout << "[Test 1] Dynamic Symbol Resolution & GOT Pointer Binding...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "reloc_uas");

    // Map a data page for client's GOT at 0x40000
    const std::uint64_t got_pa = ppm.allocate_page();
    const std::uint64_t got_va = 0x40000ULL;
    uas.map_user_page(got_va, got_pa, PagePermissions::UserReadWrite);

    // Initialize pointer to zero
    std::uint64_t zero_ptr = 0;
    uas.copyin(got_va, &zero_ptr, sizeof(zero_ptr));

    GuestDynamicLoader loader(uas, ppm, mem);

    // 1. Load dylib exporting _winios_print at offset 0x300
    auto dylib = make_exporting_dylib("_winios_print", 0x300);
    LoadedDylibInfo dylib_info{};
    std::string err;
    TEST_CHECK(loader.load_library("libruntime.dylib", dylib, &dylib_info, &err), "Loaded exporting dylib");

    const std::uint64_t expected_target_va = dylib_info.base_va + 0x300;
    TEST_CHECK(loader.find_exported_symbol("_winios_print") == expected_target_va, "Symbol address found");

    // 2. Bind symbols in client image
    auto client = make_importing_client("_winios_print", got_va, false);
    TEST_CHECK(loader.resolve_and_bind_symbols(client, 0, &err), "Resolved and bound symbols");

    // 3. Inspect pointer in guest memory
    std::uint64_t bound_ptr = 0;
    TEST_CHECK(uas.copyout(reinterpret_cast<void*>(got_va), &bound_ptr, sizeof(bound_ptr)), "Read bound pointer");
    TEST_CHECK(bound_ptr == expected_target_va, "GOT pointer bound to exact target function VA in dylib");
}

void test_undefined_symbol_rejection() {
    std::cout << "[Test 2] Strong Undefined Symbol Rejection...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "undef_uas");
    const std::uint64_t got_pa = ppm.allocate_page();
    uas.map_user_page(0x40000ULL, got_pa, PagePermissions::UserReadWrite);

    GuestDynamicLoader loader(uas, ppm, mem);

    auto client = make_importing_client("_missing_symbol", 0x40000ULL, false);
    std::string err;
    TEST_CHECK(!loader.resolve_and_bind_symbols(client, 0, &err), "Undefined symbol rejected");
    TEST_CHECK(err.find("_missing_symbol") != std::string::npos, "Error identifies undefined symbol");
}

void test_weak_symbol_binding() {
    std::cout << "[Test 3] Weak Undefined Symbol Bound to NULL...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "weak_uas");
    const std::uint64_t got_pa = ppm.allocate_page();
    uas.map_user_page(0x40000ULL, got_pa, PagePermissions::UserReadWrite);

    // Pre-populate with non-zero to verify it is cleared to NULL
    std::uint64_t initial_val = 0xDEADBEEFULL;
    uas.copyin(0x40000ULL, &initial_val, sizeof(initial_val));

    GuestDynamicLoader loader(uas, ppm, mem);

    auto client = make_importing_client("_optional_symbol", 0x40000ULL, true);
    std::string err;
    TEST_CHECK(loader.resolve_and_bind_symbols(client, 0, &err), "Weak undefined symbol succeeds");

    std::uint64_t bound_ptr = 0xDEADBEEFULL;
    uas.copyout(reinterpret_cast<void*>(0x40000ULL), &bound_ptr, sizeof(bound_ptr));
    TEST_CHECK(bound_ptr == 0ULL, "Weak undefined symbol bound to NULL (0)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.9 ARM64 Relocation & Symbol Resolution Tests ===\n";
    test_symbol_binding();
    test_undefined_symbol_rejection();
    test_weak_symbol_binding();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.9: All relocation and symbol resolution tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.9: " << g_failures << " tests failed.\n";
        return 1;
    }
}
