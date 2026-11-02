#include "inferno/inferno_launcher.h"
#include "test_harness.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view kChildExecutable = WINIOS_TEST_CHILD_EXE;

std::filesystem::path child_executable() {
    return std::filesystem::path(kChildExecutable);
}

// Creates a unique directory under the system temporary directory and removes
// it (and anything inside) on scope exit.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const char* name) {
        static int counter = 0;
        ++counter;
        path_ = std::filesystem::temp_directory_path()
                / (std::string("winios_launcher_") + name + "_"
                   + std::to_string(counter));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

InfernoConfig child_config(const std::filesystem::path& runtime_directory) {
    InfernoConfig config;
    config.executable_path = child_executable();
    config.runtime_directory = runtime_directory;
    config.timeout = std::chrono::milliseconds(15000);
    config.max_output_bytes = 64u * 1024u;
    return config;
}

std::string trimmed(std::string_view text) {
    while (!text.empty()
           && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string first_path_entry(std::string_view path) {
    const std::size_t separator = path.find(';');
    return std::string(path.substr(0, separator == std::string_view::npos
                                          ? path.size()
                                          : separator));
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

void argument_quoting_matches_msvcrt_rules() {
    CHECK(quote_windows_argument("") == R"("")");
    CHECK(quote_windows_argument("plain") == "plain");
    CHECK(quote_windows_argument("--version") == "--version");
    CHECK(quote_windows_argument("a b") == R"("a b")");
    CHECK(quote_windows_argument("a\tb") == "\"a\tb\"");
    CHECK(quote_windows_argument(R"(a\b)") == R"(a\b)");
    CHECK(quote_windows_argument("a\"b") == R"("a\"b")");
    CHECK(quote_windows_argument(R"(a b\)") == R"("a b\\")");
    CHECK(quote_windows_argument("a\\\"b") == R"("a\\\"b")");
    CHECK(quote_windows_argument(R"(C:\Program Files\Inferno)") == R"("C:\Program Files\Inferno")");
    CHECK(quote_windows_argument(R"(C:\Program Files\Inferno\)")
          == R"("C:\Program Files\Inferno\\")");
    CHECK(quote_windows_argument("-M") == "-M");
    CHECK(quote_windows_argument("t8030,help") == "t8030,help");
}

void command_lines_are_joined_without_a_shell() {
    CHECK(build_windows_command_line(R"(C:\tools\qemu.exe)", {"--version"})
          == R"(C:\tools\qemu.exe --version)");
    CHECK(build_windows_command_line(R"(C:\my tools\qemu.exe)", {"-M", "help"})
          == R"("C:\my tools\qemu.exe" -M help)");
    CHECK(build_windows_command_line("qemu.exe", {}) == "qemu.exe");
    CHECK(build_windows_command_line("qemu.exe", {"", "a b", "-M", "t8030,help"})
          == "qemu.exe \"\" \"a b\" -M t8030,help");
}

void environment_lookup_is_case_insensitive() {
    const std::vector<std::string> environment{
        "SystemRoot=C:\\Windows", "Path=D:\\base", "EMPTY=", "WEIRD=a=b=c", "BROKEN"};
    CHECK(find_environment_value(environment, "SystemRoot") == "C:\\Windows");
    CHECK(find_environment_value(environment, "SYSTEMROOT") == "C:\\Windows");
    CHECK(find_environment_value(environment, "PATH") == "D:\\base");
    CHECK(find_environment_value(environment, "empty").empty());
    CHECK(find_environment_value(environment, "weird") == "a=b=c");
    CHECK(find_environment_value(environment, "absent").empty());
    CHECK(find_environment_value({}, "PATH").empty());
}

void child_environment_prefixes_path_and_keeps_everything_else() {
    const std::vector<std::string> parent{
        "SystemRoot=C:\\Windows", "Path=C:\\base;C:\\other", "TEMP=C:\\temp",
        "BROKEN"};
    const std::filesystem::path runtime = R"(D:\msys2root\ucrt64\bin)";

    const std::vector<std::string> child = build_child_environment(parent, runtime);

    CHECK(parent.size() == 4);
    CHECK(parent[1] == "Path=C:\\base;C:\\other");
    CHECK(child.size() == parent.size());
    CHECK(child[0] == parent[0]);
    CHECK(find_environment_value(child, "PATH")
          == R"(D:\msys2root\ucrt64\bin;C:\base;C:\other)");
    CHECK(child[1].starts_with("Path="));
    CHECK(find_environment_value(child, "SystemRoot") == "C:\\Windows");
    CHECK(find_environment_value(child, "TEMP") == "C:\\temp");
    CHECK(child.back() == "BROKEN");
}

void child_environment_does_not_add_a_duplicate_path_entry() {
    const std::filesystem::path runtime = R"(D:\msys2root\ucrt64\bin)";
    const std::vector<std::string> already_present{
        R"(PATH=D:\msys2root\ucrt64\bin;C:\base)"};
    CHECK(build_child_environment(already_present, runtime) == already_present);

    const std::vector<std::string> different_case_and_separator{
        R"(PATH=d:\MSYS2ROOT\UCRT64\bin\;C:\base)"};
    CHECK(build_child_environment(different_case_and_separator, runtime)
          == different_case_and_separator);
}

void child_environment_creates_path_when_absent() {
    const std::filesystem::path runtime = R"(D:\ucrt64\bin)";
    const std::vector<std::string> parent{"TEMP=C:\\temp"};

    const std::vector<std::string> child = build_child_environment(parent, runtime);

    CHECK(child.size() == 2);
    CHECK(find_environment_value(child, "PATH") == R"(D:\ucrt64\bin)");
    CHECK(parent.size() == 1);
}

void child_environment_handles_empty_path_value() {
    const std::filesystem::path runtime = R"(D:\ucrt64\bin)";
    const std::vector<std::string> child =
        build_child_environment({"PATH="}, runtime);
    CHECK(child.size() == 1);
    CHECK(child.front() == R"(PATH=D:\ucrt64\bin)");
}

// ---------------------------------------------------------------------------
// Configuration validation
// ---------------------------------------------------------------------------

void missing_executable_is_reported() {
    TemporaryDirectory runtime("missing_exe");
    InfernoConfig config = child_config(runtime.path());
    config.executable_path.clear();

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
    CHECK(validation.message.find("--inferno-exe") != std::string::npos);
}

void nonexistent_executable_is_distinguished_from_invalid_configuration() {
    TemporaryDirectory runtime("bad_exe");
    InfernoConfig config = child_config(runtime.path());
    config.executable_path = runtime.path() / "does-not-exist" / "qemu-system-aarch64.exe";

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::executable_missing);
    CHECK(validation.message.find("qemu-system-aarch64.exe") != std::string::npos);
    CHECK(validation.message.find("not found") != std::string::npos);
}

void directory_used_as_executable_is_rejected() {
    TemporaryDirectory runtime("exe_dir");
    InfernoConfig config = child_config(runtime.path());
    config.executable_path = runtime.path();

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
    CHECK(validation.message.find("not a regular file") != std::string::npos);
}

void missing_runtime_directory_is_reported() {
    TemporaryDirectory scratch("runtime_missing");
    InfernoConfig config = child_config(scratch.path() / "absent");

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::runtime_directory_missing);
    CHECK(validation.message.find("--inferno-runtime") != std::string::npos);
}

void empty_runtime_directory_is_reported() {
    TemporaryDirectory scratch("runtime_empty");
    InfernoConfig config = child_config(scratch.path());
    config.runtime_directory.clear();

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
    CHECK(validation.message.find("--inferno-runtime") != std::string::npos);
}

void file_used_as_runtime_directory_is_rejected() {
    TemporaryDirectory scratch("runtime_file");
    InfernoConfig config = child_config(child_executable());

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::runtime_directory_missing);
    CHECK(validation.message.find("not a directory") != std::string::npos);
}

