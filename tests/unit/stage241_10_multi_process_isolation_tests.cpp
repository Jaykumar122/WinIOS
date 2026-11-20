// ============================================================================
// Stage 241.10: Multi-Process Isolation, ASID Boundaries & Resource Ownership
// Verifies hardware ASID address translation isolation, cross-process window
// ownership authorization, and deterministic cleanup without co-tenant impact.
// ============================================================================

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "graphics/window_manager.h"
#include "runtime/runtime_lifecycle_state_machine.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::graphics;
using namespace winios::runtime;

static void test_asid_memory_isolation() {
    std::cout << "[Stage 241.10 - Test 1] ASID translation and virtual address isolation...\n";
    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager init");

    const uint64_t root1 = ppm.allocate_page();
    const uint64_t root2 = ppm.allocate_page();
    TEST_ASSERT(root1 != 0 && root2 != 0, "Allocate roots");

    UserAddressSpace uas1(mem, ppm, root1, "uas_proc1");
    uas1.set_asid(101);
    uas1.set_owner_pid(101);

    UserAddressSpace uas2(mem, ppm, root2, "uas_proc2");
    uas2.set_asid(102);
    uas2.set_owner_pid(102);

    // Map common virtual address 0x20000 to distinct physical pages
    const uint64_t common_va = 0x00020000ULL;
    const uint64_t pa1 = ppm.allocate_page();
    const uint64_t pa2 = ppm.allocate_page();
    TEST_ASSERT(pa1 != 0 && pa2 != 0 && pa1 != pa2, "Physical pages must be distinct");

    TEST_ASSERT(uas1.map_user_page(common_va, pa1, PagePermissions::UserReadWrite), "Map Proc 1");
    TEST_ASSERT(uas2.map_user_page(common_va, pa2, PagePermissions::UserReadWrite), "Map Proc 2");

    // Process 1 writes 0xAA55AA55
    mem.write_u32(static_cast<size_t>(pa1), 0xAA55AA55u);
    // Process 2 writes 0x11223344
    mem.write_u32(static_cast<size_t>(pa2), 0x11223344u);

    // Translate via UAS 1
    PageDescriptorFields desc1{};
    uint64_t translated_pa1 = uas1.translate(common_va, &desc1);
    TEST_ASSERT(translated_pa1 == pa1, "UAS 1 must translate to PA 1");
    TEST_ASSERT(mem.read_u32(static_cast<size_t>(translated_pa1)) == 0xAA55AA55u, "UAS 1 sees its own data");

    // Translate via UAS 2
    PageDescriptorFields desc2{};
    uint64_t translated_pa2 = uas2.translate(common_va, &desc2);
    TEST_ASSERT(translated_pa2 == pa2, "UAS 2 must translate to PA 2");
    TEST_ASSERT(mem.read_u32(static_cast<size_t>(translated_pa2)) == 0x11223344u, "UAS 2 sees its own data");
}

static void test_window_ownership_isolation_and_cleanup() {
    std::cout << "[Stage 241.10 - Test 2] Window ownership authorization and cleanup...\n";
    WindowManager wm;

    graphics::Rect frame{10, 10, 200, 150};
    // Process 101 creates window
    uint32_t win1 = wm.create_window(101, "Proc 101 Window", frame, nullptr);
    TEST_ASSERT(win1 != 0, "Process 101 creates window");

    // Process 102 creates window
    uint32_t win2 = wm.create_window(102, "Proc 102 Window", frame, nullptr);
    TEST_ASSERT(win2 != 0, "Process 102 creates window");

    // Process 102 attempts to destroy Process 101's window -> MUST BE REJECTED
    bool unauthorized_destroy = wm.destroy_window(102, win1);
    TEST_ASSERT(!unauthorized_destroy, "Process 102 must NOT be allowed to destroy Process 101 window");

    // Process 101 destroys its own window -> MUST SUCCEED
    bool authorized_destroy = wm.destroy_window(101, win1);
    TEST_ASSERT(authorized_destroy, "Process 101 destroying its own window must succeed");

    // Process 102's window must still be active and intact
    auto win2_ptr = wm.get_window(win2);
    TEST_ASSERT(win2_ptr != nullptr, "Process 102 window must remain active");
    TEST_ASSERT(win2_ptr->can_access(102), "Process 102 owns window 2");
    TEST_ASSERT(!win2_ptr->can_access(101), "Process 101 cannot access window 2");

    // Cleanup Process 102
    wm.destroy_windows_for_process(102);
    TEST_ASSERT(wm.get_window(win2) == nullptr, "Process 102 window cleaned up");
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Stage 241.10: Multi-Process Isolation & Resource Ownership\n";
    std::cout << "==========================================================\n";

    test_asid_memory_isolation();
    test_window_ownership_isolation_and_cleanup();

    std::cout << "[PASS] Stage 241.10 Multi-process isolation tests completed successfully.\n";
    return 0;
}
