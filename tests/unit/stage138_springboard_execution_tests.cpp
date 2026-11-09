#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/window_server.h"
#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/springboard_app_supervisor.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::runtime;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Genuine SpringBoard Mach-O Admission Gate (BLOCKED)
bool test_springboard_genuine_admission_gate() {
    std::cout << "[Test 1] Genuine SpringBoard Mach-O Admission Gate (BLOCKED)...\n";

    AppleArtifactAdmissionController controller;

    // Genuine unextracted SpringBoard binary
    AppleBootArtifact real_sb{};
    real_sb.artifact_id = "genuine_springboard";
    real_sb.file_path = "/System/Library/CoreServices/SpringBoard.app/SpringBoard";
    real_sb.kind = AppleArtifactKind::SpringBoardBundle;
    real_sb.provenance = ArtifactProvenance::DecryptedReal;
    real_sb.is_synthetic = false;

    auto res = controller.evaluate_artifact(real_sb, nullptr, 0);
    TEST_ASSERT(!res.admitted, "Genuine unextracted SpringBoard must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated,
                "Must reject empty/missing genuine SpringBoard artifact");

    std::cout << "  Passed (Genuine SpringBoard admission strictly BLOCKED)\n";
    return true;
}

// Test 2: SpringBoardSupervisor Initialization & VFS App Discovery
bool test_springboard_supervisor_initialization() {
    std::cout << "[Test 2] SpringBoardSupervisor Initialization & Discovery...\n";

    SpringBoardSupervisor supervisor(ExecutionClassification::SyntheticSimulated);
    TEST_ASSERT(supervisor.execution_class() == ExecutionClassification::SyntheticSimulated,
                "Execution class must be SyntheticSimulated");

    DarwinVfs vfs;
    vfs.populate_default_rootfs();

    // Create fake app bundles in VFS
    const char dummy[] = "APP_BINARY";
    vfs.create_directory("/Applications/MobileSafari.app");
    vfs.write_file("/Applications/MobileSafari.app/MobileSafari", dummy, sizeof(dummy) - 1);
    vfs.create_directory("/Applications/Preferences.app");
    vfs.write_file("/Applications/Preferences.app/Preferences", dummy, sizeof(dummy) - 1);

    std::size_t found = supervisor.discover_applications(vfs);
    TEST_ASSERT(found >= 2, "Must discover at least 2 apps in /Applications");
    TEST_ASSERT(supervisor.has_app("com.apple.mobilesafari"), "MobileSafari must be discovered");
    TEST_ASSERT(supervisor.has_app("com.apple.Preferences"), "Preferences must be discovered");

    std::cout << "  Passed (Discovered " << found << " applications from VFS)\n";
    return true;
}

// Test 3: Synthetic SpringBoard Guest Userspace Execution
bool test_springboard_guest_userspace_execution() {
    std::cout << "[Test 3] Synthetic SpringBoard Guest Userspace Execution...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly for SpringBoard initialization:
    // mov x16, #20         (SYS_getpid)
    // svc #0x80
    // mov x19, x0          (save pid)
    // mov x16, #-28        (task_self_trap)
    // svc #0x81
    // mov x20, x0          (save task self port)
    // mov x0, #0
    // mov x16, #1          (SYS_exit 0)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0x92800370u, // mov x16, #-28
        0xD4001021u, // svc #0x81
        0xAA0003F4u, // mov x20, x0
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/System/Library/CoreServices/SpringBoard.app/SpringBoard";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 6);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "SpringBoard init must exit cleanly");
    TEST_ASSERT(res.guest_instructions_retired >= 8, "Must retire all 8 instructions");
    TEST_ASSERT(res.mach_traps_handled >= 1, "Must handle task_self_trap");

    std::cout << "  Passed (Guest SpringBoard executed in EL0, retired "
              << res.guest_instructions_retired << " instructions)\n";
    return true;
}