void missing_working_directory_is_rejected() {
    TemporaryDirectory scratch("workdir");
    InfernoConfig config = child_config(scratch.path());
    config.working_directory = scratch.path() / "absent";

    const InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
    CHECK(validation.message.find("working directory") != std::string::npos);
}

void invalid_limits_are_rejected() {
    TemporaryDirectory runtime("limits");
    InfernoConfig config = child_config(runtime.path());
    config.timeout = std::chrono::milliseconds(0);

    InfernoConfigValidation validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
    CHECK(validation.message.find("timeout") != std::string::npos);

    config = child_config(runtime.path());
    config.max_output_bytes = 0;
    validation = validate_inferno_config(config);
    CHECK(!validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::invalid_configuration);
}

void valid_configuration_passes() {
    TemporaryDirectory runtime("valid");
    const InfernoConfigValidation validation =
        validate_inferno_config(child_config(runtime.path()));
    CHECK(validation.valid());
    CHECK(validation.failure == InfernoLaunchFailure::none);
    CHECK(validation.message.empty());
}

// ---------------------------------------------------------------------------
// Result and output interpretation
// ---------------------------------------------------------------------------

void exit_codes_map_to_failure_kinds() {
    CHECK(classify_inferno_exit_code(0) == InfernoLaunchFailure::none);
    CHECK(classify_inferno_exit_code(1) == InfernoLaunchFailure::nonzero_exit);
    CHECK(classify_inferno_exit_code(2) == InfernoLaunchFailure::nonzero_exit);
    CHECK(classify_inferno_exit_code(0xC0000135u)
          == InfernoLaunchFailure::dll_initialization_failed);
    CHECK(classify_inferno_exit_code(0xC0000139u)
          == InfernoLaunchFailure::dll_initialization_failed);
    CHECK(classify_inferno_exit_code(0xC000007Bu)
          == InfernoLaunchFailure::dll_initialization_failed);
    CHECK(classify_inferno_exit_code(0x80000003u)
          == InfernoLaunchFailure::nonzero_exit);
}

