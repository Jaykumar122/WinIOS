#include <iostream>
#include <vector>
#include <cstdint>

#include "loader/macho_user_loader.h"
#include "runtime/userspace_coordinator.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_minimal_init_like_process() {
    std::cout << "[Test 1] Stage 211.20: Minimal Init-Like Process (PID 1) Launch & Execution...\n";

    const auto bin = MachoUserLoader::get_embedded_init_app();
    TEST_CHECK(!bin.empty(), "Embedded user_init_app.macho binary loaded");

    auto res = UserspaceCoordinator::launch_and_run(bin, "launchd", {"/sbin/launchd"});

    TEST_CHECK(res.success, "Init process launch succeeded: " + res.error_message);
    TEST_CHECK(res.pid == 1, "Init process assigned root PID 1");
    TEST_CHECK(res.exit_code == 0, "Init process exited cleanly with status 0");
    TEST_CHECK(res.instructions_retired >= 10, "Retired at least 10 instructions at EL0");
    TEST_CHECK(res.syscalls_handled >= 5, "Handled at least 5 syscalls (write, getpid, yield, write, exit)");

    // Check console output for both banners
    const std::string expected_banner = "[INIT] WinIOS userland init started (PID 1)\n";
    const std::string expected_done = "[INIT] Init process completed successfully";

    TEST_CHECK(res.console_output.find(expected_banner) != std::string::npos,
               "Console output contains startup banner");
    TEST_CHECK(res.console_output.find(expected_done) != std::string::npos,
               "Console output contains completion banner");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.20 Minimal Init Process Tests ===\n";
    test_minimal_init_like_process();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.20: All minimal init process tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.20: " << g_failures << " tests failed.\n";
        return 1;
    }
}
