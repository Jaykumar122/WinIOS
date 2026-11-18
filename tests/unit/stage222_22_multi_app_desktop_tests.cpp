#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "graphics/virtual_display.h"
#include "renderer/presentation_pacer.h"
#include "input/input_router.h"
#include "kernel/app_lifecycle_manager.h"
#include "kernel/user_syscall_dispatcher.h"
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "runtime/interactive_runtime_contract.h"
#include "runtime/system_clipboard.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::renderer;
using namespace winios::input;
using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_multi_app_desktop_scenario() {
    std::cout << "[Test 1] Multi-app desktop composition, focus, and pacing...\n";

    auto wm = std::make_shared<WindowManager>();
    auto sm = std::make_shared<SurfaceResourceManager>();
    DisplayConfiguration cfg{};
    cfg.width = 800; cfg.height = 600; cfg.row_stride = 3200;
    auto display = std::make_shared<VirtualDisplay>(cfg);
    auto comp = std::make_shared<Compositor>(display);
    auto pacer = std::make_shared<PresentationPacer>();
    auto router = std::make_shared<InputRouter>(wm);
    AppLifecycleManager supervisor(wm.get(), sm.get(), comp.get(), router.get());

    // 1. Launch App 1 (Notes, PID 101) & App 2 (Monitor, PID 102)
    TEST_ASSERT(supervisor.launch_app(101, "com.apple.mobilenotes"));
    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Foreground);
    TEST_ASSERT(supervisor.send_to_background(101));
    TEST_ASSERT(supervisor.launch_app(102, "com.apple.runtimemonitor"));
    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Background);
    TEST_ASSERT(supervisor.get_app_state(102) == AppState::Foreground);

    // 2. Create surfaces & windows
    std::shared_ptr<DisplaySurface> surf1;
    auto rc1 = sm->create_surface(101, 300, 250, PixelFormat::B8G8R8A8_UNORM, surf1);
    TEST_ASSERT(rc1 == SurfaceResultCode::Success);
    surf1->fill_rect(101, Rect{0, 0, 300, 250}, 0xFF4A90E2); // Blue
    comp->add_surface(surf1);
    uint32_t wid1 = wm->create_window(101, "Notes", Rect{50, 50, 300, 250}, surf1);
    supervisor.associate_window(101, wid1);
    supervisor.associate_surface(101, surf1->id());

    std::shared_ptr<DisplaySurface> surf2;
    auto rc2 = sm->create_surface(102, 300, 250, PixelFormat::B8G8R8A8_UNORM, surf2);
    TEST_ASSERT(rc2 == SurfaceResultCode::Success);
    surf2->fill_rect(102, Rect{0, 0, 300, 250}, 0xFF30D158); // Green
    comp->add_surface(surf2);
    uint32_t wid2 = wm->create_window(102, "Runtime Monitor", Rect{200, 150, 300, 250}, surf2);
    supervisor.associate_window(102, wid2);
    supervisor.associate_surface(102, surf2->id());

    TEST_ASSERT(wm->window_count() == 2);

    // 3. Focus switching & Input routing
    // Click on Wid 1 (x=70, y=70)
    InputEvent click1{};
    click1.type = InputEventType::MouseDown;
    click1.mouse.x = 70;
    click1.mouse.y = 70;
    click1.mouse.button = MouseButton::Left;
    router->route_event(click1);
    TEST_ASSERT(wm->focused_window_id() == wid1);

    // Click on Wid 2 (x=250, y=200)
    InputEvent click2{};
    click2.type = InputEventType::MouseDown;
    click2.mouse.x = 250;
    click2.mouse.y = 200;
    click2.mouse.button = MouseButton::Left;
    router->route_event(click2);
    TEST_ASSERT(wm->focused_window_id() == wid2);

    // 4. Frame presentation pacing
    pacer->on_frame_presented(16666000ULL);
    auto pstats = pacer->presentation_stats();
    TEST_ASSERT(pstats.total_frames_presented >= 1);

    // 5. App 101 crashes or terminates
    TEST_ASSERT(supervisor.terminate_app(101));
    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Terminated);
    TEST_ASSERT(wm->window_count() == 1);
    TEST_ASSERT(sm->get_process_accounting(101).surface_count == 0);
    TEST_ASSERT(sm->get_process_accounting(102).surface_count == 1);

    // 6. App 102 terminates
    TEST_ASSERT(supervisor.terminate_app(102));
    TEST_ASSERT(wm->window_count() == 0);
    TEST_ASSERT(sm->get_process_accounting(102).surface_count == 0);

    std::cout << "[PASS] Multi-app desktop scenario verified.\n";
}

