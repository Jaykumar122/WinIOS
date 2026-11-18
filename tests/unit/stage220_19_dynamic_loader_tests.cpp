// ============================================================================
// Stage 220.19 Unit Tests: Dynamic Loader Correctness & Relocation Resolution
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <cstring>

#include "loader/guest_dynamic_loader.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

static void test_duplicate_load_and_idempotency() {
    std::cout << "[Test 1] Duplicate dylib loading idempotency and guard separation...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "dylib_uas");
    GuestDynamicLoader loader(uas, ppm, mem);

    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    TEST_ASSERT(!runtime_bin.empty());

    LoadedDylibInfo info1{};
    std::string err;
    TEST_ASSERT(loader.load_library("libwinios_runtime.dylib", runtime_bin, &info1, &err));
    TEST_ASSERT(loader.is_loaded("libwinios_runtime.dylib"));
    TEST_ASSERT(loader.loaded_count() == 1);
    const size_t pages_after_first = ppm.get_stats().allocated_pages;

    // Load same library again: must be idempotent, return true, zero new pages
    LoadedDylibInfo info2{};
    TEST_ASSERT(loader.load_library("libwinios_runtime.dylib", runtime_bin, &info2, &err));
    TEST_ASSERT(loader.loaded_count() == 1);
    TEST_ASSERT(info2.base_va == info1.base_va);
    TEST_ASSERT(info2.size == info1.size);
    TEST_ASSERT(ppm.get_stats().allocated_pages == pages_after_first);

    // Verify symbol lookup bounds
    uint64_t addr_write = loader.find_exported_symbol("_winios_write");
    TEST_ASSERT(addr_write >= info1.base_va);
    TEST_ASSERT(addr_write < info1.base_va + info1.size);

    std::cout << "  Passed duplicate load idempotency and lookup bound tests.\n";
}

static void test_wx_violation_rejection() {
    std::cout << "[Test 2] Defensive rejection of W^X violating dylibs...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "wx_uas");
    GuestDynamicLoader loader(uas, ppm, mem);

    // Take valid runtime binary and modify its segment initprot to PROT_WRITE | PROT_EXEC (0x6)
    auto malicious_bin = GuestDynamicLoader::get_embedded_runtime_library();
    TEST_ASSERT(malicious_bin.size() > 120);

    // Find first LC_SEGMENT_64
    uint32_t ncmds = 0;
    std::memcpy(&ncmds, malicious_bin.data() + 16, sizeof(ncmds));
    size_t off = 32;
    bool found_segment = false;
    for (uint32_t i = 0; i < ncmds; ++i) {
        if (off + 8 > malicious_bin.size()) break;
        uint32_t cmd = 0, cmdsize = 0;
        std::memcpy(&cmd, malicious_bin.data() + off, sizeof(cmd));
        std::memcpy(&cmdsize, malicious_bin.data() + off + 4, sizeof(cmdsize));
        if (cmd == 0x19 /* LC_SEGMENT_64 */ && cmdsize >= 72) {
            // initprot is at off + 60
            int32_t wx_prot = 0x6; // Write (2) | Exec (4)
            std::memcpy(malicious_bin.data() + off + 60, &wx_prot, sizeof(wx_prot));
            found_segment = true;
            break;
        }
        off += cmdsize;
    }
    TEST_ASSERT(found_segment);

    LoadedDylibInfo info{};
    std::string err;
    TEST_ASSERT(!loader.load_library("malicious.dylib", malicious_bin, &info, &err));
    TEST_ASSERT(err.find("W^X violation") != std::string::npos);
    TEST_ASSERT(!loader.is_loaded("malicious.dylib"));

    std::cout << "  Passed W^X rejection test.\n";
}

int main() {
    std::cout << "=== Stage 220.19 Dynamic Mach-O Loader Correctness Tests ===\n";
    test_duplicate_load_and_idempotency();
    test_wx_violation_rejection();
    std::cout << "All Stage 220.19 tests passed successfully.\n";
    return 0;
}
