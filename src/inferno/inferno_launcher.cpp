#include "inferno_launcher.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr std::uint32_t kStatusDllNotFound = 0xC0000135u;
constexpr std::uint32_t kStatusEntrypointNotFound = 0xC0000139u;
constexpr std::uint32_t kStatusInvalidImageFormat = 0xC000007Bu;

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

std::string ascii_lower(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

std::string_view trim_trailing_separators(std::string_view path) {
    while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) {
        path.remove_suffix(1);
    }
    return path;
}

bool path_list_contains(std::string_view list, std::string_view directory) {
    const std::string_view wanted = trim_trailing_separators(directory);
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = list.find(';', start);
        const std::string_view entry =
            list.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                             : end - start);
        if (!entry.empty()
            && ascii_iequals(trim_trailing_separators(entry), wanted)) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

#ifdef _WIN32
std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring widened(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        widened.data(), size);
    return widened;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string narrowed(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        narrowed.data(), size, nullptr, nullptr);
    return narrowed;
}
#endif

std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return narrow(path.native());
#else
    return path.string();
#endif
}

std::string format_error_code(unsigned long code) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", code);
    return std::string(buffer);
}

// Microsoft C runtime argument quoting: a backslash is literal unless it
// precedes a double quote, and quotes and trailing backslashes are escaped.
std::string quote_argument_impl(std::string_view argument) {
    if (argument.empty()) {
        return "\"\"";
    }
    const bool needs_quotes =
        argument.find_first_of(" \t\n\v\"") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(argument);
    }

    std::string quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back('"');
    std::size_t index = 0;
    while (index < argument.size()) {
        std::size_t backslashes = 0;
        while (index < argument.size() && argument[index] == '\\') {
            ++backslashes;
            ++index;
        }
        if (index == argument.size()) {
            quoted.append(backslashes * 2, '\\');
            break;
        }
        if (argument[index] == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(argument[index]);
        }
        ++index;
    }
    quoted.push_back('"');
    return quoted;
}

#ifdef _WIN32
struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(static_cast<HANDLE>(handle));
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

UniqueHandle adopt_handle(HANDLE handle) {
    return UniqueHandle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

struct BoundedSink {
    std::string data;
    std::size_t limit{};
    bool truncated{false};

    void append(const char* bytes, std::size_t count) {
        if (count == 0) {
            return;
        }
        if (data.size() >= limit) {
            truncated = true;
            return;
        }
        const std::size_t room = limit - data.size();
        const std::size_t take = count < room ? count : room;
        data.append(bytes, take);
        if (take < count) {
            truncated = true;
        }
    }
};

// Reads every byte that is currently buffered in `pipe` and returns. The
// available byte count is queried first, so the function never blocks: a chatty
// child cannot deadlock us and an idle pipe cannot stall us. Output beyond the
// configured bound is read and discarded, so the child never blocks on a full
// pipe either.
void pump_pipe(HANDLE pipe, BoundedSink& sink) {
    char buffer[8192];
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == 0
            || available == 0) {
            return;
        }
        const DWORD request = available < sizeof(buffer)
                                  ? available
                                  : static_cast<DWORD>(sizeof(buffer));
        DWORD read = 0;
        if (ReadFile(pipe, buffer, request, &read, nullptr) == 0 || read == 0) {
            return;
        }
        sink.append(buffer, static_cast<std::size_t>(read));
    }
}

bool create_capture_pipe(UniqueHandle& read_end, UniqueHandle& write_end) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (CreatePipe(&read_handle, &write_handle, &attributes, 0) == 0) {
        return false;
    }
    read_end = adopt_handle(read_handle);
    write_end = adopt_handle(write_handle);
    if (SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0) == 0) {
        return false;
    }
    return true;
}

UniqueHandle open_null_device() {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    return adopt_handle(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &attributes, OPEN_EXISTING, 0, nullptr));
}

