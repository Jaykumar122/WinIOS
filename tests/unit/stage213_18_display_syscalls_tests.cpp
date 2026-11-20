// ============================================================================
// Stage 213.18: Guest Display and Window Syscalls Conformance Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_ui_runtime.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_display_syscalls_lifecycle() {
    std::cout << "[Test 1] Verify Window Creation, Fill, Update, Poll and Destroy via Syscalls...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("counter_app", 100);

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_uas");

    const std::uint64_t user_page_pa = ppm.allocate_page();
    constexpr std::uint64_t kUserPageVa = 0x100000ULL;
    uas.map_user_page(kUserPageVa, user_page_pa, memory::PagePermissions::UserReadWrite);

    CPU cpu;
    cpu.reset();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    // Setup graphics subsystem
    DisplayConfiguration disp_cfg{};
    disp_cfg.width = 800;
    disp_cfg.height = 600;
    disp_cfg.row_stride = 800 * 4;
    auto virtual_display = std::make_shared<VirtualDisplay>(disp_cfg);
    auto compositor = std::make_shared<Compositor>(virtual_display);
    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    auto window_mgr = std::make_shared<WindowManager>();
    auto input_router = std::make_unique<InputRouter>(window_mgr);

    disp.set_surface_resource_manager(surface_mgr.get());
    disp.set_window_manager(window_mgr.get());
    disp.set_compositor(compositor.get());
    disp.set_input_router(input_router.get());

    // 1. Write title "Counter App" to user memory
    const std::string title = "Counter App";
    TEST_CHECK(uas.copyin(kUserPageVa, title.c_str(), title.size() + 1), "Write title to user space");

    // 2. Invoke SYS_winios_window_create (500)
    // X16=500, X0=title_va, X1=len, X2=50(x), X3=100(y), X4=300(w), X5=400(h)
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, kUserPageVa);
    cpu.write_register(1, title.size() + 1);
    cpu.write_register(2, 50);
    cpu.write_register(3, 100);
    cpu.write_register(4, 300);
    cpu.write_register(5, 400);

    bool ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(ok, "window_create syscall returned success");
    std::uint32_t wid = static_cast<std::uint32_t>(cpu.read_register(0));
    TEST_CHECK(wid > 0, "Created window ID is > 0");
    TEST_CHECK(window_mgr->window_count() == 1, "Window manager has 1 window");
    TEST_CHECK(surface_mgr->total_surface_count() == 1, "Surface manager has 1 surface");

    // 3. Invoke SYS_winios_surface_fill (505)
    // X16=505, X0=wid, X1=10, X2=10, X3=50, X4=50, X5=0xFF00AAFF (color)
    cpu.write_register(16, kSysWiniosSurfaceFill);
    cpu.write_register(0, wid);
    cpu.write_register(1, 10);
    cpu.write_register(2, 10);
    cpu.write_register(3, 50);
    cpu.write_register(4, 50);
    cpu.write_register(5, 0xFF00AAFFu);

    ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(ok, "surface_fill syscall returned success");

    auto win = window_mgr->get_window(wid);
    TEST_CHECK(win && win->surface, "Window has valid surface");
    if (win && win->surface) {
        std::uint32_t px_val = 0;
        win->surface->read_pixel(proc->pid(), 20, 20, px_val);
        TEST_CHECK(px_val == 0xFF00AAFFu, "Filled pixel has color 0xFF00AAFF");
    }

    // 4. Test Event Polling via SYS_winios_poll_event (504)
    // Queue empty first
    constexpr std::uint64_t kEvtVa = kUserPageVa + 0x100;
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, kEvtVa);
    cpu.write_register(1, sizeof(GuestInputEvent));

    ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(ok, "poll_event syscall succeeded");
    TEST_CHECK(cpu.read_register(0) == 0, "Queue empty returned 0 events");

    // Route a touch event into the window
    InputEvent touch_evt{};
    touch_evt.type = InputEventType::TouchBegin;
    touch_evt.touch.contact_id = 0;
    touch_evt.touch.x = 80;  // Inside window at [50, 100, 300, 400]
    touch_evt.touch.y = 150;
    touch_evt.timestamp_us = 45000;
    input_router->route_event(touch_evt);

    // Poll event again
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, kEvtVa);
    cpu.write_register(1, sizeof(GuestInputEvent));

    ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(ok, "poll_event returned success");
    TEST_CHECK(cpu.read_register(0) == 1, "Delivered exactly 1 event");

    GuestInputEvent guest_evt{};
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(kEvtVa), &guest_evt, sizeof(guest_evt)), "Read back guest event structure");
    TEST_CHECK(guest_evt.type == static_cast<std::uint32_t>(InputEventType::TouchBegin), "Event type is TouchBegin");
    TEST_CHECK(guest_evt.contact_id == 0, "Contact ID matches 0");
    TEST_CHECK(guest_evt.x == 30 && guest_evt.y == 50, "Coordinates match local (30, 50)");
    TEST_CHECK(guest_evt.window_id == wid, "Window ID matches wid");
    TEST_CHECK(guest_evt.timestamp_us == 45000, "Timestamp matches 45000 us");

    // 5. Invoke SYS_winios_window_destroy (501)
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, wid);

    ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(ok, "window_destroy syscall returned success");
    TEST_CHECK(window_mgr->window_count() == 0, "Window manager windows count is 0");
    TEST_CHECK(surface_mgr->total_surface_count() == 0, "Surface manager surfaces count is 0");
    TEST_CHECK(surface_mgr->total_allocated_bytes() == 0, "All display memory reclaimed");
}

void test_syscall_parameter_validation() {
    std::cout << "[Test 2] Verify Invalid Pointers, Invalid Window IDs and Parameter Validation...\n";

    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("test_proc", 42);

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_uas");

    CPU cpu;
    cpu.reset();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    auto window_mgr = std::make_shared<WindowManager>();
    disp.set_surface_resource_manager(surface_mgr.get());
    disp.set_window_manager(window_mgr.get());

    // Destroy non-existent window ID -> EBADF
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, 9999);
    bool ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(!ok, "Destroying non-existent window failed");
    TEST_CHECK(cpu.read_register(0) == kErrnoEBADF, "Error code is EBADF");

    // Create window with 0 dimensions -> EINVAL
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0);
    cpu.write_register(1, 0);
    cpu.write_register(4, 0); // 0 width
    cpu.write_register(5, 100);
    ok = disp.handle_syscall(cpu, *proc, uas, sched);
    TEST_CHECK(!ok, "Zero width rejected");
    TEST_CHECK(cpu.read_register(0) == kErrnoEINVAL, "Error code is EINVAL");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.18: Display Syscalls Tests        \n";
    std::cout << "====================================================\n";

    test_display_syscalls_lifecycle();
    test_syscall_parameter_validation();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