void failure_names_are_stable() {
    CHECK(std::string_view(inferno_launch_failure_name(InfernoLaunchFailure::none))
          == "none");
    CHECK(std::string_view(
              inferno_launch_failure_name(InfernoLaunchFailure::invalid_configuration))
          == "invalid_configuration");
    CHECK(std::string_view(
              inferno_launch_failure_name(InfernoLaunchFailure::executable_missing))
          == "executable_missing");
    CHECK(std::string_view(inferno_launch_failure_name(
              InfernoLaunchFailure::runtime_directory_missing))
          == "runtime_directory_missing");
    CHECK(std::string_view(inferno_launch_failure_name(
              InfernoLaunchFailure::process_creation_failed))
          == "process_creation_failed");
    CHECK(std::string_view(inferno_launch_failure_name(
              InfernoLaunchFailure::dll_initialization_failed))
          == "dll_initialization_failed");
    CHECK(std::string_view(inferno_launch_failure_name(InfernoLaunchFailure::timeout))
          == "timeout");
    CHECK(std::string_view(
              inferno_launch_failure_name(InfernoLaunchFailure::nonzero_exit))
          == "nonzero_exit");
}

void version_banner_is_recognised() {
    const std::string_view inferno =
        "QEMU emulator version ChefKissInferno-1.0.0rc1_QEMU-10.2.2_10.1.3_hybrid\n"
        "Copyright (c) 2003-2025 Fabrice Bellard and the QEMU Project developers\n"
        "\nChefKiss Inferno\n"
        "Copyright (c) 2023-2026 Visual Ehrmanntraut and Inferno team\n";
    CHECK(version_output_identifies_inferno(inferno));
    CHECK(version_output_identifies_inferno("chefkiss inferno"));
    CHECK(!version_output_identifies_inferno(
        "QEMU emulator version 10.2.2\nCopyright (c) 2003-2025 Fabrice Bellard"));
    CHECK(!version_output_identifies_inferno(""));
}

void machine_list_matching_is_token_exact() {
    const std::string list =
        "Supported machines are:\n"
        "ast1030-evb          Aspeed AST1030 MiniBMC (Cortex-M4)\n"
        "t8030                Apple T8030 SoC (iPhone 11)\n"
        "virt                 QEMU ARM Virtual Machine\n";
    CHECK(machine_list_contains(list, "t8030"));
    CHECK(machine_list_contains(list, "ast1030-evb"));
    CHECK(!machine_list_contains(list, "t803"));
    CHECK(!machine_list_contains(list, "t8030x"));
    CHECK(!machine_list_contains(list, "Apple"));
    CHECK(!machine_list_contains(list, ""));
    CHECK(!machine_list_contains("", "t8030"));
    CHECK(machine_list_contains("t8030                Apple T8030 SoC (iPhone 11)",
                                "t8030"));
    CHECK(machine_list_contains("  t8030   Apple T8030 SoC\r\n", "t8030"));
}