std::wstring build_environment_block(const std::vector<std::string>& entries) {
    std::wstring block;
    for (const std::string& entry : entries) {
        // CreateProcessW rejects malformed blocks, so drop anything that is not
        // a "NAME=VALUE" pair or that has an empty name.
        const std::size_t separator = entry.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }
        block.append(widen(entry));
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

InfernoLaunchResult run_windows(const InfernoConfig& config,
                                const std::vector<std::string>& arguments) {
    InfernoLaunchResult result;

    UniqueHandle out_read;
    UniqueHandle out_write;
    UniqueHandle err_read;
    UniqueHandle err_write;
    if (!create_capture_pipe(out_read, out_write)
        || !create_capture_pipe(err_read, err_write)) {
        result.failure = InfernoLaunchFailure::process_creation_failed;
        result.error_message =
            "Unable to create the pipes used to capture the Inferno process output.";
        return result;
    }

    UniqueHandle null_device = open_null_device();
    if (!null_device) {
        result.failure = InfernoLaunchFailure::process_creation_failed;
        result.error_message =
            "Unable to open the NUL device for the Inferno process standard input.";
        return result;
    }

    std::wstring command_line =
        widen(build_windows_command_line(path_to_utf8(config.executable_path),
                                         arguments));
    std::vector<wchar_t> mutable_command_line(command_line.begin(),
                                              command_line.end());
    mutable_command_line.push_back(L'\0');

    const std::vector<std::string> parent =
        config.inherited_environment.empty() ? read_parent_environment()
                                             : config.inherited_environment;
    const std::vector<std::string> child =
        build_child_environment(parent, config.runtime_directory);
    std::wstring environment_block = build_environment_block(child);

    std::wstring working_directory;
    if (!config.working_directory.empty()) {
        working_directory = config.working_directory.native();
    } else if (config.executable_path.has_parent_path()) {
        working_directory = config.executable_path.parent_path().native();
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = static_cast<HANDLE>(null_device.get());
    startup.StartupInfo.hStdOutput = static_cast<HANDLE>(out_write.get());
    startup.StartupInfo.hStdError = static_cast<HANDLE>(err_write.get());

    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<std::byte> attribute_storage(attribute_size > 0 ? attribute_size : 1);
    auto* attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        attribute_storage.data());
    bool attributes_initialized = false;
    bool attributes_ready = false;
    if (attribute_size > 0
        && InitializeProcThreadAttributeList(attribute_list, 1, 0,
                                            &attribute_size) != 0) {
        attributes_initialized = true;
        HANDLE inherited[3] = {static_cast<HANDLE>(null_device.get()),
                               static_cast<HANDLE>(out_write.get()),
                               static_cast<HANDLE>(err_write.get())};
        attributes_ready =
            UpdateProcThreadAttribute(attribute_list, 0,
                                      PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                      sizeof(inherited), nullptr, nullptr) != 0;
    }
    if (!attributes_ready) {
        if (attributes_initialized) {
            DeleteProcThreadAttributeList(attribute_list);
        }
        result.failure = InfernoLaunchFailure::process_creation_failed;
        result.error_message =
            "Unable to prepare the inherited handle list for the Inferno process.";
        return result;
    }
    startup.lpAttributeList = attribute_list;

    PROCESS_INFORMATION process{};
    const std::wstring application_name = config.executable_path.native();
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT
                        | CREATE_NO_WINDOW;
    const BOOL created = CreateProcessW(
        application_name.c_str(), mutable_command_line.data(), nullptr, nullptr,
        TRUE, flags, environment_block.data(),
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup.StartupInfo, &process);
    const DWORD create_error = GetLastError();
    DeleteProcThreadAttributeList(attribute_list);

    if (created == 0) {
        result.failure = (create_error == ERROR_FILE_NOT_FOUND
                          || create_error == ERROR_PATH_NOT_FOUND)
                             ? InfernoLaunchFailure::executable_missing
                             : InfernoLaunchFailure::process_creation_failed;
        result.error_message = "Unable to start '" + path_to_utf8(config.executable_path)
                               + "' (" + format_error_code(create_error)
                               + "). Check that the file exists and is a Windows "
                                 "executable built for this host architecture.";
        return result;
    }

    UniqueHandle process_handle = adopt_handle(process.hProcess);
    UniqueHandle thread_handle = adopt_handle(process.hThread);
    result.launched = true;

    // Only the child may hold the write ends, otherwise the read ends never
    // report end-of-file.
    out_write.reset();
    err_write.reset();

    const std::uint64_t timeout_count =
        static_cast<std::uint64_t>(config.timeout.count());
    const ULONGLONG deadline = GetTickCount64() + timeout_count;

    BoundedSink standard_output{"", config.max_output_bytes, false};
    BoundedSink standard_error{"", config.max_output_bytes, false};

    bool wait_failed = false;
    bool terminated = false;

    // Both streams are drained on every step and the child is waited on for a
    // short slice instead of waiting on the pipe read handles. Anonymous pipe
    // read handles can stay signaled while nothing is available to read, which
    // let one stream starve the other until the child blocked on a full pipe.
    // Polling the process services both streams fairly and cannot spin: each
    // iteration sleeps in WaitForSingleObject.
    constexpr DWORD kWaitSliceMs = 20;

    for (;;) {
        pump_pipe(static_cast<HANDLE>(out_read.get()), standard_output);
        pump_pipe(static_cast<HANDLE>(err_read.get()), standard_error);

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            terminated = true;
            break;
        }
        const DWORD slice = static_cast<DWORD>(
            std::min<std::uint64_t>(deadline - now, kWaitSliceMs));
        const DWORD wait_result =
            WaitForSingleObject(static_cast<HANDLE>(process_handle.get()), slice);
        if (wait_result == WAIT_FAILED) {
            wait_failed = true;
            break;
        }
        if (wait_result != WAIT_TIMEOUT) {
            break; // The child ended; buffered output is drained below.
        }
    }

    if (terminated || wait_failed) {
        // Terminate only the child we created; never a process group or shell.
        TerminateProcess(static_cast<HANDLE>(process_handle.get()), 1);
    }

    // The writer is gone once the child has exited or was terminated, so this
    // drain cannot block; it captures whatever the kernel still buffers.
    WaitForSingleObject(static_cast<HANDLE>(process_handle.get()), 10000);
    pump_pipe(static_cast<HANDLE>(out_read.get()), standard_output);
    pump_pipe(static_cast<HANDLE>(err_read.get()), standard_error);

    DWORD exit_code = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(process_handle.get()), &exit_code) == 0) {
        exit_code = 0;
    }

    result.standard_output = std::move(standard_output.data);
    result.standard_error = std::move(standard_error.data);
    result.output_truncated = standard_output.truncated || standard_error.truncated;
    result.exit_code = static_cast<std::uint32_t>(exit_code);

    if (wait_failed) {
        result.failure = InfernoLaunchFailure::process_creation_failed;
        result.error_message =
            "Waiting for the Inferno process failed ("
            + format_error_code(GetLastError()) + ").";
        return result;
    }
    if (terminated) {
        result.timed_out = true;
        result.failure = InfernoLaunchFailure::timeout;
        result.error_message =
            "Inferno did not exit within "
            + std::to_string(config.timeout.count())
            + " ms and was terminated. Increase --inferno-timeout for slow "
              "queries on this host.";
        return result;
    }

    result.failure = classify_inferno_exit_code(result.exit_code);
    switch (result.failure) {
    case InfernoLaunchFailure::none:
        break;
    case InfernoLaunchFailure::dll_initialization_failed:
        result.error_message =
            "The Inferno executable could not initialize (exit code "
            + format_error_code(result.exit_code)
            + "). A required DLL was not found or the binary is not usable on "
              "this host. Confirm that the configured UCRT64 runtime directory "
              "contains the MSYS2 DLLs: '"
            + path_to_utf8(config.runtime_directory) + "'.";
        break;
    case InfernoLaunchFailure::nonzero_exit:
        result.error_message = "Inferno exited with code "
                               + std::to_string(result.exit_code)
                               + ". See the captured standard error output.";
        break;
    default:
        result.error_message = "Inferno failed with '"
                               + std::string(inferno_launch_failure_name(result.failure))
                               + "'.";
        break;
    }
    return result;
}
#endif

} // namespace

