#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "graphics/window_server.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/springboard_app_supervisor.h"

using namespace winios::runtime;
using namespace winios::graphics;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

bool test_installed_applications_catalog() {
    std::cout << "[Test 1] Installed iOS Applications Catalog...\n";

    SpringBoardSupervisor sb;
    TEST_ASSERT(sb.has_app("com.apple.mobilesafari"), "MobileSafari must be registered");
    TEST_ASSERT(sb.has_app("com.apple.Preferences"), "Settings must be registered");
    TEST_ASSERT(sb.has_app("com.apple.camera"), "Camera must be registered");
    TEST_ASSERT(sb.has_app("com.apple.MobileStore"), "App Store must be registered");

    const auto* safari = sb.find_app("com.apple.mobilesafari");
    TEST_ASSERT(safari != nullptr, "Safari app info lookup");
    TEST_ASSERT(safari->display_name == "Safari", "Display name match");
    TEST_ASSERT(safari->bundle_path == "/Applications/MobileSafari.app/MobileSafari", "Bundle path match");

    std::cout << "  Passed (Discovered 4 standard iOS application bundles)\n";
    return true;
}

bool test_execution_classification_truthfulness() {
    std::cout << "[Test 2] Architectural Truthfulness & Classification...\n";

    SpringBoardSupervisor sb(ExecutionClassification::SyntheticSimulated);
    TEST_ASSERT(sb.execution_class() == ExecutionClassification::SyntheticSimulated,
                "Test harness must report SyntheticSimulated execution");

    std::cout << "  Passed (Strictly classified as " 
              << execution_classification_string(sb.execution_class()) << ")\n";
    return true;
}

bool test_application_launch_and_frontboard_scene() {
    std::cout << "[Test 3] Application Launch & FrontBoard Scene Creation...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    bool ok_launch = sb.launch_app("com.apple.mobilesafari", bsd, ipc, comp, mem);
    TEST_ASSERT(ok_launch, "launch_app must succeed for MobileSafari");

    const auto* inst = sb.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(inst != nullptr, "Running app lookup");
    TEST_ASSERT(inst->state == AppLifecycleState::ForegroundActive, "State must be ForegroundActive");
    TEST_ASSERT(inst->pid > 1, "Spawned app must have PID > 1");
    TEST_ASSERT(inst->surface != nullptr, "App window surface must be allocated");

    auto proc = bsd.get_process(inst->pid);
    TEST_ASSERT(proc != nullptr, "BSD process lookup");
    TEST_ASSERT(proc->uid == 501, "Application must execute under mobile user (UID 501)");

    std::cout << "  Passed (Safari launched in ForegroundActive with PID " << inst->pid << ")\n";
    return true;
}

bool test_mach_bootstrap_registration_per_app() {
    std::cout << "[Test 4] Per-App Mach Bootstrap Registration...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    sb.launch_app("com.apple.camera", bsd, ipc, comp, mem);

    // Look up Camera Mach port from client space
    mach_port_t cam_port = ipc.bootstrap_look_up(100, "com.apple.camera");
    TEST_ASSERT(cam_port != kMachPortNull, "Bootstrap lookup for Camera port must succeed");

    std::cout << "  Passed (Camera registered Mach IPC port 0x" << std::hex << cam_port << std::dec << ")\n";
    return true;
}

bool test_home_screen_touch_event_dispatch() {
    std::cout << "[Test 5] Touch Event Hit Testing (Home Screen Tap)...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    // Safari icon frame is Rect{100, 300, 180, 180}. Tap at (150, 350)
    bool handled = sb.handle_touch_down(150, 350, bsd, ipc, comp, mem);
    TEST_ASSERT(handled, "handle_touch_down must hit Safari icon and launch app");

    const auto* inst = sb.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(inst != nullptr && inst->state == AppLifecycleState::ForegroundActive, "Safari running");

    // Tap outside any icon (e.g. at (10, 10))
    bool missed = sb.handle_touch_down(10, 10, bsd, ipc, comp, mem);
    TEST_ASSERT(!missed, "Tap outside icon grid must return false");

    std::cout << "  Passed (Tapped icon at (150, 350) -> launched MobileSafari)\n";
    return true;
}

bool test_background_transition() {
    std::cout << "[Test 6] FrontBoard Background Lifecycle Transition...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    sb.launch_app("com.apple.mobilesafari", bsd, ipc, comp, mem);
    bool ok_bg = sb.send_to_background("com.apple.mobilesafari", comp);
    TEST_ASSERT(ok_bg, "send_to_background must succeed");

    const auto* inst = sb.get_running_app("com.apple.mobilesafari");
    TEST_ASSERT(inst->state == AppLifecycleState::Background, "State must transition to Background");

    std::cout << "  Passed (Transitioned Safari to Background)\n";
    return true;
}

