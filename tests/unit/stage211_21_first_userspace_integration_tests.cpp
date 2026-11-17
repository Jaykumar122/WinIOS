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

void test_full_userspace_integration_pipeline() {
    std::cout << "[Test 1] Stage 211.21: End-to-End Userspace Launch, Syscall Loop & Teardown...\n";

    // Run First App
    const auto bin1 = MachoUserLoader::get_embedded_first_app();
    auto res1 = UserspaceCoordinator::launch_and_run(bin1, "first_app", {"first_app"});
    TEST_CHECK(res1.success, "First app launch succeeded");
    TEST_CHECK(res1.pid == 1, "First app was PID 1");
    TEST_CHECK(res1.exit_code == 0, "First app exit code 0");
    TEST_CHECK(res1.console_output.find("Hello from WinIOS EL0 Userspace!\n") != std::string::npos, "First app console output bit-exact");

    // Run Init App
    const auto bin2 = MachoUserLoader::get_embedded_init_app();
    auto res2 = UserspaceCoordinator::launch_and_run(bin2, "init_app", {"init_app"});
    TEST_CHECK(res2.success, "Init app launch succeeded");
    TEST_CHECK(res2.pid == 1, "Init app was PID 1");
    TEST_CHECK(res2.exit_code == 0, "Init app exit code 0");
    TEST_CHECK(res2.syscalls_handled >= 5, "Init app handled all 5 syscalls");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.21 Userspace Integration Tests ===\n";
    test_full_userspace_integration_pipeline();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.21: All userspace integration tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.21: " << g_failures << " tests failed.\n";
        return 1;
    }
}