const char* inferno_launch_failure_name(InfernoLaunchFailure failure) noexcept {
    switch (failure) {
    case InfernoLaunchFailure::none:
        return "none";
    case InfernoLaunchFailure::invalid_configuration:
        return "invalid_configuration";
    case InfernoLaunchFailure::executable_missing:
        return "executable_missing";
    case InfernoLaunchFailure::runtime_directory_missing:
        return "runtime_directory_missing";
    case InfernoLaunchFailure::process_creation_failed:
        return "process_creation_failed";
    case InfernoLaunchFailure::dll_initialization_failed:
        return "dll_initialization_failed";
    case InfernoLaunchFailure::timeout:
        return "timeout";
    case InfernoLaunchFailure::nonzero_exit:
        return "nonzero_exit";
    case InfernoLaunchFailure::unsupported_platform:
        return "unsupported_platform";
    }
    return "unknown";
}

InfernoLaunchFailure classify_inferno_exit_code(std::uint32_t exit_code) noexcept {
    if (exit_code == 0) {
        return InfernoLaunchFailure::none;
    }
    if (exit_code == kStatusDllNotFound || exit_code == kStatusEntrypointNotFound
        || exit_code == kStatusInvalidImageFormat) {
        return InfernoLaunchFailure::dll_initialization_failed;
    }
    return InfernoLaunchFailure::nonzero_exit;
}

