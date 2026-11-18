#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/bsd_process.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

static void test_address_space_identity_and_ownership() {
    std::cout << "[Test 1] Address space identity, ASID & process ownership...\n";
    Memory mem(0x20000000ULL); // 512 MiB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL); // DRAM 16 MiB - 256 MiB
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_a = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a_uas");
    uas_a.set_asid(101);
    uas_a.set_owner_pid(10);

    TEST_ASSERT(uas_a.asid() == 101);
    TEST_ASSERT(uas_a.owner_pid() == 10);
    TEST_ASSERT(uas_a.is_valid_for_process(10));
    TEST_ASSERT(!uas_a.is_valid_for_process(20));
    TEST_ASSERT((uas_a.root_table_pa_with_asid() >> 48u) == 101);
    TEST_ASSERT((uas_a.root_table_pa_with_asid() & 0x0000FFFFFFFFFFFFULL) == root_a);
    TEST_ASSERT(uas_a.owns_physical_page(root_a));
}

static void test_dual_process_address_space_isolation() {
    std::cout << "[Test 2] Dual process address space isolation at identical virtual addresses...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_a = ppm.allocate_page();
    const uint64_t root_b = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a_uas");
    UserAddressSpace uas_b(mem, ppm, root_b, "proc_b_uas");
    uas_a.set_asid(1);
    uas_a.set_owner_pid(100);
    uas_b.set_asid(2);
    uas_b.set_owner_pid(200);

    const uint64_t shared_va = 0x100010000ULL;
    const uint64_t pa_a = ppm.allocate_page();
    const uint64_t pa_b = ppm.allocate_page();
    TEST_ASSERT(pa_a != pa_b);

    TEST_ASSERT(uas_a.map_user_page(shared_va, pa_a, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas_b.map_user_page(shared_va, pa_b, PagePermissions::UserReadWrite));

    TEST_ASSERT(uas_a.owns_physical_page(pa_a));
    TEST_ASSERT(!uas_a.owns_physical_page(pa_b));
    TEST_ASSERT(uas_b.owns_physical_page(pa_b));
    TEST_ASSERT(!uas_b.owns_physical_page(pa_a));

    const char msg_a[] = "PROCESS_A_CONFIDENTIAL_PAYLOAD";
    const char msg_b[] = "PROCESS_B_ISOLATED_SECRET_DATA";

    TEST_ASSERT(uas_a.copyin(shared_va, msg_a, sizeof(msg_a)));
    TEST_ASSERT(uas_b.copyin(shared_va, msg_b, sizeof(msg_b)));

    char read_buf_a[64]{0};
    char read_buf_b[64]{0};
    TEST_ASSERT(uas_a.copyout(reinterpret_cast<const void*>(shared_va), read_buf_a, sizeof(msg_a)));
    TEST_ASSERT(uas_b.copyout(reinterpret_cast<const void*>(shared_va), read_buf_b, sizeof(msg_b)));

    TEST_ASSERT(std::strcmp(read_buf_a, msg_a) == 0);
    TEST_ASSERT(std::strcmp(read_buf_b, msg_b) == 0);
    TEST_ASSERT(std::strcmp(read_buf_a, read_buf_b) != 0);
}

static void test_mapping_accounting_and_lifecycle() {
    std::cout << "[Test 3] Mapping accounting & unmap lifecycle...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root = ppm.allocate_page();
    {
        UserAddressSpace uas(mem, ppm, root, "test_uas");
        TEST_ASSERT(uas.mapped_page_count() == 0);
        TEST_ASSERT(uas.unmapped_page_count() == 0);

        const uint64_t va1 = 0x100004000ULL;
        const uint64_t pa1 = ppm.allocate_page();
        TEST_ASSERT(uas.map_user_page(va1, pa1, PagePermissions::UserReadWrite));
        TEST_ASSERT(uas.mapped_page_count() == 1);

        const uint64_t va2 = 0x100008000ULL;
        const uint64_t pa2 = ppm.allocate_page();
        TEST_ASSERT(uas.map_user_page(va2, pa2, PagePermissions::UserReadWrite));
        TEST_ASSERT(uas.mapped_page_count() == 2);

        TEST_ASSERT(uas.unmap_user_page(va1));
        TEST_ASSERT(uas.mapped_page_count() == 1);
        TEST_ASSERT(uas.unmapped_page_count() == 1);
        TEST_ASSERT(!uas.owns_physical_page(pa1));
        TEST_ASSERT(uas.owns_physical_page(pa2));
    }
}

static void test_partially_constructed_address_space() {
    std::cout << "[Test 4] Partially constructed address space safety...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    {
        // Address space initialized with root_pa = 0 (allocation failed)
        UserAddressSpace uas(mem, ppm, 0, "null_uas");
        TEST_ASSERT(!uas.map_user_page(0x100004000ULL, 0x2000000ULL, PagePermissions::UserReadWrite));
        TEST_ASSERT(uas.translate(0x100004000ULL) == 0);
        TEST_ASSERT(!uas.unmap_user_page(0x100004000ULL));
    } // Must cleanly destruct without crash or double-free
}

int main() {
    std::cout << "=== WinIOS Stage 220.3 Address Space Ownership Tests ===\n";
    test_address_space_identity_and_ownership();
    test_dual_process_address_space_isolation();
    test_mapping_accounting_and_lifecycle();
    test_partially_constructed_address_space();
    std::cout << "[PASS] All Stage 220.3 Address Space Ownership tests passed.\n";
    return 0;
}
