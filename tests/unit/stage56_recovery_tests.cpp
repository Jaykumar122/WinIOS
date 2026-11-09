#include "runtime/transaction_recovery.h"
#include "runtime/artifact_publication.h"
#include "runtime/session_manifest.h"
#include "runtime/elf_session.h"
#include "app/elf_session_cli.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <array>
#include <atomic>
#include <barrier>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), text.size());
}

TransactionArtifacts make_paths(const std::filesystem::path& dir, const std::string& prefix) {
    TransactionArtifacts paths;
    paths.elf = dir / (prefix + ".elf");
    paths.snapshot = dir / (prefix + ".wsnap");
    paths.log = dir / (prefix + ".sndl");
    paths.manifest = dir / (prefix + ".manifest");
    paths.journal = dir / (prefix + ".txjournal");
    return paths;
}

int run_cli(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    try {
        return run_elf_session_cli(static_cast<int>(argv.size()), argv.data());
    } catch (...) {
        return 1;
    }
}

// ----------------------------------------------------------------------------
// Test 1: Successful recovery of already committed transaction (Idempotence)
// ----------------------------------------------------------------------------
void test_committed_idempotence() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "committed_idempotence");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    auto coord = TransactionCoordinator::create("tx-comm-idem", paths);
    CHECK(coord->execute(session, 10).success);
    CHECK(coord->state() == TransactionState::committed);

    // Initial inspection
    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.valid_journal);
    CHECK(ins.classification == RecoveryClassification::fully_committed);
    CHECK(ins.elf_valid && ins.snapshot_valid && ins.log_valid && ins.manifest_valid);

    // Repeated recoveries
    for (int i = 0; i < 5; ++i) {
        const auto res = TransactionRecovery::recover(paths.journal);
        CHECK(res.success);
        CHECK(res.final_state == TransactionState::committed);
        CHECK(!res.resumed); // Was already committed
        CHECK(std::filesystem::exists(paths.manifest));
        CHECK(std::filesystem::exists(paths.snapshot));
        CHECK(std::filesystem::exists(paths.log));
    }
}

// ----------------------------------------------------------------------------
// Test 2: Recovery after recording publication (Resumable Incomplete)
// ----------------------------------------------------------------------------
void test_resumption_before_manifest() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "resumable_tx");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    // Simulate crash before manifest publication using ScopedPublicationFault on manifest
    {
        auto coord = TransactionCoordinator::create("tx-resume-mf", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        const auto res = coord->execute(session, 10);
        CHECK(!res.success);
    }

    CHECK(std::filesystem::exists(paths.snapshot));
    CHECK(std::filesystem::exists(paths.log));
    CHECK(!std::filesystem::exists(paths.manifest)); // Manifest missing
    CHECK(std::filesystem::exists(paths.journal));

    // Inspect: must be classified as resumable incomplete
    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.valid_journal);
    CHECK(ins.classification == RecoveryClassification::resumable_incomplete);
    CHECK(ins.snapshot_valid);
    CHECK(ins.log_valid);
    CHECK(!ins.manifest_exists);

    // Recover: should resume, publish manifest, and commit
    const auto res = TransactionRecovery::recover(paths.journal);
    CHECK(res.success);
    CHECK(res.resumed);
    CHECK(res.final_state == TransactionState::committed);

    // Verify manifest was created and is valid
    CHECK(std::filesystem::exists(paths.manifest));
    const auto manifest = load_session_manifest(paths.manifest);
    verify_session_manifest(manifest, paths.elf, paths.snapshot, paths.log);
    CHECK(manifest.replay_verified);

    // Check journal on disk is now committed
    const auto journal = load_transaction_journal(paths.journal);
    CHECK(journal.state == TransactionState::committed);

    // Second recovery should be a no-op idempotent success
    const auto res2 = TransactionRecovery::recover(paths.journal);
    CHECK(res2.success);
    CHECK(!res2.resumed);
    CHECK(res2.final_state == TransactionState::committed);
}