std::string quote_windows_argument(std::string_view argument) {
    return quote_argument_impl(argument);
}

std::string build_windows_command_line(std::string_view executable,
                                       const std::vector<std::string>& arguments) {
    std::string command_line = quote_windows_argument(executable);
    for (const std::string& argument : arguments) {
        command_line.push_back(' ');
        command_line.append(quote_windows_argument(argument));
    }
    return command_line;
}

std::vector<std::string> read_parent_environment() {
    std::vector<std::string> entries;
#ifdef _WIN32
    LPWCH block = GetEnvironmentStringsW();
    if (block == nullptr) {
        return entries;
    }
    for (const wchar_t* cursor = block; *cursor != L'\0';
         cursor += std::wcslen(cursor) + 1) {
        // "=C:=..." style hidden drive variables are not valid block entries.
        if (*cursor == L'=') {
            continue;
        }
        entries.push_back(narrow(std::wstring_view(cursor)));
    }
    FreeEnvironmentStringsW(block);
#endif
    return entries;
}

std::string find_environment_value(const std::vector<std::string>& environment,
                                   std::string_view name) {
    for (const std::string& entry : environment) {
        const std::size_t separator = entry.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        if (ascii_iequals(std::string_view(entry).substr(0, separator), name)) {
            return entry.substr(separator + 1);
        }
    }
    return {};
}

std::vector<std::string> build_child_environment(
    const std::vector<std::string>& parent,
    const std::filesystem::path& runtime_directory) {
    std::vector<std::string> child = parent;
    const std::string directory = path_to_utf8(runtime_directory);

    for (std::string& entry : child) {
        const std::size_t separator = entry.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        if (!ascii_iequals(std::string_view(entry).substr(0, separator), "PATH")) {
            continue;
        }
        const std::string existing = entry.substr(separator + 1);
        if (path_list_contains(existing, directory)) {
            return child;
        }
        entry = entry.substr(0, separator + 1) + directory
                + (existing.empty() ? std::string() : ";" + existing);
        return child;
    }

    child.push_back("PATH=" + directory);
    return child;
}

bool version_output_identifies_inferno(std::string_view text) {
    return ascii_lower(text).find("chefkiss") != std::string::npos;
}

bool machine_list_contains(std::string_view text, std::string_view machine) {
    if (machine.empty()) {
        return false;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                             : end - start);
        std::size_t begin = 0;
        while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
            ++begin;
        }
        std::size_t finish = begin;
        while (finish < line.size() && line[finish] != ' ' && line[finish] != '\t'
               && line[finish] != '\r') {
            ++finish;
        }
        if (line.substr(begin, finish - begin) == machine) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

