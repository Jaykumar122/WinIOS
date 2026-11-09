#include "runtime/transaction_recovery.h"
#include "runtime/artifact_publication.h"
#include "runtime/session_manifest.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_validation.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::mutex g_recovery_mutex;

bool is_owned_temporary(const std::filesystem::path& file, const std::string& tx_id) {
    const auto name = file.filename().string();
    if (!name.ends_with(".tmp")) return false;
    // WinIOS publication and journal update temporary prefixes
    if (name.starts_with(".winios-publish-") || name.starts_with(".winios-txj-")) {
        // Must be in the same directory and be a recent WinIOS temporary file
        return true;
    }
    if (!tx_id.empty() && name.find(tx_id) != std::string::npos) {
        return true;
    }
    return false;
}

} // namespace

const char* recovery_classification_name(RecoveryClassification c) {
    switch (c) {
    case RecoveryClassification::fully_committed: return "FULLY_COMMITTED";
    case RecoveryClassification::resumable_incomplete: return "RESUMABLE_INCOMPLETE";
    case RecoveryClassification::nonrecoverable: return "NONRECOVERABLE";
    case RecoveryClassification::abandoned: return "ABANDONED";
    case RecoveryClassification::corrupted_journal: return "CORRUPTED_JOURNAL";
    }
    return "UNKNOWN";
}

RecoveryClassification parse_recovery_classification(std::string_view name) {
    if (name == "FULLY_COMMITTED") return RecoveryClassification::fully_committed;
    if (name == "RESUMABLE_INCOMPLETE") return RecoveryClassification::resumable_incomplete;
    if (name == "NONRECOVERABLE") return RecoveryClassification::nonrecoverable;
    if (name == "ABANDONED") return RecoveryClassification::abandoned;
    if (name == "CORRUPTED_JOURNAL") return RecoveryClassification::corrupted_journal;
    throw std::invalid_argument("Unknown recovery classification name");
}

TransactionInspection TransactionRecovery::inspect(const std::filesystem::path& journal_path) {
    TransactionInspection ins;
    ins.journal_path = journal_path;

    if (!std::filesystem::is_regular_file(journal_path)) {
        ins.valid_journal = false;
        ins.classification = RecoveryClassification::corrupted_journal;
        ins.diagnostic = "Journal file does not exist or is not a regular file";
        return ins;
    }

    try {
        ins.record = load_transaction_journal(journal_path);
        ins.valid_journal = true;
    } catch (const std::exception& e) {
        ins.valid_journal = false;
        ins.classification = RecoveryClassification::corrupted_journal;
        ins.diagnostic = std::string("Corrupted transaction journal: ") + e.what();
        return ins;
    }

    const auto& r = ins.record;
    const auto parent = journal_path.parent_path();

    // Scan for owned temporary files in the same directory
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto p = entry.path();
        if (is_owned_temporary(p, r.transaction_id)) {
            ins.owned_temp_files.push_back(p);
        }
    }

    // Check ELF input
    ins.elf_exists = std::filesystem::is_regular_file(r.paths.elf);
    if (ins.elf_exists) {
        try {
            const auto elf_id = session_file_identity(r.paths.elf, ElfSession::kMaxMemory);
            ins.elf_valid = (elf_id == r.elf);
        } catch (...) { ins.elf_valid = false; }
    }

    // Check snapshot
    ins.snapshot_exists = std::filesystem::is_regular_file(r.paths.snapshot);
    if (ins.snapshot_exists) {
        try {
            const auto snap_id = session_file_identity(r.paths.snapshot, r.memory + 1024u * 1024u);
            winios::snapshot::SnapshotInspection snap_info;
            ins.snapshot_valid = (snap_id == r.snapshot)
                && winios::snapshot::SnapshotManager::inspect_file(r.paths.snapshot, snap_info).success
                && snap_info.header.header_crc32c == r.snapshot_association;
        } catch (...) { ins.snapshot_valid = false; }
    }

    // Check log
    ins.log_exists = std::filesystem::is_regular_file(r.paths.log);
    if (ins.log_exists) {
        try {
            const auto log_id = session_file_identity(r.paths.log, ElfSession::kMaxMemory);
            winios::sndl::SndlInspectionInfo log_info;
            ins.log_valid = (log_id == r.log)
                && winios::sndl::SndlValidation::inspect_file(r.paths.log, log_info).is_success()
                && log_info.header.base_snapshot_hash == r.snapshot_association
                && log_info.footer.total_instructions == r.steps
                && log_info.footer.total_records == r.events;
        } catch (...) { ins.log_valid = false; }
    }

    // Check manifest
    ins.manifest_exists = std::filesystem::is_regular_file(r.paths.manifest);
    if (ins.manifest_exists) {
        try {
            const auto manifest = load_session_manifest(r.paths.manifest);
            verify_session_manifest(manifest, r.paths.elf, r.paths.snapshot, r.paths.log);
            ins.manifest_valid = manifest.replay_verified;
        } catch (...) { ins.manifest_valid = false; }
    }

    // Perform deep validation
    validate(ins);
    return ins;
}

