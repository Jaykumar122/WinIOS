#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "security/capability_manager.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "runtime/interactive_runtime_contract.h"
#include "winios/ui/window.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::graphics;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::security;

void test_window_geometry_struct_layout() {
    std::cout << "[Test 1] Window Geometry struct layout and alignment...\n";
    TEST_ASSERT(sizeof(winios_rect_t) == 16);
    TEST_ASSERT(sizeof(winios_window_geometry_t) == 32);

    winios_window_geometry_t geom{};
    geom.frame.x = 10;
    geom.frame.y = 20;
    geom.frame.width = 300;
    geom.frame.height = 200;
    geom.z_order = 1;
    geom.state = WINIOS_WINDOW_STATE_VISIBLE;
    geom.is_focused = 1;
    geom.always_on_top = 0;

    TEST_ASSERT(geom.frame.width == 300);
    TEST_ASSERT(geom.state == WINIOS_WINDOW_STATE_VISIBLE);
    std::cout << "[PASS] Struct layout verified.\n";
}

void test_window_manager_visibility_and_focus() {
    std::cout << "[Test 2] WindowManager visibility and focus manipulation...\n";
    WindowManager wm;
    auto surf = std::make_shared<DisplaySurface>(1, 100, 400, 300);
    Rect frame{50, 50, 400, 300};
    uint32_t wid = wm.create_window(100, "Test Window", frame, surf);
    TEST_ASSERT(wid > 0);

    auto win = wm.get_window(wid);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->is_visible());
    TEST_ASSERT(win->owner_pid == 100);

    // Set hidden
    TEST_ASSERT(wm.set_window_state(100, wid, WindowState::Hidden));
    TEST_ASSERT(!win->is_visible());

    // Restore visible
    TEST_ASSERT(wm.set_window_state(100, wid, WindowState::Normal));
    TEST_ASSERT(win->is_visible());

    // Set focus
    TEST_ASSERT(wm.set_focus(wid));
    TEST_ASSERT(wm.focused_window_id() == wid);
    TEST_ASSERT(wm.focused_pid() == 100);

    // Cross-process access rejection
    TEST_ASSERT(!wm.set_window_state(200, wid, WindowState::Hidden));
    TEST_ASSERT(win->is_visible()); // Unaltered

    std::cout << "[PASS] WindowManager visibility and focus operations passed.\n";
}

void test_syscall_window_visibility_and_geometry() {
    std::cout << "[Test 3] Syscall dispatcher window visibility & geometry...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "uas_win_test");

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    CapabilityManager cm;
    disp.set_capability_manager(&cm);

    WindowManager wm;
    SurfaceResourceManager surf_mgr;
    disp.set_window_manager(&wm);
    disp.set_surface_resource_manager(&surf_mgr);

    BsdProcess proc(101, 1, "test_app");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // Syscall 500: Create Window
    // Args: x0=title_va, x1=title_len, x2=x, x3=y, x4=width, x5=height, x16=sys_num
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0); // null title
    cpu.write_register(1, 0);
    cpu.write_register(2, 10);
    cpu.write_register(3, 10);
    cpu.write_register(4, 200);
    cpu.write_register(5, 150);
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    uint32_t wid = static_cast<uint32_t>(cpu.read_register(0));
    TEST_ASSERT(wid > 0);

    auto win = wm.get_window(wid);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->is_visible());

    // Syscall 533: Set Visibility to Hidden (0)
    cpu.write_register(16, kSysWiniosWindowSetVisibility);
    cpu.write_register(0, wid);
    cpu.write_register(1, 0); // hidden
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(!win->is_visible());

    // Foreign PID cannot hide or alter window
    BsdProcess foreign_proc(102, 1, "foreign_app");
    foreign_proc.set_address_space(&uas);
    cpu.write_register(16, kSysWiniosWindowSetVisibility);
    cpu.write_register(0, wid);
    cpu.write_register(1, 1);
    TEST_ASSERT(!disp.handle_syscall(cpu, foreign_proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);
    TEST_ASSERT(!win->is_visible()); // Still hidden

    // Restore visibility from owner
    cpu.write_register(16, kSysWiniosWindowSetVisibility);
    cpu.write_register(0, wid);
    cpu.write_register(1, 1); // visible
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(win->is_visible());

    // Syscall 510: Set Focus
    cpu.write_register(16, kSysWiniosWindowSetFocus);
    cpu.write_register(0, wid);
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(wm.focused_window_id() == wid);

    // Syscall 501: Destroy Window
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, wid);
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(wm.get_window(wid) == nullptr);

    std::cout << "[PASS] Syscall window visibility and geometry passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.3: GUEST WINDOW API TESTS\n";
    std::cout << "========================================\n";

    test_window_geometry_struct_layout();
    test_window_manager_visibility_and_focus();
    test_syscall_window_visibility_and_geometry();

    std::cout << "\nALL STAGE 222.3 TESTS PASSED (100%).\n";
    return 0;
}
