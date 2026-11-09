#include "runtime/execution_checkpoint.h"
#include "runtime/artifact_publication.h"
#include "runtime/session_transaction.h"
#include "runtime/transaction_recovery.h"
#include "runtime/session_manifest.h"
#include "runtime/elf_session.h"
#include "loader/elf_loader.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::uint8_t> make_program(std::size_t nops = 200) {
    std::vector<std::uint32_t> words(nops, 0xD503201Fu); // NOP
    words.push_back(0xD4400000u); // HLT
    elf_test::Segment seg;
    seg.address = 0x1000;
    seg.size = words.size() * 4;
    seg.flags = 5;
    seg.words = words;
    return elf_test::image({seg});
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
// Test 1: Full End-to-End Lifecycle Integration
// ----------------------------------------------------------------------------
void test_end_to_end_full_lifecycle() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "e2e_lifecycle");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x10000, 0, 0, 0x10000 - 4096, 4096, 100, std::nullopt, false};

    // 1-4: Load, initialize, execute under bounded budget (25 instructions)
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);
    const auto exec_res1 = session.run(25);
    CHECK(exec_res1.instructions_executed == 25);
    CHECK(session.retired() == 25);

    // 5: Capture execution checkpoint
    const auto cp1 = CheckpointManager::capture(session, 100);
    CHECK(CheckpointManager::validate(cp1));
    CHECK(cp1.header.retired_instructions == 25);
    const auto cp_file = dir / "e2e_checkpoint.wchk";
    CHECK(CheckpointManager::save(cp_file, cp1));
    CHECK(std::filesystem::exists(cp_file));

    // 6-8: Transaction coordinator, publication and commit
    {
        auto coord = TransactionCoordinator::create("tx-e2e-001", paths);
        CHECK(coord != nullptr);
        CHECK(coord->state() == TransactionState::created);
        const auto tx_exec = coord->execute(session, 25);
        CHECK(tx_exec.success);
        CHECK(coord->state() == TransactionState::committed);
    }
    CHECK(std::filesystem::exists(paths.snapshot));
    CHECK(std::filesystem::exists(paths.log));
    CHECK(std::filesystem::exists(paths.manifest));
    CHECK(std::filesystem::exists(paths.journal));

    // 9: Reconcile transaction state
    const auto inspection = TransactionRecovery::inspect(paths.journal);
    CHECK(inspection.valid_journal);
    CHECK(inspection.classification == RecoveryClassification::fully_committed);
    const auto recovery_res = TransactionRecovery::recover(paths.journal);
    CHECK(recovery_res.success);
    CHECK(recovery_res.final_state == TransactionState::committed);

    // 10: Restore checkpoint into a fresh session
    ElfSession resumed_session(config);
    CHECK(resumed_session.load(paths.elf).status == ExecutionStatus::completed);
    CHECK(CheckpointManager::restore(resumed_session, cp1));
    CHECK(resumed_session.retired() == 25);

    // 11: Continue execution for 25 more instructions
    const auto exec_res2 = resumed_session.run(25);
    CHECK(exec_res2.instructions_executed == 25);
    CHECK(resumed_session.retired() == 50);

    // 12: Verify parity against uninterrupted execution of 50 instructions
    ElfSession uninterrupted(config);
    CHECK(uninterrupted.load(paths.elf).status == ExecutionStatus::completed);
    const auto un_exec = uninterrupted.run(50);
    CHECK(un_exec.instructions_executed == 50);
    CHECK(uninterrupted.retired() == 50);

    const auto divergence = CheckpointManager::compare_state(resumed_session, uninterrupted);
    CHECK(!divergence.has_divergence);
    CHECK(resumed_session.hashes() == uninterrupted.hashes());
}

// ----------------------------------------------------------------------------
// Test 2: Stress - Long Running Instruction Execution
// ----------------------------------------------------------------------------
void test_long_running_execution() {
    constexpr std::size_t kNopCount = 3000;
    elf_test::File owner;
    owner.write(make_program(kNopCount));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "long_running.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x20000, 0, 0, 0x20000 - 4096, 4096, 4000, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_path).status == ExecutionStatus::completed);

    // Batch 1: Run 1,000 instructions
    const auto res1 = session.run(1000);
    CHECK(res1.instructions_executed == 1000);
    CHECK(res1.status == ExecutionStatus::instruction_limit_reached);
    CHECK(session.retired() == 1000);

    // Batch 2: Run 1,000 instructions
    const auto res2 = session.run(1000);
    CHECK(res2.instructions_executed == 1000);
    CHECK(res2.status == ExecutionStatus::instruction_limit_reached);
    CHECK(session.retired() == 2000);

    // Batch 3: Run remaining instructions up to and including HLT (1,001 instructions)
    const auto res3 = session.run(1500);
    CHECK(res3.instructions_executed == 1001); // 1,000 NOPs + 1 HLT
    CHECK(res3.status == ExecutionStatus::completed);
    CHECK(session.retired() == 3001);
}