// ----------------------------------------------------------------------------
// Test 3: Recovery after manifest publication but before commit journal update
// ----------------------------------------------------------------------------
void test_resumption_after_manifest_before_commit() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "manifest_uncommitted");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    auto coord = TransactionCoordinator::create("tx-mf-uncomm", paths);
    CHECK(coord->execute(session, 10).success);
    CHECK(coord->state() == TransactionState::committed);

    // Revert the journal state on disk to 'validating' (simulating crash before commit journal write)
    auto j = load_transaction_journal(paths.journal);
    j.state = TransactionState::validating;
    save_transaction_journal(paths.journal, j, false);

    // Inspect
    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.classification == RecoveryClassification::resumable_incomplete);
    CHECK(ins.manifest_exists && ins.manifest_valid);

    // Recover
    const auto res = TransactionRecovery::recover(paths.journal);
    CHECK(res.success);
    CHECK(res.resumed);
    CHECK(res.final_state == TransactionState::committed);

    // Verify journal committed
    const auto j_after = load_transaction_journal(paths.journal);
    CHECK(j_after.state == TransactionState::committed);
}

// ----------------------------------------------------------------------------
// Test 4: Snapshot only (interrupted before recording) -> Nonrecoverable
// ----------------------------------------------------------------------------
void test_snapshot_only_nonrecoverable() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "snap_only");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    // Inject fault on SNDL recording publication
    {
        auto coord = TransactionCoordinator::create("tx-snap-only", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".sndl");
        const auto res = coord->execute(session, 10);
        CHECK(!res.success);
    }

    CHECK(std::filesystem::exists(paths.snapshot));
    CHECK(!std::filesystem::exists(paths.log)); // Log missing
    CHECK(!std::filesystem::exists(paths.manifest));

    // Inspect
    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.classification == RecoveryClassification::nonrecoverable);

    // Recover must fail safely and not delete the snapshot or manifest
    const auto res = TransactionRecovery::recover(paths.journal);
    CHECK(!res.success);
    CHECK(res.final_state == TransactionState::recovery_required);
    CHECK(std::filesystem::exists(paths.snapshot)); // Existing valid snapshot preserved!
}

// ----------------------------------------------------------------------------
// Test 5: Abandoned transaction (interrupted in CREATED state)
// ----------------------------------------------------------------------------
void test_abandoned_transaction() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "abandoned_tx");

    {
        auto coord = TransactionCoordinator::create("tx-abandoned-01", paths);
        CHECK(coord->state() == TransactionState::created);
        // Exiting scope without executing aborts to incomplete
    }

    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.classification == RecoveryClassification::abandoned);

    const auto res = TransactionRecovery::recover(paths.journal);
    CHECK(!res.success);
    CHECK(res.final_state == TransactionState::incomplete);
}

// ----------------------------------------------------------------------------
// Test 6: Corrupted journal & artifact identity mismatch
// ----------------------------------------------------------------------------
void test_corrupted_journal_and_tampered_artifact() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();

    // Corrupted journal
    const auto paths1 = make_paths(dir, "corrupt_j");
    write_file(paths1.journal, "winios_txjournal=1\ngarbage text\n");

    const auto ins1 = TransactionRecovery::inspect(paths1.journal);
    CHECK(!ins1.valid_journal);
    CHECK(ins1.classification == RecoveryClassification::corrupted_journal);

    const auto res1 = TransactionRecovery::recover(paths1.journal);
    CHECK(!res1.success);
    CHECK(res1.final_state == TransactionState::recovery_required);

    // Tampered artifact in resumable transaction
    const auto paths2 = make_paths(dir, "tampered_recovery");
    std::filesystem::copy_file(owner.path(), paths2.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths2.elf).status == ExecutionStatus::completed);

    {
        auto coord = TransactionCoordinator::create("tx-tamper-rec", paths2);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        CHECK(!coord->execute(session, 10).success);
    }

    // Tamper the log file on disk
    auto log_bytes = read_file(paths2.log);
    log_bytes[log_bytes.size() - 5] ^= 0x7f;
    write_file(paths2.log, log_bytes);

    // Inspect: tampered artifact must classify as nonrecoverable
    const auto ins2 = TransactionRecovery::inspect(paths2.journal);
    CHECK(ins2.classification == RecoveryClassification::nonrecoverable);

    const auto res2 = TransactionRecovery::recover(paths2.journal);
    CHECK(!res2.success);
    CHECK(res2.final_state == TransactionState::recovery_required);
    CHECK(!std::filesystem::exists(paths2.manifest)); // Manifest never published!
}