bool test_foreground_resume() {
    std::cout << "[Test 7] FrontBoard Foreground Resume Transition...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    sb.launch_app("com.apple.Preferences", bsd, ipc, comp, mem);
    sb.send_to_background("com.apple.Preferences", comp);

    bool ok_fg = sb.bring_to_foreground("com.apple.Preferences", comp);
    TEST_ASSERT(ok_fg, "bring_to_foreground must succeed");

    const auto* inst = sb.get_running_app("com.apple.Preferences");
    TEST_ASSERT(inst->state == AppLifecycleState::ForegroundActive, "State must resume to ForegroundActive");

    std::cout << "  Passed (Resumed Settings to ForegroundActive)\n";
    return true;
}

bool test_application_termination() {
    std::cout << "[Test 8] Clean Application Process Termination...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    sb.launch_app("com.apple.mobilesafari", bsd, ipc, comp, mem);
    TEST_ASSERT(sb.running_app_count() == 1, "1 app running");

    bool ok_term = sb.terminate_app("com.apple.mobilesafari", bsd, comp);
    TEST_ASSERT(ok_term, "terminate_app must succeed");
    TEST_ASSERT(sb.running_app_count() == 0, "0 apps running");
    TEST_ASSERT(sb.get_running_app("com.apple.mobilesafari") == nullptr, "App removed from running list");

    std::cout << "  Passed (Terminated Safari and cleaned up window layer)\n";
    return true;
}

bool test_multiple_concurrent_apps() {
    std::cout << "[Test 9] Multi-Tasking Concurrent Application Execution...\n";

    SpringBoardSupervisor sb;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    WindowServerCompositor comp;
    Memory mem(0x10000);

    sb.launch_app("com.apple.mobilesafari", bsd, ipc, comp, mem);
    sb.launch_app("com.apple.camera", bsd, ipc, comp, mem);

    TEST_ASSERT(sb.running_app_count() == 2, "2 apps must be running");

    const auto* safari = sb.get_running_app("com.apple.mobilesafari");
    const auto* camera = sb.get_running_app("com.apple.camera");

    TEST_ASSERT(safari != nullptr && camera != nullptr, "Both app instances must exist");
    TEST_ASSERT(safari->pid != camera->pid, "PIDs must be distinct");
    TEST_ASSERT(safari->layer_id != camera->layer_id, "Layer IDs must be distinct");

    std::cout << "  Passed (Concurrent Safari PID " << safari->pid << " & Camera PID " << camera->pid << ")\n";
    return true;
}

bool test_genuine_vs_synthetic_classification_matrix() {
    std::cout << "[Test 10] Execution Classification Matrix Truthfulness...\n";

    SpringBoardSupervisor sb_synth(ExecutionClassification::SyntheticSimulated);
    SpringBoardSupervisor sb_blocked(ExecutionClassification::GenuineEncrypted_Blocked);
    SpringBoardSupervisor sb_genuine(ExecutionClassification::GenuineIOS);

    TEST_ASSERT(sb_synth.execution_class() == ExecutionClassification::SyntheticSimulated, "Synthetic");
    TEST_ASSERT(sb_blocked.execution_class() == ExecutionClassification::GenuineEncrypted_Blocked, "Blocked");
    TEST_ASSERT(sb_genuine.execution_class() == ExecutionClassification::GenuineIOS, "Genuine");

    std::cout << "  Passed (Verified 3 execution classifications: "
              << execution_classification_string(sb_synth.execution_class()) << ", "
              << execution_classification_string(sb_blocked.execution_class()) << ", "
              << execution_classification_string(sb_genuine.execution_class()) << ")\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 128: SPRINGBOARD & NATIVE APP EXECUTION TESTS     \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: SpringBoard User Shell & FrontBoard App Execution      \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_installed_applications_catalog()) passed++;
    if (test_execution_classification_truthfulness()) passed++;
    if (test_application_launch_and_frontboard_scene()) passed++;
    if (test_mach_bootstrap_registration_per_app()) passed++;
    if (test_home_screen_touch_event_dispatch()) passed++;
    if (test_background_transition()) passed++;
    if (test_foreground_resume()) passed++;
    if (test_application_termination()) passed++;
    if (test_multiple_concurrent_apps()) passed++;
    if (test_genuine_vs_synthetic_classification_matrix()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 128 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