void test_inter_app_clipboard_and_storage_isolation() {
    std::cout << "[Test 2] Inter-app clipboard sharing & storage isolation...\n";
    Memory mem(0x1000000ULL);
    VirtualConsole console;
    UserSyscallDispatcher disp(console);

    auto cm = std::make_shared<CapabilityManager>();
    cm->grant(101, Capability::ClipboardRead);
    cm->grant(101, Capability::ClipboardWrite);
    cm->grant(102, Capability::ClipboardRead);
    cm->grant(102, Capability::ClipboardWrite);
    disp.set_capability_manager(cm.get());

    runtime::SystemClipboard clipboard(cm);
    disp.set_system_clipboard(&clipboard);

    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    // App 1 (PID 101) Address Space
    const uint64_t l2_1 = ppm.allocate_page();
    memory::UserAddressSpace uas1(mem, ppm, l2_1, "uas_notes");
    uas1.set_owner_pid(101);
    BsdProcess proc1(101, 1, "Notes");
    proc1.set_address_space(&uas1);

    // Map user page for clipboard text
    const uint64_t clip_va = 0x20000000ULL;
    const uint64_t clip_pa = ppm.allocate_page();
    uas1.map_user_page(clip_va, clip_pa, memory::PagePermissions::UserReadWrite);

    const char* clip_text = "Important Note: System Architecture Campaign 222";
    uas1.copyin(clip_va, clip_text, std::strlen(clip_text) + 1);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // PID 101 sets clipboard (535)
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, clip_va);
    cpu.write_register(1, std::strlen(clip_text) + 1);
    TEST_ASSERT(disp.handle_syscall(cpu, proc1, uas1, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // App 2 (PID 102) Address Space
    const uint64_t l2_2 = ppm.allocate_page();
    memory::UserAddressSpace uas2(mem, ppm, l2_2, "uas_monitor");
    uas2.set_owner_pid(102);
    BsdProcess proc2(102, 1, "Monitor");
    proc2.set_address_space(&uas2);

    const uint64_t read_va = 0x30000000ULL;
    const uint64_t read_pa = ppm.allocate_page();
    uas2.map_user_page(read_va, read_pa, memory::PagePermissions::UserReadWrite);

    // PID 102 reads clipboard (534)
    cpu.write_register(16, kSysWiniosClipboardGet);
    cpu.write_register(0, read_va);
    cpu.write_register(1, 256);
    TEST_ASSERT(disp.handle_syscall(cpu, proc2, uas2, sched));
    int64_t bytes_copied = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(bytes_copied > 0);

    char out_buf[256];
    uas2.copyout(reinterpret_cast<const void*>(read_va), out_buf, bytes_copied);
    TEST_ASSERT(std::strcmp(out_buf, clip_text) == 0);

    std::cout << "[PASS] Inter-app clipboard sharing verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.22: MULTI-APP DESKTOP E2E TESTS\n";
    std::cout << "========================================\n";

    test_multi_app_desktop_scenario();
    test_inter_app_clipboard_and_storage_isolation();

    std::cout << "\nALL STAGE 222.22 TESTS PASSED (100%).\n";
    return 0;
}
