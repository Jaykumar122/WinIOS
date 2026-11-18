#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "loader/guest_dynamic_loader.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
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

void test_dynamic_library_loading_and_exports() {
    std::cout << "[Test 1] Stage 219.20: Dylib Loading & Exported Symbol Discovery...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "dylib_uas");

    GuestDynamicLoader loader(uas, ppm, mem);

    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    TEST_ASSERT(!runtime_bin.empty());

    LoadedDylibInfo info{};
    std::string err;
    TEST_ASSERT(loader.load_library("libwinios_runtime.dylib", runtime_bin, &info, &err));
    TEST_ASSERT(info.base_va >= 0x20000000ULL);
    TEST_ASSERT(info.size > 0);
    TEST_ASSERT(loader.is_loaded("libwinios_runtime.dylib"));
    TEST_ASSERT(loader.loaded_count() == 1);

    // Verify key runtime exports
    uint64_t addr_write = loader.find_exported_symbol("_winios_write");
    uint64_t addr_exit = loader.find_exported_symbol("_winios_exit");
    uint64_t addr_getpid = loader.find_exported_symbol("_winios_getpid");
    uint64_t addr_yield = loader.find_exported_symbol("_winios_yield");

    TEST_ASSERT(addr_write >= info.base_va);
    TEST_ASSERT(addr_exit >= info.base_va);
    TEST_ASSERT(addr_getpid >= info.base_va);
    TEST_ASSERT(addr_yield >= info.base_va);
    TEST_ASSERT(addr_write != addr_exit);

    // Unknown symbol should return 0
    TEST_ASSERT(loader.find_exported_symbol("_nonexistent_symbol") == 0);

    std::cout << "  -> libwinios_runtime.dylib loaded and exports mapped successfully.\n";
}

void test_dynamic_symbol_binding_into_got() {
    std::cout << "[Test 2] Stage 219.20: Dynamic Symbol Binding into GOT Section...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "client_uas");

    GuestDynamicLoader loader(uas, ppm, mem);
    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    LoadedDylibInfo info{};
    TEST_ASSERT(loader.load_library("libwinios_runtime.dylib", runtime_bin, &info));

    // Map a client GOT data page at 0x10008000
    uint64_t got_pa = ppm.allocate_page();
    const uint64_t got_slot_va = 0x10008000ULL;
    TEST_ASSERT(uas.map_user_page(got_slot_va, got_pa, PagePermissions::UserReadWrite));

    // Initialize GOT slot with 0
    uint64_t zero_ptr = 0;
    TEST_ASSERT(uas.copyin(got_slot_va, &zero_ptr, sizeof(zero_ptr)));

    // Resolve symbol and patch GOT slot
    uint64_t resolved_va = loader.find_exported_symbol("_winios_write");
    TEST_ASSERT(resolved_va != 0);

    // Bind into GOT slot
    TEST_ASSERT(uas.copyin(got_slot_va, &resolved_va, sizeof(resolved_va)));

    // Verify GOT slot contains target function VA
    uint64_t bound_ptr = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(got_slot_va), &bound_ptr, sizeof(bound_ptr)));
    TEST_ASSERT(bound_ptr == resolved_va);

    std::cout << "  -> Dynamic GOT pointer binding verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.20: Bounded Dynamic Mach-O Loader Tests\n";
    std::cout << "=================================================================\n";

    test_dynamic_library_loading_and_exports();
    test_dynamic_symbol_binding_into_got();

    std::cout << "\n[PASS] All Stage 219.20 tests passed successfully!\n";
    return 0;
}
