// ============================================================================
// Stage 214.22: Multi-Application Shell and 32-Vector Adversarial Security Suite
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "graphics/virtual_display.h"
#include "input/input_router.h"
#include "kernel/app_lifecycle_manager.h"
#include "kernel/app_preferences.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/app_package.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/ui/view_group.h"
#include "runtime/ui/event_dispatcher.h"
#include "runtime/ui/controls.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime::ui;

namespace {

int g_failures = 0;
int g_passed_vectors = 0;

#define ADV_TEST(cond, id, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ADV_FAIL] Vector " << (id) << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [ADV_OK] Vector " << (id) << ": " << (msg) << "\n"; \
            ++g_passed_vectors; \
        } \
    } while (0)

void run_adversarial_suite() {
    std::cout << "[Stage 214.22] Executing 32 Adversarial Security Vectors...\n";

    // Setup Kernel and Graphics Subsystems
    Memory mem(0x2000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* procShell = proc_mgr.create_process("shell", 1);
    auto* procCounter = proc_mgr.create_process("counter", 2);
    auto* procNotes = proc_mgr.create_process("notes", 3);
    (void)procNotes;

    const std::uint64_t root_l2_A = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_A, "test_vm");

    CPU cpu;
    cpu.reset();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    DisplayConfiguration disp_cfg{};
    disp_cfg.width = 800;
    disp_cfg.height = 1200;
    disp_cfg.row_stride = 800 * 4;
    disp_cfg.pixel_format = PixelFormat::B8G8R8A8_UNORM;
    auto virtual_display = std::make_shared<VirtualDisplay>(disp_cfg);
    auto compositor = std::make_shared<Compositor>(virtual_display);
    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    auto window_mgr = std::make_shared<WindowManager>();
    auto input_router = std::make_unique<InputRouter>(window_mgr);

    disp.set_surface_resource_manager(surface_mgr.get());
    disp.set_window_manager(window_mgr.get());
    disp.set_compositor(compositor.get());
    disp.set_input_router(input_router.get());

    AppLifecycleManager lifecycle_mgr(window_mgr.get(), surface_mgr.get(), compositor.get(), input_router.get());
    auto& pref_mgr = AppPreferencesManager::instance();
    pref_mgr.reset();

    // Setup initial Shell Window (PID 1)
    std::shared_ptr<DisplaySurface> shell_surf;
    surface_mgr->create_surface(1, 800, 1200, PixelFormat::B8G8R8A8_UNORM, shell_surf);
    std::uint32_t shell_wid = window_mgr->create_window(1, "Shell", {0, 0, 800, 1200}, shell_surf);

    // Vector 1: Cross-process window destruction (PID 2 tries to destroy PID 1 window)
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, shell_wid);
    disp.handle_syscall(cpu, *procCounter, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 1, "Cross-process window destroy rejected");

    // Vector 2: Cross-process surface destruction
    auto res_s2 = surface_mgr->destroy_surface(procCounter->pid(), shell_surf->id());
    ADV_TEST(res_s2 == SurfaceResultCode::AccessDenied, 2, "Cross-process surface destroy rejected");

    // Vector 3: Cross-process surface pixel copy
    std::vector<std::uint8_t> dummy_pixels(800 * 1200 * 4, 0);
    auto res_s3 = shell_surf->copy_pixels(procCounter->pid(), dummy_pixels.data(), dummy_pixels.size());
    ADV_TEST(res_s3 == SurfaceResultCode::AccessDenied, 3, "Cross-process surface pixel copy rejected");

    // Vector 4: Cross-process preferences read
    pref_mgr.set_string("com.winios.notes", "secret_key", "top_secret");
    auto res_p4 = pref_mgr.get_string("com.winios.counter", "secret_key");
    ADV_TEST(!res_p4.has_value(), 4, "Cross-process preferences read returns nullopt");

    // Vector 5: Cross-process preferences write
    pref_mgr.set_int("com.winios.counter", "count", 55);
    ADV_TEST(!pref_mgr.has_key("com.winios.notes", "count"), 5, "Counter preference write isolated from Notes");

    // Vector 6: Unknown syscall ID 999 rejected with ENOSYS
    cpu.write_register(16, 999);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS), 6, "Syscall 999 rejected with ENOSYS");

    // Vector 7: Out-of-bounds Category C syscall rejected with ENOSYS
    cpu.write_register(16, kSysWiniosMaxCategoryC + 1);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS), 7, "Syscall out of bounds rejected with ENOSYS");

    // Vector 8: Window create with w=0 rejected with EINVAL
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0); cpu.write_register(1, 0);
    cpu.write_register(2, 10); cpu.write_register(3, 10);
    cpu.write_register(4, 0);  // w=0
    cpu.write_register(5, 100);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL), 8, "Window create w=0 rejected with EINVAL");

    // Vector 9: Window create with h=0 rejected with EINVAL
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0); cpu.write_register(1, 0);
    cpu.write_register(2, 10); cpu.write_register(3, 10);
    cpu.write_register(4, 100); cpu.write_register(5, 0); // h=0
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL), 9, "Window create h=0 rejected with EINVAL");

    // Vector 10: Window create with invalid title pointer
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0xFFFFFFFFFF000000ULL); // invalid VA
    cpu.write_register(1, 50);
    cpu.write_register(2, 10); cpu.write_register(3, 10);
    cpu.write_register(4, 100); cpu.write_register(5, 100);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) > 0, 10, "Window create safely falls back to default title on unmapped title VA");

    // Vector 11: Surface update with unmapped buffer pointer (EFAULT)
    cpu.write_register(16, kSysWiniosSurfaceUpdate);
    cpu.write_register(0, shell_wid);
    cpu.write_register(1, 0xFFFFFFFFFF000000ULL); // unmapped VA
    cpu.write_register(2, 800 * 1200 * 4);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT), 11, "Surface update unmapped buffer rejected with EFAULT");

    // Vector 12: Surface update with truncated buffer size (EINVAL)
    cpu.write_register(16, kSysWiniosSurfaceUpdate);
    cpu.write_register(0, shell_wid);
    cpu.write_register(1, 0x1000);
    cpu.write_register(2, 100); // Truncated size
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL), 12, "Surface update truncated buffer size rejected with EINVAL");

    // Vector 13: Surface update on non-existent window ID (EBADF)
    cpu.write_register(16, kSysWiniosSurfaceUpdate);
    cpu.write_register(0, 9999);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 13, "Surface update invalid window ID rejected with EBADF");

    // Vector 14: Surface fill on non-existent window ID (EBADF)
    cpu.write_register(16, kSysWiniosSurfaceFill);
    cpu.write_register(0, 9999);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 14, "Surface fill invalid window ID rejected with EBADF");

    // Vector 15: Poll event with buffer size < 32 bytes (EINVAL)
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, 0x1000);
    cpu.write_register(1, 16); // Too small
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL), 15, "Poll event undersized buffer rejected with EINVAL");

    // Vector 16: Poll event with unmapped buffer pointer (EFAULT)
    InputEvent evt16{};
    evt16.type = InputEventType::TouchBegin;
    evt16.device_id = 1;
    evt16.touch = {0, 100, 100, 1.0f, true};
    input_router->route_event(evt16);
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, 0xFFFFFFFFFF000000ULL);
    cpu.write_register(1, 32);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT), 16, "Poll event unmapped buffer rejected with EFAULT");

    // Vector 17: Process tries to destroy window 0 (EBADF)
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, 0);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 17, "Window destroy ID 0 rejected with EBADF");

    // Vector 18: Process tries to destroy window already destroyed
    std::shared_ptr<DisplaySurface> temp_surf;
    surface_mgr->create_surface(1, 100, 100, PixelFormat::B8G8R8A8_UNORM, temp_surf);
    std::uint32_t temp_wid = window_mgr->create_window(1, "Temp", {0, 0, 100, 100}, temp_surf);
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, temp_wid);
    disp.handle_syscall(cpu, *procShell, uas, sched); // Destroy once (success)
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, temp_wid);
    disp.handle_syscall(cpu, *procShell, uas, sched); // Destroy twice (fails)
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 18, "Double window destroy rejected with EBADF");

    // Vector 19: Set window title on non-existent window (EBADF)
    cpu.write_register(16, kSysWiniosSetWindowTitle);
    cpu.write_register(0, 9999);
    disp.handle_syscall(cpu, *procShell, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 19, "Set window title on bad ID rejected with EBADF");

    // Vector 20: Cross-process set window title
    cpu.write_register(16, kSysWiniosSetWindowTitle);
    cpu.write_register(0, shell_wid);
    disp.handle_syscall(cpu, *procCounter, uas, sched);
    ADV_TEST(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF), 20, "Cross-process set window title rejected with EBADF");

    // Vector 21: Package manifest with empty app_id rejected
    AppManifest m21{};
    m21.app_id = "";
    ADV_TEST(AppPackageValidator::validate_manifest(m21) == PackageValidationResult::InvalidAppId, 21, "Empty app_id rejected");

    // Vector 22: Package manifest with illegal app_id rejected
    AppManifest m22{};
    m22.app_id = "bad$name*app";
    ADV_TEST(AppPackageValidator::validate_manifest(m22) == PackageValidationResult::InvalidAppId, 22, "Illegal app_id chars rejected");

    // Vector 23: Package manifest with missing display name rejected
    AppManifest m23{};
    m23.app_id = "com.winios.test";
    m23.display_name = "";
    ADV_TEST(AppPackageValidator::validate_manifest(m23) == PackageValidationResult::MissingRequiredField, 23, "Missing display name rejected");

    // Vector 24: Package manifest requesting excessive memory rejected
    AppManifest m24{};
    m24.app_id = "com.winios.test";
    m24.display_name = "Test";
    m24.binary_path = "bin";
    m24.max_memory_bytes = 256 * 1024 * 1024;
    ADV_TEST(AppPackageValidator::validate_manifest(m24) == PackageValidationResult::ResourceBudgetExceeded, 24, "Excessive memory budget rejected");

    // Vector 25: Package manifest requesting excessive surfaces rejected
    AppManifest m25 = m24;
    m25.max_memory_bytes = 1024 * 1024;
    m25.max_surfaces = 64;
    ADV_TEST(AppPackageValidator::validate_manifest(m25) == PackageValidationResult::ResourceBudgetExceeded, 25, "Excessive surface count rejected");

    // Vector 26: Package manifest with tampered binary hash rejected
    AppManifest m26 = m25;
    m26.max_surfaces = 4;
    m26.binary_sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    std::uint8_t payload[4] = {1, 2, 3, 4};
    ADV_TEST(AppPackageValidator::validate_binary_hash(m26, payload, 4) == PackageValidationResult::BinaryHashMismatch, 26, "Tampered binary hash rejected");

    // Vector 27: Lifecycle transition from Created to Resuming rejected
    lifecycle_mgr.register_app(10, "com.app10");
    ADV_TEST(!lifecycle_mgr.transition_to(10, AppState::Resuming), 27, "Invalid transition Created -> Resuming rejected");

    // Vector 28: Lifecycle transition from Terminated to Foreground rejected
    lifecycle_mgr.transition_to(10, AppState::Terminating);
    lifecycle_mgr.transition_to(10, AppState::Terminated);
    ADV_TEST(!lifecycle_mgr.transition_to(10, AppState::Foreground), 28, "Invalid transition Terminated -> Foreground rejected");

    // Vector 29: Lifecycle transition from Suspended to Starting rejected
    lifecycle_mgr.register_app(11, "com.app11");
    lifecycle_mgr.transition_to(11, AppState::Starting);
    lifecycle_mgr.transition_to(11, AppState::Foreground);
    lifecycle_mgr.transition_to(11, AppState::Background);
    lifecycle_mgr.transition_to(11, AppState::Suspended);
    ADV_TEST(!lifecycle_mgr.transition_to(11, AppState::Starting), 29, "Invalid transition Suspended -> Starting rejected");

    // Vector 30: Hierarchy cycle: View added to itself rejected
    auto vg = std::make_shared<ViewGroup>(100, 1);
    ADV_TEST(!vg->add_child(vg), 30, "Hierarchy cycle self-addition rejected");

    // Vector 31: Hierarchy cycle: View added to descendant rejected
    auto child = std::make_shared<ViewGroup>(101, 1);
    vg->add_child(child);
    ADV_TEST(!child->add_child(vg), 31, "Hierarchy cycle descendant-addition rejected");

    // Vector 32: Premature process exit during active touch capture does not crash dispatcher
    vg->set_frame({0, 0, 800, 1200});
    child->set_frame({0, 0, 800, 1200});
    EventDispatcher dispatcher(vg.get());
    auto btn = std::make_shared<Button>("Test", 102, 1);
    btn->set_frame({10, 10, 100, 40});
    child->add_child(btn);
    dispatcher.dispatch_touch_down(15, 15);
    ADV_TEST(dispatcher.captured_view() == btn.get(), 32, "Touch captured; process exits safely and clears capture");
    dispatcher.on_view_destroyed(btn.get());
    ADV_TEST(dispatcher.captured_view() == nullptr, 32, "Capture cleanly cleared on destroyed view");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.22: Adversarial Security Suite    \n";
    std::cout << "====================================================\n";

    run_adversarial_suite();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL 32 ADVERSARIAL VECTORS PASSED" : "ADVERSARIAL FAILURES DETECTED")
              << " (" << g_passed_vectors << "/32 passed, " << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