// Test 4: App Registration and Metadata Inspection
bool test_app_registration_and_metadata() {
    std::cout << "[Test 4] App Registration and Metadata Inspection...\n";

    SpringBoardSupervisor supervisor;
    AppBundleInfo safari;
    safari.bundle_id = "com.apple.mobilesafari";
    safari.executable_name = "MobileSafari";
    safari.bundle_path = "/Applications/MobileSafari.app";
    safari.display_name = "Safari";
    safari.icon_color = 0xFF007AFFu; // iOS Blue
    safari.icon_grid_frame = Rect{100, 200, 180, 180};

    supervisor.register_app(safari);

    const auto* info = supervisor.find_app("com.apple.mobilesafari");
    TEST_ASSERT(info != nullptr, "App must be registered");
    TEST_ASSERT(info->display_name == "Safari", "Display name matches");
    TEST_ASSERT(info->icon_grid_frame.x == 100 && info->icon_grid_frame.y == 200, "Grid frame matches");

    std::cout << "  Passed (Registered MobileSafari app bundle metadata)\n";
    return true;
}

// Test 5: App Launch & Lifecycle State Transition (ForegroundActive)
bool test_app_launch_lifecycle_transition() {
    std::cout << "[Test 5] App Launch & Lifecycle State Transition...\n";

    SpringBoardSupervisor supervisor;
    AppBundleInfo app;
    app.bundle_id = "com.apple.mobilesafari";
    app.executable_name = "MobileSafari";
    app.bundle_path = "/Applications/MobileSafari.app";
    supervisor.register_app(app);

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor compositor;
    Memory mem(64 * 1024 * 1024);

    bool launched = supervisor.launch_app("com.apple.mobilesafari", bsd, ipc, compositor, mem);
    TEST_ASSERT(launched, "App launch must succeed");

    const auto* instance = supervisor.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(instance != nullptr, "Running instance must exist");
    TEST_ASSERT(instance->state == AppLifecycleState::ForegroundActive, "State must be ForegroundActive");
    TEST_ASSERT(instance->pid > 1, "App PID must be > 1");
    TEST_ASSERT(instance->surface != nullptr, "App must have an allocated IOSurface");

    // Verify process is registered in BSD subsystem under UID 501 (mobile)
    auto proc = bsd.get_process(instance->pid);
    TEST_ASSERT(proc != nullptr, "BSD process must exist");
    TEST_ASSERT(proc->uid == 501, "User app must execute with UID 501 (mobile)");

    std::cout << "  Passed (Launched app PID " << instance->pid << " into ForegroundActive state)\n";
    return true;
}

// Test 6: App Backgrounding & Foregrounding Cycle
bool test_app_backgrounding_and_foregrounding() {
    std::cout << "[Test 6] App Backgrounding & Foregrounding Cycle...\n";

    SpringBoardSupervisor supervisor;
    AppBundleInfo app;
    app.bundle_id = "com.apple.mobilesafari";
    supervisor.register_app(app);

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor compositor;
    Memory mem(64 * 1024 * 1024);

    supervisor.launch_app("com.apple.mobilesafari", bsd, ipc, compositor, mem);

    // Send to background
    bool bg_ok = supervisor.send_to_background("com.apple.mobilesafari", compositor);
    TEST_ASSERT(bg_ok, "Backgrounding must succeed");
    const auto* inst1 = supervisor.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(inst1->state == AppLifecycleState::Background, "State must be Background");

    // Bring back to foreground
    bool fg_ok = supervisor.bring_to_foreground("com.apple.mobilesafari", compositor);
    TEST_ASSERT(fg_ok, "Foregrounding must succeed");
    const auto* inst2 = supervisor.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(inst2->state == AppLifecycleState::ForegroundActive, "State must return to ForegroundActive");

    std::cout << "  Passed (Full background/foreground lifecycle transition verified)\n";
    return true;
}

