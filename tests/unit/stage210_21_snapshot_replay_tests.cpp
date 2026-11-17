#include <iostream>
#include <vector>
#include <cstdint>

#include "kernel/task.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "memory/xnu_zone_allocator.h"

using namespace winios;
using namespace winios::kernel;
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

void test_snapshot_replay_subsystems() {
    std::cout << "[Test 1] Stage 210.21: Subsystem Snapshot Serialization & Deterministic Replay...\n";

    std::string err;

    // 1. Snapshot & Replay: XnuPhysicalPageAllocator
    XnuPhysicalPageAllocator page_alloc1;
    page_alloc1.initialize(0x0ULL, 0x800000ULL);
    page_alloc1.reserve_range(0x0ULL, 0x40000ULL, "reserved_boot");
    auto p1 = page_alloc1.allocate_pages(2); // Allocate 2 pages
    TEST_CHECK(p1.has_value(), "Allocated 2 pages in page_alloc1");

    const auto page_snapshot = page_alloc1.serialize();
    TEST_CHECK(!page_snapshot.empty(), "Page allocator serialized");

    XnuPhysicalPageAllocator page_alloc2;
    TEST_CHECK(page_alloc2.deserialize(page_snapshot.data(), page_snapshot.size(), &err), "Page allocator deserialized");
    TEST_CHECK(page_alloc2.total_pages() == page_alloc1.total_pages(), "Total pages matched");
    TEST_CHECK(page_alloc2.free_pages_count() == page_alloc1.free_pages_count(), "Free pages matched");
    TEST_CHECK(page_alloc2.allocated_pages_count() == page_alloc1.allocated_pages_count(), "Allocated pages matched");

    // 2. Snapshot & Replay: XnuVmMap
    XnuVmMap map1(0x0ULL, 0x1000000ULL, "snapshot_test_map");
    map1.vm_map_enter(0x10000ULL, 0x10000ULL, kVmProtRead | kVmProtExecute, kVmProtAll, "code");
    map1.vm_map_enter(0x20000ULL, 0x20000ULL, kVmProtRead | kVmProtWrite, kVmProtAll, "data");
    map1.vm_map_wire(0x20000ULL, 0x10000ULL);

    const auto map_snapshot = map1.serialize();
    TEST_CHECK(!map_snapshot.empty(), "VM map serialized");

    XnuVmMap map2;
    TEST_CHECK(map2.deserialize(map_snapshot.data(), map_snapshot.size(), &err), "VM map deserialized");
    TEST_CHECK(map2.entries().size() == 2, "Entry count matched");
    const auto* e_code = map2.vm_map_lookup(0x14000ULL);
    TEST_CHECK(e_code != nullptr && e_code->tag == "code", "Lookup in replayed map finds 'code'");
    const auto* e_data = map2.vm_map_lookup(0x24000ULL);
    TEST_CHECK(e_data != nullptr && e_data->wired_count == 1, "Wired count preserved in replayed map");

    // 3. Snapshot & Replay: XnuZoneAllocator
    XnuZoneAllocator za1(0x200000ULL, 0x200000ULL);
    za1.create_zone("test_zone", 64, 32);
    auto zptr1 = za1.zalloc("test_zone");
    auto zptr2 = za1.zalloc("test_zone");
    TEST_CHECK(zptr1 != 0 && zptr2 != 0, "Allocated 2 zone elements");

    const auto za_snapshot = za1.serialize();
    TEST_CHECK(!za_snapshot.empty(), "Zone allocator serialized");

    XnuZoneAllocator za2;
    TEST_CHECK(za2.deserialize(za_snapshot.data(), za_snapshot.size(), &err), "Zone allocator deserialized");
    TEST_CHECK(za2.zone_count() == 1, "Zone count matched");
    ZoneStats zs1{}, zs2{};
    za1.get_zone_stats("test_zone", zs1);
    za2.get_zone_stats("test_zone", zs2);
    TEST_CHECK(zs2.count == zs1.count, "Active element count matched in replayed zone");
    TEST_CHECK(zs2.free_count == zs1.free_count, "Free element count matched in replayed zone");

    // 4. Snapshot & Replay: KernelTaskManager
    KernelTaskManager tm1;
    auto kt = tm1.create_kernel_task(nullptr);
    auto ut1 = tm1.create_task("user_app", nullptr);
    ut1->suspend();

    const auto tm_snapshot = tm1.serialize();
    TEST_CHECK(!tm_snapshot.empty(), "TaskManager serialized");

    KernelTaskManager tm2;
    TEST_CHECK(tm2.deserialize(tm_snapshot.data(), tm_snapshot.size(), &err), "TaskManager deserialized");
    TEST_CHECK(tm2.task_count() == 2, "Task count matched");
    auto re_kt = tm2.get_task(0);
    auto re_ut = tm2.get_task(1);
    TEST_CHECK(re_kt != nullptr && re_kt->name() == "kernel_task", "kernel_task restored");
    TEST_CHECK(re_ut != nullptr && re_ut->state() == TaskState::Suspended, "user_app state preserved as Suspended");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.21 Snapshot Replay Tests ===\n";
    test_snapshot_replay_subsystems();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.21: All snapshot replay tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.21: " << g_failures << " tests failed.\n";
        return 1;
    }
}
