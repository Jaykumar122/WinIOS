#include "cpu/cpu.h"
#include "debugger/debugger.h"
#include "inferno/inferno_launcher.h"
#include "loader/binary_loader.h"
#include "runtime/guest_runtime.h"
#include "trace/trace_analysis_report.h"
#include "trace/trace_analyzer.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_validation.h"
#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_validator.h"
#include "replay/replay_engine.h"
#include "replay/replay_report.h"
#include "replay/replay_diagnostics.h"
#include "replay/replay_memory_report.h"
#include "replay/replay_profiler.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_scheduler.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
                 "                [--inferno-workdir <dir>] [--inferno-timeout <ms>]\n"
              << "  win_ios.exe trace inspect|summary|timeline|validate|export <trace-file.wtr>\n"
                 "                [--core <id>] [--event <id>] [--from-sequence <seq>]\n"
                 "                [--to-sequence <seq>] [--from-time <time>] [--to-time <time>]\n"
                 "                [--max-events <count>] [--output <path>] [--format text|json]\n"
              << "  win_ios.exe snapshot inspect|validate|create|restore <file.wsnap>\n"
              << "  win_ios.exe sndl inspect|validate|summary <file.sndl> [--snapshot <file.wsnap>]\n"
              << "  win_ios.exe replay validate|inspect|run <file.wsnap> <file.sndl> [--max-instructions <n>] [--trace-wtr <out.wtr>] [--divergence-report <rep.json>] [--checkpoint-interval <n>] [--allow-smp] [--allow-four-core] [--allow-eight-core]\n"
              << "  win_ios.exe replay profile <file.wsnap> <file.sndl> [--max-instructions <n>] [--checkpoint-interval <n>] [--output <profile.json|profile.txt>] [--format text|json]\n"
              << "  win_ios.exe replay benchmark <file.wsnap> <file.sndl> [--max-instructions <n>] [--checkpoint-interval <n>] [--iterations <n>] [--profiling]\n"
              << "  win_ios.exe replay diagnostics <file.wsnap> <file.sndl> [--max-instructions <n>] [--checkpoint-interval <n>]\n"
              << "  win_ios.exe replay memory-report <file.wsnap> <file.sndl> [--label <text>] [--max-instructions <n>] [--checkpoint-interval <n>] [--output <report.json|report.txt>] [--format text|json]\n"
              << "  win_ios.exe replay smp-readiness\n"
              << "  win_ios.exe replay scalability <file.wsnap> <file.sndl> [--max-instructions <n>] [--checkpoint-interval <n>]\n"
              << "  win_ios.exe smp inspect|cores|scheduler|benchmark|readiness [--cores <n>] [--quantum <n>] [--max-instructions <n>]\n"
              << "  win_ios.exe smp-snapshot inspect|validate|capture|restore|compare <file.wsnap> [--cores <n>] [--quantum <n>]\n"
              << "  win_ios.exe sndl2 inspect|summary|validate|events|compare|record <file.sndl> [--snapshot <file.wsnap>] [--cores <n>] [--instructions <n>] [--max-events <n>]\n";
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

int run_trace_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view command = argv[2];
    if (argc < 4) {
        std::cerr << "Error: Missing trace file path\n";
        print_usage();
        return 2;
    }
    const std::filesystem::path trace_file = argv[3];

    winios::trace::TraceFilter filter;
    std::string format = "text";
    std::string output_path;
    std::size_t max_events = 1000;

    for (int i = 4; i < argc; ++i) {
        const std::string_view opt = argv[i];
        if (opt == "--core") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --core");
            const auto core_val = static_cast<std::uint16_t>(parse_integer(argv[i], "--core"));
            if (!filter.core_ids.has_value()) filter.core_ids = std::vector<std::uint16_t>{};
            filter.core_ids->push_back(core_val);
        } else if (opt == "--event") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --event");
            const auto event_val = static_cast<winios::trace::TraceEventType>(parse_integer(argv[i], "--event"));
            if (!filter.event_types.has_value()) filter.event_types = std::vector<winios::trace::TraceEventType>{};
            filter.event_types->push_back(event_val);
        } else if (opt == "--from-sequence") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --from-sequence");
            filter.min_sequence = parse_integer(argv[i], "--from-sequence");
        } else if (opt == "--to-sequence") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --to-sequence");
            filter.max_sequence = parse_integer(argv[i], "--to-sequence");
        } else if (opt == "--from-time") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --from-time");
            filter.min_timestamp = parse_integer(argv[i], "--from-time");
        } else if (opt == "--to-time") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --to-time");
            filter.max_timestamp = parse_integer(argv[i], "--to-time");
        } else if (opt == "--format") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --format");
            format = argv[i];
        } else if (opt == "--output") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --output");
            output_path = argv[i];
        } else if (opt == "--max-events") {
            if (++i >= argc) throw std::invalid_argument("Missing value for --max-events");
            max_events = static_cast<std::size_t>(parse_integer(argv[i], "--max-events"));
        } else {
            throw std::invalid_argument("Unknown trace option: " + std::string(opt));
        }
    }

    winios::trace::TraceAnalyzer analyzer;
    std::string output_str;

    if (command == "inspect") {
        const auto insp = analyzer.inspect_file(trace_file);
        output_str = winios::trace::format_inspection_report(insp);
    } else if (command == "summary") {
        const auto rep = analyzer.analyze_file(trace_file, filter, max_events);
        if (format == "json") {
            output_str = winios::trace::export_report_to_json(rep);
        } else {
            output_str = winios::trace::format_summary_report(rep);
        }
    } else if (command == "validate") {
        const auto rep = analyzer.analyze_file(trace_file, filter, max_events);
        if (format == "json") {
            output_str = winios::trace::export_report_to_json(rep);
        } else {
            output_str = winios::trace::format_validation_report(rep);
        }
    } else if (command == "timeline") {
        const auto timeline = analyzer.extract_timeline(trace_file, filter, max_events);
        if (format == "json") {
            output_str = winios::trace::export_timeline_to_json(timeline);
        } else {
            output_str = winios::trace::format_timeline_report(timeline);
        }
    } else if (command == "export") {
        const auto rep = analyzer.analyze_file(trace_file, filter, max_events);
        output_str = winios::trace::export_report_to_json(rep);
    } else {
        std::cerr << "Unknown trace command: " << command << "\n";
        print_usage();
        return 2;
    }

    if (!output_path.empty()) {
        std::ofstream ofs(output_path);
        if (!ofs) {
            std::cerr << "Error: Could not open output file " << output_path << "\n";
            return 1;
        }
        ofs << output_str;
    } else {
        std::cout << output_str;
    }
    return 0;
}
} // namespace

