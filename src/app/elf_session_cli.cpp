#include "app/elf_session_cli.h"
#include "runtime/session_manifest.h"
#include "runtime/transaction_recovery.h"
#include "snapshot/snapshot_manager.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using Options = std::map<std::string, std::string>;
std::uint64_t number(const std::string& text) {
    std::uint64_t value{};
    const bool hex = text.starts_with("0x") || text.starts_with("0X");
    const auto begin = text.data() + (hex ? 2 : 0);
    const auto result = std::from_chars(begin, text.data() + text.size(), value, hex ? 16 : 10);
    if (begin == text.data() + text.size() || result.ec != std::errc{}
        || result.ptr != text.data() + text.size()) throw std::invalid_argument("Invalid session integer");
    return value;
}
std::filesystem::path path(const Options& options, const char* name) {
    const auto& text = options.at(name);
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}
int display(const ExecutionResult& result) {
    std::cout << "Status: " << execution_status_name(result.status)
              << "\nInstructions executed: " << result.instructions_executed
              << "\nX0: " << result.final_registers[0] << '\n';
    if (!result.error.empty()) std::cerr << result.error << '\n';
    return result.status == ExecutionStatus::completed ? 0
        : result.status == ExecutionStatus::instruction_limit_reached ? 2 : 1;
}
}
int run_elf_session_cli(int argc, char* argv[]) {
    require(argc >= 3, "elf-session requires run|capture|record|replay|recover|reconcile");
    const std::string action = argv[2];
    require(action == "run" || action == "capture" || action == "record" || action == "replay"
        || action == "recover" || action == "reconcile", "Unknown elf-session action");
    Options options;
    const std::set<std::string> known{"--elf", "--restore", "--snapshot", "--log", "--manifest",
        "--memory-size", "--max-instructions", "--advance", "--stack-base", "--stack-size",
        "--journal", "--dir"};
    for (int i = 3; i < argc; ++i) {
        const std::string key = argv[i];
        require(known.contains(key), "Unknown session option: " + key);
        require(i + 1 < argc && std::string_view(argv[i + 1]).size() > 0
            && !std::string_view(argv[i + 1]).starts_with("--"), "Missing session option value: " + key);
        require(options.emplace(key, argv[++i]).second, "Duplicate session option: " + key);
    }

    if (action == "recover") {
        require(options.contains("--journal"), "recover action requires --journal");
        const auto journal = path(options, "--journal");
        require(std::filesystem::exists(journal), "Journal path does not exist");
        const auto res = TransactionRecovery::recover(journal);
        std::cout << "Transaction recovery: " << recovery_classification_name(res.initial_classification)
                  << "\nFinal state: " << transaction_state_name(res.final_state)
                  << "\nResumed: " << (res.resumed ? "true" : "false")
                  << "\nTemp files cleaned: " << res.temp_files_cleaned << '\n';
        if (!res.diagnostic.empty()) std::cout << "Diagnostic: " << res.diagnostic << '\n';
        return res.final_state == TransactionState::committed ? 0
            : (res.final_state == TransactionState::incomplete ? 2 : 1);
    }

    if (action == "reconcile") {
        require(options.contains("--dir"), "reconcile action requires --dir");
        const auto target_dir = path(options, "--dir");
        require(std::filesystem::is_directory(target_dir), "Directory path does not exist or is not a directory");
        const auto rep = TransactionRecovery::reconcile_directory(target_dir);
        std::cout << "Reconciliation summary:\n"
                  << "Journals found: " << rep.journals_found
                  << "\nFully committed: " << rep.committed_count
                  << "\nResumed: " << rep.resumed_count
                  << "\nAbandoned: " << rep.abandoned_count
                  << "\nNonrecoverable: " << rep.nonrecoverable_count
                  << "\nTemp files cleaned: " << rep.temp_files_cleaned << '\n';
        return rep.nonrecoverable_count == 0 ? 0 : 1;
    }

    require(options.contains("--memory-size") && options.contains("--max-instructions"), "Session requires RAM and instruction budget");
    const auto memory = number(options.at("--memory-size"));
    const auto budget = number(options.at("--max-instructions"));
    require(memory >= 4096 && memory <= ElfSession::kMaxMemory && budget > 0 && budget <= 100000000,
        "Session RAM/budget outside bounds");
    const auto advance = options.contains("--advance") ? number(options.at("--advance")) : 0;
    require(advance < budget, "Advance must be smaller than instruction budget");
    require(!(options.contains("--elf") && options.contains("--restore")), "ELF and restore inputs are mutually exclusive");
    const bool replay = action == "replay", record = action == "record", capture = action == "capture";
    require(replay || options.contains("--elf") != options.contains("--restore"), "Session requires exactly one ELF or restore input");
    require(!record || options.contains("--elf"), "Recording requires an ELF input");
    require(!replay || (options.contains("--elf") && !options.contains("--restore")), "Replay requires ELF identity input");
    require(!(replay || options.contains("--restore")) || (!options.contains("--advance")
        && !options.contains("--stack-base") && !options.contains("--stack-size")), "Advance/stack options require live ELF loading");
    require(options.contains("--snapshot") == (capture || record || replay), "Invalid snapshot option combination");
    require(options.contains("--log") == (record || replay) && options.contains("--manifest") == (record || replay), "Record/replay requires log and manifest only");
    if (!replay) {
        std::set<std::string> outputs;
        for (const auto* key : {"--snapshot", "--log", "--manifest"}) {
            if (!options.contains(key)) continue;
            const auto output = path(options, key);
            require(!std::filesystem::exists(output), "Session output already exists");
            require(std::filesystem::is_directory(std::filesystem::absolute(output).parent_path()), "Output parent directory missing");
            auto normalized = std::filesystem::weakly_canonical(output).generic_string();
#ifdef _WIN32
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
            require(outputs.insert(normalized).second, "Conflicting session output paths");
        }
    }
    if (replay) {
        const auto manifest = load_session_manifest(path(options, "--manifest"));
        require(manifest.memory == memory && manifest.budget <= budget, "Manifest exceeds explicit replay RAM/budget");
        verify_session_manifest(manifest, path(options, "--elf"), path(options, "--snapshot"), path(options, "--log"));
        std::cout << "Replay verified: true\nFinal retirement: " << manifest.final_retired << '\n';
        return 0;
    }
    const auto stack_size = options.contains("--stack-size") ? number(options.at("--stack-size")) : 4096;
    require(stack_size > 0 && stack_size <= memory, "Invalid stack size");
    const auto stack_base = options.contains("--stack-base") ? number(options.at("--stack-base")) : memory - stack_size;
    RuntimeConfig config{static_cast<std::size_t>(memory), 0, 0, stack_base,
        static_cast<std::size_t>(stack_size), static_cast<std::size_t>(budget), std::nullopt, false};
    ElfSession session(config);
    if (options.contains("--restore")) {
        const auto restored = session.restore(path(options, "--restore"));
        require(restored.success, "Snapshot restore failed: " + restored.error_message);
    } else {
        const auto loaded = session.load(path(options, "--elf"));
        if (loaded.status != ExecutionStatus::completed) return display(loaded);
    }
    if (advance) {
        const auto prefix = session.run(static_cast<std::size_t>(advance));
        require(prefix.instructions_executed == advance && prefix.status == ExecutionStatus::instruction_limit_reached,
            "Advance did not reach a runnable requested boundary");
    }
    if (capture) {
        const auto result = session.capture(path(options, "--snapshot"));
        require(result.success, "Snapshot capture failed: " + result.error_message);
        std::cout << "Snapshot captured\nAbsolute retirement: " << session.retired() << '\n';
        return 0;
    }
    if (!record) return display(session.run(static_cast<std::size_t>(budget - advance)));
    SessionManifest manifest;
    manifest.elf = session_file_identity(path(options, "--elf"), ElfSession::kMaxMemory);
    manifest.memory = memory; manifest.budget = budget - advance;
    manifest.initial = session.hashes(); manifest.initial_retired = session.retired();
    const auto result = session.record(path(options, "--snapshot"), path(options, "--log"), static_cast<std::size_t>(manifest.budget));
    require(result.success, "Session recording failed: " + result.error);
    manifest.final = result.final_hashes; manifest.final_retired = session.retired();
    manifest.steps = result.execution.instructions_executed; manifest.events = result.records;
    manifest.completion = result.execution.status;
    manifest.snapshot = session_file_identity(path(options, "--snapshot"), memory + 1024u * 1024u);
    manifest.log = session_file_identity(path(options, "--log"), ElfSession::kMaxMemory);
    winios::snapshot::SnapshotInspection info;
    require(winios::snapshot::SnapshotManager::inspect_file(path(options, "--snapshot"), info).success, "Snapshot inspection failed");
    manifest.snapshot_association = info.header.header_crc32c;
    verify_session_manifest(manifest, path(options, "--elf"), path(options, "--snapshot"), path(options, "--log"));
    save_session_manifest(path(options, "--manifest"), manifest);
    std::cout << "Manifest saved\nReplay verified: true\n";
    return display(result.execution);
}