// ---------------------------------------------------------------------------
// Process execution (Windows only; uses the launcher test child)
// ---------------------------------------------------------------------------

void process_success_captures_output() {
    TemporaryDirectory runtime("process_ok");
    const InfernoLauncher launcher(child_config(runtime.path()));

    const InfernoLaunchResult result = launcher.run({"ok"});

    CHECK(result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::none);
    CHECK(result.launched);
    CHECK(result.exit_code == 0);
    CHECK(result.standard_output == "child-ok\n");
    CHECK(result.standard_error.empty());
    CHECK(!result.timed_out);
    CHECK(!result.output_truncated);
    CHECK(result.error_message.empty());
}

void process_failure_reports_exit_code_and_stderr() {
    TemporaryDirectory runtime("process_fail");
    const InfernoLauncher launcher(child_config(runtime.path()));

    const InfernoLaunchResult result = launcher.run({"fail"});

    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::nonzero_exit);
    CHECK(result.exit_code == 3);
    CHECK(result.standard_error == "child failure text\n");
    CHECK(result.standard_output.empty());
    CHECK(result.error_message.find("3") != std::string::npos);
}

void process_arguments_survive_windows_quoting() {
    TemporaryDirectory runtime("process_args");
    const InfernoLauncher launcher(child_config(runtime.path()));

    const InfernoLaunchResult result =
        launcher.run({"echo", "a b", "quote\"inside", "trail\\", ""});

    CHECK(result.succeeded());
    CHECK(result.standard_output
          == "arg02=[a b]\n"
             "arg03=[quote\"inside]\n"
             "arg04=[trail\\]\n"
             "arg05=[]\n");
}

void process_timeout_terminates_the_child() {
    TemporaryDirectory runtime("process_timeout");
    InfernoConfig config = child_config(runtime.path());
    config.timeout = std::chrono::milliseconds(400);
    const InfernoLauncher launcher(config);

    const InfernoLaunchResult result = launcher.run({"sleep", "8000"});

    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::timeout);
    CHECK(result.timed_out);
    CHECK(result.error_message.find("400") != std::string::npos);
    CHECK(result.standard_output.find("child sleeping") != std::string::npos);
}

void process_output_is_bounded() {
    TemporaryDirectory runtime("process_bound");
    InfernoConfig config = child_config(runtime.path());
    config.max_output_bytes = 1000;
    const InfernoLauncher launcher(config);

    const InfernoLaunchResult result = launcher.run({"spam", "40000"});

    CHECK(result.succeeded());
    CHECK(result.output_truncated);
    CHECK(result.standard_output.size() == 1000);
}

void process_error_output_is_bounded_without_deadlock() {
    TemporaryDirectory runtime("process_bound_err");
    InfernoConfig config = child_config(runtime.path());
    config.max_output_bytes = 500;
    const InfernoLauncher launcher(config);

    // A child that floods stderr while stdout stays idle must not block either
    // side: both pipes are part of the same wait set.
    const InfernoLaunchResult result = launcher.run({"spamerr", "40000"});

    CHECK(result.succeeded());
    CHECK(result.output_truncated);
    CHECK(result.standard_error.size() == 500);
    CHECK(result.standard_output.empty());
}

void process_uses_the_configured_working_directory() {
    TemporaryDirectory runtime("process_cwd_runtime");
    TemporaryDirectory working("process_cwd");
    InfernoConfig config = child_config(runtime.path());
    config.working_directory = working.path();
    const InfernoLauncher launcher(config);

    const InfernoLaunchResult result = launcher.run({"cwd"});

    CHECK(result.succeeded());
    const std::string reported = trimmed(result.standard_output);
    CHECK(reported.starts_with("cwd="));
    const std::filesystem::path child_directory =
        std::filesystem::path(reported.substr(4));
    std::error_code error;
    const bool same = std::filesystem::exists(child_directory, error)
                      && std::filesystem::equivalent(child_directory,
                                                     working.path(), error);
    CHECK(same);
}

