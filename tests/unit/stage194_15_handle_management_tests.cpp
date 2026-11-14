#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "kernel/handle_table.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_handle_allocation() {
    std::cout << "Running test_handle_allocation...\n";
    HandleTable table(64);
    EXPECT_EQ(table.active_handles_count(), 0u);

    auto thread_obj = std::make_shared<KernelObject>(KernelObjectType::Thread, 101);
    std::int32_t h1 = table.allocate_handle(thread_obj, HandleRights::All);
    EXPECT_TRUE(h1 > 0);
    EXPECT_TRUE(table.is_valid_handle(h1));
    EXPECT_EQ(table.get_rights(h1), HandleRights::All);
    EXPECT_EQ(table.active_handles_count(), 1u);

    auto retrieved = table.get_object(h1);
    EXPECT_EQ(retrieved, thread_obj);
    EXPECT_EQ(static_cast<std::uint32_t>(retrieved->object_type()), static_cast<std::uint32_t>(KernelObjectType::Thread));
    EXPECT_EQ(retrieved->object_id(), 101u);
}

void test_handle_rights_enforcement() {
    std::cout << "Running test_handle_rights_enforcement...\n";
    HandleTable table(64);

    auto port_obj = std::make_shared<KernelObject>(KernelObjectType::IpcPort, 202);
    const std::uint32_t rights = HandleRights::Read | HandleRights::Write;
    std::int32_t h = table.allocate_handle(port_obj, rights);

    // Read access allowed
    EXPECT_TRUE(table.get_object(h, HandleRights::Read) != nullptr);
    // Write access allowed
    EXPECT_TRUE(table.get_object(h, HandleRights::Write) != nullptr);
    // Both Read and Write allowed
    EXPECT_TRUE(table.get_object(h, HandleRights::Read | HandleRights::Write) != nullptr);
    // Execute access NOT allowed -> returns nullptr
    EXPECT_TRUE(table.get_object(h, HandleRights::Execute) == nullptr);
    // Destroy access NOT allowed
    EXPECT_TRUE(table.get_object(h, HandleRights::Destroy) == nullptr);
}

void test_handle_capability_attenuation() {
    std::cout << "Running test_handle_capability_attenuation...\n";
    HandleTable table(64);

    auto obj = std::make_shared<KernelObject>(KernelObjectType::MemoryRegion, 303);
    const std::uint32_t parent_rights = HandleRights::Read | HandleRights::Write | HandleRights::Duplicate;
    std::int32_t h_parent = table.allocate_handle(obj, parent_rights);

    // Duplicate with attenuation to Read only
    std::int32_t h_child1 = table.duplicate_handle(h_parent, HandleRights::Read);
    EXPECT_TRUE(h_child1 > 0);
    EXPECT_EQ(table.get_rights(h_child1), HandleRights::Read);
    EXPECT_TRUE(table.get_object(h_child1, HandleRights::Read) != nullptr);
    EXPECT_TRUE(table.get_object(h_child1, HandleRights::Write) == nullptr);

    // Cannot attenuate to rights the parent does not hold (e.g. Execute)
    std::int32_t h_child2 = table.duplicate_handle(h_parent, HandleRights::Read | HandleRights::Execute);
    EXPECT_TRUE(h_child2 > 0);
    // Rights must be intersection: only Read (Execute not granted)
    EXPECT_EQ(table.get_rights(h_child2), HandleRights::Read);

    // Child1 has no Duplicate right -> attempting duplicate must fail
    std::int32_t h_bad = table.duplicate_handle(h_child1, HandleRights::Read);
    EXPECT_EQ(h_bad, -1);
}

void test_handle_lifetime_and_refcounting() {
    std::cout << "Running test_handle_lifetime_and_refcounting...\n";
    HandleTable table(64);

    auto obj = std::make_shared<KernelObject>(KernelObjectType::SyncPrimitive, 404);
    EXPECT_EQ(obj->ref_count(), 1u);

    std::int32_t h1 = table.allocate_handle(obj, HandleRights::All);
    EXPECT_EQ(obj->ref_count(), 2u);

    std::int32_t h2 = table.duplicate_handle(h1, HandleRights::All);
    EXPECT_EQ(obj->ref_count(), 3u);

    EXPECT_TRUE(table.close_handle(h1));
    EXPECT_EQ(obj->ref_count(), 2u);
    EXPECT_TRUE(!table.is_valid_handle(h1));

    EXPECT_TRUE(table.close_handle(h2));
    EXPECT_EQ(obj->ref_count(), 1u);
    EXPECT_TRUE(!table.is_valid_handle(h2));

    // Closing already closed handle must return false
    EXPECT_TRUE(!table.close_handle(h1));
}

void test_handle_slot_reuse() {
    std::cout << "Running test_handle_slot_reuse...\n";
    HandleTable table(64);

    auto o1 = std::make_shared<KernelObject>(KernelObjectType::Thread, 1);
    auto o2 = std::make_shared<KernelObject>(KernelObjectType::Thread, 2);
    auto o3 = std::make_shared<KernelObject>(KernelObjectType::Thread, 3);

    std::int32_t h1 = table.allocate_handle(o1);
    std::int32_t h2 = table.allocate_handle(o2);
    std::int32_t h3 = table.allocate_handle(o3);

    EXPECT_EQ(h1, 1);
    EXPECT_EQ(h2, 2);
    EXPECT_EQ(h3, 3);

    // Close h2 (slot 2)
    EXPECT_TRUE(table.close_handle(h2));
    EXPECT_TRUE(!table.is_valid_handle(h2));

    // Allocate new object -> must reuse slot 2
    auto o4 = std::make_shared<KernelObject>(KernelObjectType::Thread, 4);
    std::int32_t h4 = table.allocate_handle(o4);
    EXPECT_EQ(h4, 2);
    EXPECT_TRUE(table.is_valid_handle(2));
    EXPECT_EQ(table.get_object(2)->object_id(), 4u);
}

void test_handle_table_capacity_exhaustion() {
    std::cout << "Running test_handle_table_capacity_exhaustion...\n";
    HandleTable table(3); // Capacity 3

    auto obj = std::make_shared<KernelObject>(KernelObjectType::Device, 505);

    std::int32_t h1 = table.allocate_handle(obj);
    std::int32_t h2 = table.allocate_handle(obj);
    std::int32_t h3 = table.allocate_handle(obj);

    EXPECT_TRUE(h1 > 0);
    EXPECT_TRUE(h2 > 0);
    EXPECT_TRUE(h3 > 0);
    EXPECT_EQ(table.active_handles_count(), 3u);

    // 4th allocation must fail
    std::int32_t h4 = table.allocate_handle(obj);
    EXPECT_EQ(h4, -1);
}

int main() {
    std::cout << "=== WINIOS STAGE 194.15: KERNEL OBJECT & HANDLE MANAGEMENT TESTS ===\n";

    test_handle_allocation();
    test_handle_rights_enforcement();
    test_handle_capability_attenuation();
    test_handle_lifetime_and_refcounting();
    test_handle_slot_reuse();
    test_handle_table_capacity_exhaustion();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
