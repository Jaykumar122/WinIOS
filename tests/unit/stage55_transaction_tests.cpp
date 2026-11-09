#include "runtime/session_transaction.h"
#include "runtime/artifact_publication.h"
#include "runtime/session_manifest.h"
#include "runtime/elf_session.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#define CHECK_LOGIC_ERROR(expression)                                         \
    test::expect_throw<std::logic_error>(                                     \
        [&] { (void)(expression); }, #expression, __FILE__, __LINE__)

#include <array>
#include <fstream>
#include <iostream>
#include <string>
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

// ----------------------------------------------------------------------------
// Test 1: Journal serialization, parsing, truncation & corruption
// ----------------------------------------------------------------------------
void test_journal_serialization_and_corruption() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "journal_test");

    TransactionRecord r;
    r.transaction_id = "tx-20261107-test01";
    r.state = TransactionState::created;
    r.version = 1;
    r.created_time_ms = 123456789;
    r.updated_time_ms = 123456790;
    r.paths = paths;
    r.elf = {1024, 0x12345678};
    r.snapshot = {2048, 0x23456789};
    r.log = {4096, 0x3456789a};
    r.manifest = {512, 0x456789ab};
    r.snapshot_association = 0xabcdef01;
    r.memory = 0x8000;
    r.budget = 1000;
    r.initial_retired = 0;
    r.final_retired = 10;
    r.steps = 10;
    r.events = 5;
    r.completion = ExecutionStatus::completed;

    const auto text = serialize_transaction_journal(r);
    CHECK(!text.empty());
    CHECK(text.find("transaction_id=tx-20261107-test01\n") != std::string::npos);
    CHECK(text.find("state=CREATED\n") != std::string::npos);
    CHECK(text.find("journal_crc32c=") != std::string::npos);

    // Roundtrip parse
    const auto parsed = parse_transaction_journal(text);
    CHECK(parsed == r);

    // Save and load
    save_transaction_journal(paths.journal, r, true);
    CHECK(std::filesystem::exists(paths.journal));
    const auto loaded = load_transaction_journal(paths.journal);
    CHECK(loaded == r);

    // Duplicate creation on existing journal path must fail
    CHECK_INVALID_ARGUMENT(save_transaction_journal(paths.journal, r, true));

    // Corruption: flip a byte in the serialized body
    auto corrupted = text;
    const auto pos = corrupted.find("final_retired=");
    CHECK(pos != std::string::npos);
    corrupted[pos + 14] = (corrupted[pos + 14] == '9') ? '8' : '9';
    CHECK_INVALID_ARGUMENT(parse_transaction_journal(corrupted));

    // Truncation at multiple lengths
    for (std::size_t len : {0ull, 10ull, 50ull, text.size() / 2, text.size() - 5}) {
        CHECK_INVALID_ARGUMENT(parse_transaction_journal(text.substr(0, len)));
    }

    // Trailing garbage
    CHECK_INVALID_ARGUMENT(parse_transaction_journal(text + "extra_junk=1\n"));
}

