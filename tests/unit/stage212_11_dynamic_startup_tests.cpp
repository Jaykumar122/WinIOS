#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "loader/guest_dynamic_loader.h"
#include "loader/macho_user_loader.h"
#include "runtime/userspace_coordinator.h"

using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

void test_dynamic_application_startup() {
    std::cout << "[Test 1] Dynamic Application Startup with libwinios_runtime.dylib..." << std::endl;

    // 1. Get genuine embedded runtime dylib
    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    TEST_CHECK(!runtime_bin.empty(), "Runtime dylib retrieved");

    std::unordered_map<std::string, std::vector<std::uint8_t>> dylib_registry;
    dylib_registry["/usr/lib/libSystem.B.dylib"] = runtime_bin;

    // 2. Use embedded init app (or first app)
    const auto app_bin = MachoUserLoader::get_embedded_init_app();
    TEST_CHECK(!app_bin.empty(), "App binary retrieved");

    // Launch dynamically
    auto result = UserspaceCoordinator::launch_and_run_dynamic(
        app_bin,
        dylib_registry,
        "init_dynamic",
        {"init_dynamic", "arg1"},
        {"PATH=/usr/bin", "TERM=xterm-256color"});

    if (!result.error_message.empty()) {
        std::cout << "  [ERROR MSG]: " << result.error_message << std::endl;
    }

    TEST_CHECK(result.success, "Dynamic application launched and ran successfully");
    TEST_CHECK(result.instructions_retired > 0, "Guest instructions retired at EL0");
    TEST_CHECK(result.syscalls_handled > 0, "Syscalls handled");
    TEST_CHECK(result.exit_code == 0, "Exit code is 0");
    TEST_CHECK(!result.console_output.empty(), "Console output produced by guest application");
    std::cout << "  [INFO] Retired: " << result.instructions_retired
              << " instructions, Syscalls: " << result.syscalls_handled
              << ", Output: '" << result.console_output << "'" << std::endl;
}

void test_dynamic_startup_missing_dependency_rollback() {
    std::cout << "[Test 2] Missing Dependency Rollback During Startup..." << std::endl;

    // Registry is empty, but app requests missing_lib.dylib
    std::unordered_map<std::string, std::vector<std::uint8_t>> empty_registry;

    // Create app Mach-O declaring missing dependency
    std::string dep_name = "libmissing.dylib";
    const std::size_t raw_size = 24 + dep_name.size() + 1;
    const std::uint32_t cmdsize = static_cast<std::uint32_t>((raw_size + 7) & ~7);

    std::vector<std::uint8_t> bin(32 + cmdsize, 0);
    const std::uint32_t magic = 0xFEEDFACFu;
    const std::int32_t cputype = 0x0100000Cu;
    const std::uint32_t filetype = 2u;
    const std::uint32_t ncmds = 1u;
    const std::uint32_t sizeofcmds = cmdsize;
    std::memcpy(bin.data() + 0, &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 12, &filetype, 4);
    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);

    // LC_LOAD_DYLIB for "libmissing.dylib"
    const std::uint32_t cmd = 0x0Cu;
    const std::uint32_t stroff = 24u;
    std::memcpy(bin.data() + 32 + 0, &cmd, 4);
    std::memcpy(bin.data() + 32 + 4, &cmdsize, 4);
    std::memcpy(bin.data() + 32 + 8, &stroff, 4);
    std::memcpy(bin.data() + 32 + 24, dep_name.c_str(), dep_name.size() + 1);

    auto result = UserspaceCoordinator::launch_and_run_dynamic(
        bin,
        empty_registry,
        "failing_app");

    TEST_CHECK(!result.success, "Dynamic launch failed gracefully when dependency is missing");
    TEST_CHECK(!result.error_message.empty(), "Error message indicates missing dependency");
    std::cout << "  [INFO] Error message: " << result.error_message << std::endl;
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.11 Dynamic Application Startup Tests ===" << std::endl;
    test_dynamic_application_startup();
    test_dynamic_startup_missing_dependency_rollback();

    std::cout << "  [DEBUG] g_failures = " << g_failures << std::endl;
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.11: All dynamic application startup tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.11: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
