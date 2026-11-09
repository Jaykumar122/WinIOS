#pragma once

#include "runtime/session_manifest.h"
#include "runtime/artifact_publication.h"
#include "runtime/elf_session.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class TransactionState : std::uint32_t {
    created = 1,
    preparing = 2,
    artifacts_published = 3,
    validating = 4,
    committed = 5,
    incomplete = 6,
    recovery_required = 7
};

const char* transaction_state_name(TransactionState state);
TransactionState parse_transaction_state(std::string_view name);

struct TransactionArtifacts {
    std::filesystem::path elf;
    std::filesystem::path snapshot;
    std::filesystem::path log;
    std::filesystem::path manifest;
    std::filesystem::path journal;
    bool operator==(const TransactionArtifacts& o) const noexcept {
        return elf.generic_string() == o.elf.generic_string()
            && snapshot.generic_string() == o.snapshot.generic_string()
            && log.generic_string() == o.log.generic_string()
            && manifest.generic_string() == o.manifest.generic_string()
            && journal.generic_string() == o.journal.generic_string();
    }
};

struct TransactionRecord {
    std::string transaction_id;
    TransactionState state{TransactionState::created};
    std::uint32_t version{1};
    std::uint64_t created_time_ms{0};
    std::uint64_t updated_time_ms{0};

    // Artifact identities (computed as artifacts are staged/published)
    SessionFileIdentity elf{};
    SessionFileIdentity snapshot{};
    SessionFileIdentity log{};
    SessionFileIdentity manifest{};

    // Paths
    TransactionArtifacts paths;

    // Execution metadata
    std::uint64_t memory{0};
    std::uint64_t budget{0};
    std::uint64_t initial_retired{0};
    std::uint64_t final_retired{0};
    std::uint64_t steps{0};
    std::uint64_t events{0};
    std::uint32_t snapshot_association{0};
    ExecutionStatus completion{ExecutionStatus::completed};

    // State hashes
    winios::replay::ReplayComponentHashes initial_hashes{};
    winios::replay::ReplayComponentHashes final_hashes{};

    bool operator==(const TransactionRecord&) const = default;
};

std::string serialize_transaction_journal(const TransactionRecord& record);
TransactionRecord parse_transaction_journal(std::string_view text);
TransactionRecord load_transaction_journal(const std::filesystem::path& path);
void save_transaction_journal(const std::filesystem::path& path, const TransactionRecord& record, bool create_new = false);

class TransactionCoordinator {
public:
    static constexpr std::size_t kMaxJournalBytes = 64u * 1024u;

    // Creates a new transaction with unique ID and paths. Fails if journal or outputs exist.
    static std::unique_ptr<TransactionCoordinator> create(
        const std::string& transaction_id,
        const TransactionArtifacts& paths);

    // Opens an existing transaction journal for inspection/recovery.
    static std::unique_ptr<TransactionCoordinator> open(
        const std::filesystem::path& journal_path);

    ~TransactionCoordinator();
    TransactionCoordinator(const TransactionCoordinator&) = delete;
    TransactionCoordinator& operator=(const TransactionCoordinator&) = delete;

    const TransactionRecord& record() const noexcept { return record_; }
    TransactionState state() const noexcept { return record_.state; }
    const std::string& id() const noexcept { return record_.transaction_id; }

    // Explicit state transitions
    void set_state(TransactionState target);

    // Stepwise protocol methods
    void register_artifacts(const SessionFileIdentity& elf_id, std::uint64_t memory, std::uint64_t budget);
    void publish_snapshot(std::span<const std::uint8_t> snapshot_bytes);
    void publish_snapshot_file(const std::filesystem::path& staged_snapshot);
    void publish_recording_file(const std::filesystem::path& staged_log, std::uint64_t steps, std::uint64_t events);
    void validate_published_artifacts();
    void publish_manifest(const SessionManifest& manifest);
    void commit();
    void abort(TransactionState fail_state = TransactionState::incomplete);

    // Coordinated end-to-end execution wrapping ElfSession recording
    struct ExecuteResult {
        bool success{false};
        ExecutionResult execution{};
        std::string error;
    };
    ExecuteResult execute(ElfSession& session, std::uint64_t budget);

private:
    TransactionCoordinator(TransactionRecord record, bool is_new);
    TransactionRecord record_;
    bool active_{true};
    void update_journal();
};