// ----------------------------------------------------------------------------
// Test 2: State transitions and invalid transition enforcement
// ----------------------------------------------------------------------------
void test_state_transitions() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "state_test");

    auto coord = TransactionCoordinator::create("tx-state-01", paths);
    CHECK(coord->state() == TransactionState::created);

    // Invalid transition: CREATED -> COMMITTED
    CHECK_LOGIC_ERROR(coord->set_state(TransactionState::committed));
    // Invalid transition: CREATED -> VALIDATING
    CHECK_LOGIC_ERROR(coord->set_state(TransactionState::validating));

    // Valid transition: CREATED -> PREPARING
    coord->set_state(TransactionState::preparing);
    CHECK(coord->state() == TransactionState::preparing);

    // Invalid transition: PREPARING -> CREATED
    CHECK_LOGIC_ERROR(coord->set_state(TransactionState::created));
    // Invalid transition: PREPARING -> COMMITTED
    CHECK_LOGIC_ERROR(coord->set_state(TransactionState::committed));

    // Valid transition: PREPARING -> ARTIFACTS_PUBLISHED
    coord->set_state(TransactionState::artifacts_published);
    CHECK(coord->state() == TransactionState::artifacts_published);

    // Valid transition: ARTIFACTS_PUBLISHED -> VALIDATING
    coord->set_state(TransactionState::validating);
    CHECK(coord->state() == TransactionState::validating);

    // Valid transition: VALIDATING -> INCOMPLETE (via abort)
    coord->abort(TransactionState::incomplete);
    CHECK(coord->state() == TransactionState::incomplete);

    // Valid resumption transition: INCOMPLETE -> PREPARING
    coord->set_state(TransactionState::preparing);
    CHECK(coord->state() == TransactionState::preparing);

    // Move to committed
    coord->set_state(TransactionState::artifacts_published);
    coord->set_state(TransactionState::validating);

    // To commit, we need manifest identity in record; set dummy manifest file and update
    {
        SessionManifest m{};
        m.memory = 0x8000; m.budget = 10; m.steps = 5; m.events = 1;
        m.initial_retired = 0; m.final_retired = 5;
        m.snapshot_association = 0x1111;
        m.elf = {10, 0x1111}; m.snapshot = {10, 0x2222}; m.log = {10, 0x3333};
        m.completion = ExecutionStatus::completed;
        m.replay_verified = true;
        // Directly test immutable terminal state
        coord->set_state(TransactionState::committed);
        CHECK(coord->state() == TransactionState::committed);
        // Committed is terminal: NO transitions allowed
        CHECK_LOGIC_ERROR(coord->set_state(TransactionState::created));
        CHECK_LOGIC_ERROR(coord->set_state(TransactionState::preparing));
        CHECK_LOGIC_ERROR(coord->set_state(TransactionState::validating));
        CHECK_LOGIC_ERROR(coord->abort(TransactionState::incomplete));
    }
}

// ----------------------------------------------------------------------------
// Test 3: Duplicate transaction ID and destination collision
// ----------------------------------------------------------------------------
void test_duplicate_and_collision_rejection() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "dup_test");

    auto coord1 = TransactionCoordinator::create("tx-dup-01", paths);
    CHECK(coord1 != nullptr);

    // Duplicate transaction coordinator on same paths/journal
    CHECK_INVALID_ARGUMENT(TransactionCoordinator::create("tx-dup-01", paths));

    // Collision with pre-existing snapshot destination
    auto paths2 = make_paths(dir, "col_snap");
    write_file(paths2.snapshot, "pre-existing snapshot content");
    CHECK_INVALID_ARGUMENT(TransactionCoordinator::create("tx-col-01", paths2));

    // Collision with pre-existing log destination
    auto paths3 = make_paths(dir, "col_log");
    write_file(paths3.log, "pre-existing log content");
    CHECK_INVALID_ARGUMENT(TransactionCoordinator::create("tx-col-02", paths3));

    // Collision with pre-existing manifest destination
    auto paths4 = make_paths(dir, "col_mf");
    write_file(paths4.manifest, "pre-existing manifest content");
    CHECK_INVALID_ARGUMENT(TransactionCoordinator::create("tx-col-03", paths4));
}

// ----------------------------------------------------------------------------
// Test 4: Successful end-to-end coordinated transaction
// ----------------------------------------------------------------------------
void test_successful_transaction() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "success_tx");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    const auto loaded = session.load(paths.elf);
    CHECK(loaded.status == ExecutionStatus::completed);

    auto coord = TransactionCoordinator::create("tx-success-01", paths);
    CHECK(coord->state() == TransactionState::created);

    const auto res = coord->execute(session, 10);
    CHECK(res.success);
    CHECK(res.error.empty());
    CHECK(res.execution.status == ExecutionStatus::completed || res.execution.status == ExecutionStatus::instruction_limit_reached);
    CHECK(coord->state() == TransactionState::committed);

    // Verify all 4 artifacts exist on disk
    CHECK(std::filesystem::exists(paths.snapshot));
    CHECK(std::filesystem::exists(paths.log));
    CHECK(std::filesystem::exists(paths.manifest));
    CHECK(std::filesystem::exists(paths.journal));

    // Load and verify manifest against published artifacts
    const auto manifest = load_session_manifest(paths.manifest);
    verify_session_manifest(manifest, paths.elf, paths.snapshot, paths.log);
    CHECK(manifest.replay_verified);

    // Load journal from disk and verify it is COMMITTED
    const auto journal = load_transaction_journal(paths.journal);
    CHECK(journal.state == TransactionState::committed);
    CHECK(journal.transaction_id == "tx-success-01");
    CHECK(journal.snapshot.size > 0);
    CHECK(journal.log.size > 0);
    CHECK(journal.manifest.size > 0);
    CHECK(journal.steps > 0);
}