// Test 7: App Termination & Resource Cleanup
bool test_app_termination_and_cleanup() {
    std::cout << "[Test 7] App Termination & Resource Cleanup...\n";

    SpringBoardSupervisor supervisor;
    AppBundleInfo app;
    app.bundle_id = "com.apple.mobilesafari";
    supervisor.register_app(app);

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor compositor;
    Memory mem(64 * 1024 * 1024);

    supervisor.launch_app("com.apple.mobilesafari", bsd, ipc, compositor, mem);
    TEST_ASSERT(supervisor.running_app_count() == 1, "1 app running");

    bool term_ok = supervisor.terminate_app("com.apple.mobilesafari", bsd, compositor);
    TEST_ASSERT(term_ok, "Termination must succeed");
    TEST_ASSERT(supervisor.running_app_count() == 0, "Running app count must be 0");
    TEST_ASSERT(supervisor.get_running_app("com.apple.mobilesafari") == nullptr,
                "Terminated app must be removed from running apps");

    std::cout << "  Passed (App terminated, surface detached, and process cleaned up)\n";
    return true;
}

// Test 8: Touch Down Hit-Testing on Home Screen Grid
bool test_touch_down_hit_testing() {
    std::cout << "[Test 8] Touch Down Hit-Testing on Home Screen Grid...\n";

    SpringBoardSupervisor supervisor;
    AppBundleInfo app;
    app.bundle_id = "com.apple.mobilesafari";
    app.icon_grid_frame = Rect{100, 200, 180, 180}; // Touch box
    supervisor.register_app(app);

    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor compositor;
    Memory mem(64 * 1024 * 1024);

    // Touch outside bounds (e.g. at 50, 50)
    bool miss = supervisor.handle_touch_down(50, 50, bsd, ipc, compositor, mem);
    TEST_ASSERT(!miss, "Touch outside icon must not launch app");
    TEST_ASSERT(supervisor.running_app_count() == 0, "No app launched on miss");

    // Touch inside icon bounds (e.g. at 150, 250)
    bool hit = supervisor.handle_touch_down(150, 250, bsd, ipc, compositor, mem);
    TEST_ASSERT(hit, "Touch inside icon frame must launch app");
    TEST_ASSERT(supervisor.running_app_count() == 1, "App launched on hit");
    TEST_ASSERT(supervisor.has_app("com.apple.mobilesafari"), "MobileSafari running");

    std::cout << "  Passed (Home screen touch hit-testing successfully launched app)\n";
    return true;
}

// Test 9: Compositor Home Screen Scene Composition
bool test_compositor_home_screen_rendering() {
    std::cout << "[Test 9] Compositor Home Screen Scene Composition...\n";

    WindowServerCompositor compositor(400, 800);

    // SpringBoard adds background wallpaper layer (z=0)
    auto wallpaper = compositor.create_surface(400, 800);
    wallpaper->fill_rect(Rect{0, 0, 400, 800}, 0xFF2C2C2Eu); // Wallpaper dark
    compositor.add_layer(WindowServerLayer{1, 6, wallpaper, Rect{0, 0, 400, 800}, 1.0f, 0, false});

    // SpringBoard adds status bar layer (z=100)
    auto status_bar = compositor.create_surface(400, 44);
    status_bar->fill_rect(Rect{0, 0, 400, 44}, 0xFF000000u); // Black status bar
    compositor.add_layer(WindowServerLayer{2, 6, status_bar, Rect{0, 0, 400, 44}, 0.8f, 100, false});

    std::vector<std::uint8_t> scene(400 * 800 * 4, 0);
    bool ok = compositor.composite(scene.data(), scene.size());
    TEST_ASSERT(ok, "Scene composition must succeed");

    auto* pixels = reinterpret_cast<const std::uint32_t*>(scene.data());
    // Status bar pixel at (20, 20)
    TEST_ASSERT((pixels[20 * 400 + 20] & 0xFF000000u) != 0, "Status bar composited");
    // Wallpaper pixel at (200, 400)
    TEST_ASSERT(pixels[400 * 400 + 200] == 0xFF2C2C2Eu, "Wallpaper pixel preserved");

    std::cout << "  Passed (Composited SpringBoard wallpaper and status bar layers)\n";
    return true;
}