// ----------------------------------------------------------------------------
// Test 3: Stress - Memory Boundary & Permitted RAM Limits
// ----------------------------------------------------------------------------
void test_memory_boundary_and_limits() {
    elf_test::File owner;
    owner.write(make_program(50));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "memory_limits.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    // 0 bytes memory configuration must throw invalid_argument
    CHECK_INVALID_ARGUMENT(ElfSession(RuntimeConfig{0, 0, 0, 0, 0, 100, std::nullopt, false}));

    // Memory configuration exceeding kMaxMemory (64MB) must throw invalid_argument
    constexpr std::size_t kExceededMemory = ElfSession::kMaxMemory + 1024u * 1024u;
    CHECK_INVALID_ARGUMENT(ElfSession(RuntimeConfig{kExceededMemory, 0, 0, 0x1000, 4096, 100, std::nullopt, false}));

    // Zero max_instructions must throw invalid_argument
    CHECK_INVALID_ARGUMENT(ElfSession(RuntimeConfig{0x10000, 0, 0, 0x8000, 4096, 0, std::nullopt, false}));

    // Tracing enabled must throw invalid_argument
    CHECK_INVALID_ARGUMENT(ElfSession(RuntimeConfig{0x10000, 0, 0, 0x8000, 4096, 100, std::nullopt, true}));

    // Maximum permitted RAM (64 MB) must succeed
    RuntimeConfig max_config{ElfSession::kMaxMemory, 0, 0, ElfSession::kMaxMemory - 4096, 4096, 100, std::nullopt, false};
    ElfSession max_session(max_config);
    CHECK(max_session.memory_size() == ElfSession::kMaxMemory);
    CHECK(max_session.load(elf_path).status == ExecutionStatus::completed);
    const auto run_res = max_session.run(20);
    CHECK(run_res.instructions_executed == 20);
    CHECK(max_session.retired() == 20);
}

// ----------------------------------------------------------------------------
// Test 4: Stress - Repeated Checkpoint and Restore Cycles
// ----------------------------------------------------------------------------
void test_repeated_checkpoint_cycles() {
    elf_test::File owner;
    owner.write(make_program(200));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "repeated_cp.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x10000, 0, 0, 0x10000 - 4096, 4096, 500, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_path).status == ExecutionStatus::completed);

    constexpr int kCycles = 25;
    std::uint64_t previous_retired = 0;

    for (int cycle = 0; cycle < kCycles; ++cycle) {
        const auto run_res = session.run(2);
        CHECK(run_res.instructions_executed == 2);
        const auto current_retired = session.retired();
        CHECK(current_retired == previous_retired + 2);
        previous_retired = current_retired;

        // Capture, validate, serialize, deserialize, validate
        const auto cp = CheckpointManager::capture(session, 500);
        CHECK(CheckpointManager::validate(cp));
        CHECK(cp.header.retired_instructions == current_retired);

        const auto bytes = CheckpointManager::serialize(cp);
        CHECK(!bytes.empty());
        const auto restored_cp = CheckpointManager::deserialize(bytes);
        CHECK(CheckpointManager::validate(restored_cp));
        CHECK(restored_cp.header.retired_instructions == current_retired);

        // Restore into a secondary session and verify identical state hashes
        ElfSession verifier_session(config);
        CHECK(verifier_session.load(elf_path).status == ExecutionStatus::completed);
        CHECK(CheckpointManager::restore(verifier_session, restored_cp));
        CHECK(verifier_session.retired() == current_retired);
        CHECK(verifier_session.hashes() == session.hashes());
    }
}