int run_sndl_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view action = argv[2];
    if (action == "inspect" || action == "summary") {
        if (argc < 4) {
            std::cerr << "Error: Missing SNDL file path\n";
            return 2;
        }
        winios::sndl::SndlInspectionInfo info{};
        const auto res = winios::sndl::SndlValidation::inspect_file(argv[3], info);
        if (res.is_failure()) {
            std::cerr << "Inspection error: " << res.message << "\n";
            return 1;
        }
        std::cout << winios::sndl::SndlValidation::format_summary(info);
        return 0;
    }
    if (action == "validate") {
        if (argc < 4) {
            std::cerr << "Error: Missing SNDL file path\n";
            return 2;
        }
        std::optional<std::filesystem::path> snapshot_path = std::nullopt;
        for (int i = 4; i < argc; ++i) {
            if ((std::string_view(argv[i]) == "--snapshot" || std::string_view(argv[i]) == "-s") && i + 1 < argc) {
                snapshot_path = argv[++i];
            }
        }
        const auto val_res = winios::sndl::SndlValidation::validate_file(argv[3], snapshot_path);
        if (!val_res.is_valid) {
            std::cerr << "SNDL validation FAILED: " << val_res.message << "\n";
            return 1;
        }
        std::cout << "SNDL validation SUCCESS: " << argv[3] << "\n";
        std::cout << "  " << val_res.message << "\n";
        std::cout << "  Records: " << val_res.total_records << " | Blocks: " << val_res.total_blocks << "\n";
        if (val_res.snapshot_checked) {
            std::cout << "  Baseline Snapshot Match: " << (val_res.snapshot_matched ? "YES" : "NO") << "\n";
        }
        return 0;
    }
    print_usage();
    return 2;
}

int run_snapshot_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view action = argv[2];
    if (action == "inspect") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 2;
        }
        winios::snapshot::SnapshotInspection inspection{};
        const auto res = winios::snapshot::SnapshotManager::inspect_file(argv[3], inspection);
        if (!res.success) {
            std::cerr << "Inspection error: " << res.error_message << "\n";
            return 1;
        }
        std::cout << inspection.formatted_summary;
        return 0;
    }
    if (action == "validate") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 2;
        }
        const auto val_res = winios::snapshot::SnapshotManager::validate_file(argv[3]);
        if (!val_res.valid) {
            std::cerr << "Snapshot validation FAILED: " << val_res.error_message << "\n";
            for (const auto& d : val_res.diagnostics) {
                std::cerr << "  " << d << "\n";
            }
            return 1;
        }
        std::cout << "Snapshot validation SUCCESS: " << argv[3] << "\n";
        for (const auto& d : val_res.diagnostics) {
            std::cout << "  " << d << "\n";
        }
        return 0;
    }
    if (action == "create") {
        if (argc < 4) {
            std::cerr << "Error: Missing output snapshot path\n";
            return 2;
        }
        CPU cpu;
        Memory memory(1024 * 1024);
        winios::VirtualTime vt;
        winios::GICv3 gic;
        winios::snapshot::SnapshotRestoreContext ctx{&cpu, &memory, &vt, &gic, {}};
        winios::snapshot::SnapshotCaptureOptions opts{};
        opts.description = "WinIOS Baseline Snapshot";
        opts.creator = "WinIOS CLI";
        const auto res = winios::snapshot::SnapshotManager::capture_to_file(ctx, argv[3], opts);
        if (!res.success) {
            std::cerr << "Snapshot creation failed: " << res.error_message << "\n";
            return 1;
        }
        std::cout << "Created architectural snapshot: " << argv[3] << "\n";
        return 0;
    }
    if (action == "restore") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 2;
        }
        winios::snapshot::SnapshotInspection inspection{};
        auto res = winios::snapshot::SnapshotManager::inspect_file(argv[3], inspection);
        if (!res.success) {
            std::cerr << "Failed to inspect snapshot before restore: " << res.error_message << "\n";
            return 1;
        }
        const std::size_t mem_size = inspection.header.memory_capacity > 0 ? static_cast<std::size_t>(inspection.header.memory_capacity) : 1024 * 1024;
        CPU cpu;
        Memory memory(mem_size);
        winios::VirtualTime vt;
        winios::GICv3 gic;
        winios::snapshot::SnapshotRestoreContext ctx{&cpu, &memory, &vt, &gic, {}};
        res = winios::snapshot::SnapshotManager::restore_from_file(argv[3], ctx);
        if (!res.success) {
            std::cerr << "Snapshot restoration failed: " << res.error_message << "\n";
            return 1;
        }
        std::cout << "Successfully restored snapshot: " << argv[3] << "\n";
        std::cout << "Restored PC: 0x" << std::hex << cpu.get_program_counter() << std::dec << "\n";
        return 0;
    }
    print_usage();
    return 2;
}