// ----------------------------------------------------------------------------
// Test 7: Stale owned temporary file cleanup & unowned file preservation
// ----------------------------------------------------------------------------
void test_temporary_file_reconciliation() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "temp_clean_tx");

    auto coord = TransactionCoordinator::create("tx-temp-01", paths);

    // Create an owned temporary file
    const auto owned_tmp = dir / ".winios-publish-12345-67890-0.tmp";
    write_file(owned_tmp, "owned temporary content");

    // Create an unowned temporary file (foreign name, not matching WinIOS pattern)
    const auto unowned_tmp = dir / ".winios-stale-unowned.tmp";
    write_file(unowned_tmp, "unowned temporary content");

    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(!ins.owned_temp_files.empty());

    // Recover
    TransactionRecovery::recover(paths.journal);

    // Owned temporary must be cleaned up
    CHECK(!std::filesystem::exists(owned_tmp));
    // Unowned temporary must be strictly preserved!
    CHECK(std::filesystem::exists(unowned_tmp));
}

// ----------------------------------------------------------------------------
// Test 8: Concurrent recovery attempts
// ----------------------------------------------------------------------------
void test_concurrent_recovery() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "concurrent_rec");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    {
        auto coord = TransactionCoordinator::create("tx-concurrent-rec", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        CHECK(!coord->execute(session, 10).success);
    }

    std::barrier gate(4);
    std::atomic<int> success_count{0};
    std::array<std::thread, 4> workers;

    for (std::size_t i = 0; i < 4; ++i) {
        workers[i] = std::thread([&] {
            gate.arrive_and_wait();
            const auto res = TransactionRecovery::recover(paths.journal);
            if (res.success && res.final_state == TransactionState::committed) {
                ++success_count;
            }
        });
    }

    for (auto& w : workers) w.join();
    CHECK(success_count == 4); // All concurrent recoveries see committed final state safely

    const auto journal = load_transaction_journal(paths.journal);
    CHECK(journal.state == TransactionState::committed);
}

// ----------------------------------------------------------------------------
// Test 9: CLI integration for recover and reconcile
// ----------------------------------------------------------------------------
void test_cli_integration() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();

    // Case A: CLI recover on resumable incomplete transaction
    const auto paths = make_paths(dir, "cli_rec");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    {
        auto coord = TransactionCoordinator::create("tx-cli-rec", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        CHECK(!coord->execute(session, 10).success);
    }

    // CLI recover command
    CHECK(run_cli({"win_ios", "elf-session", "recover", "--journal", paths.journal.string()}) == 0);
    CHECK(std::filesystem::exists(paths.manifest));

    // Second CLI recover command is idempotent
    CHECK(run_cli({"win_ios", "elf-session", "recover", "--journal", paths.journal.string()}) == 0);

    // Case B: CLI reconcile command on directory
    CHECK(run_cli({"win_ios", "elf-session", "reconcile", "--dir", dir.string()}) == 0);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    try {
        std::cout << "[1] test_committed_idempotence..." << std::endl;
        test_committed_idempotence();
        std::cout << "[2] test_resumption_before_manifest..." << std::endl;
        test_resumption_before_manifest();
        std::cout << "[3] test_resumption_after_manifest_before_commit..." << std::endl;
        test_resumption_after_manifest_before_commit();
        std::cout << "[4] test_snapshot_only_nonrecoverable..." << std::endl;
        test_snapshot_only_nonrecoverable();
        std::cout << "[5] test_abandoned_transaction..." << std::endl;
        test_abandoned_transaction();
        std::cout << "[6] test_corrupted_journal_and_tampered_artifact..." << std::endl;
        test_corrupted_journal_and_tampered_artifact();
        std::cout << "[7] test_temporary_file_reconciliation..." << std::endl;
        test_temporary_file_reconciliation();
        std::cout << "[8] test_concurrent_recovery..." << std::endl;
        test_concurrent_recovery();
        std::cout << "[9] test_cli_integration..." << std::endl;
        test_cli_integration();
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in recovery tests: " << e.what() << std::endl;
        return 1;
    }
    return test::summarize("stage56_recovery_tests");
}
