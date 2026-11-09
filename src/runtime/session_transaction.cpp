#include "runtime/session_transaction.h"
#include "runtime/artifact_publication.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "sndl/sndl_validation.h"
#include "trace/trace_serializer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using Hashes = winios::replay::ReplayComponentHashes;

[[noreturn]] void tx_invalid(const char* msg) {
    throw std::invalid_argument(msg);
}

std::uint64_t current_time_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string hex_str(std::uint64_t value, std::size_t width) {
    std::array<char, 16> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value, 16);
    const std::string digits(buf.data(), res.ptr);
    if (digits.size() > width) tx_invalid("Hex width overflow in transaction serialization");
    return std::string(width - digits.size(), '0') + digits;
}

std::string read_bounded_file(const std::filesystem::path& path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(path)) tx_invalid("File does not exist or is not regular");
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > limit) tx_invalid("File size is zero or exceeds limit");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))
        || input.peek() != std::char_traits<char>::eof()) {
        tx_invalid("File read error or file truncated");
    }
    return bytes;
}

bool is_valid_tx_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return true;
}

bool can_transition(TransactionState from, TransactionState to) {
    if (from == to) return true;
    switch (from) {
    case TransactionState::created:
        return to == TransactionState::preparing || to == TransactionState::incomplete || to == TransactionState::recovery_required;
    case TransactionState::preparing:
        return to == TransactionState::artifacts_published || to == TransactionState::incomplete || to == TransactionState::recovery_required;
    case TransactionState::artifacts_published:
        return to == TransactionState::validating || to == TransactionState::incomplete || to == TransactionState::recovery_required;
    case TransactionState::validating:
        return to == TransactionState::committed || to == TransactionState::incomplete || to == TransactionState::recovery_required;
    case TransactionState::incomplete:
        return to == TransactionState::preparing || to == TransactionState::artifacts_published
            || to == TransactionState::validating || to == TransactionState::recovery_required;
    case TransactionState::recovery_required:
        return to == TransactionState::preparing || to == TransactionState::artifacts_published
            || to == TransactionState::validating || to == TransactionState::incomplete;
    case TransactionState::committed:
        return false; // Immutable!
    }
    return false;
}

} // namespace

const char* transaction_state_name(TransactionState state) {
    switch (state) {
    case TransactionState::created: return "CREATED";
    case TransactionState::preparing: return "PREPARING";
    case TransactionState::artifacts_published: return "ARTIFACTS_PUBLISHED";
    case TransactionState::validating: return "VALIDATING";
    case TransactionState::committed: return "COMMITTED";
    case TransactionState::incomplete: return "INCOMPLETE";
    case TransactionState::recovery_required: return "RECOVERY_REQUIRED";
    }
    return "UNKNOWN";
}

TransactionState parse_transaction_state(std::string_view name) {
    if (name == "CREATED") return TransactionState::created;
    if (name == "PREPARING") return TransactionState::preparing;
    if (name == "ARTIFACTS_PUBLISHED") return TransactionState::artifacts_published;
    if (name == "VALIDATING") return TransactionState::validating;
    if (name == "COMMITTED") return TransactionState::committed;
    if (name == "INCOMPLETE") return TransactionState::incomplete;
    if (name == "RECOVERY_REQUIRED") return TransactionState::recovery_required;
    tx_invalid("Unknown transaction state name");
}

