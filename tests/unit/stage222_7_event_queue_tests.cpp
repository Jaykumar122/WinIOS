#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"
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
#include "winios/ui/event.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::security;

void test_event_v2_struct_layout() {
    std::cout << "[Test 1] Event V2 struct layout and alignment...\n";
    TEST_ASSERT(sizeof(winios_input_provenance_t) == 24);
    TEST_ASSERT(sizeof(winios_event_v2_t) == 64);

    winios_event_v2_t evt{};
    evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    evt.contact_id = 1; // Left button
    evt.x = 25;
    evt.y = 40;
    evt.modifiers = WINIOS_KEYMOD_CTRL | WINIOS_KEYMOD_SHIFT;
    evt.wheel_delta = 0;
    evt.window_id = 10;
    evt.timestamp_us = 12345678ULL;
    evt.provenance.device_type = 1; // Mouse
    evt.provenance.device_id = 2;
    evt.provenance.sequence_number = 1001;

    TEST_ASSERT(evt.type == WINIOS_EVENT_V2_MOUSE_DOWN);
    TEST_ASSERT(evt.modifiers == (WINIOS_KEYMOD_CTRL | WINIOS_KEYMOD_SHIFT));
    TEST_ASSERT(evt.provenance.sequence_number == 1001);
    std::cout << "[PASS] Struct layout verified.\n";
}

void test_event_queue_v2_delivery() {
    std::cout << "[Test 2] Event Queue v2 delivery via syscall dispatcher...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "uas_event_test");

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    CapabilityManager cm;
    disp.set_capability_manager(&cm);

    auto wm = std::make_shared<WindowManager>();
    auto surf_mgr = std::make_shared<SurfaceResourceManager>();
    auto router = std::make_shared<InputRouter>(wm);
    disp.set_window_manager(wm.get());
    disp.set_surface_resource_manager(surf_mgr.get());
    disp.set_input_router(router.get());

    BsdProcess proc(42, 1, "interactive_app");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // Create a window for PID 42
    auto surf = std::make_shared<DisplaySurface>(1, 42, 300, 200);
    Rect frame{50, 50, 300, 200};
    uint32_t wid = wm->create_window(42, "EventWin", frame, surf);
    TEST_ASSERT(wid > 0);
    wm->set_focus(wid);

    // 1. Enqueue MouseDown event to Router
    InputEvent in_evt{};
    in_evt.type = InputEventType::MouseDown;
    in_evt.device_id = 1;
    in_evt.timestamp_us = 9999ULL;
    in_evt.target_pid = 42;
    in_evt.target_window_id = wid;
    in_evt.win32_msg_id = 501;
    in_evt.provenance = InputProvenance::PhysicalWindowsInput;
    in_evt.mouse.x = 80;  // Virtual display X
    in_evt.mouse.y = 70;  // Virtual display Y
    in_evt.mouse.button = MouseButton::Left;
    in_evt.mouse.modifier_flags = WINIOS_KEYMOD_SHIFT;

    TEST_ASSERT(router->route_event(in_evt));

    // Allocate guest buffer for winios_event_v2_t
    const uint64_t evt_pa = ppm.allocate_page();
    const uint64_t evt_va = 0x100000;
    TEST_ASSERT(uas.map_user_page(evt_va, evt_pa, memory::PagePermissions::UserReadWrite));

    // Syscall 504: Poll Event V2
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, evt_va);
    cpu.write_register(1, sizeof(winios_event_v2_t));
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1); // 1 event delivered!

    winios_event_v2_t delivered_v2{};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(evt_va), &delivered_v2, sizeof(delivered_v2)));
    TEST_ASSERT(delivered_v2.type == WINIOS_EVENT_V2_MOUSE_DOWN);
    TEST_ASSERT(delivered_v2.contact_id == 1); // Left button
    // Window-relative coordinates: (80 - 50, 70 - 50) = (30, 20)
    TEST_ASSERT(delivered_v2.x == 30);
    TEST_ASSERT(delivered_v2.y == 20);
    TEST_ASSERT(delivered_v2.modifiers == WINIOS_KEYMOD_SHIFT);
    TEST_ASSERT(delivered_v2.window_id == wid);
    TEST_ASSERT(delivered_v2.provenance.sequence_number == 501);
    TEST_ASSERT(delivered_v2.provenance.flags == 1); // Physical

    // 2. Enqueue MouseWheel event
    InputEvent wheel_evt{};
    wheel_evt.type = InputEventType::MouseWheel;
    wheel_evt.device_id = 1;
    wheel_evt.timestamp_us = 10500ULL;
    wheel_evt.target_pid = 42;
    wheel_evt.target_window_id = wid;
    wheel_evt.win32_msg_id = 502;
    wheel_evt.provenance = InputProvenance::PhysicalWindowsInput;
    wheel_evt.mouse.x = 80;
    wheel_evt.mouse.y = 70;
    wheel_evt.mouse.wheel_delta = 120; // 1 notch forward

    TEST_ASSERT(router->route_event(wheel_evt));

    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, evt_va);
    cpu.write_register(1, sizeof(winios_event_v2_t));
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1);

    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(evt_va), &delivered_v2, sizeof(delivered_v2)));
    TEST_ASSERT(delivered_v2.type == WINIOS_EVENT_V2_MOUSE_WHEEL);
    TEST_ASSERT(delivered_v2.wheel_delta == 120);

    // 3. Backward compatibility: legacy size (sizeof(winios_event_t))
    InputEvent timer_evt{};
    timer_evt.type = InputEventType::TimerTick;
    timer_evt.target_pid = 42;
    timer_evt.touch.contact_id = 77; // timer ID
    TEST_ASSERT(router->route_event(timer_evt));

    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, evt_va);
    cpu.write_register(1, sizeof(winios_event_t));
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1);

    winios_event_t legacy_evt{};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(evt_va), &legacy_evt, sizeof(legacy_evt)));
    TEST_ASSERT(legacy_evt.type == 8); // WINIOS_EVENT_TIMER
    TEST_ASSERT(legacy_evt.contact_id == 77);

    // 4. Empty queue returns 0
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, evt_va);
    cpu.write_register(1, sizeof(winios_event_v2_t));
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // No events

    std::cout << "[PASS] Event Queue v2 delivery verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.7: EVENT QUEUE & LOOP V2 TESTS\n";
    std::cout << "========================================\n";

    test_event_v2_struct_layout();
    test_event_queue_v2_delivery();

    std::cout << "\nALL STAGE 222.7 TESTS PASSED (100%).\n";
    return 0;
}