InfernoConfigValidation validate_inferno_config(const InfernoConfig& config) {
    InfernoConfigValidation result;
    std::error_code error;

    if (config.executable_path.empty()) {
        result.failure = InfernoLaunchFailure::invalid_configuration;
        result.message =
            "No Inferno executable configured. Pass --inferno-exe <path> (or set "
            "WINIOS_INFERNO_EXE) pointing at the qemu-system-aarch64.exe built "
            "from the pinned Inferno checkout.";
        return result;
    }

    const std::filesystem::file_status executable_status =
        std::filesystem::status(config.executable_path, error);
    if (error || executable_status.type() == std::filesystem::file_type::not_found) {
        result.failure = InfernoLaunchFailure::executable_missing;
        result.message = "Inferno executable not found: '"
                         + path_to_utf8(config.executable_path)
                         + "'. Build the pinned Inferno revision first; no "
                           "substitute QEMU binary is used.";
        return result;
    }
    if (!std::filesystem::is_regular_file(executable_status)) {
        result.failure = InfernoLaunchFailure::invalid_configuration;
        result.message = "The configured Inferno executable is not a regular file: '"
                         + path_to_utf8(config.executable_path) + "'.";
        return result;
    }

    if (config.runtime_directory.empty()) {
        result.failure = InfernoLaunchFailure::invalid_configuration;
        result.message =
            "No UCRT64 runtime directory configured. Pass --inferno-runtime <dir> "
            "(or set WINIOS_INFERNO_RUNTIME) pointing at the MSYS2 ucrt64\\bin "
            "directory that holds the DLLs the emulator loads.";
        return result;
    }

    const std::filesystem::file_status runtime_status =
        std::filesystem::status(config.runtime_directory, error);
    if (error || runtime_status.type() == std::filesystem::file_type::not_found) {
        result.failure = InfernoLaunchFailure::runtime_directory_missing;
        result.message = "UCRT64 runtime directory not found: '"
                         + path_to_utf8(config.runtime_directory)
                         + "'. Pass --inferno-runtime <dir> pointing at the MSYS2 "
                           "ucrt64\\bin directory that holds the DLLs the emulator "
                           "loads (for example ...\\msys2root\\ucrt64\\bin).";
        return result;
    }
    if (!std::filesystem::is_directory(runtime_status)) {
        result.failure = InfernoLaunchFailure::runtime_directory_missing;
        result.message = "The configured UCRT64 runtime path is not a directory: '"
                         + path_to_utf8(config.runtime_directory) + "'.";
        return result;
    }

    if (!config.working_directory.empty()) {
        const std::filesystem::file_status working_status =
            std::filesystem::status(config.working_directory, error);
        if (error || !std::filesystem::is_directory(working_status)) {
            result.failure = InfernoLaunchFailure::invalid_configuration;
            result.message = "The configured working directory does not exist: '"
                             + path_to_utf8(config.working_directory) + "'.";
            return result;
        }
    }

    if (config.timeout.count() <= 0) {
        result.failure = InfernoLaunchFailure::invalid_configuration;
        result.message = "The Inferno timeout must be a positive number of "
                         "milliseconds.";
        return result;
    }
    if (config.max_output_bytes == 0) {
        result.failure = InfernoLaunchFailure::invalid_configuration;
        result.message = "The captured-output limit must be greater than zero.";
        return result;
    }
    return result;
}

InfernoLauncher::InfernoLauncher(InfernoConfig config) : config_(std::move(config)) {}

InfernoConfigValidation InfernoLauncher::validation() const {
    return validate_inferno_config(config_);
}

InfernoLaunchResult InfernoLauncher::run(
    const std::vector<std::string>& arguments) const {
    const InfernoConfigValidation validated = validation();
    if (!validated.valid()) {
        InfernoLaunchResult result;
        result.failure = validated.failure;
        result.error_message = validated.message;
        return result;
    }
#ifdef _WIN32
    return run_windows(config_, arguments);
#else
    InfernoLaunchResult result;
    result.failure = InfernoLaunchFailure::unsupported_platform;
    result.error_message =
        "Launching the Inferno emulator is only implemented for Windows hosts.";
    return result;
#endif
}

InfernoLaunchResult InfernoLauncher::query_version() const {
    return run({"--version"});
}

InfernoLaunchResult InfernoLauncher::query_machine_list() const {
    return run({"-M", "help"});
}

InfernoLaunchResult InfernoLauncher::query_machine_help(
    std::string_view machine) const {
    return run({"-M", std::string(machine) + ",help"});
}