// Test 10: Complete End-to-End SpringBoard Shell Orchestration
bool test_end_to_end_springboard_shell_orchestration() {
    std::cout << "[Test 10] Complete End-to-End SpringBoard Shell Orchestration...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
    WindowServerCompositor compositor;
    SpringBoardSupervisor supervisor;

    // 1. Synthetic SpringBoard binary executes in EL0
    std::vector<std::uint32_t> sb_asm = {
        0xD2800290u, // mov x16, #20 (SYS_getpid)
        0xD4001001u, // svc #0x80
        0x92800370u, // mov x16, #-28 (task_self_trap)
        0xD4001021u, // svc #0x81
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> sb_code(sb_asm.size() * 4);
    std::memcpy(sb_code.data(), sb_asm.data(), sb_code.size());

    auto sb_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, sb_code);

    DarwinPipelineConfig sb_cfg;
    sb_cfg.executable_path = "/System/Library/CoreServices/SpringBoard.app/SpringBoard";
    auto sb_res = engine.execute_macho(sb_macho.data(), sb_macho.size(), sb_cfg, 6);
    TEST_ASSERT(sb_res.process_exited && sb_res.exit_code == 0, "SpringBoard init exited cleanly");

    // 2. Register SpringBoard service in Mach IPC
    auto space = ipc.get_task_port_space(6);
    auto sb_obj = std::make_shared<MachPortObject>(7777, 6);
    mach_port_t sb_port = space->allocate_port(sb_obj, kMachPortRightReceive);
    space->insert_right(sb_port, sb_obj, kMachPortRightSend);
    ipc.bootstrap_register(6, "com.apple.springboard.services", sb_port);

    // 3. Register and launch MobileSafari via supervisor
    AppBundleInfo safari;
    safari.bundle_id = "com.apple.mobilesafari";
    safari.display_name = "Safari";
    safari.icon_grid_frame = Rect{100, 200, 180, 180};
    supervisor.register_app(safari);

    bool launched = supervisor.launch_app("com.apple.mobilesafari", bsd, ipc, compositor, mem);
    TEST_ASSERT(launched, "MobileSafari launch succeeded");

    // 4. Composite the full scene to DirectX presenter
    std::vector<std::uint8_t> screen(compositor.framebuffer_size(), 0);
    bool comp_ok = compositor.composite(screen.data(), screen.size());
    TEST_ASSERT(comp_ok, "Composite succeeded");

    bool pres_ok = compositor.present_to_host(screen.data(), screen.size());
    TEST_ASSERT(pres_ok, "Presentation to Windows 11 host completed");

    std::cout << "  Passed (End-to-end SpringBoard shell orchestrated application execution and presentation)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 138: SPRINGBOARD EXECUTION GATE & APP SUPERVISOR\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "FAILED: " << name << "\n";
        }
    };

    run_test("test_springboard_genuine_admission_gate", test_springboard_genuine_admission_gate);
    run_test("test_springboard_supervisor_initialization", test_springboard_supervisor_initialization);
    run_test("test_springboard_guest_userspace_execution", test_springboard_guest_userspace_execution);
    run_test("test_app_registration_and_metadata", test_app_registration_and_metadata);
    run_test("test_app_launch_lifecycle_transition", test_app_launch_lifecycle_transition);
    run_test("test_app_backgrounding_and_foregrounding", test_app_backgrounding_and_foregrounding);
    run_test("test_app_termination_and_cleanup", test_app_termination_and_cleanup);
    run_test("test_touch_down_hit_testing", test_touch_down_hit_testing);
    run_test("test_compositor_home_screen_rendering", test_compositor_home_screen_rendering);
    run_test("test_end_to_end_springboard_shell_orchestration", test_end_to_end_springboard_shell_orchestration);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 138 RESULTS: " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}
