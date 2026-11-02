#include "cpu/cpu.h"
#include "debugger/debugger.h"
#include "inferno/inferno_launcher.h"
#include "loader/binary_loader.h"
#include "runtime/guest_runtime.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::uint64_t parse_integer(std::string_view text, const char* option) {
    std::uint64_t value = 0;
    const int base = text.starts_with("0x") || text.starts_with("0X") ? 16 : 10;
    const auto first = text.data() + ((base == 16) ? 2 : 0);
    const auto last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::invalid_argument(std::string("Invalid value for ") + option);
    }
    return value;
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  win_ios.exe\n"
              << "  win_ios.exe --debug\n"
              << "  win_ios.exe --run-binary <path> --memory-size <bytes>"
                 " --entry <address> --max-instructions <count> [--trace]\n"
              << "  win_ios.exe --inferno info|version|machines|t8030"
                 " [--inferno-exe <path>] [--inferno-runtime <dir>]\n"
                 "                [--inferno-workdir <dir>] [--inferno-timeout <ms>]\n";
}

std::filesystem::path path_from_utf8(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string path_to_display(const std::filesystem::path& path) {
    if (path.empty()) {
        return "(not configured)";
    }
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void print_launch_result(const InfernoLaunchResult& result) {
    if (!result.standard_output.empty()) {
        std::cout << "--- stdout ---\n" << result.standard_output;
        if (result.standard_output.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (!result.standard_error.empty()) {
        std::cout << "--- stderr ---\n" << result.standard_error;
        if (result.standard_error.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (result.output_truncated) {
        std::cout << "(captured output was truncated at the configured limit)\n";
    }
    if (result.launched) {
        std::cout << "Exit code: " << result.exit_code << '\n';
    }
    std::cout << "Result: "
              << (result.succeeded() ? "success"
                                     : inferno_launch_failure_name(result.failure))
              << '\n';
    if (!result.error_message.empty()) {
        std::cerr << "Launch error: " << result.error_message << '\n';
    }
}

int run_inferno_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view action(argv[2]);
    if (action != "info" && action != "version" && action != "machines"
        && action != "t8030") {
        print_usage();
        return 2;
    }

    InfernoConfig config;
    for (int index = 3; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--inferno-exe") {
            if (++index >= argc) throw std::invalid_argument("Missing Inferno executable path");
            config.executable_path = std::filesystem::path(argv[index]);
        } else if (option == "--inferno-runtime") {
            if (++index >= argc) throw std::invalid_argument("Missing UCRT64 runtime directory");
            config.runtime_directory = std::filesystem::path(argv[index]);
        } else if (option == "--inferno-workdir") {
            if (++index >= argc) throw std::invalid_argument("Missing working directory");
            config.working_directory = std::filesystem::path(argv[index]);
        } else if (option == "--inferno-timeout") {
            if (++index >= argc) throw std::invalid_argument("Missing Inferno timeout");
            const std::uint64_t milliseconds = parse_integer(argv[index], "--inferno-timeout");
            if (milliseconds == 0
                || milliseconds > static_cast<std::uint64_t>(INT32_MAX)) {
                throw std::invalid_argument("Invalid Inferno timeout");
            }
            config.timeout = std::chrono::milliseconds(milliseconds);
        } else {
            print_usage();
            return 2;
        }
    }

    // The executable and runtime directory may also come from read-only
    // environment variables, so an interactive session does not need to repeat
    // the long paths. The launcher never writes to the environment.
    const std::vector<std::string> environment = read_parent_environment();
    if (config.executable_path.empty()) {
        const std::string value = find_environment_value(
            environment, inferno_launcher::kExecutableEnvironmentVariable);
        if (!value.empty()) {
            config.executable_path = path_from_utf8(value);
        }
    }
    if (config.runtime_directory.empty()) {
        const std::string value = find_environment_value(
            environment, inferno_launcher::kRuntimeEnvironmentVariable);
        if (!value.empty()) {
            config.runtime_directory = path_from_utf8(value);
        }
    }

    InfernoLauncher launcher(config);

    if (action == "info") {
        const InfernoConfigValidation validation = launcher.validation();
        std::cout << "Inferno executable  : " << path_to_display(config.executable_path) << '\n'
                  << "UCRT64 runtime dir  : " << path_to_display(config.runtime_directory) << '\n'
                  << "Working directory   : "
                  << (config.working_directory.empty()
                          ? std::string("(emulator directory)")
                          : path_to_display(config.working_directory))
                  << '\n'
                  << "Timeout (ms)        : " << config.timeout.count() << '\n'
                  << "Output limit (bytes): " << config.max_output_bytes
                  << " per stream\n"
                  << "Configuration       : "
                  << (validation.valid() ? std::string("valid")
                                         : std::string("invalid (")
                                               + inferno_launch_failure_name(
                                                     validation.failure)
                                               + ")")
                  << '\n';
        if (!validation.valid()) {
            std::cerr << "Launch error: " << validation.message << '\n';
            return 1;
        }
        return 0;
    }

    if (action == "version") {
        std::cout << "Running: " << path_to_display(config.executable_path)
                  << " --version\n";
        const InfernoLaunchResult result = launcher.query_version();
        print_launch_result(result);
        if (!result.succeeded()) {
            return 1;
        }
        const bool identified = version_output_identifies_inferno(result.standard_output);
        std::cout << "Inferno identity: "
                  << (identified ? "confirmed" : "not recognised") << '\n';
        return identified ? 0 : 1;
    }

    if (action == "machines") {
        std::cout << "Running: " << path_to_display(config.executable_path)
                  << " -M help\n";
        const InfernoLaunchResult result = launcher.query_machine_list();
        print_launch_result(result);
        if (!result.succeeded()) {
            return 1;
        }
        const bool registered = machine_list_contains(result.standard_output, "t8030");
        std::cout << "t8030 registered: " << (registered ? "yes" : "no") << '\n';
        return registered ? 0 : 1;
    }

    std::cout << "Running: " << path_to_display(config.executable_path)
              << " -M t8030,help\n";
    const InfernoLaunchResult result = launcher.query_machine_help("t8030");
    print_launch_result(result);
    if (!result.succeeded()) {
        return 1;
    }
    std::cout << "t8030 help reported: " << result.standard_output.size()
              << " bytes\n";
    return 0;
}

int run_binary_mode(int argc, char* argv[]) {
    std::string binary_path;
    std::size_t memory_size = 0;
    std::uint64_t entry = 0;
    std::uint64_t stack_base = 0;
    std::size_t stack_size = 0;
    std::size_t max_instructions = 0;
    bool have_memory_size = false;
    bool have_entry = false;
    bool have_limit = false;
    std::optional<std::uint64_t> sentinel;
    bool trace = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--run-binary") {
            if (++index >= argc) throw std::invalid_argument("Missing binary path");
            binary_path = argv[index];
        } else if (option == "--memory-size") {
            if (++index >= argc) throw std::invalid_argument("Missing memory size");
            memory_size = static_cast<std::size_t>(
                parse_integer(argv[index], "--memory-size"));
            have_memory_size = true;
        } else if (option == "--entry") {
            if (++index >= argc) throw std::invalid_argument("Missing entry address");
            entry = parse_integer(argv[index], "--entry");
            have_entry = true;
        } else if (option == "--max-instructions") {
            if (++index >= argc) throw std::invalid_argument("Missing instruction limit");
            max_instructions = static_cast<std::size_t>(
                parse_integer(argv[index], "--max-instructions"));
            have_limit = true;
        } else if (option == "--sentinel") {
            if (++index >= argc) throw std::invalid_argument("Missing completion sentinel");
            sentinel = parse_integer(argv[index], "--sentinel");
        } else if (option == "--trace") {
            trace = true;
        } else {
            throw std::invalid_argument("Unknown binary-run option: " + std::string(option));
        }
    }
    if (binary_path.empty() || !have_memory_size || !have_entry || !have_limit
        || memory_size == 0) {
        throw std::invalid_argument(
            "--run-binary requires path, nonzero memory-size, entry, and "
            "max-instructions");
    }

    stack_size = 4096;
    if (memory_size < stack_size) {
        throw std::invalid_argument("Memory size is too small for the default stack");
    }
    stack_base = memory_size - stack_size;
    RuntimeConfig config{memory_size, entry, entry, stack_base, stack_size,
                         max_instructions, sentinel, trace};
    CPU::TraceSink sink;
    if (trace) {
        sink = [](const CPU::TraceRecord& record) {
            std::cout << "PC=0x" << std::hex << record.instruction_address
                      << " WORD=0x" << record.instruction_word << std::dec
                      << " " << record.disassembly << " PC_AFTER=0x"
                      << std::hex << record.program_counter_after << std::dec
                      << '\n';
        };
    }
    const ExecutionResult result = GuestRuntime::execute(binary_path, config, sink);
    std::cout << "Status: " << execution_status_name(result.status) << '\n'
              << "Loaded bytes: " << result.loaded_bytes << '\n'
              << "Instructions executed: " << result.instructions_executed << '\n'
              << "Final PC: 0x" << std::hex << result.final_pc << std::dec << '\n'
              << "X0: " << result.final_registers[0] << '\n'
              << "X1: " << result.final_registers[1] << '\n'
              << "SP: " << result.final_stack_pointer << '\n';
    if (result.memory_at_0x100.has_value()) {
        std::cout << "Memory[0x100]: " << *result.memory_at_0x100 << '\n';
    }
    if (!result.error.empty()) {
        std::cerr << "Execution error: " << result.error << '\n';
    }
    return result.status == ExecutionStatus::completed ? 0
         : result.status == ExecutionStatus::instruction_limit_reached ? 2 : 1;
}
} // namespace

int main(int argc, char* argv[]) {
    std::cout << "WinIOS v0.1.0\n";
    std::cout << "ARM64 emulation research prototype\n";

    try {
    if (argc > 1 && std::string_view(argv[1]) == "--run-binary") {
        return run_binary_mode(argc, argv);
    }
    if (argc > 1 && std::string_view(argv[1]) == "--inferno") {
        return run_inferno_mode(argc, argv);
    }
    if (argc > 1 && std::string_view(argv[1]) == "--debug") {
        Memory memory(8);
        memory.write_u32(0, 0xD2800540u); // MOVZ X0, #42
        memory.write_u32(4, 0x91001401u); // ADD X1, X0, #5
        CPU cpu;
        Debugger debugger(cpu, memory);
        debugger.set_tracing(true, [](const CPU::TraceRecord& record) {
            std::cout << "PC=0x" << std::hex << record.instruction_address
                      << " WORD=0x" << record.instruction_word << std::dec
                      << " " << record.disassembly << " PC_AFTER=0x"
                      << std::hex << record.program_counter_after << std::dec
                      << '\n';
        });
        std::cout << debugger.inspect_cpu() << '\n';
        debugger.continue_execution(2);
        std::cout << debugger.inspect_cpu() << '\n';
        std::cout << "Memory[0..7]: " << debugger.inspect_memory(0, 8) << '\n';
        return 0;
    }
    if (argc > 1) {
        print_usage();
        return 2;
    }

    CPU cpu;
    cpu.run_demo();

    return 0;
    } catch (const std::exception& error) {
        std::cerr << "Execution error: " << error.what() << '\n';
        return 1;
    }
}
