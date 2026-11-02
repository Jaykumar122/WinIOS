#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Launches a separately built ChefKiss Inferno (QEMU fork) executable as a
// child process. WinIOS never links against, vendors, or embeds QEMU/Inferno;
// this component only starts the external binary and reports what it did.
//
// The launcher is experimental and intentionally narrow:
//   * the executable and the UCRT64 runtime directory are configured
//     explicitly, and are never substituted with another QEMU build,
//   * arguments are passed as a list and quoted for CreateProcessW, so no
//     shell (`cmd.exe`, PowerShell) is involved and no string is re-parsed,
//   * the child receives a child-only environment whose PATH is prefixed with
//     the configured runtime directory; the parent environment is untouched,
//   * stdout/stderr are captured with a bound and the child is terminated when
//     the timeout expires.
namespace inferno_launcher {

// Default per-run wall-clock limit and per-stream capture bound.
inline constexpr std::chrono::milliseconds kDefaultTimeout{30000};
inline constexpr std::size_t kDefaultOutputLimit = 256u * 1024u;

// Environment variables consulted by the command line when the explicit
// options are omitted. They are only read, never written.
inline constexpr std::string_view kExecutableEnvironmentVariable =
    "WINIOS_INFERNO_EXE";
inline constexpr std::string_view kRuntimeEnvironmentVariable =
    "WINIOS_INFERNO_RUNTIME";

} // namespace inferno_launcher

enum class InfernoLaunchFailure {
    none,
    invalid_configuration,
    executable_missing,
    runtime_directory_missing,
    process_creation_failed,
    dll_initialization_failed,
    timeout,
    nonzero_exit,
    unsupported_platform
};

// Stable, human-readable identifier for a failure kind ("none", "timeout", ...).
const char* inferno_launch_failure_name(InfernoLaunchFailure failure) noexcept;

// Maps a process exit code to a failure kind. Windows reports DLL loading
// problems before main() through the exit code, which is the only portable
// signal available for "the child could not start because a DLL is missing".
InfernoLaunchFailure classify_inferno_exit_code(std::uint32_t exit_code) noexcept;

struct InfernoConfig {
    // Full path to qemu-system-aarch64.exe built from the pinned Inferno tree.
    std::filesystem::path executable_path;
    // UCRT64 runtime directory (for example ...\msys2root\ucrt64\bin) that
    // holds the MSYS2 DLLs the emulator needs at run time.
    std::filesystem::path runtime_directory;
    // Optional child working directory; empty means "next to the executable".
    std::filesystem::path working_directory;
    std::chrono::milliseconds timeout{inferno_launcher::kDefaultTimeout};
    // Upper bound for each captured stream, to keep memory use predictable.
    std::size_t max_output_bytes{inferno_launcher::kDefaultOutputLimit};
    // Optional inherited environment block ("NAME=VALUE" entries). Empty means
    // "read the real parent environment"; tests use this to stay hermetic.
    std::vector<std::string> inherited_environment;
};

struct InfernoLaunchResult {
    InfernoLaunchFailure failure{InfernoLaunchFailure::invalid_configuration};
    std::uint32_t exit_code{};
    std::string standard_output;
    std::string standard_error;
    bool launched{false};
    bool timed_out{false};
    bool output_truncated{false};
    std::string error_message;

    [[nodiscard]] bool succeeded() const noexcept {
        return failure == InfernoLaunchFailure::none;
    }
};

struct InfernoConfigValidation {
    InfernoLaunchFailure failure{InfernoLaunchFailure::none};
    std::string message;

    [[nodiscard]] bool valid() const noexcept {
        return message.empty();
    }
};

// Checks that the configured paths exist and the limits are usable. The
// message is written for an end user and names the exact path that is wrong.
InfernoConfigValidation validate_inferno_config(const InfernoConfig& config);

class InfernoLauncher {
public:
    explicit InfernoLauncher(InfernoConfig config);

    [[nodiscard]] const InfernoConfig& config() const noexcept { return config_; }
    [[nodiscard]] InfernoConfigValidation validation() const;

    // Runs the executable with `arguments` as its argv[1..] list.
    InfernoLaunchResult run(const std::vector<std::string>& arguments) const;

    // Convenience queries used by the CLI and by the integration checks.
    InfernoLaunchResult query_version() const;
    InfernoLaunchResult query_machine_list() const;
    InfernoLaunchResult query_machine_help(std::string_view machine) const;

private:
    InfernoConfig config_;
};

// ---------------------------------------------------------------------------
// Building blocks, exposed so they can be tested without starting a process.
// ---------------------------------------------------------------------------

// Quotes one argument according to the Microsoft C runtime rules that
// CommandLineToArgvW and CreateProcessW-based programs use, so arguments
// survive round-tripping even when they contain spaces, quotes or trailing
// backslashes.
std::string quote_windows_argument(std::string_view argument);

// Builds a CreateProcessW command line: the quoted executable followed by the
// quoted arguments, separated by single spaces.
std::string build_windows_command_line(std::string_view executable,
                                       const std::vector<std::string>& arguments);

// Returns the current process environment as "NAME=VALUE" entries (UTF-8).
std::vector<std::string> read_parent_environment();

// Looks up a variable in an environment block, comparing names case
// insensitively. Returns an empty string when the variable is absent.
std::string find_environment_value(const std::vector<std::string>& environment,
                                   std::string_view name);

// Returns a copy of `parent` with `runtime_directory` prepended to PATH. Other
// variables keep their order and value, and `parent` itself is not modified.
// The runtime directory is not added twice.
std::vector<std::string> build_child_environment(
    const std::vector<std::string>& parent,
    const std::filesystem::path& runtime_directory);

// True when the text looks like the ChefKiss Inferno version banner. Used
// instead of trusting that an executable named qemu-system-aarch64 is Inferno.
bool version_output_identifies_inferno(std::string_view text);

// True when `machine` appears as a machine name in `qemu -M help` output.
// Matching is done per line and on the whole first token, so "t8030x" or a
// substring in a description does not count.
bool machine_list_contains(std::string_view text, std::string_view machine);
