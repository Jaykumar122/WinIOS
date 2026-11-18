#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "services/diagnostics_service.h"
#include "kernel/app_supervisor.h"
#include "packaging/app_registry.h"
#include "security/capability_manager.h"
#include "graphics/window_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::services;
using namespace winios::kernel;
using namespace winios::packaging;
using namespace winios::security;
using namespace winios::graphics;

void test_structured_logging_and_ring_buffer() {
    std::cout << "[Test 1] Structured Leveled Logging & Ring Buffer Bounds...\n";

    DiagnosticsService diag;

    diag.log("com.winios.calc", 10, LogLevel::Info, "Calculator starting");
    diag.log("com.winios.calc", 10, LogLevel::Warning, "High memory load");
    diag.log("com.winios.calc", 10, LogLevel::Error, "Division by zero handled");

    TEST_ASSERT(diag.total_log_count() == 3);

    auto all_logs = diag.get_logs(10, LogLevel::Debug);
    TEST_ASSERT(all_logs.size() == 3);

    // Filter by warning or higher
    auto warn_logs = diag.get_logs(10, LogLevel::Warning);
    TEST_ASSERT(warn_logs.size() == 2);

    // Verify ring buffer capping: emit 600 logs -> only 512 retained
    for (int i = 0; i < 600; ++i) {
        diag.log("com.winios.flood", 20, LogLevel::Debug, "Burst message");
    }
    auto flood_logs = diag.get_logs(1000, LogLevel::Debug);
    TEST_ASSERT(flood_logs.size() == DiagnosticsService::kMaxLogCapacity);

    std::cout << "[PASS] Structured logging and ring buffer verified.\n";
}

void test_diagnostics_summary_metrics() {
    std::cout << "[Test 2] Platform Diagnostics Summary Metrics...\n";

    AppLifecycleManager lm;
    CapabilityManager cm;
    ApplicationRegistry reg;
    WindowManager wm;
    AppSupervisor supervisor(&lm, &cm, &reg);

    DiagnosticsService diag(&supervisor, &reg, &wm, &cm);

    // Register an app in registry
    AppRegistration app{};
    app.bundle_id = "com.winios.test";
    app.name = "Test";
    app.exec_path = "bin/test";
    reg.register_app(app);

    // Register running process
    supervisor.register_process(50, "com.winios.test");

    auto summary = diag.get_summary();
    TEST_ASSERT(summary.installed_apps == 1);
    TEST_ASSERT(summary.active_processes == 1);
    TEST_ASSERT(summary.total_crashes == 0);

    std::cout << "[PASS] Diagnostics summary metrics verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.18: DIAGNOSTICS & LOGGING TESTS\n";
    std::cout << "========================================\n";

    test_structured_logging_and_ring_buffer();
    test_diagnostics_summary_metrics();

    std::cout << "\nALL STAGE 224.18 TESTS PASSED (100%).\n";
    return 0;
}