int run_replay_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view action = argv[2];
    if (action == "validate") {
        if (argc < 5) {
            std::cerr << "Error: Replay validate requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        const auto res = winios::replay::ReplayEngine::validate(wsnap_path, sndl_path);
        if (res.is_failure()) {
            std::cerr << "Replay validation FAILED: " << res.message << "\n";
            return 1;
        }
        std::cout << "Replay validation SUCCESS: Snapshot and SNDL log are valid and compatible.\n";
        return 0;
    }
    if (action == "inspect") {
        if (argc < 5) {
            std::cerr << "Error: Replay inspect requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayInspectionInfo info{};
        const auto res = winios::replay::ReplayEngine::inspect(wsnap_path, sndl_path, info);
        if (res.is_failure()) {
            std::cerr << "Replay inspection FAILED: " << res.message << "\n";
            return 1;
        }
        std::cout << winios::replay::ReplayEngine::format_inspection(info);
        return 0;
    }
    if (action == "run") {
        if (argc < 5) {
            std::cerr << "Error: Replay run requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};

        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = std::stoull(argv[++i]);
            } else if (opt == "--trace-wtr" && i + 1 < argc) {
                config.enable_wtr_tracing = true;
                config.wtr_output_path = argv[++i];
            } else if (opt == "--divergence-report" && i + 1 < argc) {
                config.divergence_report_path = argv[++i];
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = std::stoull(argv[++i]);
            } else if (opt == "--allow-smp") {
                config.allow_experimental_smp = true;
                config.mode = winios::replay::ReplayMode::MultiCore;
            } else if (opt == "--allow-four-core") {
                config.allow_four_core_replay = true;
            } else if (opt == "--allow-eight-core") {
                config.allow_eight_core_replay = true;
            }
        }

        winios::replay::ReplayExecutionStats stats{};
        const auto res = winios::replay::ReplayEngine::run(wsnap_path, sndl_path, config, stats);
        std::cout << winios::replay::ReplayReport::to_summary_string(stats);
        if (res.is_failure()) {
            std::cerr << "Replay FAILED: " << res.message << "\n";
            return 1;
        }
        return 0;
    }
    if (action == "profile") {
        if (argc < 5) {
            std::cerr << "Error: Replay profile requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};
        config.enable_profiling = true;
        std::string output_path;
        bool json_format = true;

        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = parse_integer(argv[++i], "--max-instructions");
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = parse_integer(argv[++i], "--checkpoint-interval");
            } else if (opt == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (opt == "--format" && i + 1 < argc) {
                const std::string_view format = argv[++i];
                if (format == "text") {
                    json_format = false;
                } else if (format == "json") {
                    json_format = true;
                } else {
                    std::cerr << "Error: Unsupported profile format: " << format << "\n";
                    return 2;
                }
            }
        }

        winios::replay::ReplayExecutionStats stats{};
        winios::replay::ReplayProfile profile{};
        const auto res = winios::replay::ReplayEngine::run(
            wsnap_path, sndl_path, config, stats, &profile);
        std::cout << winios::replay::ReplayReport::to_summary_string(stats);
        if (res.is_failure()) {
            std::cerr << "Replay profile FAILED: " << res.message << "\n";
            return 1;
        }
        if (output_path.empty()) {
            std::cout << (json_format ? winios::replay::ReplayProfiler::to_json(profile)
                                      : winios::replay::ReplayProfiler::to_text(profile));
        } else {
            const auto write_res = json_format
                ? winios::replay::ReplayProfiler::write_json(path_from_utf8(output_path), profile)
                : winios::replay::ReplayProfiler::write_text(path_from_utf8(output_path), profile);
            if (write_res.is_failure()) {
                std::cerr << "Failed to write profile report: " << write_res.message << "\n";
                return 1;
            }
            std::cout << "Profile report written to " << output_path << "\n";
        }
        return 0;
    }
    if (action == "benchmark") {
        if (argc < 5) {
            std::cerr << "Error: Replay benchmark requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};
        std::uint64_t iterations = 3;
        bool profiling = false;

        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = parse_integer(argv[++i], "--max-instructions");
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = parse_integer(argv[++i], "--checkpoint-interval");
            } else if (opt == "--iterations" && i + 1 < argc) {
                iterations = parse_integer(argv[++i], "--iterations");
                if (iterations == 0) {
                    std::cerr << "Error: --iterations must be at least 1\n";
                    return 2;
                }
            } else if (opt == "--profiling") {
                profiling = true;
            }
        }

        std::vector<double> wall_ms;
        std::vector<double> replay_ms;
        std::vector<winios::replay::ReplayExecutionStats> results;
        wall_ms.reserve(static_cast<std::size_t>(iterations));
        replay_ms.reserve(static_cast<std::size_t>(iterations));
        results.reserve(static_cast<std::size_t>(iterations));

        std::cout << "Stage 21 replay benchmark: " << iterations
                  << " iteration(s), profiling=" << (profiling ? "on" : "off") << "\n";
        for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            winios::replay::ReplayExecutionStats stats{};
            winios::replay::ReplayProfile profile{};
            const auto res = winios::replay::ReplayEngine::run(
                wsnap_path, sndl_path, config, stats, profiling ? &profile : nullptr);
            const auto end = std::chrono::steady_clock::now();
            const double observed_ms =
                std::chrono::duration<double, std::milli>(end - start).count();
            if (res.is_failure()) {
                std::cerr << "Replay benchmark iteration " << iteration
                          << " FAILED: " << res.message << "\n";
                return 1;
            }
            wall_ms.push_back(observed_ms);
            replay_ms.push_back(stats.elapsed_ms);
            results.push_back(stats);
            const double mips = stats.elapsed_ms > 0.0
                ? static_cast<double>(stats.instructions_executed) / (stats.elapsed_ms * 1000.0)
                : 0.0;
            std::cout << "  iteration " << iteration
                      << ": wall=" << std::fixed << std::setprecision(2) << observed_ms
                      << " ms, replay=" << stats.elapsed_ms
                      << " ms, instructions=" << stats.instructions_executed
                      << ", events=" << stats.events_consumed
                      << ", checkpoints=" << stats.checkpoints_taken
                      << ", MIPS=" << std::setprecision(4) << mips
                      << ", combined=0x" << std::hex
                      << stats.final_state_hashes.combined_hash << std::dec << "\n";
        }

        const auto [min_wall, max_wall] = std::minmax_element(wall_ms.begin(), wall_ms.end());
        double wall_sum = 0.0;
        double replay_sum = 0.0;
        bool reproducible = true;
        for (std::size_t i = 0; i < results.size(); ++i) {
            wall_sum += wall_ms[i];
            replay_sum += replay_ms[i];
            if (results[i].final_state_hashes != results[0].final_state_hashes ||
                results[i].instructions_executed != results[0].instructions_executed ||
                results[i].events_consumed != results[0].events_consumed ||
                results[i].completion_status != results[0].completion_status) {
                reproducible = false;
            }
        }
        const double mean_wall = wall_sum / static_cast<double>(wall_ms.size());
        std::cout << "Summary: mean wall=" << std::fixed << std::setprecision(2)
                  << mean_wall << " ms, min=" << *min_wall
                  << " ms, max=" << *max_wall
                  << " ms, mean replay=" << (replay_sum / static_cast<double>(replay_ms.size()))
                  << " ms, hash_reproducible=" << (reproducible ? "yes" : "no") << "\n";
        if (!reproducible) {
            std::cerr << "Replay benchmark FAILED: iterations produced different results\n";
            return 1;
        }
        return 0;
    }
    if (action == "diagnostics") {
        if (argc < 5) {
            std::cerr << "Error: Replay diagnostics requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};
        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = parse_integer(argv[++i], "--max-instructions");
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = parse_integer(argv[++i], "--checkpoint-interval");
            }
        }

        const auto artifact_report =
            winios::replay::ReplayDiagnostics::validate_artifacts(wsnap_path, sndl_path, true);
        std::cout << winios::replay::ReplayDiagnostics::format_artifact_report(artifact_report);
        if (!artifact_report.valid) {
            return 1;
        }
        const auto verification = winios::replay::ReplayDiagnostics::verify_replay(
            wsnap_path, sndl_path, config, true);
        std::cout << winios::replay::ReplayDiagnostics::format_verification(verification);
        return verification.valid ? 0 : 1;
    }
    if (action == "memory-report") {
        if (argc < 5) {
            std::cerr << "Error: Replay memory-report requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};
        std::string label = "replay";
        std::string output_path;
        bool json_format = true;

        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = parse_integer(argv[++i], "--max-instructions");
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = parse_integer(argv[++i], "--checkpoint-interval");
            } else if (opt == "--label" && i + 1 < argc) {
                label = argv[++i];
            } else if (opt == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (opt == "--format" && i + 1 < argc) {
                const std::string_view format = argv[++i];
                if (format == "text") {
                    json_format = false;
                } else if (format == "json") {
                    json_format = true;
                } else {
                    std::cerr << "Error: Unsupported memory report format: " << format << "\n";
                    return 2;
                }
            }
        }

        const auto report =
            winios::replay::ReplayMemoryReporter::measure(wsnap_path, sndl_path, config, label);
        if (output_path.empty()) {
            std::cout << (json_format ? winios::replay::ReplayMemoryReporter::to_json(report)
                                      : winios::replay::ReplayMemoryReporter::to_text(report));
        } else {
            const auto write_res = json_format
                ? winios::replay::ReplayMemoryReporter::write_json(path_from_utf8(output_path), report)
                : winios::replay::ReplayMemoryReporter::write_text(path_from_utf8(output_path), report);
            if (write_res.is_failure()) {
                std::cerr << "Failed to write memory report: " << write_res.message << "\n";
                return 1;
            }
            std::cout << "Memory report written to " << output_path << "\n";
        }
        return report.replay_success ? 0 : 1;
    }
    if (action == "smp-readiness") {
        const auto assessment = winios::replay::ReplayDiagnostics::assess_smp_readiness();
        std::cout << winios::replay::ReplayDiagnostics::format_smp_readiness(assessment);
        return 0;
    }
    if (action == "scalability") {
        if (argc < 5) {
            std::cerr << "Error: Replay scalability requires <snapshot.wsnap> and <log.sndl>\n";
            return 2;
        }
        const auto wsnap_path = argv[3];
        const auto sndl_path = argv[4];
        winios::replay::ReplayConfig config{};
        for (int i = 5; i < argc; ++i) {
            const std::string_view opt = argv[i];
            if (opt == "--max-instructions" && i + 1 < argc) {
                config.max_instructions = parse_integer(argv[++i], "--max-instructions");
            } else if (opt == "--checkpoint-interval" && i + 1 < argc) {
                config.checkpoint_interval = parse_integer(argv[++i], "--checkpoint-interval");
            }
        }

        std::cout << "================================================================================\n";
        std::cout << "             WINIOS STAGE 21 SCALABILITY SUMMARY (SINGLE-CORE ONLY)             \n";
        std::cout << "================================================================================\n";
        const auto artifact_report =
            winios::replay::ReplayDiagnostics::validate_artifacts(wsnap_path, sndl_path, true);
        std::cout << "Artifact Validation:    " << (artifact_report.valid ? "PASS" : "FAIL") << "\n";
        std::cout << "WSNap Size:             " << artifact_report.wsnap_file_size << " bytes\n";
        std::cout << "SNDL Size:              " << artifact_report.sndl_file_size << " bytes\n";
        std::cout << "Recorded Instructions:  " << artifact_report.total_instructions << "\n";
        std::cout << "Recorded Records:       " << artifact_report.total_records << "\n";
        if (!artifact_report.valid) {
            std::cout << "Scalability Verdict:    FAIL (invalid artifacts)\n";
            std::cout << "================================================================================\n";
            return 1;
        }
        const auto verification = winios::replay::ReplayDiagnostics::verify_replay(
            wsnap_path, sndl_path, config, true);
        std::cout << "Exact Replay:           " << (verification.valid ? "PASS" : "FAIL") << "\n";
        std::cout << "Instructions Executed:  " << verification.instructions_executed << "\n";
        std::cout << "Events Consumed:        " << verification.events_consumed
                  << " / " << verification.events_expected << "\n";
        std::cout << "Checkpoints Taken:      " << verification.checkpoints_taken << "\n";
        std::cout << "Combined Hash:          0x" << std::hex
                  << verification.hashes.combined_hash << std::dec << "\n";
        std::cout << "SMP Readiness:          NOT READY (multi-core replay rejected)\n";
        std::cout << "Scalability Verdict:    "
                  << (verification.valid ? "PASS (single-core)" : "FAIL") << "\n";
        std::cout << "================================================================================\n";
        return verification.valid ? 0 : 1;
    }
    print_usage();
    return 2;
}

