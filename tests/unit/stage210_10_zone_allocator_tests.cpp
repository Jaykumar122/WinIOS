#include <iostream>
#include <vector>
#include <cstdint>

#include "memory/xnu_zone_allocator.h"

using namespace winios;
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

void test_zone_allocator_operations() {
    std::cout << "[Test 1] Stage 210.10: XnuZoneAllocator Creation, Allocation, Free & Exhaustion...\n";

    XnuZoneAllocator za(0x200000ULL, 0x200000ULL); // 2 MiB zone pool
    std::string err;

    // 1. Create standard Darwin kernel zones
    TEST_CHECK(za.create_zone("vm_page", 32, 128, &err), "Create vm_page zone");
    TEST_CHECK(za.create_zone("task", 128, 64, &err), "Create task zone");
    TEST_CHECK(za.create_zone("thread", 512, 64, &err), "Create thread zone");
    TEST_CHECK(za.create_zone("ipc_port", 64, 128, &err), "Create ipc_port zone");
    TEST_CHECK(za.zone_count() == 4, "Zone count is 4");

    // 2. Allocate elements from thread zone
    auto t1 = za.zalloc("thread");
    TEST_CHECK(t1 != 0, "zalloc('thread') 1 succeeded");
    TEST_CHECK((t1 % 16) == 0, "t1 is 16-byte aligned");

    auto t2 = za.zalloc("thread");
    TEST_CHECK(t2 != 0, "zalloc('thread') 2 succeeded");
    TEST_CHECK(t2 != t1, "t2 is distinct from t1");

    ZoneStats stats{};
    TEST_CHECK(za.get_zone_stats("thread", stats), "Query thread zone stats");
    TEST_CHECK(stats.count == 2, "Thread zone active count is 2");
    TEST_CHECK(stats.free_count == 62, "Thread zone free count is 62");

    // 3. Free element
    TEST_CHECK(za.zfree("thread", t1, &err), "zfree('thread', t1) succeeded");
    TEST_CHECK(za.get_zone_stats("thread", stats), "Query stats after free");
    TEST_CHECK(stats.count == 1, "Active count is 1");
    TEST_CHECK(stats.free_count == 63, "Free count restored to 63");

    // 4. Double free rejection
    TEST_CHECK(!za.zfree("thread", t1, &err), "Double free of t1 properly rejected");

    // 5. Invalid pointer rejection
    TEST_CHECK(!za.zfree("thread", 0x12345ULL, &err), "Out-of-bounds pointer rejected");
    TEST_CHECK(!za.zfree("thread", t2 + 3, &err), "Misaligned pointer rejected");

    // 6. Zone exhaustion
    std::vector<std::uint64_t> page_ptrs;
    for (std::size_t i = 0; i < 128; ++i) {
        auto p = za.zalloc("vm_page");
        TEST_CHECK(p != 0, "vm_page allocation " + std::to_string(i));
        page_ptrs.push_back(p);
    }
    // Next allocation must fail due to exhaustion
    auto p_overflow = za.zalloc("vm_page");
    TEST_CHECK(p_overflow == 0, "Allocation on exhausted zone returns 0");

    // Free one element and allocate again
    TEST_CHECK(za.zfree("vm_page", page_ptrs.back(), &err), "Free one vm_page");
    page_ptrs.pop_back();

    auto p_realloc = za.zalloc("vm_page");
    TEST_CHECK(p_realloc != 0, "Allocation succeeds after freeing an element");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.10 Zone Allocator Tests ===\n";
    test_zone_allocator_operations();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.10: All zone allocator tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.10: " << g_failures << " tests failed.\n";
        return 1;
    }
}