bool TransactionRecovery::validate(TransactionInspection& ins) {
    const auto& r = ins.record;
    if (!ins.valid_journal) {
        ins.classification = RecoveryClassification::corrupted_journal;
        return false;
    }

    // If fully committed and manifest is valid, verify committed status
    if (r.state == TransactionState::committed) {
        if (ins.elf_valid && ins.snapshot_valid && ins.log_valid && ins.manifest_valid) {
            ins.classification = RecoveryClassification::fully_committed;
            ins.diagnostic = "Transaction is complete and fully committed";
            return true;
        } else {
            ins.classification = RecoveryClassification::nonrecoverable;
            ins.diagnostic = "Transaction marked committed but artifact validation failed";
            return false;
        }
    }

    // Interrupted before snapshot publication
    if (!ins.snapshot_exists) {
        ins.classification = RecoveryClassification::abandoned;
        ins.diagnostic = "Transaction abandoned before snapshot publication";
        return false;
    }

    // Snapshot published but log missing
    if (ins.snapshot_valid && !ins.log_exists) {
        ins.classification = RecoveryClassification::nonrecoverable;
        ins.diagnostic = "Recording log missing; nondeterministic execution cannot be inferred";
        return false;
    }

    // Both snapshot and log exist; check validity and replay
    if (ins.snapshot_valid && ins.log_valid && ins.elf_valid) {
        try {
            winios::replay::ReplayExecutionStats stats;
            const auto replay_res = ElfSession::verify(
                r.paths.snapshot, r.paths.log, r.final_hashes,
                static_cast<std::size_t>(r.steps), stats);
            ins.replay_verified = replay_res.is_success()
                && (stats.instructions_executed == r.steps)
                && (stats.events_consumed == r.events);
        } catch (...) {
            ins.replay_verified = false;
        }

        if (ins.replay_verified) {
            ins.classification = RecoveryClassification::resumable_incomplete;
            ins.diagnostic = "Published artifacts verified; eligible for resumption";
            return true;
        } else {
            ins.classification = RecoveryClassification::nonrecoverable;
            ins.diagnostic = "Artifacts present but deterministic replay verification failed";
            return false;
        }
    }

    // Any corrupted artifact makes the transaction nonrecoverable
    ins.classification = RecoveryClassification::nonrecoverable;
    ins.diagnostic = "Artifact corruption or identity mismatch detected";
    return false;
}

std::size_t TransactionRecovery::reconcile_temporary_files(const TransactionInspection& inspection) {
    std::size_t cleaned = 0;
    for (const auto& temp : inspection.owned_temp_files) {
        // Double check it is regular file and strictly matches temporary naming
        if (std::filesystem::is_regular_file(temp) && is_owned_temporary(temp, inspection.record.transaction_id)) {
            std::error_code ec;
            if (std::filesystem::remove(temp, ec)) {
                ++cleaned;
            }
        }
    }
    return cleaned;
}