int run_smp_mode(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: win_ios.exe smp inspect|cores|scheduler|benchmark|readiness [options]\n";
        return 2;
    }
    const std::string_view action = argv[2];
    std::size_t num_cores = 4;
    std::uint64_t quantum = 10000;
    std::uint64_t max_instructions = 100000;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--cores" && i + 1 < argc) {
            num_cores = static_cast<std::size_t>(parse_integer(argv[++i], "--cores"));
        } else if (arg == "--quantum" && i + 1 < argc) {
            quantum = parse_integer(argv[++i], "--quantum");
        } else if (arg == "--max-instructions" && i + 1 < argc) {
            max_instructions = parse_integer(argv[++i], "--max-instructions");
        }
    }

    if (action == "inspect") {
        std::cout << "================================================================================\n";
        std::cout << "WinIOS SMP Foundation Architectural Subsystem Overview\n";
        std::cout << "================================================================================\n";
        std::cout << "Architecture:           AArch64 Multi-Core Foundation (ARMv8-A)\n";
        std::cout << "Host Execution Model:   Deterministic Single Host Thread Round-Robin Scheduler\n";
        std::cout << "Default Quantum (Q):    " << winios::SmpScheduler::kDefaultQuantumInstructions << " instructions\n";
        std::cout << "Physical Memory Model:  Centralized SmpSharedMemory with Address Identity\n";
        std::cout << "Exclusive Monitor:      SmpExclusiveMonitorManager (64-byte granule, LDXR/STXR/CLREX)\n";
        std::cout << "Interrupt Controller:   SmpInterruptRouter (GICv3 SGI/PPI/SPI deterministic queues)\n";
        std::cout << "State Isolation:        SmpCoreState (Independent GPRs, SIMD, Sysregs, Lifecycle)\n";
        std::cout << "Replay Integration:     STRICTLY REJECTED (MultiCore replay explicitly gated)\n";
        std::cout << "================================================================================\n";
        return 0;
    }

    if (action == "cores") {
        std::cout << "================================================================================\n";
        std::cout << "WinIOS Virtual Core Allocation Inspection (" << num_cores << " Cores)\n";
        std::cout << "================================================================================\n";
        for (std::size_t i = 0; i < num_cores; ++i) {
            winios::SmpCoreState state;
            state.reset(i, (i == 0) ? winios::CoreLifecycleState::Runnable : winios::CoreLifecycleState::Halted);
            std::cout << "  Core " << i << ": Lifecycle=" << winios::core_lifecycle_state_name(state.lifecycle)
                      << " | Canonical Hash=0x" << std::hex << state.compute_canonical_hash() << std::dec << "\n";
        }
        std::cout << "================================================================================\n";
        return 0;
    }

    if (action == "scheduler") {
        std::cout << "================================================================================\n";
        std::cout << "WinIOS Deterministic SMP Scheduler Execution (" << num_cores << " Cores, Q=" << quantum << ")\n";
        std::cout << "================================================================================\n";
        const std::size_t mem_sz = (num_cores + 1) * 65536;
        auto mem = std::make_shared<winios::SmpSharedMemory>(mem_sz, num_cores);
        winios::SmpScheduler sched(mem, num_cores, quantum);

        for (std::size_t c = 0; c < num_cores; ++c) {
            const std::uint64_t base = c * 0x1000;
            for (std::size_t i = 0; i < 500; ++i) {
                mem->write_u32(c, base + i * 4, 0x91000400u + static_cast<std::uint32_t>(c));
            }
            mem->write_u32(c, base + 500 * 4, 0xD4400000u); // HLT
            auto cpu = std::make_shared<winios::CPU>();
            cpu->set_program_counter(base);
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t executed = sched.run_deterministic(max_instructions);
        const auto end = std::chrono::steady_clock::now();
        const double dur_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Instructions Retired:   " << executed << "\n";
        std::cout << "Elapsed Time:           " << std::fixed << std::setprecision(2) << dur_ms << " ms\n";
        std::cout << "Scheduling Throughput:  " << (executed / (dur_ms / 1000.0) / 1e6) << " MIPS\n";
        std::cout << "Quantums Executed:      " << sched.transition_history().size() << "\n";
        std::cout << "Canonical SMP Hash:     0x" << std::hex << sched.compute_canonical_multicore_hash() << std::dec << "\n";
        std::cout << "================================================================================\n";
        return 0;
    }

    if (action == "benchmark") {
        std::cout << "Running WinIOS SMP Foundation Quick Benchmark (" << num_cores << " Cores)...\n";
        const std::size_t mem_sz = (num_cores + 1) * 65536;
        auto mem = std::make_shared<winios::SmpSharedMemory>(mem_sz, num_cores);
        winios::SmpScheduler sched(mem, num_cores, quantum);

        for (std::size_t c = 0; c < num_cores; ++c) {
            const std::uint64_t base = c * 0x1000;
            for (std::size_t i = 0; i < 1000; ++i) {
                mem->write_u32(c, base + i * 4, 0x91000400u);
            }
            mem->write_u32(c, base + 1000 * 4, 0x17ffff00u); // B -4000
            auto cpu = std::make_shared<winios::CPU>();
            cpu->set_program_counter(base);
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t executed = sched.run_deterministic(max_instructions);
        const auto end = std::chrono::steady_clock::now();
        const double dur_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "Throughput: " << std::fixed << std::setprecision(2)
                  << (executed / (dur_ms / 1000.0) / 1e6) << " MIPS ("
                  << executed << " instructions in " << dur_ms << " ms)\n";
        return 0;
    }

    if (action == "readiness") {
        std::cout << "================================================================================\n";
        std::cout << "WinIOS Deterministic SMP Execution & Replay Readiness Assessment\n";
        std::cout << "================================================================================\n";
        std::cout << "Core State Architecture:           READY\n";
        std::cout << "Shared Physical Memory:            READY\n";
        std::cout << "Exclusive Monitor Subsystem:       READY\n";
        std::cout << "Cross-Core Interrupt Router:       READY\n";
        std::cout << "Deterministic Scheduler:           READY (Single Host Thread Round-Robin)\n";
        std::cout << "Multicore Canonical Hash:          READY\n";
        std::cout << "WSnap Multi-Core Snapshotting:     READY (.wsnap v2 cluster snapshots fully supported)\n";
        std::cout << "--------------------------------------------------------------------------------\n";
        std::cout << "SNDL Multi-Core Recording (v2):    NOT READY (Format schema v1 active)\n";
        std::cout << "SMP Deterministic Replay Engine:   NOT READY (Strictly UnsupportedConfiguration)\n";
        std::cout << "Overall SMP Replay Verdict:        NOT READY (Mandatory Rejection Maintained)\n";
        std::cout << "================================================================================\n";
        return 0;
    }

    std::cerr << "Unknown smp action: " << action << "\n";
    return 2;
}

int run_smp_snapshot_mode(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: win_ios.exe smp-snapshot inspect|validate|capture|restore|compare <args...>\n";
        return 1;
    }

    const std::string_view action = argv[2];

    if (action == "inspect") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 1;
        }
        winios::snapshot::SmpSnapshotInspection inspection{};
        const auto res = winios::snapshot::SmpSnapshotManager::inspect_file(argv[3], inspection);
        if (!res.success) {
            std::cerr << "Failed to inspect cluster snapshot: " << res.error_message << "\n";
            return 1;
        }
        std::cout << inspection.formatted_summary << "\n";
        return 0;
    }

    if (action == "validate") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 1;
        }
        const auto val_res = winios::snapshot::SmpSnapshotManager::validate_file(argv[3]);
        if (!val_res.valid) {
            std::cerr << "Cluster snapshot validation FAILED: " << val_res.error_message << "\n";
            for (const auto& diag : val_res.diagnostics) {
                std::cerr << "  diag: " << diag << "\n";
            }
            return 1;
        }
        std::cout << "Cluster snapshot validation SUCCESS: " << argv[3] << "\n";
        return 0;
    }

    if (action == "capture") {
        if (argc < 4) {
            std::cerr << "Error: Missing output snapshot path\n";
            return 1;
        }
        std::size_t num_cores = 4;
        std::uint64_t quantum = 10000;
        for (int i = 4; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--cores" && i + 1 < argc) {
                num_cores = static_cast<std::size_t>(std::stoul(argv[++i]));
            } else if (std::string_view(argv[i]) == "--quantum" && i + 1 < argc) {
                quantum = std::stoull(argv[++i]);
            }
        }
        auto shared_mem = std::make_shared<winios::SmpSharedMemory>(16 * 1024 * 1024, num_cores);
        winios::SmpScheduler sched(shared_mem, num_cores, quantum);
        for (std::size_t c = 0; c < num_cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }
        winios::snapshot::SmpSnapshotCaptureOptions opts{};
        opts.description = "WinIOS Multi-Core Cluster Snapshot";
        opts.creator = "WinIOS CLI";
        const auto res = winios::snapshot::SmpSnapshotManager::capture_to_file(sched, argv[3], opts);
        if (!res.success) {
            std::cerr << "Cluster snapshot capture failed: " << res.error_message << "\n";
            return 1;
        }
        std::cout << "Created multi-core cluster snapshot: " << argv[3]
                  << " (" << num_cores << " cores, quantum=" << quantum << ")\n";
        return 0;
    }

    if (action == "restore") {
        if (argc < 4) {
            std::cerr << "Error: Missing snapshot file path\n";
            return 1;
        }
        winios::snapshot::SmpSnapshotInspection inspection{};
        auto res = winios::snapshot::SmpSnapshotManager::inspect_file(argv[3], inspection);
        if (!res.success) {
            std::cerr << "Failed to inspect cluster snapshot before restore: " << res.error_message << "\n";
            return 1;
        }
        const std::size_t num_cores = inspection.header.core_count;
        const std::size_t mem_bytes = static_cast<std::size_t>(inspection.header.memory_capacity);
        auto shared_mem = std::make_shared<winios::SmpSharedMemory>(mem_bytes, num_cores);
        winios::SmpScheduler sched(shared_mem, num_cores);
        for (std::size_t c = 0; c < num_cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }
        res = winios::snapshot::SmpSnapshotManager::restore_from_file(argv[3], sched);
        if (!res.success) {
            std::cerr << "Cluster snapshot restoration failed: " << res.error_message << "\n";
            return 1;
        }
        std::cout << "Successfully restored cluster snapshot: " << argv[3]
                  << " (" << num_cores << " cores, canonical_hash=0x"
                  << std::hex << sched.compute_canonical_multicore_hash() << std::dec << ")\n";
        return 0;
    }

    if (action == "compare") {
        if (argc < 5) {
            std::cerr << "Error: Compare requires two snapshot files: <file1.wsnap> <file2.wsnap>\n";
            return 1;
        }
        std::string diff_report;
        const auto res = winios::snapshot::SmpSnapshotManager::compare_snapshots(argv[3], argv[4], diff_report);
        if (!res.success) {
            std::cerr << "Cluster snapshot compare failed: " << res.error_message << "\n";
            return 1;
        }
        std::cout << diff_report << "\n";
        return 0;
    }

    std::cerr << "Unknown smp-snapshot action: " << action << "\n";
    return 2;
}

