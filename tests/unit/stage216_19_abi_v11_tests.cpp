#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_event_services.h"
#include "winios_sdk/winios_sdk.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::runtime;
using namespace winios::memory;

void test_abi_constants_and_struct_sizes() {
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION == 0x00010100);
    TEST_ASSERT(kWiniosCategoryCAbiVersion == 0x00010100);

    TEST_ASSERT(is_category_c_extension(500));
    TEST_ASSERT(is_category_c_extension(507));
    TEST_ASSERT(is_category_c_extension(508));
    TEST_ASSERT(is_category_c_extension(512));
    TEST_ASSERT(!is_category_c_extension(499));
    TEST_ASSERT(!is_category_c_extension(513));

    TEST_ASSERT(sizeof(winios_rect_t) == 16);
    TEST_ASSERT(sizeof(winios_event_t) == 32);
    TEST_ASSERT(sizeof(winios_window_geometry_t) == 32);
    TEST_ASSERT(sizeof(winios_window_list_t) == 136);

    std::cout << "[PASS] test_abi_constants_and_struct_sizes\n";
}

void test_abi_v11_syscall_dispatch() {
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    auto wm = std::make_shared<WindowManager>();
    auto srm = std::make_shared<SurfaceResourceManager>();
    auto router = std::make_shared<InputRouter>(wm);
    auto ev_svc = std::make_shared<GuestEventServices>(router);

    dispatcher.set_window_manager(wm.get());
    dispatcher.set_surface_resource_manager(srm.get());
    dispatcher.set_input_router(router.get());
    dispatcher.set_event_services(ev_svc.get());

    Memory mem{0x1000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL};
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    sched.initialize();

    CPU cpu;
    cpu.reset();

    BsdProcess proc1(10, 1, "App1", nullptr);
    BsdProcess proc2(20, 1, "App2", nullptr);

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_vm");

    const std::uint64_t kUserBufVa = 0x10000;
    const std::uint64_t pa = ppm.allocate_page();
    uas.map_user_page(kUserBufVa, pa, memory::PagePermissions::UserReadWrite);

    // 1. Syscall 507: Get ABI Version
    cpu.write_register(16, kSysWiniosGetAbiVersion);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0x00010100);

    // Create a surface & window for App1 (PID 10)
    std::shared_ptr<DisplaySurface> surf1;
    auto res1 = srm->create_surface(10, 300, 200, PixelFormat::B8G8R8A8_UNORM, surf1);
    TEST_ASSERT(res1 == SurfaceResultCode::Success);
    std::uint32_t wid1 = wm->create_window(10, "App1 Window", Rect{50, 60, 300, 200}, surf1);

    // 2. Syscall 508: Window Get Geometry
    // Test Cross-process access: App2 (PID 20) querying App1's window -> should fail with EBADF
    cpu.write_register(16, kSysWiniosWindowGetGeometry);
    cpu.write_register(0, wid1);
    cpu.write_register(1, kUserBufVa);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc2, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEBADF);

    // App1 (PID 10) querying its own window -> should succeed
    cpu.write_register(16, kSysWiniosWindowGetGeometry);
    cpu.write_register(0, wid1);
    cpu.write_register(1, kUserBufVa);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    winios_window_geometry_t geom_out{};
    uas.copyout(reinterpret_cast<const void*>(kUserBufVa), &geom_out, sizeof(geom_out));
    TEST_ASSERT(geom_out.frame.x == 50);
    TEST_ASSERT(geom_out.frame.y == 60);
    TEST_ASSERT(geom_out.frame.width == 300);
    TEST_ASSERT(geom_out.frame.height == 200);
    TEST_ASSERT(geom_out.state == WINIOS_WINDOW_STATE_NORMAL);

    // 3. Syscall 509: Window Set Geometry
    winios_window_geometry_t new_geom{};
    new_geom.frame = {100, 120, 350, 250};
    new_geom.z_order = 5;
    new_geom.always_on_top = 1;
    uas.copyin(kUserBufVa, &new_geom, sizeof(new_geom));

    cpu.write_register(16, kSysWiniosWindowSetGeometry);
    cpu.write_register(0, wid1);
    cpu.write_register(1, kUserBufVa);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    auto win1 = wm->get_window(wid1);
    TEST_ASSERT(win1->frame.x == 100);
    TEST_ASSERT(win1->frame.y == 120);
    TEST_ASSERT(win1->frame.width == 350);
    TEST_ASSERT(win1->frame.height == 250);
    TEST_ASSERT(win1->always_on_top == true);

    // 4. Syscall 510: Window Set Focus
    cpu.write_register(16, kSysWiniosWindowSetFocus);
    cpu.write_register(0, wid1);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(wm->focused_window_id() == wid1);

    // 5. Syscall 511: Window List
    // App1 should have 1 window
    cpu.write_register(16, kSysWiniosWindowList);
    cpu.write_register(0, kUserBufVa);
    cpu.write_register(1, 0); // struct mode
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1);

    winios_window_list_t list_out{};
    uas.copyout(reinterpret_cast<const void*>(kUserBufVa), &list_out, sizeof(list_out));
    TEST_ASSERT(list_out.count == 1);
    TEST_ASSERT(list_out.window_ids[0] == wid1);

    // 6. Syscall 512: Create Timer
    cpu.write_register(16, kSysWiniosCreateTimer);
    cpu.write_register(0, 50); // 50 ms
    cpu.write_register(1, 1);  // periodic
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    std::uint32_t timer_id = static_cast<std::uint32_t>(cpu.read_register(0));
    TEST_ASSERT(timer_id > 0);

    // Step event services by 50 ms
    ev_svc->step(50);

    // Poll event using Syscall 504
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, kUserBufVa);
    cpu.write_register(1, sizeof(winios_event_t));
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc1, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1); // 1 event polled

    winios_event_t polled_ev{};
    uas.copyout(reinterpret_cast<const void*>(kUserBufVa), &polled_ev, sizeof(polled_ev));
    TEST_ASSERT(polled_ev.type == WINIOS_EVENT_TIMER);
    TEST_ASSERT(polled_ev.contact_id == timer_id);

    std::cout << "[PASS] test_abi_v11_syscall_dispatch\n";
}

int main() {
    std::cout << "Starting Stage 216.19 Category C ABI v1.1 Tests...\n";
    test_abi_constants_and_struct_sizes();
    test_abi_v11_syscall_dispatch();
    std::cout << "All Stage 216.19 Tests Passed Successfully!\n";
    return 0;
}
