#pragma once

#include "runtime/session_transaction.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class RecoveryClassification {
    fully_committed,
    resumable_incomplete,
    nonrecoverable,
    abandoned,
    corrupted_journal
};

const char* recovery_classification_name(RecoveryClassification c);
RecoveryClassification parse_recovery_classification(std::string_view name);

struct TransactionInspection {
    std::filesystem::path journal_path;
    bool valid_journal{false};
    TransactionRecord record{};
    RecoveryClassification classification{RecoveryClassification::nonrecoverable};

    // Artifact verification statuses
    bool elf_exists{false};
    bool elf_valid{false};
    bool snapshot_exists{false};
    bool snapshot_valid{false};
    bool log_exists{false};
    bool log_valid{false};
    bool manifest_exists{false};
    bool manifest_valid{false};
    bool replay_verified{false};

    std::vector<std::filesystem::path> owned_temp_files;
    std::string diagnostic;
};

struct RecoveryResult {
    bool success{false};
    RecoveryClassification initial_classification{RecoveryClassification::nonrecoverable};
    TransactionState final_state{TransactionState::incomplete};
    std::size_t temp_files_cleaned{0};
    bool resumed{false};
    std::string diagnostic;
};

class TransactionRecovery {
public:
    // Inspect transaction without modifying disk state
    static TransactionInspection inspect(const std::filesystem::path& journal_path);

    // Deep validation of artifacts and deterministic replay
    static bool validate(TransactionInspection& inspection);

    // Safely cleans only owned temporary files associated with this transaction
    static std::size_t reconcile_temporary_files(const TransactionInspection& inspection);

    // Safely recovers and resumes an eligible transaction
    static RecoveryResult recover(const std::filesystem::path& journal_path);

    // Reconciles all transactions found in a directory
    struct DirectoryReconciliationReport {
        std::size_t journals_found{0};
        std::size_t committed_count{0};
        std::size_t resumed_count{0};
        std::size_t nonrecoverable_count{0};
        std::size_t abandoned_count{0};
        std::size_t temp_files_cleaned{0};
        std::vector<TransactionInspection> inspections;
    };
    static DirectoryReconciliationReport reconcile_directory(const std::filesystem::path& dir);
};