void child_receives_runtime_directory_on_path() {
    TemporaryDirectory runtime("process_path");
    const InfernoLauncher launcher(child_config(runtime.path()));
    const std::string parent_path =
        find_environment_value(read_parent_environment(), "PATH");

    const InfernoLaunchResult result = launcher.run({"path"});

    CHECK(result.succeeded());
    const std::string reported = trimmed(result.standard_output);
    CHECK(reported.starts_with("path="));
    const std::string child_path = reported.substr(5);
    const std::string first = first_path_entry(child_path);
    std::error_code error;
    CHECK(std::filesystem::equivalent(first, runtime.path(), error));
    if (!parent_path.empty()) {
        CHECK(child_path.ends_with(";" + parent_path));
    }
    CHECK(find_environment_value(read_parent_environment(), "PATH") == parent_path);
}

void launching_does_not_modify_the_parent_environment() {
    TemporaryDirectory runtime("process_parent_env");
    const std::vector<std::string> before = read_parent_environment();
    const InfernoLauncher launcher(child_config(runtime.path()));

    (void)launcher.run({"ok"});
    (void)launcher.run({"fail"});
    (void)launcher.run({"path"});

    CHECK(read_parent_environment() == before);
}

void invalid_configuration_is_reported_without_launching() {
    TemporaryDirectory runtime("process_invalid");
    InfernoConfig config = child_config(runtime.path());
    config.executable_path = runtime.path() / "missing.exe";
    const InfernoLauncher launcher(config);

    const InfernoLaunchResult result = launcher.run({"--version"});

    CHECK(!result.succeeded());
    CHECK(result.failure == InfernoLaunchFailure::executable_missing);
    CHECK(!result.launched);
    CHECK(result.exit_code == 0);
    CHECK(result.standard_output.empty());
    CHECK(result.standard_error.empty());
    CHECK(!result.error_message.empty());
}

void launcher_queries_use_the_expected_arguments() {
    TemporaryDirectory runtime("process_queries");
    const InfernoLauncher launcher(child_config(runtime.path()));

    // The fixture rejects any first argument it does not understand, so the
    // failure mode proves which arguments actually reached the child.
    const InfernoLaunchResult version = launcher.query_version();
    CHECK(!version.succeeded());
    CHECK(version.exit_code == 5);
    CHECK(version.standard_error.find("unknown child mode: --version")
          != std::string::npos);

    const InfernoLaunchResult machines = launcher.query_machine_list();
    CHECK(!machines.succeeded());
    CHECK(machines.exit_code == 5);
    CHECK(machines.standard_error.find("unknown child mode: -M") != std::string::npos);

    const InfernoLaunchResult help = launcher.query_machine_help("t8030");
    CHECK(!help.succeeded());
    CHECK(help.exit_code == 5);
    CHECK(help.standard_error.find("unknown child mode: -M") != std::string::npos);
}

} // namespace

int main() {
    argument_quoting_matches_msvcrt_rules();
    command_lines_are_joined_without_a_shell();
    environment_lookup_is_case_insensitive();
    child_environment_prefixes_path_and_keeps_everything_else();
    child_environment_does_not_add_a_duplicate_path_entry();
    child_environment_creates_path_when_absent();
    child_environment_handles_empty_path_value();

    missing_executable_is_reported();
    nonexistent_executable_is_distinguished_from_invalid_configuration();
    directory_used_as_executable_is_rejected();
    missing_runtime_directory_is_reported();
    empty_runtime_directory_is_reported();
    file_used_as_runtime_directory_is_rejected();
    missing_working_directory_is_rejected();
    invalid_limits_are_rejected();
    valid_configuration_passes();

    exit_codes_map_to_failure_kinds();
    failure_names_are_stable();
    version_banner_is_recognised();
    machine_list_matching_is_token_exact();

    process_success_captures_output();
    process_failure_reports_exit_code_and_stderr();
    process_arguments_survive_windows_quoting();
    process_timeout_terminates_the_child();
    process_output_is_bounded();
    process_error_output_is_bounded_without_deadlock();
    process_uses_the_configured_working_directory();
    child_receives_runtime_directory_on_path();
    launching_does_not_modify_the_parent_environment();
    invalid_configuration_is_reported_without_launching();
    launcher_queries_use_the_expected_arguments();

    return test::summarize("inferno_launcher_tests");
}