// ----------------------------------------------------------------------------
// Test 5: Stress - Repeated Recovery and Directory Reconciliation
// ----------------------------------------------------------------------------
void test_repeated_recovery_cycles() {
    elf_test::File owner;
    owner.write(make_program(50));
    const auto dir = owner.path().parent_path();
    const auto recon_dir = dir / "recon_stress";
    std::filesystem::create_directories(recon_dir);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};

    constexpr int kTransactions = 8;
    for (int i = 0; i < kTransactions; ++i) {
        const std::string name = "tx_recon_" + std::to_string(i);
        const auto paths = make_paths(recon_dir, name);
        std::filesystem::copy_file(owner.path(), paths.elf);

        ElfSession session(config);
        CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

        // Stage interrupted transaction missing manifest
        auto coord = TransactionCoordinator::create(name, paths);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        coord->execute(session, 10);
    }

    // Reconcile directory: all 8 must be discovered and resumed
    const auto report1 = TransactionRecovery::reconcile_directory(recon_dir);
    CHECK(report1.journals_found == kTransactions);
    CHECK(report1.resumed_count == kTransactions);

    // Repeated reconciliation: must be idempotent no-op
    const auto report2 = TransactionRecovery::reconcile_directory(recon_dir);
    CHECK(report2.journals_found == kTransactions);
    CHECK(report2.committed_count == kTransactions);
    CHECK(report2.resumed_count == 0);
}

// ----------------------------------------------------------------------------
// Test 6: Stress - Concurrent Independent Sessions
// ----------------------------------------------------------------------------
void test_concurrent_independent_sessions() {
    elf_test::File owner;
    owner.write(make_program(100));
    const auto dir = owner.path().parent_path();
    const auto elf_template = owner.path();

    constexpr int kThreads = 4;
    std::vector<std::thread> workers;
    std::atomic<int> success_count{0};

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            const auto elf_worker = dir / ("thread_" + std::to_string(t) + ".elf");
            std::filesystem::copy_file(elf_template, elf_worker, std::filesystem::copy_options::overwrite_existing);

            RuntimeConfig config{0x10000, 0, 0, 0x10000 - 4096, 4096, 100, std::nullopt, false};
            ElfSession session(config);
            if (session.load(elf_worker).status != ExecutionStatus::completed) return;

            const auto run1 = session.run(20);
            if (run1.instructions_executed != 20) return;

            const auto cp = CheckpointManager::capture(session, 100);
            if (!CheckpointManager::validate(cp)) return;

            ElfSession resumed(config);
            if (resumed.load(elf_worker).status != ExecutionStatus::completed) return;
            if (!CheckpointManager::restore(resumed, cp)) return;

            const auto run2 = resumed.run(20);
            if (run2.instructions_executed != 20 || resumed.retired() != 40) return;

            ++success_count;
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    CHECK(success_count.load() == kThreads);
}

// ----------------------------------------------------------------------------
// Test 7: Stress - Concurrent Recovery Serialization
// ----------------------------------------------------------------------------
void test_concurrent_recovery_locking() {
    elf_test::File owner;
    owner.write(make_program(50));
    const auto dir = owner.path().parent_path();
    const auto paths = make_paths(dir, "concurrent_rec");
    std::filesystem::copy_file(owner.path(), paths.elf);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

    {
        auto coord = TransactionCoordinator::create("tx-conc-rec", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".manifest");
        coord->execute(session, 10);
    }
    CHECK(std::filesystem::exists(paths.journal));

    constexpr int kThreads = 4;
    std::vector<std::thread> workers;
    std::atomic<int> success_count{0};

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&]() {
            const auto res = TransactionRecovery::recover(paths.journal);
            if (res.success && res.final_state == TransactionState::committed) {
                ++success_count;
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    CHECK(success_count.load() == kThreads);
    const auto ins = TransactionRecovery::inspect(paths.journal);
    CHECK(ins.classification == RecoveryClassification::fully_committed);
}

// ----------------------------------------------------------------------------
// Test 8: Stress - Resource Budget Exhaustion
// ----------------------------------------------------------------------------
void test_resource_budget_exhaustion() {
    elf_test::File owner;
    owner.write(make_program(200));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "budget_exhaustion.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x10000, 0, 0, 0x10000 - 4096, 4096, 1000, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_path).status == ExecutionStatus::completed);

    // Exact budget of 17 instructions
    const auto res1 = session.run(17);
    CHECK(res1.instructions_executed == 17);
    CHECK(res1.status == ExecutionStatus::instruction_limit_reached);
    CHECK(session.retired() == 17);

    // Additional budget of 33 instructions -> exact total 50
    const auto res2 = session.run(33);
    CHECK(res2.instructions_executed == 33);
    CHECK(res2.status == ExecutionStatus::instruction_limit_reached);
    CHECK(session.retired() == 50);
}

// ----------------------------------------------------------------------------
// Test 9: Fault Injection - Malformed ELF Binary Inputs
// ----------------------------------------------------------------------------
void test_malformed_elf_fault_injection() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    Memory mem(0x10000);

    // 1. Truncated ELF (< 64 bytes)
    {
        const auto path = dir / "truncated.elf";
        std::vector<std::uint8_t> short_bytes(32, 0);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(short_bytes.data()), short_bytes.size());
        out.close();
        CHECK_INVALID_ARGUMENT(ElfLoader::load(path, mem));
    }

    // 2. Corrupted Magic
    {
        const auto path = dir / "bad_magic.elf";
        auto bytes = make_program(10);
        bytes[0] = 0x00; // Invalid magic byte
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.close();
        CHECK_INVALID_ARGUMENT(ElfLoader::load(path, mem));
    }

    // 3. 32-bit ELF class (invalid for AArch64 64-bit engine)
    {
        const auto path = dir / "elf32.elf";
        auto bytes = make_program(10);
        bytes[4] = 1; // ELFCLASS32
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.close();
        CHECK_INVALID_ARGUMENT(ElfLoader::load(path, mem));
    }

    // 4. Non-AArch64 machine architecture (x86_64: 62)
    {
        const auto path = dir / "x86_64.elf";
        auto bytes = make_program(10);
        bytes[18] = 62; // EM_X86_64
        bytes[19] = 0;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.close();
        CHECK_INVALID_ARGUMENT(ElfLoader::load(path, mem));
    }

    // 5. Oversized ELF file
    {
        const auto path = dir / "oversized.elf";
        auto bytes = make_program(10);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.close();
        test::expect_throw<std::length_error>(
            [&] { (void)ElfLoader::load(path, mem, 16); },
            "ElfLoader::load(path, mem, 16)", __FILE__, __LINE__);
    }
}

