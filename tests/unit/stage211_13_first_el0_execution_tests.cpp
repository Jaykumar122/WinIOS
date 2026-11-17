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

void test_first_userspace_macho_execution() {
    std::cout << "[Test 1] Stage 211.13: First Genuine ARM64 Mach-O EL0 Userspace Execution...\n";

    const auto bin = MachoUserLoader::get_embedded_first_app();
    TEST_CHECK(!bin.empty(), "Embedded first app Mach-O binary loaded");

    auto res = UserspaceCoordinator::launch_and_run(bin, "first_app", {"first_app", "--run"});

    TEST_CHECK(res.success, "Userspace launch succeeded: " + res.error_message);
    TEST_CHECK(res.pid == 1, "Process assigned PID 1");
    TEST_CHECK(res.exit_code == 0, "Process exited cleanly with status 0");
    TEST_CHECK(res.instructions_retired >= 8, "Retired at least 8 guest instructions");
    TEST_CHECK(res.syscalls_handled >= 3, "Handled at least 3 syscalls (write, getpid, exit)");

    // Observable proof of userspace execution via virtual console output
    TEST_CHECK(res.console_output.find("Hello from WinIOS EL0 Userspace!\n") != std::string::npos,
               "Virtual console captured expected output from userspace");

    // Check trace steps for EL0 execution
    bool saw_el0 = false;
    bool saw_syscall_entry = false;
    for (const auto& step : res.trace) {
        if (step.current_el == 0) saw_el0 = true;
        if (step.is_syscall) saw_syscall_entry = true;
    }
    TEST_CHECK(saw_el0, "Execution trace confirms instructions retired at EL0");
    TEST_CHECK(saw_syscall_entry, "Execution trace confirms SVC syscall entry");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.13 First EL0 Execution Tests ===\n";
    test_first_userspace_macho_execution();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.13: All first EL0 execution tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.13: " << g_failures << " tests failed.\n";
        return 1;
    }
}
