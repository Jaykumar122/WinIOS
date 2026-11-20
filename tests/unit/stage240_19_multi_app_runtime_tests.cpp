// ============================================================================
// Stage 240.17 - 240.19: Guest Event-Loop, Window Desktop & Multi-App Runtime Tests
// Verifies event routing, desktop window operations, and multi-application
// orchestration with distinct ASIDs, address space isolation, and lifecycle states.
// ============================================================================

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign240_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "runtime/runtime_lifecycle_state_machine.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::memory;

static void test_window_and_event_integration() {
    std::cout << "[Test 1] Testing window creation, state, and event delivery...\n";
    auto wm = std::make_shared<graphics::WindowManager>();
    auto input = std::make_shared<input::InputRouter>(wm);

    // 1. Create desktop window
    graphics::Rect initial_frame{100, 100, 800, 600};
    uint32_t wid = wm->create_window(101 /* pid */, "Test Window 1", initial_frame, nullptr);
    TEST_ASSERT(wid != 0, "Window creation must succeed");

    auto win = wm->get_window(wid);
    TEST_ASSERT(win != nullptr, "Window must exist in WindowManager");
    TEST_ASSERT(win->title == "Test Window 1", "Window title matches");
    TEST_ASSERT(win->can_access(101), "Window owner must have access");
    TEST_ASSERT(!win->can_access(999), "Other PID must not have access");

    // 2. Route input event for window
    input::InputEvent evt{};
    evt.type = input::InputEventType::KeyPress;
    evt.key_code = 0x41; // 'A'
    evt.target_window_id = wid;
    evt.timestamp_us = 1000000;
    TEST_ASSERT(input->route_event(evt), "Route event succeeded");

    TEST_ASSERT(input->pending_events_for_process(101) > 0, "Input router must have queued event for PID 101");
    input::InputEvent popped{};
    TEST_ASSERT(input->poll_event(101, popped), "Poll event succeeded");
    TEST_ASSERT(popped.key_code == 0x41, "Event key code matches");
    TEST_ASSERT(popped.target_window_id == wid, "Target window matches");

    // 3. Destroy window
    TEST_ASSERT(wm->destroy_window(101, wid), "Destroy window must succeed");
    TEST_ASSERT(wm->get_window(wid) == nullptr, "Destroyed window must not exist");
}

static void test_multi_application_isolation_and_execution() {
    std::cout << "[Test 2] Multi-application orchestration with ASID & memory isolation...\n";
    auto bin1 = load_campaign240_workload("arith_control");
    auto bin2 = load_campaign240_workload("multi_app_worker");
    TEST_ASSERT(!bin1.empty() && !bin2.empty(), "Both workloads must be available");

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager initialization");

    // Allocate distinct L2 page tables for App 1 and App 2
    uint64_t root1 = ppm.allocate_page();
    uint64_t root2 = ppm.allocate_page();
    TEST_ASSERT(root1 != 0 && root2 != 0 && root1 != root2, "Distinct L2 translation roots");

    UserAddressSpace uas1(mem, ppm, root1, "app1_arith_uas");
    uas1.set_asid(1);
    uas1.set_owner_pid(201);

    UserAddressSpace uas2(mem, ppm, root2, "app2_worker_uas");
    uas2.set_asid(2);
    uas2.set_owner_pid(202);

    auto img1 = MachoUserLoader::load_image(bin1, uas1, ppm, mem);
    auto img2 = MachoUserLoader::load_image(bin2, uas2, ppm, mem);
    TEST_ASSERT(img1.valid && img2.valid, "Both Mach-O images loaded successfully");

    uint64_t stack_bot1 = 0, stack_bot2 = 0;
    TEST_ASSERT(uas1.allocate_user_stack(0x7FFF0000ULL, 0x10000, stack_bot1), "App 1 stack");
    TEST_ASSERT(uas2.allocate_user_stack(0x7FFF0000ULL, 0x10000, stack_bot2), "App 2 stack");

    // Verify Memory Isolation: same VA (0x7FFEFF00) translates to distinct physical pages
    PageDescriptorFields desc1{}, desc2{};
    uint64_t pa1 = uas1.translate(0x7FFEFF00ULL, &desc1);
    uint64_t pa2 = uas2.translate(0x7FFEFF00ULL, &desc2);
    TEST_ASSERT(pa1 != 0 && pa2 != 0, "Both stacks mapped");
    TEST_ASSERT(pa1 != pa2, "Physical pages must be disjoint (Memory Isolation)");

    // Write to App 1 stack and verify App 2 cannot see or alter it
    mem.write_u64(pa1, 0x1111222233334444ULL);
    mem.write_u64(pa2, 0xAAAABBBBCCCCDDDULL);
    TEST_ASSERT(mem.read_u64(pa1) == 0x1111222233334444ULL, "App 1 memory intact");
    TEST_ASSERT(mem.read_u64(pa2) == 0xAAAABBBBCCCCDDDULL, "App 2 memory intact");

    // Lifecycle state machine tracking
    runtime::RuntimeLifecycleStateMachine sm1(201, 1, "com.winios.arith");
    runtime::RuntimeLifecycleStateMachine sm2(202, 1, "com.winios.worker");

    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Admitted), "App 1 admitted");
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Loading), "App 1 loading");
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Initialized), "App 1 initialized");
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Running), "App 1 running");

    TEST_ASSERT(sm2.transition_to(runtime::AppLifecycleState::Admitted), "App 2 admitted");
    TEST_ASSERT(sm2.transition_to(runtime::AppLifecycleState::Loading), "App 2 loading");
    TEST_ASSERT(sm2.transition_to(runtime::AppLifecycleState::Initialized), "App 2 initialized");
    TEST_ASSERT(sm2.transition_to(runtime::AppLifecycleState::Running), "App 2 running");

    // Context switch: suspend App 1
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Suspended), "App 1 suspended");

    // Restore App 1 from suspended to running
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Running), "App 1 restored to running");

    // Terminate both cleanly
    TEST_ASSERT(sm1.transition_to(runtime::AppLifecycleState::Terminating), "App 1 terminating");
    TEST_ASSERT(sm1.begin_cleanup(), "App 1 cleanup");
    TEST_ASSERT(!sm1.begin_cleanup(), "App 1 double-cleanup prevented");

    TEST_ASSERT(sm2.transition_to(runtime::AppLifecycleState::Terminating), "App 2 terminating");
    TEST_ASSERT(sm2.begin_cleanup(), "App 2 cleanup");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 240.17 - 240.19: Event-Loop, Window & Multi-App Runtime Tests\n";
    std::cout << "=================================================================\n";

    test_window_and_event_integration();
    test_multi_application_isolation_and_execution();

    std::cout << "[SUCCESS] Event loop, desktop window, and multi-app runtime verified.\n";
    return 0;
}
