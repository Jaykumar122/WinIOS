#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "kernel/app_supervisor.h"
#include "kernel/app_lifecycle_manager.h"
#include "security/capability_manager.h"
#include "packaging/app_registry.h"
#include "graphics/window_manager.h"
#include "graphics/compositor.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::security;
using namespace winios::packaging;
using namespace winios::graphics;

void test_supervisor_process_tracking_and_termination() {
    std::cout << "[Test 1] Supervisor Process Tracking & Graceful Termination...\n";

    AppLifecycleManager lm;
    CapabilityManager cm;
    ApplicationRegistry reg;
    WindowManager wm;
    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 300;
    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor comp(disp);

    AppSupervisor supervisor(&lm, &cm, &reg, &wm, &comp);

    supervisor.register_process(101, "com.winios.calc");
    supervisor.register_process(102, "com.winios.notes");

    TEST_ASSERT(supervisor.running_count() == 2);
    TEST_ASSERT(supervisor.is_running("com.winios.calc"));
    TEST_ASSERT(supervisor.is_running("com.winios.notes"));
    TEST_ASSERT(supervisor.find_pid("com.winios.calc").value_or(0) == 101u);

    // Create a window owned by PID 101
    Rect frame{ 10, 10, 200, 200 };
    uint32_t wid = wm.create_window(101, "Calc", frame, nullptr);
    TEST_ASSERT(wid > 0);
    TEST_ASSERT(wm.window_count() == 1);

    // Request termination of PID 101 -> cleans up window and unregisters!
    TEST_ASSERT(supervisor.request_termination(101));
    TEST_ASSERT(!supervisor.is_running("com.winios.calc"));
    TEST_ASSERT(wm.window_count() == 0); // Window auto-cleaned!
    TEST_ASSERT(supervisor.running_count() == 1);

    std::cout << "[PASS] Process tracking and graceful termination verified.\n";
}

void test_crash_recording_and_diagnostics() {
    std::cout << "[Test 2] Crash Recording & Failure Taxonomy...\n";

    AppLifecycleManager lm;
    CapabilityManager cm;
    ApplicationRegistry reg;
    WindowManager wm;
    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 300;
    auto disp = std::make_shared<VirtualDisplay>(cfg);
    Compositor comp(disp);

    AppSupervisor supervisor(&lm, &cm, &reg, &wm, &comp);

    supervisor.register_process(201, "com.winios.faulty");

    // Record crash: MemoryFault at PC 0x10004000, 5023 retired instructions
    supervisor.record_crash(201, CrashCategory::MemoryFault, 139, 0x10004000ULL, 5023ULL);

    TEST_ASSERT(supervisor.total_crashes() == 1);
    auto crashes = supervisor.get_recent_crashes(10);
    TEST_ASSERT(crashes.size() == 1);
    TEST_ASSERT(crashes[0].pid == 201);
    TEST_ASSERT(crashes[0].bundle_id == "com.winios.faulty");
    TEST_ASSERT(crashes[0].category == CrashCategory::MemoryFault);
    TEST_ASSERT(crashes[0].exit_code == 139);
    TEST_ASSERT(crashes[0].guest_pc == 0x10004000ULL);
    TEST_ASSERT(crashes[0].instructions_retired == 5023ULL);
    TEST_ASSERT(crashes[0].resources_cleaned_up);

    std::cout << "[PASS] Crash recording and failure taxonomy verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.15: SUPERVISOR & CRASH TESTS\n";
    std::cout << "========================================\n";

    test_supervisor_process_tracking_and_termination();
    test_crash_recording_and_diagnostics();

    std::cout << "\nALL STAGE 224.15 TESTS PASSED (100%).\n";
    return 0;
}