int run_sndl_v2_mode(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const std::string_view action = argv[2];
    if (action == "inspect" || action == "summary") {
        if (argc < 4) {
            std::cerr << "Error: Missing SNDL v2 file path\n";
            return 2;
        }
        winios::sndl::SndlV2InspectionInfo info{};
        const auto res = winios::sndl::SndlV2Validator::inspect_file(argv[3], info);
        if (res.is_failure()) {
            std::cerr << "Inspection error: " << res.message << "\n";
            return 1;
        }
        std::cout << info.formatted_summary;
        return 0;
    }
    if (action == "validate") {
        if (argc < 4) {
            std::cerr << "Error: Missing SNDL v2 file path\n";
            return 2;
        }
        std::optional<std::filesystem::path> snapshot_path = std::nullopt;
        for (int i = 4; i < argc; ++i) {
            if ((std::string_view(argv[i]) == "--snapshot" || std::string_view(argv[i]) == "-s") && i + 1 < argc) {
                snapshot_path = argv[++i];
            }
        }
        const auto val_res = winios::sndl::SndlV2Validator::validate_file(argv[3], snapshot_path);
        if (!val_res.is_valid) {
            std::cerr << "SNDL v2 validation FAILED: " << val_res.message << "\n";
            for (const auto& d : val_res.diagnostics) {
                std::cerr << "  " << d << "\n";
            }
            return 1;
        }
        std::cout << "SNDL v2 validation SUCCESS: " << argv[3] << "\n";
        for (const auto& d : val_res.diagnostics) {
            std::cout << "  " << d << "\n";
        }
        return 0;
    }
    if (action == "events") {
        if (argc < 4) {
            std::cerr << "Error: Missing SNDL v2 file path\n";
            return 2;
        }
        std::size_t max_events = 50;
        std::optional<std::uint32_t> filter_core = std::nullopt;
        for (int i = 4; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--max-events" && i + 1 < argc) {
                max_events = static_cast<std::size_t>(parse_integer(argv[++i], "--max-events"));
            } else if (std::string_view(argv[i]) == "--core" && i + 1 < argc) {
                filter_core = static_cast<std::uint32_t>(parse_integer(argv[++i], "--core"));
            }
        }
        std::vector<winios::sndl::SndlV2Record> records;
        const auto res = winios::sndl::SndlV2Validator::extract_events(argv[3], records, 0);
        if (res.is_failure()) {
            std::cerr << "Failed to extract events: " << res.message << "\n";
            return 1;
        }
        std::cout << "Extracted " << records.size() << " events from " << argv[3] << ":\n";
        std::size_t printed = 0;
        for (const auto& r : records) {
            if (filter_core.has_value() && r.core_id != *filter_core) continue;
            std::cout << "  [Seq " << std::setw(6) << r.global_event_sequence << "] Core "
                      << r.core_id << " | RetSeq " << std::setw(6) << r.global_retirement_sequence
                      << " | CoreRet " << std::setw(6) << r.core_retirement_count
                      << " | PC 0x" << std::hex << r.pc << std::dec
                      << " | " << winios::sndl::sndl_v2_event_type_name(r.event_type)
                      << " (" << winios::sndl::sndl_v2_boundary_type_name(r.boundary_type) << ")\n";
            if (++printed >= max_events) break;
        }
        return 0;
    }
    if (action == "compare") {
        if (argc < 5) {
            std::cerr << "Error: Compare requires two SNDL v2 files: <file1.sndl> <file2.sndl>\n";
            return 2;
        }
        winios::sndl::SndlV2InspectionInfo info1{}, info2{};
        const auto res1 = winios::sndl::SndlV2Validator::inspect_file(argv[3], info1);
        const auto res2 = winios::sndl::SndlV2Validator::inspect_file(argv[4], info2);
        if (res1.is_failure() || res2.is_failure()) {
            std::cerr << "Comparison error: failed to inspect one or both files\n";
            return 1;
        }
        std::cout << "=== SNDL v2 Recording Comparison ===\n"
                  << "File 1: " << argv[3] << " (" << info1.total_records << " events, " << info1.header.core_count << " cores)\n"
                  << "File 2: " << argv[4] << " (" << info2.total_records << " events, " << info2.header.core_count << " cores)\n"
                  << "Core Count Match:        " << (info1.header.core_count == info2.header.core_count ? "YES" : "NO") << "\n"
                  << "Total Events Match:      " << (info1.total_records == info2.total_records ? "YES" : "NO") << "\n"
                  << "Total Blocks Match:      " << (info1.total_blocks == info2.total_blocks ? "YES" : "NO") << "\n"
                  << "Baseline Snapshot Match:  " << (info1.header.base_snapshot_hash == info2.header.base_snapshot_hash ? "YES" : "NO") << "\n";
        return 0;
    }

    std::cerr << "Unknown sndl2 action: " << action << "\n";
    return 2;
}

int main(int argc, char* argv[]) {
    std::cout << "WinIOS v0.1.0\n";
    std::cout << "ARM64 emulation research prototype\n";

    try {
    if (argc > 1 && (std::string_view(argv[1]) == "smp" || std::string_view(argv[1]) == "--smp")) {
        return run_smp_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "smp-snapshot" || std::string_view(argv[1]) == "--smp-snapshot")) {
        return run_smp_snapshot_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "sndl2" || std::string_view(argv[1]) == "--sndl2" ||
                     std::string_view(argv[1]) == "smp-sndl" || std::string_view(argv[1]) == "--smp-sndl")) {
        return run_sndl_v2_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "replay" || std::string_view(argv[1]) == "--replay")) {
        return run_replay_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "sndl" || std::string_view(argv[1]) == "--sndl")) {
        return run_sndl_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "snapshot" || std::string_view(argv[1]) == "--snapshot")) {
        return run_snapshot_mode(argc, argv);
    }
    if (argc > 1 && (std::string_view(argv[1]) == "trace" || std::string_view(argv[1]) == "--trace-analyze")) {
        return run_trace_mode(argc, argv);
    }
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
