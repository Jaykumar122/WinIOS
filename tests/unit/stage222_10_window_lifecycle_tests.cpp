#include <cassert>
#include <iostream>
#include <memory>
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

using namespace winios;
using namespace winios::graphics;
using namespace winios::kernel;

void test_dynamic_resizing_and_clamping() {
    std::cout << "[Test 1] Dynamic resizing and dimension clamping...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 200, 200);

    // Initial window: 300x200
    uint32_t wid = wm->create_window(100, "ResizeApp", Rect{10, 10, 300, 200}, surf);
    auto win = wm->get_window(wid);
    assert(win != nullptr);

    // Resize window to 400x350
    bool ok = wm->resize_window(100, wid, 400, 350);
    assert(ok);
    assert(win->frame.width == 400);
    assert(win->frame.height == 350);
    auto cf = win->client_frame();
    assert(surf->width() == cf.width);
    assert(surf->height() == cf.height);
    assert(win->is_dirty);

    // Test minimum clamping (min_width = 100, min_height = 80)
    ok = wm->resize_window(100, wid, 50, 40);
    assert(ok);
    assert(win->frame.width == win->min_width);
    assert(win->frame.height == win->min_height);

    // Foreign process attempt to resize must fail
    bool foreign_ok = wm->resize_window(999, wid, 500, 500);
    assert(!foreign_ok);
    (void)foreign_ok;
    std::cout << "[PASS] Dynamic resizing and clamping verified.\n";
}

void test_damage_tracking_on_move_and_resize() {
    std::cout << "[Test 2] Damage rectangle calculation on move and resize...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 100, 100);

    uint32_t wid = wm->create_window(100, "DamageApp", Rect{100, 100, 200, 150}, surf);
    (void)wid;
    auto win = wm->get_window(wid);

    wm->clear_window_dirty(wid);
    assert(!wm->is_window_dirty(wid));

    // Move window by +50, +30 -> new frame {150, 130, 200, 150}
    bool moved = wm->move_window(100, wid, 50, 30);
    assert(moved);
    (void)moved;
    assert(wm->is_window_dirty(wid));
    Rect d = wm->get_window_dirty_rect(wid);
    assert(d.x == 100);
    assert(d.y == 100);
    assert(d.width == 250);
    assert(d.height == 180);

    // Resize window
    wm->clear_window_dirty(wid);
    bool resized = wm->resize_window(100, wid, 300, 250);
    assert(resized);
    (void)resized;
    Rect rd = wm->get_window_dirty_rect(wid);
    assert(rd.width >= 300);
    assert(rd.height >= 250);
    std::cout << "[PASS] Damage tracking verified.\n";
}

void test_non_client_hit_testing() {
    std::cout << "[Test 3] Non-client hit testing (title bar, buttons, client)...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 200, 100);

    // Frame {100, 100, 300, 200}
    // Title bar is y in [100, 128)
    uint32_t wid = wm->create_window(100, "NCApp", Rect{100, 100, 300, 200}, surf);

    // Hit test title bar area (middle of title bar)
    auto zone = wm->hit_test_non_client(wid, 150, 110);
    assert(zone == NonClientZone::TitleBar);

    // Hit test inside client area (e.g. {150, 160})
    zone = wm->hit_test_non_client(wid, 150, 160);
    assert(zone == NonClientZone::Client);

    // Hit test close button (near right edge of title bar)
    zone = wm->hit_test_non_client(wid, 380, 110);
    assert(zone == NonClientZone::CloseButton);

    // Hit test outside window frame completely
    zone = wm->hit_test_non_client(wid, 50, 50);
    assert(zone == NonClientZone::None);

    std::cout << "[PASS] Non-client hit testing verified.\n";
}

void test_set_geometry_syscall() {
    std::cout << "[Test 4] kSysWiniosWindowSetGeometry syscall integration...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "uas_lifecycle_test");

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    security::CapabilityManager cm;
    disp.set_capability_manager(&cm);

    WindowManager wm;
    SurfaceResourceManager surf_mgr;
    disp.set_window_manager(&wm);
    disp.set_surface_resource_manager(&surf_mgr);

    BsdProcess proc(101, 1, "lifecycle_app");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // Create window: width 200, height 150
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0); // null title
    cpu.write_register(1, 0);
    cpu.write_register(2, 20); // x
    cpu.write_register(3, 20); // y
    cpu.write_register(4, 200); // w
    cpu.write_register(5, 150); // h
    assert(disp.handle_syscall(cpu, proc, uas, sched));
    uint32_t wid = static_cast<uint32_t>(cpu.read_register(0));
    assert(wid > 0);

    auto win = wm.get_window(wid);
    assert(win != nullptr);

    // Setup user space buffer with new geometry
    const uint64_t geom_va = 0x200000;
    const uint64_t geom_pa = ppm.allocate_page();
    uas.map_user_page(geom_va, geom_pa, memory::PagePermissions::UserReadWrite);

    winios_window_geometry_t new_geom{};
    new_geom.frame.x = 80;
    new_geom.frame.y = 90;
    new_geom.frame.width = 350;
    new_geom.frame.height = 250;
    new_geom.z_order = 5;
    new_geom.always_on_top = 1;

    uas.copyin(geom_va, &new_geom, sizeof(new_geom));

    // Call kSysWiniosWindowSetGeometry (531)
    cpu.write_register(16, kSysWiniosWindowSetGeometry);
    cpu.write_register(0, wid);
    cpu.write_register(1, geom_va);
    assert(disp.handle_syscall(cpu, proc, uas, sched));
    assert(cpu.read_register(0) == 0);

    // Verify window state updated
    assert(win->frame.x == 80);
    assert(win->frame.y == 90);
    assert(win->frame.width == 350);
    assert(win->frame.height == 250);
    assert(win->z_order == 5);
    assert(win->always_on_top);
    assert(win->is_dirty);

    std::cout << "[PASS] kSysWiniosWindowSetGeometry syscall verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.10: WINDOW LIFECYCLE TESTS\n";
    std::cout << "========================================\n";

    test_dynamic_resizing_and_clamping();
    test_damage_tracking_on_move_and_resize();
    test_non_client_hit_testing();
    test_set_geometry_syscall();

    std::cout << "\nALL STAGE 222.10 TESTS PASSED (100%).\n";
    return 0;
}