// ----------------------------------------------------------------------------
// Test 10: Fault Injection - Checkpoint Corruption
// ----------------------------------------------------------------------------
void test_checkpoint_fault_injection() {
    elf_test::File owner;
    owner.write(make_program(20));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "cp_faults.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_path).status == ExecutionStatus::completed);
    session.run(5);

    const auto valid_cp = CheckpointManager::capture(session, 100);
    CHECK(CheckpointManager::validate(valid_cp));
    const auto valid_bytes = CheckpointManager::serialize(valid_cp);

    // 1. Corrupted magic (byte 0 flipped causes CRC failure or magic mismatch)
    {
        auto bytes = valid_bytes;
        bytes[0] ^= 0xFF;
        CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(bytes));
    }

    // 2. Corrupted version (causes CRC failure or version mismatch)
    {
        auto bytes = valid_bytes;
        bytes[4] = 99; // invalid version
        CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(bytes));
    }

    // 3. Truncated bytes (< min size)
    {
        std::vector<std::uint8_t> short_bytes(valid_bytes.begin(), valid_bytes.begin() + 32);
        CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(short_bytes));
    }

    // 4. Bit flip in snapshot payload (causes CRC failure)
    {
        auto bytes = valid_bytes;
        bytes.back() ^= 0x01;
        CHECK_INVALID_ARGUMENT(CheckpointManager::deserialize(bytes));
    }
}

// ----------------------------------------------------------------------------
// Test 11: Fault Injection - Publication Seam Faults
// ----------------------------------------------------------------------------
void test_publication_fault_injection() {
    elf_test::File owner;
    owner.write(make_program(20));
    const auto dir = owner.path().parent_path();
    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};

    // 1. Fault during snapshot writing
    {
        const auto paths = make_paths(dir, "fault_snapshot");
        std::filesystem::copy_file(owner.path(), paths.elf);
        ElfSession session(config);
        CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

        auto coord = TransactionCoordinator::create("tx-fault-snap", paths);
        ScopedPublicationFault fault(PublicationFault::write, ".wsnap");
        const auto res = coord->execute(session, 5);
        CHECK(!res.success);
        CHECK(!std::filesystem::exists(paths.snapshot));
        CHECK(!std::filesystem::exists(paths.manifest));
    }

    // 2. Fault during SNDL publishing
    {
        const auto paths = make_paths(dir, "fault_sndl");
        std::filesystem::copy_file(owner.path(), paths.elf);
        ElfSession session(config);
        CHECK(session.load(paths.elf).status == ExecutionStatus::completed);

        auto coord = TransactionCoordinator::create("tx-fault-sndl", paths);
        ScopedPublicationFault fault(PublicationFault::publish, ".sndl");
        const auto res = coord->execute(session, 5);
        CHECK(!res.success);
        CHECK(!std::filesystem::exists(paths.log));
        CHECK(!std::filesystem::exists(paths.manifest));
    }
}