// ----------------------------------------------------------------------------
// Test 5: Injected publication failures at every boundary
// ----------------------------------------------------------------------------
void test_publication_failure_boundaries() {
    for (const auto ext : {".wsnap", ".sndl", ".manifest"}) {
        for (const auto fault_type : {PublicationFault::write, PublicationFault::flush,
                                      PublicationFault::close, PublicationFault::publish}) {
            elf_test::File owner;
            owner.write(elf_test::image());
            const auto dir = owner.path().parent_path();
            const std::string pfx = "fail_" + std::string(ext + 1) + "_" + std::to_string(static_cast<int>(fault_type));
            const auto paths = make_paths(dir, pfx);
            std::filesystem::copy_file(owner.path(), paths.elf);

            RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
            ElfSession session(config);
            CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

            auto coord = TransactionCoordinator::create("tx-" + pfx, paths);
            {
                ScopedPublicationFault fault(fault_type, ext);
                const auto res = coord->execute(session, 10);
                CHECK(!res.success);
            }

            // Manifest must NEVER be published when any earlier step or manifest publication fails
            CHECK(!std::filesystem::exists(paths.manifest));

            // Journal must exist and record incomplete / recovery_required state
            CHECK(std::filesystem::exists(paths.journal));
            const auto journal = load_transaction_journal(paths.journal);
            CHECK(journal.state == TransactionState::incomplete || journal.state == TransactionState::recovery_required);

            // Staged temporary files should not linger
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                CHECK(!entry.path().filename().string().starts_with(".winios-publish-"));
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Test 6: Missing artifact and artifact identity mismatch
// ----------------------------------------------------------------------------
void test_artifact_validation_and_mismatch() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();

    // Run a successful transaction first to generate valid artifacts
    const auto paths = make_paths(dir, "mismatch_base");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    auto coord = TransactionCoordinator::create("tx-mismatch-base", paths);
    CHECK(coord->execute(session, 10).success);

    // Test: Altering snapshot byte causes verification failure
    {
        const auto test_paths = make_paths(dir, "tampered_snap");
        std::filesystem::copy_file(paths.elf, test_paths.elf);

        auto coord2 = TransactionCoordinator::create("tx-tampered-snap", test_paths);
        std::filesystem::copy_file(paths.snapshot, test_paths.snapshot);
        std::filesystem::copy_file(paths.log, test_paths.log);

        // Tamper snapshot content
        auto snap_bytes = read_file(test_paths.snapshot);
        snap_bytes[snap_bytes.size() - 10] ^= 0x5a;
        write_file(test_paths.snapshot, snap_bytes);

        coord2->register_artifacts(session_file_identity(test_paths.elf, ElfSession::kMaxMemory), 0x8000, 10);
        coord2->set_state(TransactionState::artifacts_published);

        // Validating with tampered snapshot must fail
        CHECK_INVALID_ARGUMENT(coord2->validate_published_artifacts());
        CHECK(coord2->state() == TransactionState::recovery_required);
    }

    // Test: Missing log file causes validation failure
    {
        const auto test_paths = make_paths(dir, "missing_log");
        std::filesystem::copy_file(paths.elf, test_paths.elf);

        auto coord3 = TransactionCoordinator::create("tx-missing-log", test_paths);
        std::filesystem::copy_file(paths.snapshot, test_paths.snapshot);

        coord3->register_artifacts(session_file_identity(test_paths.elf, ElfSession::kMaxMemory), 0x8000, 10);
        coord3->set_state(TransactionState::artifacts_published);

        CHECK_INVALID_ARGUMENT(coord3->validate_published_artifacts());
        CHECK(coord3->state() == TransactionState::recovery_required);
    }
}

} // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    try {
        std::cout << "[1] test_journal_serialization_and_corruption..." << std::endl;
        test_journal_serialization_and_corruption();
        std::cout << "[2] test_state_transitions..." << std::endl;
        test_state_transitions();
        std::cout << "[3] test_duplicate_and_collision_rejection..." << std::endl;
        test_duplicate_and_collision_rejection();
        std::cout << "[4] test_successful_transaction..." << std::endl;
        test_successful_transaction();
        std::cout << "[5] test_publication_failure_boundaries..." << std::endl;
        test_publication_failure_boundaries();
        std::cout << "[6] test_artifact_validation_and_mismatch..." << std::endl;
        test_artifact_validation_and_mismatch();
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in main: " << e.what() << std::endl;
        return 1;
    }
    return test::summarize("stage55_transaction_tests");
}