std::string serialize_transaction_journal(const TransactionRecord& r) {
    if (!is_valid_tx_id(r.transaction_id)) tx_invalid("Invalid transaction ID");
    std::string text = "winios_txjournal=1\n";
    const auto field = [&](const std::string& k, const std::string& v) { text += k + "=" + v + "\n"; };

    field("transaction_id", r.transaction_id);
    field("state", transaction_state_name(r.state));
    field("version", std::to_string(r.version));
    field("created_time_ms", std::to_string(r.created_time_ms));
    field("updated_time_ms", std::to_string(r.updated_time_ms));

    field("elf_path", r.paths.elf.generic_string());
    field("elf_size", std::to_string(r.elf.size));
    field("elf_crc32c", hex_str(r.elf.crc32c, 8));

    field("snapshot_path", r.paths.snapshot.generic_string());
    field("snapshot_size", std::to_string(r.snapshot.size));
    field("snapshot_crc32c", hex_str(r.snapshot.crc32c, 8));

    field("log_path", r.paths.log.generic_string());
    field("log_size", std::to_string(r.log.size));
    field("log_crc32c", hex_str(r.log.crc32c, 8));

    field("manifest_path", r.paths.manifest.generic_string());
    field("manifest_size", std::to_string(r.manifest.size));
    field("manifest_crc32c", hex_str(r.manifest.crc32c, 8));

    field("journal_path", r.paths.journal.generic_string());

    field("snapshot_association", hex_str(r.snapshot_association, 8));

    const auto write_hashes = [&](const char* pfx, const Hashes& h) {
        field(std::string(pfx) + "_cpu", hex_str(h.cpu_registers_hash, 16));
        field(std::string(pfx) + "_simd", hex_str(h.simd_registers_hash, 16));
        field(std::string(pfx) + "_sysreg", hex_str(h.system_registers_hash, 16));
        field(std::string(pfx) + "_memory", hex_str(h.memory_hash, 16));
        field(std::string(pfx) + "_timer", hex_str(h.timer_hash, 16));
        field(std::string(pfx) + "_gic", hex_str(h.gic_hash, 16));
        field(std::string(pfx) + "_combined", hex_str(h.combined_hash, 16));
    };
    write_hashes("initial", r.initial_hashes);
    write_hashes("final", r.final_hashes);

    field("memory", std::to_string(r.memory));
    field("budget", std::to_string(r.budget));
    field("initial_retired", std::to_string(r.initial_retired));
    field("final_retired", std::to_string(r.final_retired));
    field("steps", std::to_string(r.steps));
    field("events", std::to_string(r.events));
    field("completion", execution_status_name(r.completion));

    const auto crc = winios::trace::calculate_crc32c(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    text += "journal_crc32c=" + hex_str(crc, 8) + "\n";
    return text;
}

TransactionRecord parse_transaction_journal(std::string_view text) {
    if (text.empty() || text.size() > TransactionCoordinator::kMaxJournalBytes) {
        tx_invalid("Journal size is empty or exceeds limit");
    }

    std::size_t offset = 0;
    const auto take_line = [&](const std::string& key) {
        const auto end = text.find('\n', offset);
        if (end == std::string_view::npos) tx_invalid("Truncated journal line");
        const auto line = text.substr(offset, end - offset);
        const std::string pfx = key + "=";
        if (!line.starts_with(pfx)) tx_invalid("Unexpected, missing, or out-of-order journal key");
        offset = end + 1;
        return line.substr(pfx.size());
    };

    const auto parse_num = [&](const std::string& key, int base, std::size_t width = 0) {
        const auto val = take_line(key);
        std::uint64_t out{};
        const auto res = std::from_chars(val.data(), val.data() + val.size(), out, base);
        if (val.empty() || (width && val.size() != width) || res.ec != std::errc{} || res.ptr != val.data() + val.size()) {
            tx_invalid("Malformed integer/hex in journal");
        }
        return out;
    };

    if (take_line("winios_txjournal") != "1") tx_invalid("Unsupported journal schema version");

    TransactionRecord r;
    r.transaction_id = std::string(take_line("transaction_id"));
    if (!is_valid_tx_id(r.transaction_id)) tx_invalid("Invalid transaction ID format");

    r.state = parse_transaction_state(take_line("state"));
    r.version = static_cast<std::uint32_t>(parse_num("version", 10));
    r.created_time_ms = parse_num("created_time_ms", 10);
    r.updated_time_ms = parse_num("updated_time_ms", 10);

    r.paths.elf = std::filesystem::path(std::string(take_line("elf_path")));
    r.elf.size = parse_num("elf_size", 10);
    r.elf.crc32c = static_cast<std::uint32_t>(parse_num("elf_crc32c", 16, 8));

    r.paths.snapshot = std::filesystem::path(std::string(take_line("snapshot_path")));
    r.snapshot.size = parse_num("snapshot_size", 10);
    r.snapshot.crc32c = static_cast<std::uint32_t>(parse_num("snapshot_crc32c", 16, 8));

    r.paths.log = std::filesystem::path(std::string(take_line("log_path")));
    r.log.size = parse_num("log_size", 10);
    r.log.crc32c = static_cast<std::uint32_t>(parse_num("log_crc32c", 16, 8));

    r.paths.manifest = std::filesystem::path(std::string(take_line("manifest_path")));
    r.manifest.size = parse_num("manifest_size", 10);
    r.manifest.crc32c = static_cast<std::uint32_t>(parse_num("manifest_crc32c", 16, 8));

    r.paths.journal = std::filesystem::path(std::string(take_line("journal_path")));

    r.snapshot_association = static_cast<std::uint32_t>(parse_num("snapshot_association", 16, 8));

    const auto parse_hashes = [&](const char* pfx, Hashes& h) {
        h.cpu_registers_hash = parse_num(std::string(pfx) + "_cpu", 16, 16);
        h.simd_registers_hash = parse_num(std::string(pfx) + "_simd", 16, 16);
        h.system_registers_hash = parse_num(std::string(pfx) + "_sysreg", 16, 16);
        h.memory_hash = parse_num(std::string(pfx) + "_memory", 16, 16);
        h.timer_hash = parse_num(std::string(pfx) + "_timer", 16, 16);
        h.gic_hash = parse_num(std::string(pfx) + "_gic", 16, 16);
        h.combined_hash = parse_num(std::string(pfx) + "_combined", 16, 16);
    };
    parse_hashes("initial", r.initial_hashes);
    parse_hashes("final", r.final_hashes);

    r.memory = parse_num("memory", 10);
    r.budget = parse_num("budget", 10);
    r.initial_retired = parse_num("initial_retired", 10);
    r.final_retired = parse_num("final_retired", 10);
    r.steps = parse_num("steps", 10);
    r.events = parse_num("events", 10);

    const auto comp_str = take_line("completion");
    if (comp_str == "completed") r.completion = ExecutionStatus::completed;
    else if (comp_str == "instruction_limit_reached") r.completion = ExecutionStatus::instruction_limit_reached;
    else tx_invalid("Invalid completion status in journal");

    (void)parse_num("journal_crc32c", 16, 8);

    if (offset != text.size() || serialize_transaction_journal(r) != text) {
        tx_invalid("Non-canonical, corrupted, or trailing data in transaction journal");
    }

    return r;
}

TransactionRecord load_transaction_journal(const std::filesystem::path& path) {
    return parse_transaction_journal(read_bounded_file(path, TransactionCoordinator::kMaxJournalBytes));
}

void save_transaction_journal(const std::filesystem::path& path, const TransactionRecord& record, bool create_new) {
    const auto text = serialize_transaction_journal(record);
    if (create_new) {
        if (std::filesystem::exists(path)) tx_invalid("Transaction journal destination exists");
        ArtifactPublication publication(path);
        publication.write(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
        (void)parse_transaction_journal(text);
        publication.commit();
    } else {
#ifdef _WIN32
        const auto parent = path.parent_path();
        const auto temp = parent / (".winios-txj-" + std::to_string(GetCurrentProcessId()) + "-"
            + std::to_string(current_time_ms()) + ".tmp");
        const HANDLE h = CreateFileW(temp.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to create temporary journal update file");
        }
        DWORD written = 0;
        if (!WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
            || written != text.size() || !FlushFileBuffers(h) || !CloseHandle(h)) {
            CloseHandle(h);
            DeleteFileW(temp.c_str());
            throw std::runtime_error("Failed to write/flush temporary journal update file");
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            throw std::runtime_error("Failed to atomically replace transaction journal");
        }
#else
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(text.data(), text.size());
        out.flush();
#endif
    }
}

// ----------------------------------------------------------------------------
// TransactionCoordinator
// ----------------------------------------------------------------------------

TransactionCoordinator::TransactionCoordinator(TransactionRecord record, bool /*is_new*/)
    : record_(std::move(record)), active_(true) {}

TransactionCoordinator::~TransactionCoordinator() {
    if (active_ && record_.state != TransactionState::committed) {
        try {
            abort(TransactionState::incomplete);
        } catch (...) {}
    }
}

std::unique_ptr<TransactionCoordinator> TransactionCoordinator::create(
    const std::string& transaction_id,
    const TransactionArtifacts& paths) {
    if (!is_valid_tx_id(transaction_id)) tx_invalid("Invalid transaction ID");
    if (paths.journal.empty()) tx_invalid("Empty journal path");

    // Output destination collision checks
    if (std::filesystem::exists(paths.journal)) tx_invalid("Transaction journal exists (duplicate transaction ID)");
    if (!paths.snapshot.empty() && std::filesystem::exists(paths.snapshot)) tx_invalid("Snapshot destination exists");
    if (!paths.log.empty() && std::filesystem::exists(paths.log)) tx_invalid("Log destination exists");
    if (!paths.manifest.empty() && std::filesystem::exists(paths.manifest)) tx_invalid("Manifest destination exists");

    TransactionRecord r;
    r.transaction_id = transaction_id;
    r.state = TransactionState::created;
    r.version = 1;
    r.created_time_ms = current_time_ms();
    r.updated_time_ms = r.created_time_ms;
    r.paths = paths;

    save_transaction_journal(paths.journal, r, true);
    return std::unique_ptr<TransactionCoordinator>(new TransactionCoordinator(std::move(r), true));
}

std::unique_ptr<TransactionCoordinator> TransactionCoordinator::open(
    const std::filesystem::path& journal_path) {
    auto r = load_transaction_journal(journal_path);
    r.paths.journal = journal_path;
    return std::unique_ptr<TransactionCoordinator>(new TransactionCoordinator(std::move(r), false));
}

void TransactionCoordinator::update_journal() {
    record_.updated_time_ms = current_time_ms();
    save_transaction_journal(record_.paths.journal, record_, false);
}

void TransactionCoordinator::set_state(TransactionState target) {
    if (!can_transition(record_.state, target)) {
        throw std::logic_error(std::string("Invalid transaction state transition from ")
            + transaction_state_name(record_.state) + " to " + transaction_state_name(target));
    }
    record_.state = target;
    update_journal();
}

void TransactionCoordinator::register_artifacts(
    const SessionFileIdentity& elf_id, std::uint64_t memory, std::uint64_t budget) {
    if (record_.state != TransactionState::created) {
        throw std::logic_error("register_artifacts requires state CREATED");
    }
    if (elf_id.size == 0 || memory == 0 || budget == 0) tx_invalid("Invalid registration parameters");
    record_.elf = elf_id;
    record_.memory = memory;
    record_.budget = budget;
    set_state(TransactionState::preparing);
}

void TransactionCoordinator::publish_snapshot(std::span<const std::uint8_t> snapshot_bytes) {
    if (record_.state != TransactionState::preparing) {
        throw std::logic_error("publish_snapshot requires state PREPARING");
    }
    if (snapshot_bytes.empty()) tx_invalid("Empty snapshot bytes");

    ArtifactPublication pub(record_.paths.snapshot);
    pub.write(snapshot_bytes);
    if (!winios::snapshot::SnapshotManager::validate_file(pub.temporary_path()).valid) {
        tx_invalid("Staged snapshot failed validation");
    }
    pub.commit();

    record_.snapshot = session_file_identity(record_.paths.snapshot, record_.memory + 1024u * 1024u);
    winios::snapshot::SnapshotInspection info;
    if (!winios::snapshot::SnapshotManager::inspect_file(record_.paths.snapshot, info).success) {
        tx_invalid("Failed to inspect published snapshot");
    }
    record_.snapshot_association = info.header.header_crc32c;
    update_journal();
}

void TransactionCoordinator::publish_snapshot_file(const std::filesystem::path& staged_snapshot) {
    if (record_.state != TransactionState::preparing) {
        throw std::logic_error("publish_snapshot_file requires state PREPARING");
    }
    if (!std::filesystem::is_regular_file(staged_snapshot)) tx_invalid("Staged snapshot file missing");

    const auto bytes = read_bounded_file(staged_snapshot, record_.memory + 1024u * 1024u);
    publish_snapshot(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

void TransactionCoordinator::publish_recording_file(
    const std::filesystem::path& staged_log, std::uint64_t steps, std::uint64_t events) {
    if (record_.state != TransactionState::preparing) {
        throw std::logic_error("publish_recording_file requires state PREPARING");
    }
    if (record_.snapshot.size == 0) {
        throw std::logic_error("Snapshot must be published before recording");
    }
    if (!std::filesystem::is_regular_file(staged_log)) tx_invalid("Staged recording file missing");

    const auto bytes = read_bounded_file(staged_log, ElfSession::kMaxMemory);
    ArtifactPublication pub(record_.paths.log);
    pub.write(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));

    if (!winios::sndl::SndlValidation::validate_file(pub.temporary_path(), record_.paths.snapshot).is_valid) {
        tx_invalid("Staged SNDL failed validation against published snapshot");
    }
    pub.commit();

    record_.log = session_file_identity(record_.paths.log, ElfSession::kMaxMemory);
    record_.steps = steps;
    record_.events = events;
    set_state(TransactionState::artifacts_published);
}

void TransactionCoordinator::validate_published_artifacts() {
    if (record_.state != TransactionState::artifacts_published) {
        throw std::logic_error("validate_published_artifacts requires state ARTIFACTS_PUBLISHED");
    }
    set_state(TransactionState::validating);

    // Verify snapshot file
    if (!std::filesystem::exists(record_.paths.snapshot)) {
        abort(TransactionState::recovery_required);
        tx_invalid("Published snapshot file is missing");
    }
    const auto actual_snap_id = session_file_identity(record_.paths.snapshot, record_.memory + 1024u * 1024u);
    if (actual_snap_id != record_.snapshot) {
        abort(TransactionState::recovery_required);
        tx_invalid("Snapshot file identity mismatch");
    }

    // Verify log file
    if (!std::filesystem::exists(record_.paths.log)) {
        abort(TransactionState::recovery_required);
        tx_invalid("Published log file is missing");
    }
    const auto actual_log_id = session_file_identity(record_.paths.log, ElfSession::kMaxMemory);
    if (actual_log_id != record_.log) {
        abort(TransactionState::recovery_required);
        tx_invalid("Log file identity mismatch");
    }

    // Verify snapshot inspection and association
    winios::snapshot::SnapshotInspection info;
    if (!winios::snapshot::SnapshotManager::inspect_file(record_.paths.snapshot, info).success
        || info.header.header_crc32c != record_.snapshot_association) {
        abort(TransactionState::recovery_required);
        tx_invalid("Snapshot association verification failed");
    }

    // Verify SNDL inspection and base association
    winios::sndl::SndlInspectionInfo log_info;
    if (winios::sndl::SndlValidation::inspect_file(record_.paths.log, log_info).is_failure()
        || log_info.header.base_snapshot_hash != record_.snapshot_association
        || log_info.footer.total_instructions != record_.steps
        || log_info.footer.total_records != record_.events) {
        abort(TransactionState::recovery_required);
        tx_invalid("Log metadata verification failed");
    }

    // Deterministic replay verification
    winios::replay::ReplayExecutionStats stats;
    const auto replay_res = ElfSession::verify(
        record_.paths.snapshot, record_.paths.log, record_.final_hashes,
        static_cast<std::size_t>(record_.steps), stats);
    if (replay_res.is_failure() || stats.instructions_executed != record_.steps
        || stats.events_consumed != record_.events) {
        abort(TransactionState::recovery_required);
        tx_invalid("Replay verification failed");
    }
}

void TransactionCoordinator::publish_manifest(const SessionManifest& manifest) {
    if (record_.state != TransactionState::validating) {
        throw std::logic_error("publish_manifest requires state VALIDATING");
    }
    // Manifest must only be published after all artifacts are validated
    verify_session_manifest(manifest, record_.paths.elf, record_.paths.snapshot, record_.paths.log);
    save_session_manifest(record_.paths.manifest, manifest);

    record_.manifest = session_file_identity(record_.paths.manifest, 16384);
    update_journal();
}

void TransactionCoordinator::commit() {
    if (record_.state != TransactionState::validating) {
        throw std::logic_error("commit requires state VALIDATING");
    }
    if (record_.manifest.size == 0 || !std::filesystem::exists(record_.paths.manifest)) {
        throw std::logic_error("Cannot commit without published manifest");
    }
    // Verify manifest one more time at commit boundary
    const auto loaded = load_session_manifest(record_.paths.manifest);
    verify_session_manifest(loaded, record_.paths.elf, record_.paths.snapshot, record_.paths.log);

    set_state(TransactionState::committed);
    active_ = false;
}

void TransactionCoordinator::abort(TransactionState fail_state) {
    if (record_.state == TransactionState::committed) {
        throw std::logic_error("Cannot abort a committed transaction");
    }
    if (fail_state != TransactionState::incomplete && fail_state != TransactionState::recovery_required) {
        fail_state = TransactionState::incomplete;
    }
    record_.state = fail_state;
    update_journal();
    active_ = false;
}

TransactionCoordinator::ExecuteResult TransactionCoordinator::execute(ElfSession& session, std::uint64_t budget) {
    ExecuteResult res;
    try {
        if (record_.state != TransactionState::created) {
            throw std::logic_error("execute requires state CREATED");
        }
        if (budget == 0) tx_invalid("Instruction budget must be > 0");

        // Step 1: Register expected artifacts
        const auto elf_id = session_file_identity(record_.paths.elf, ElfSession::kMaxMemory);
        register_artifacts(elf_id, session.memory_size(), budget);
        record_.initial_hashes = session.hashes();
        record_.initial_retired = session.retired();
        update_journal();

        // Step 2: Publish Snapshot
        std::vector<std::uint8_t> snap_bytes;
        // Capture snapshot from session machine
        // ElfSession doesn't expose capture_to_buffer directly, but capture(path) does.
        // We capture snapshot using ElfSession::capture into paths.snapshot directly!
        const auto snap_res = session.capture(record_.paths.snapshot);
        if (!snap_res.success) {
            abort(TransactionState::incomplete);
            res.error = snap_res.error_message;
            return res;
        }
        record_.snapshot = session_file_identity(record_.paths.snapshot, record_.memory + 1024u * 1024u);
        winios::snapshot::SnapshotInspection snap_info;
        if (!winios::snapshot::SnapshotManager::inspect_file(record_.paths.snapshot, snap_info).success) {
            abort(TransactionState::recovery_required);
            res.error = "Snapshot inspection failed";
            return res;
        }
        record_.snapshot_association = snap_info.header.header_crc32c;
        update_journal();

        // Step 3: Record and publish log
        ArtifactPublication log_pub(record_.paths.log);
        winios::sndl::SndlRecorder recorder;
        const auto started = recorder.start_recording(log_pub.temporary_path(), 1, record_.snapshot_association);
        if (started.is_failure()) {
            abort(TransactionState::incomplete);
            res.error = started.message;
            return res;
        }
        {
            winios::sndl::ScopedThreadRecorder binding(&recorder);
            res.execution = session.run(static_cast<std::size_t>(budget));
        }
        record_.steps = res.execution.instructions_executed;
        record_.events = recorder.total_recorded();
        record_.final_hashes = session.hashes();
        record_.final_retired = session.retired();
        record_.completion = res.execution.status;

        if (res.execution.status != ExecutionStatus::completed
            && res.execution.status != ExecutionStatus::instruction_limit_reached) {
            recorder.abort_recording();
            abort(TransactionState::incomplete);
            res.error = res.execution.error;
            return res;
        }
        const auto stopped = recorder.stop_recording(res.execution.instructions_executed);
        if (stopped.is_failure()) {
            abort(TransactionState::incomplete);
            res.error = stopped.message;
            return res;
        }
        if (!winios::sndl::SndlValidation::validate_file(log_pub.temporary_path(), record_.paths.snapshot).is_valid) {
            abort(TransactionState::recovery_required);
            res.error = "Staged SNDL failed validation";
            return res;
        }
        log_pub.commit();
        record_.log = session_file_identity(record_.paths.log, ElfSession::kMaxMemory);
        set_state(TransactionState::artifacts_published);

        // Step 4: Validate all artifacts & replay
        validate_published_artifacts();

        // Step 5: Publish manifest
        SessionManifest manifest;
        manifest.elf = record_.elf;
        manifest.snapshot = record_.snapshot;
        manifest.log = record_.log;
        manifest.snapshot_association = record_.snapshot_association;
        manifest.initial = record_.initial_hashes;
        manifest.final = record_.final_hashes;
        manifest.memory = record_.memory;
        manifest.budget = record_.budget;
        manifest.initial_retired = record_.initial_retired;
        manifest.final_retired = record_.final_retired;
        manifest.steps = record_.steps;
        manifest.events = record_.events;
        manifest.completion = record_.completion;
        manifest.replay_verified = true;

        publish_manifest(manifest);

        // Step 6: Commit
        commit();
        res.success = true;
    } catch (const std::exception& e) {
        if (record_.state != TransactionState::committed) {
            try { abort(TransactionState::incomplete); } catch (...) {}
        }
        res.error = e.what();
    }
    return res;
}