// ----------------------------------------------------------------------------
// Test 12: Divergence Diagnostics
// ----------------------------------------------------------------------------
void test_divergence_diagnostics() {
    elf_test::File owner;
    owner.write(make_program(50));
    const auto dir = owner.path().parent_path();
    const auto elf_path = dir / "divergence_diag.elf";
    std::filesystem::copy_file(owner.path(), elf_path);

    RuntimeConfig config{0x8000, 0, 0, 0x8000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session1(config);
    CHECK(session1.load(elf_path).status == ExecutionStatus::completed);
    session1.run(10);

    ElfSession session2(config);
    CHECK(session2.load(elf_path).status == ExecutionStatus::completed);
    session2.run(15); // Diverged instruction count and PC

    const auto report = CheckpointManager::compare_state(session1, session2);
    CHECK(report.has_divergence);
    CHECK(report.component == "PC" || report.component == "RETIREMENT");
    CHECK(report.expected_value != report.actual_value);
}

// ----------------------------------------------------------------------------
// Test 13: Resource Hardening - Arithmetic Overflow & Cleanup Isolation
// ----------------------------------------------------------------------------
void test_integer_overflow_and_bounds() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    Memory mem(0x10000);

    // 1. Arithmetic overflow in ELF segment address + size
    {
        elf_test::Segment overflow_seg;
        overflow_seg.address = 0xFFFFFFFFFFFFFF00ULL;
        overflow_seg.size = 0x2000ULL; // Wraps around 64-bit integer
        const auto overflow_bytes = elf_test::image({overflow_seg});
        const auto path = dir / "overflow_seg.elf";
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(overflow_bytes.data()), overflow_bytes.size());
        out.close();
        CHECK_INVALID_ARGUMENT(ElfLoader::load(path, mem));
    }

    // 2. Temporary file cleanup isolation: ensure foreign files are preserved
    {
        const auto dummy_user_file = dir / "user_notes.txt";
        const auto dummy_foreign_tmp = dir / "foreign_process.tmp";
        {
            std::ofstream f1(dummy_user_file); f1 << "Do not touch";
            std::ofstream f2(dummy_foreign_tmp); f2 << "Alien temp file";
        }

        TransactionInspection inspection;
        inspection.journal_path = dir / "dummy.txjournal";
        // Attempt temporary file cleanup
        TransactionRecovery::reconcile_temporary_files(inspection);

        // Verify foreign files remain completely untouched
        CHECK(std::filesystem::exists(dummy_user_file));
        CHECK(std::filesystem::exists(dummy_foreign_tmp));
    }
}

} // namespace

int main() {
    try {
        std::cout << "[1] test_end_to_end_full_lifecycle..." << std::endl;
        test_end_to_end_full_lifecycle();

        std::cout << "[2] test_long_running_execution..." << std::endl;
        test_long_running_execution();

        std::cout << "[3] test_memory_boundary_and_limits..." << std::endl;
        test_memory_boundary_and_limits();

        std::cout << "[4] test_repeated_checkpoint_cycles..." << std::endl;
        test_repeated_checkpoint_cycles();

        std::cout << "[5] test_repeated_recovery_cycles..." << std::endl;
        test_repeated_recovery_cycles();

        std::cout << "[6] test_concurrent_independent_sessions..." << std::endl;
        test_concurrent_independent_sessions();

        std::cout << "[7] test_concurrent_recovery_locking..." << std::endl;
        test_concurrent_recovery_locking();

        std::cout << "[8] test_resource_budget_exhaustion..." << std::endl;
        test_resource_budget_exhaustion();

        std::cout << "[9] test_malformed_elf_fault_injection..." << std::endl;
        test_malformed_elf_fault_injection();

        std::cout << "[10] test_checkpoint_fault_injection..." << std::endl;
        test_checkpoint_fault_injection();

        std::cout << "[11] test_publication_fault_injection..." << std::endl;
        test_publication_fault_injection();

        std::cout << "[12] test_divergence_diagnostics..." << std::endl;
        test_divergence_diagnostics();

        std::cout << "[13] test_integer_overflow_and_bounds..." << std::endl;
        test_integer_overflow_and_bounds();
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in main: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in main!" << std::endl;
        return 1;
    }

    return test::summarize("stage58_integration_tests");
}