RecoveryResult TransactionRecovery::recover(const std::filesystem::path& journal_path) {
    std::lock_guard<std::mutex> lock(g_recovery_mutex);
    RecoveryResult res;

    auto ins = inspect(journal_path);
    res.initial_classification = ins.classification;

    // Clean up owned temporary files safely
    res.temp_files_cleaned = reconcile_temporary_files(ins);

    // Case 1: Already fully committed
    if (ins.classification == RecoveryClassification::fully_committed) {
        res.success = true;
        res.final_state = TransactionState::committed;
        res.resumed = false;
        res.diagnostic = "Transaction already committed and verified; no action needed";
        return res;
    }

    // Case 2: Resumable incomplete
    if (ins.classification == RecoveryClassification::resumable_incomplete) {
        auto coord = TransactionCoordinator::open(journal_path);

        // If manifest doesn't exist, create and publish it
        if (!ins.manifest_exists || !ins.manifest_valid) {
            SessionManifest manifest;
            manifest.elf = ins.record.elf;
            manifest.snapshot = ins.record.snapshot;
            manifest.log = ins.record.log;
            manifest.snapshot_association = ins.record.snapshot_association;
            manifest.initial = ins.record.initial_hashes;
            manifest.final = ins.record.final_hashes;
            manifest.memory = ins.record.memory;
            manifest.budget = ins.record.budget;
            manifest.initial_retired = ins.record.initial_retired;
            manifest.final_retired = ins.record.final_retired;
            manifest.steps = ins.record.steps;
            manifest.events = ins.record.events;
            manifest.completion = ins.record.completion;
            manifest.replay_verified = true;

            try {
                coord->set_state(TransactionState::validating);
                coord->publish_manifest(manifest);
            } catch (const std::exception& e) {
                res.success = false;
                res.final_state = TransactionState::recovery_required;
                res.diagnostic = std::string("Failed to publish manifest during recovery: ") + e.what();
                return res;
            }
        } else {
            coord->set_state(TransactionState::validating);
        }

        // Finalize commit
        try {
            coord->commit();
            res.success = true;
            res.resumed = true;
            res.final_state = TransactionState::committed;
            res.diagnostic = "Transaction successfully reconciled, validated, and committed";
            return res;
        } catch (const std::exception& e) {
            res.success = false;
            res.final_state = TransactionState::recovery_required;
            res.diagnostic = std::string("Commit finalization failed: ") + e.what();
            return res;
        }
    }

    // Case 3: Abandoned transaction
    if (ins.classification == RecoveryClassification::abandoned) {
        if (ins.valid_journal && ins.record.state != TransactionState::incomplete) {
            try {
                auto coord = TransactionCoordinator::open(journal_path);
                coord->abort(TransactionState::incomplete);
            } catch (...) {}
        }
        res.success = false;
        res.final_state = TransactionState::incomplete;
        res.diagnostic = "Transaction was abandoned before snapshot completion; marked incomplete";
        return res;
    }

    // Case 4: Nonrecoverable / corrupted
    if (ins.valid_journal && ins.record.state != TransactionState::recovery_required) {
        try {
            auto coord = TransactionCoordinator::open(journal_path);
            coord->abort(TransactionState::recovery_required);
        } catch (...) {}
    }
    res.success = false;
    res.final_state = TransactionState::recovery_required;
    res.diagnostic = std::string("Transaction cannot be recovered safely: ") + ins.diagnostic;
    return res;
}

TransactionRecovery::DirectoryReconciliationReport TransactionRecovery::reconcile_directory(
    const std::filesystem::path& dir) {
    DirectoryReconciliationReport report;
    if (!std::filesystem::is_directory(dir)) return report;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto p = entry.path();
        if (p.extension() == ".txjournal") {
            ++report.journals_found;
            const auto ins = inspect(p);
            report.inspections.push_back(ins);

            const auto result = recover(p);
            report.temp_files_cleaned += result.temp_files_cleaned;
            if (result.final_state == TransactionState::committed) {
                if (result.resumed) ++report.resumed_count;
                else ++report.committed_count;
            } else if (result.final_state == TransactionState::incomplete) {
                ++report.abandoned_count;
            } else {
                ++report.nonrecoverable_count;
            }
        }
    }
    return report;
}
