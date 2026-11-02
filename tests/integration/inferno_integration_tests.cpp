// Opt-in integration checks against a locally built ChefKiss Inferno emulator.
//
// The normal test suite must not depend on an Inferno checkout, so this test
// reports CTest's skip return code (77) unless both WINIOS_INFERNO_EXE and
// WINIOS_INFERNO_RUNTIME point at an existing local build. It never boots a
// guest, never loads firmware and never writes to the environment.
#include "inferno/inferno_launcher.h"
#include "test_harness.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int kSkipReturnCode = 77;

std::filesystem::path path_from_utf8(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

struct LocalInferno {
    std::filesystem::path executable;
    std::filesystem::path runtime_directory;
    bool available{false};
    std::string reason;
};

LocalInferno discover_local_inferno() {
    LocalInferno local;
    const std::vector<std::string> environment = read_parent_environment();
    const std::string executable = find_environment_value(
        environment, inferno_launcher::kExecutableEnvironmentVariable);
    const std::string runtime = find_environment_value(
        environment, inferno_launcher::kRuntimeEnvironmentVariable);

    if (executable.empty()) {
        local.reason = "WINIOS_INFERNO_EXE is not set";
        return local;
    }
    if (runtime.empty()) {
        local.reason = "WINIOS_INFERNO_RUNTIME is not set";
        return local;
    }

    local.executable = path_from_utf8(executable);
    local.runtime_directory = path_from_utf8(runtime);

    std::error_code error;
    if (!std::filesystem::is_regular_file(local.executable, error)) {
        local.reason = "no emulator executable at '" + executable + "'";
        return local;
    }
    if (!std::filesystem::is_directory(local.runtime_directory, error)) {
        local.reason = "no runtime directory at '" + runtime + "'";
        return local;
    }

    local.available = true;
    return local;
}

InfernoConfig integration_config(const LocalInferno& local) {
    InfernoConfig config;
    config.executable_path = local.executable;
    config.runtime_directory = local.runtime_directory;
    config.timeout = std::chrono::milliseconds(60000);
    config.max_output_bytes = 8u * 1024u * 1024u;
    return config;
}

void version_reports_chefkiss_inferno(const InfernoLauncher& launcher) {
    const InfernoLaunchResult result = launcher.query_version();

    CHECK(result.succeeded());
    CHECK(result.launched);
    CHECK(result.exit_code == 0);
    CHECK(!result.timed_out);
    CHECK(version_output_identifies_inferno(result.standard_output));

    std::cout << "  version: ";
    const std::size_t newline = result.standard_output.find('\n');
    std::cout << result.standard_output.substr(0, newline) << '\n';
}

void machine_list_registers_t8030(const InfernoLauncher& launcher) {
    const InfernoLaunchResult result = launcher.query_machine_list();

    CHECK(result.succeeded());
    CHECK(result.exit_code == 0);
    CHECK(machine_list_contains(result.standard_output, "t8030"));
    CHECK(!machine_list_contains(result.standard_output, "t8030-not-a-machine"));

    std::cout << "  -M help: " << result.standard_output.size()
              << " bytes, t8030 registered\n";
}

void t8030_help_lists_the_expected_properties(const InfernoLauncher& launcher) {
    const InfernoLaunchResult result = launcher.query_machine_help("t8030");

    CHECK(result.succeeded());
    CHECK(result.exit_code == 0);
    for (const char* property : {"trustcache", "ticket", "sep-fw", "sep-rom",
                                 "kaslr-off", "usb-conn-type", "usb-conn-addr",
                                 "usb-conn-port"}) {
        CHECK(result.standard_output.find(property) != std::string::npos);
    }
    CHECK(result.standard_output.find("t8030-machine options:") != std::string::npos);
}

void missing_executable_produces_a_useful_error(const LocalInferno& local) {
    InfernoConfig config = integration_config(local);
    config.executable_path =
        local.runtime_directory / "winios-missing-inferno" / "qemu-system-aarch64.exe";
    const InfernoLauncher launcher(config);

    const InfernoLaunchResult result = launcher.query_version();

    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::executable_missing);
    CHECK(result.error_message.find("qemu-system-aarch64.exe") != std::string::npos);
    CHECK(result.error_message.find("no substitute QEMU binary") != std::string::npos);
    CHECK(result.standard_output.empty());
}

void invalid_runtime_directory_produces_a_useful_error(const LocalInferno& local) {
    InfernoConfig config = integration_config(local);
    config.runtime_directory = local.runtime_directory / "winios-missing-runtime";
    const InfernoLauncher launcher(config);

    InfernoLaunchResult result = launcher.query_version();
    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::runtime_directory_missing);
    CHECK(result.error_message.find("ucrt64") != std::string::npos);

    // A file where the runtime directory should be is also rejected.
    config = integration_config(local);
    config.runtime_directory = local.executable;
    const InfernoLauncher file_launcher(config);
    result = file_launcher.query_version();
    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::runtime_directory_missing);
}

void launching_does_not_modify_the_parent_environment(const LocalInferno& local) {
    const std::vector<std::string> before = read_parent_environment();
    const InfernoLauncher launcher(integration_config(local));

    (void)launcher.query_version();
    (void)launcher.query_machine_list();

    CHECK(read_parent_environment() == before);
}

} // namespace

int main() {
    const LocalInferno local = discover_local_inferno();
    if (!local.available) {
        std::cout << "SKIPPED: no local ChefKiss Inferno build configured ("
                  << local.reason << ").\n"
                  << "Set WINIOS_INFERNO_EXE to the built qemu-system-aarch64.exe and\n"
                  << "WINIOS_INFERNO_RUNTIME to the MSYS2 ucrt64\\bin directory to run\n"
                  << "these opt-in integration checks.\n";
        return kSkipReturnCode;
    }

    std::cout << "Local Inferno executable: " << local.executable.string() << '\n'
              << "Local UCRT64 runtime    : " << local.runtime_directory.string()
              << '\n';

    const InfernoLauncher launcher(integration_config(local));
    version_reports_chefkiss_inferno(launcher);
    machine_list_registers_t8030(launcher);
    t8030_help_lists_the_expected_properties(launcher);
    missing_executable_produces_a_useful_error(local);
    invalid_runtime_directory_produces_a_useful_error(local);
    launching_does_not_modify_the_parent_environment(local);

    return test::summarize("inferno_integration_tests");
}
