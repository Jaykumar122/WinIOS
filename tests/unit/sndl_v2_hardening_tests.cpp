#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "replay/replay_engine.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_writer.h"
#include "trace/trace_serializer.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::sndl;
using namespace winios::snapshot;
using namespace winios::replay;

// =============================================================================
// Group A: Source Audit & Nondeterministic Input Inventory
// =============================================================================
void test_group_a_source_audit() {
    std::cout << "[Group A] Testing Source Audit & Event Registry Bounds...\n";

    // 1. Verify opcode range boundaries
    assert(sndl_v2_event_type_name(SndlV2EventType::CoreCreated) == "CoreCreated");
    assert(sndl_v2_event_type_name(SndlV2EventType::QuantumStart) == "QuantumStart");
    assert(sndl_v2_event_type_name(SndlV2EventType::ExternalInterruptAsserted) == "ExternalInterruptAsserted");
    assert(sndl_v2_event_type_name(SndlV2EventType::CntpctRead) == "CntpctRead");
    assert(sndl_v2_event_type_name(SndlV2EventType::MmioReadResult) == "MmioReadResult");
    assert(sndl_v2_event_type_name(SndlV2EventType::StoreExclusiveResult) == "StoreExclusiveResult");
    assert(sndl_v2_event_type_name(SndlV2EventType::Unknown) == "Unknown");

    // 2. Verify boundary types
    assert(sndl_v2_boundary_type_name(SndlV2BoundaryType::BeforeInstruction) == "BeforeInstruction");
    assert(sndl_v2_boundary_type_name(SndlV2BoundaryType::SchedulerBoundary) == "SchedulerBoundary");
    assert(sndl_v2_boundary_type_name(SndlV2BoundaryType::ExceptionBoundary) == "ExceptionBoundary");

    // 3. Verify wire sizes
    static_assert(sizeof(WSndlV2Header) == 64, "Header must be 64 bytes");
    static_assert(sizeof(WSndlV2BlockHeader) == 48, "Block Header must be 48 bytes");
    static_assert(sizeof(SndlV2Record) == 80, "Record must be 80 bytes");
    static_assert(sizeof(WSndlV2Footer) == 64, "Footer must be 64 bytes");

    std::cout << "  -> Group A PASS\n";
}

// =============================================================================
// Group B: Ordering Semantics & Instruction Retirement Boundaries
// =============================================================================
void test_group_b_ordering_and_retirement() {
    std::cout << "[Group B] Testing Global Ordering & Retirement Boundaries...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_b";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "ordering.sndl";

    WSndlV2Header hdr{};
    hdr.core_count = 2;
    hdr.initial_global_sequence = 1;

    SndlV2Writer writer(SndlV2WriterConfig{8, 100});
    assert(writer.open(sndl_path, hdr).is_success());

    // Generate consecutive strictly monotonic records
    for (std::uint64_t i = 1; i <= 20; ++i) {
        SndlV2Record r{};
        r.global_event_sequence = i;
        r.global_retirement_sequence = i / 2;
        r.core_retirement_count = i / 4;
        r.core_id = static_cast<std::uint32_t>(i % 2);
        r.event_type = SndlV2EventType::QuantumStart;
        r.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
        r.payload1 = 100; // valid quantum budget
        assert(writer.write_record(r).is_success());
    }
    assert(writer.finalize(SndlV2CompletionStatus::Completed).is_success());

    SndlV2ValidationOptions opts{};
    opts.enable_semantic_validation = true;
    opts.enforce_strict_monotonicity = true;

    const auto rep = SndlV2Validator::validate_file(sndl_path, std::nullopt, opts);
    assert(rep.is_valid);
    assert(rep.total_events == 20);
    assert(rep.first_global_sequence == 1);
    assert(rep.last_global_sequence == 20);

    // Test exception boundary: instruction exception must NOT increment retirement
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024);
    SmpScheduler sched(mem, 1, 10);
    auto cpu = std::make_shared<CPU>();
    cpu->set_architectural_exceptions(true);
    sched.register_core(0, cpu, CoreLifecycleState::Runnable);

    SndlV2Recorder recorder(SndlV2RecorderOptions{1, 1024 * 1024});
    const auto rec_path = temp_dir / "exception_test.sndl";
    assert(recorder.start_recording(rec_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Force an invalid opcode instruction to cause exception
    mem->raw_memory().write_u32(0x0, 0x00000000); // Invalid AArch64 instruction
    cpu->set_program_counter(0x0);

    sched.step_quantum();

    assert(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    // Instruction faulted; total instructions retired should be 0
    assert(sched.get_slot(0).total_instructions_retired == 0);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group B PASS\n";
}

// =============================================================================
// Group C: Event Payload Semantics Validation
// =============================================================================
void test_group_c_payload_semantics() {
    std::cout << "[Group C] Testing Semantic Payload Validation...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_c";
    std::filesystem::create_directories(temp_dir);

    WSndlV2Header hdr{};
    hdr.core_count = 2;
    hdr.initial_global_sequence = 1;

    // 1. Invalid event opcode
    {
        const auto p = temp_dir / "bad_opcode.sndl";
        SndlV2Writer w(SndlV2WriterConfig{4, 10});
        assert(w.open(p, hdr).is_success());
        SndlV2Record r{};
        r.global_event_sequence = 1;
        r.event_type = static_cast<SndlV2EventType>(999); // Unknown opcode
        r.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
        assert(w.write_record(r).is_success());
        assert(w.finalize().is_success());

        SndlV2ValidationOptions opts{};
        opts.enable_semantic_validation = true;
        const auto rep = SndlV2Validator::validate_file(p, std::nullopt, opts);
        assert(!rep.is_valid);
        assert(rep.code == SndlV2ErrorCode::MalformedRecord);
    }

    // 2. QuantumStart with zero budget
    {
        const auto p = temp_dir / "bad_quantum.sndl";
        SndlV2Writer w(SndlV2WriterConfig{4, 10});
        assert(w.open(p, hdr).is_success());
        SndlV2Record r{};
        r.global_event_sequence = 1;
        r.event_type = SndlV2EventType::QuantumStart;
        r.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
        r.payload1 = 0; // Invalid 0 budget
        assert(w.write_record(r).is_success());
        assert(w.finalize().is_success());

        SndlV2ValidationOptions opts{};
        opts.enable_semantic_validation = true;
        const auto rep = SndlV2Validator::validate_file(p, std::nullopt, opts);
        assert(!rep.is_valid);
        assert(rep.code == SndlV2ErrorCode::InvalidPayload);
    }

    // 3. StoreExclusiveResult with invalid status
    {
        const auto p = temp_dir / "bad_stxr.sndl";
        SndlV2Writer w(SndlV2WriterConfig{4, 10});
        assert(w.open(p, hdr).is_success());
        SndlV2Record r{};
        r.global_event_sequence = 1;
        r.event_type = SndlV2EventType::StoreExclusiveResult;
        r.boundary_type = SndlV2BoundaryType::DuringInstruction;
        r.payload1 = 0x1000;
        r.payload2 = 2; // Invalid status (> 1)
        assert(w.write_record(r).is_success());
        assert(w.finalize().is_success());

        SndlV2ValidationOptions opts{};
        opts.enable_semantic_validation = true;
        const auto rep = SndlV2Validator::validate_file(p, std::nullopt, opts);
        assert(!rep.is_valid);
        assert(rep.code == SndlV2ErrorCode::InvalidPayload);
    }

    // 4. MmioReadResult with invalid width
    {
        const auto p = temp_dir / "bad_mmio.sndl";
        SndlV2Writer w(SndlV2WriterConfig{4, 10});
        assert(w.open(p, hdr).is_success());
        SndlV2Record r{};
        r.global_event_sequence = 1;
        r.event_type = SndlV2EventType::MmioReadResult;
        r.boundary_type = SndlV2BoundaryType::DuringInstruction;
        r.payload1 = 0x2000;
        r.payload3 = 3; // Invalid width (must be 1, 2, 4, 8)
        assert(w.write_record(r).is_success());
        assert(w.finalize().is_success());

        SndlV2ValidationOptions opts{};
        opts.enable_semantic_validation = true;
        const auto rep = SndlV2Validator::validate_file(p, std::nullopt, opts);
        assert(!rep.is_valid);
        assert(rep.code == SndlV2ErrorCode::InvalidPayload);
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group C PASS\n";
}

// =============================================================================
// Group D: Snapshot Binding Hardening & Canonical Hash Verification
// =============================================================================
void test_group_d_snapshot_binding() {
    std::cout << "[Group D] Testing Snapshot Binding & Canonical Hash Verification...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_d";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "base.wsnap";
    const auto sndl_path = temp_dir / "valid_bound.sndl";

    // 1. Create a 4-core snapshot
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024);
    SmpScheduler sched(mem, 4, 100);
    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);
    SmpSnapshotInspection snap_info{};
    assert(SmpSnapshotManager::inspect_file(wsnap_path, snap_info).success);
    const std::uint64_t true_hash = snap_info.integrity_data.canonical_cluster_hash;
    assert(true_hash != 0);

    // 2. Create matching SNDL v2 recording with base_snapshot_hash bound
    {
        SndlV2Recorder recorder(SndlV2RecorderOptions{
            4,
            snap_info.header.memory_capacity,
            true_hash,
            snap_info.header.total_file_size,
            1,
            64
        });
        assert(recorder.start_recording(sndl_path).is_success());
        recorder.record_core_selected(0, 0, 0x1000);
        recorder.record_quantum_start(0, 100, 0x1000);
        assert(recorder.stop_recording().is_success());

        const auto val_ok = SndlV2Validator::validate_file(sndl_path, wsnap_path);
        assert(val_ok.is_valid);
        assert(val_ok.snapshot_checked);
        assert(val_ok.snapshot_matched);
    }

    // 3. Test Snapshot Hash Mismatch
    {
        const auto bad_hash_path = temp_dir / "bad_hash.sndl";
        SndlV2Recorder recorder(SndlV2RecorderOptions{
            4,
            snap_info.header.memory_capacity,
            true_hash ^ 0xDEADBEEF, // Mismatched hash
            snap_info.header.total_file_size,
            1,
            64
        });
        assert(recorder.start_recording(bad_hash_path).is_success());
        recorder.record_core_selected(0, 0, 0x1000);
        assert(recorder.stop_recording().is_success());

        const auto val_bad = SndlV2Validator::validate_file(bad_hash_path, wsnap_path);
        assert(!val_bad.is_valid);
        assert(val_bad.code == SndlV2ErrorCode::SnapshotMismatch);
    }

    // 4. Test Missing Snapshot File
    {
        const auto val_missing = SndlV2Validator::validate_file(sndl_path, temp_dir / "nonexistent.wsnap");
        assert(!val_missing.is_valid);
        assert(val_missing.code == SndlV2ErrorCode::SnapshotNotFound);
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group D PASS\n";
}

// =============================================================================
// Group E: Adversarial Binary Format Validation
// =============================================================================
void test_group_e_adversarial_validation() {
    std::cout << "[Group E] Testing Adversarial Binary Validation & Tampering Detection...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_e";
    std::filesystem::create_directories(temp_dir);

    // Create a valid baseline SNDL v2 file
    const auto valid_path = temp_dir / "baseline.sndl";
    WSndlV2Header hdr{};
    hdr.core_count = 2;
    hdr.initial_global_sequence = 1;

    SndlV2Writer w(SndlV2WriterConfig{4, 100});
    assert(w.open(valid_path, hdr).is_success());
    for (std::uint64_t i = 1; i <= 8; ++i) {
        SndlV2Record r{};
        r.global_event_sequence = i;
        r.core_id = 0;
        r.event_type = SndlV2EventType::CoreSelected;
        assert(w.write_record(r).is_success());
    }
    assert(w.finalize(SndlV2CompletionStatus::Completed).is_success());

    std::vector<std::uint8_t> valid_bytes;
    {
        std::ifstream f(valid_path, std::ios::binary);
        valid_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    // Helper to test mutated byte vector
    auto test_mutation = [&](const std::vector<std::uint8_t>& bytes, SndlV2ErrorCode expected_code) {
        const auto p = temp_dir / "mutated.sndl";
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();

        const auto rep = SndlV2Validator::validate_file(p);
        assert(!rep.is_valid);
        assert(rep.code == expected_code);
    };

    // 1. Corrupted Header Magic
    {
        auto b = valid_bytes;
        b[0] = 'X';
        test_mutation(b, SndlV2ErrorCode::InvalidMagic);
    }

    // 2. Unsupported Version (e.g. version 3)
    {
        auto b = valid_bytes;
        trace::encode_u16_le(b.data() + 8, 3);
        // Recalculate header CRC to test version check specifically
        const auto crc = trace::calculate_crc32c(b.data(), 60);
        trace::encode_u32_le(b.data() + 60, crc);
        test_mutation(b, SndlV2ErrorCode::UnsupportedVersion);
    }

    // 3. Header CRC Mismatch
    {
        auto b = valid_bytes;
        b[16] ^= 0xFF; // Corrupt payload byte in header without fixing CRC
        test_mutation(b, SndlV2ErrorCode::HeaderCrcMismatch);
    }

    // 4. Corrupted Block Magic
    {
        auto b = valid_bytes;
        b[kWSndlV2HeaderSize] = 'Z'; // Block magic offset
        test_mutation(b, SndlV2ErrorCode::InvalidMagic);
    }

    // 5. Block Header CRC Mismatch
    {
        auto b = valid_bytes;
        b[kWSndlV2HeaderSize + 8] ^= 0x55; // Corrupt first_global_seq
        test_mutation(b, SndlV2ErrorCode::BlockHeaderCrcMismatch);
    }

    // 6. Block Payload CRC Mismatch
    {
        auto b = valid_bytes;
        b[kWSndlV2HeaderSize + kWSndlV2BlockHeaderSize + 4] ^= 0xAA; // Corrupt record byte
        test_mutation(b, SndlV2ErrorCode::BlockPayloadCrcMismatch);
    }

    // 7. Footer Truncation
    {
        auto b = valid_bytes;
        b.resize(b.size() - 20); // Truncate part of footer
        test_mutation(b, SndlV2ErrorCode::TruncatedBlock);
    }

    // 8. Trailing Garbage after Footer
    {
        auto b = valid_bytes;
        b.push_back(0xDE);
        b.push_back(0xAD);
        const auto p = temp_dir / "trailing.sndl";
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        out.close();

        SndlV2ValidationOptions opts{};
        opts.check_trailing_garbage = true;
        const auto rep = SndlV2Validator::validate_file(p, std::nullopt, opts);
        assert(!rep.is_valid);
        assert(rep.code == SndlV2ErrorCode::TrailingGarbage);
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group E PASS\n";
}

// =============================================================================
// Group F: Overflow, Crash and Recovery Semantics
// =============================================================================
void test_group_f_overflow_and_recovery() {
    std::cout << "[Group F] Testing Buffer Overflow, Crash Interruption & Safe Recovery...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_f";
    std::filesystem::create_directories(temp_dir);

    // 1. Buffer Overflow: write 25 records into capacity 10 buffer
    {
        const auto p = temp_dir / "overflow.sndl";
        WSndlV2Header hdr{};
        hdr.core_count = 2;
        SndlV2Writer w(SndlV2WriterConfig{4, 10});
        assert(w.open(p, hdr).is_success());

        for (std::uint64_t i = 1; i <= 25; ++i) {
            SndlV2Record r{};
            r.global_event_sequence = i;
            r.event_type = SndlV2EventType::CoreSelected;
            (void)w.write_record(r);
        }
        assert(w.finalize().is_success());
        assert(w.has_overflowed());
        assert(w.dropped_events() == 15);

        const auto rep = SndlV2Validator::validate_file(p);
        assert(rep.is_valid);
        assert(rep.completion_status == SndlV2CompletionStatus::Overflow);
    }

    // 2. Crash Interruption: Write 2 valid blocks, then terminate without footer
    {
        const auto crashed_p = temp_dir / "crashed.sndl";
        const auto recovered_p = temp_dir / "recovered.sndl";

        WSndlV2Header hdr{};
        hdr.core_count = 2;
        hdr.initial_global_sequence = 1;

        SndlV2Writer w(SndlV2WriterConfig{4, 100});
        assert(w.open(crashed_p, hdr).is_success());
        for (std::uint64_t i = 1; i <= 8; ++i) {
            SndlV2Record r{};
            r.global_event_sequence = i;
            r.event_type = SndlV2EventType::CoreSelected;
            assert(w.write_record(r).is_success());
        }
        // Force crash: close file without calling finalize()
        w.close();

        // Validating crashed file must report IncompleteRecording
        const auto crashed_rep = SndlV2Validator::validate_file(crashed_p);
        assert(!crashed_rep.is_valid);
        assert(crashed_rep.code == SndlV2ErrorCode::IncompleteRecording);

        // Recover crashed file
        const auto rec_res = SndlV2Validator::recover_incomplete_recording(crashed_p, recovered_p);
        assert(rec_res.is_success());

        // Recovered file must validate and be marked Aborted (never Completed)
        const auto rec_rep = SndlV2Validator::validate_file(recovered_p);
        assert(rec_rep.is_valid);
        assert(rec_rep.completion_status == SndlV2CompletionStatus::Aborted);
        assert(rec_rep.total_events == 8);
        assert(rec_rep.total_blocks == 2);
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group F PASS\n";
}

// =============================================================================
// Group G: Independent Recording Determinism (10 Consecutive Multi-Core Runs)
// =============================================================================
void test_group_g_independent_determinism() {
    std::cout << "[Group G] Testing Independent Multi-Core Recording Determinism (10 Runs)...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_g";
    std::filesystem::create_directories(temp_dir);

    auto run_cluster = [&](std::size_t core_count, const std::filesystem::path& out_path) -> std::uint64_t {
        auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024);
        SmpScheduler sched(mem, core_count, 10);
        for (std::size_t c = 0; c < core_count; ++c) {
            auto cpu = std::make_shared<CPU>();
            sched.register_core(c, cpu, CoreLifecycleState::Runnable);
        }

        SndlV2Recorder recorder(SndlV2RecorderOptions{static_cast<std::uint16_t>(core_count), 1024 * 1024});
        assert(recorder.start_recording(out_path).is_success());
        sched.attach_sndl_recorder(&recorder);

        // Deterministic instruction workload across all cores
        for (std::size_t c = 0; c < core_count; ++c) {
            const std::uint64_t code_addr = c * 0x1000;
            mem->raw_memory().write_u32(code_addr + 0, 0xD2800020u | (static_cast<std::uint32_t>(c) << 5)); // MOV X<c>, #1
            mem->raw_memory().write_u32(code_addr + 4, 0xD2800041u);             // MOV X1, #2
            mem->raw_memory().write_u32(code_addr + 8, 0x14000000u);             // B . (loop)
            sched.get_slot(c).cpu->set_program_counter(code_addr);
        }

        for (int step = 0; step < 10; ++step) {
            sched.step_quantum();
        }

        assert(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
        return sched.compute_canonical_multicore_hash();
    };

    // 10 consecutive runs of 4-core cluster
    std::vector<std::uint8_t> baseline_bytes;
    std::uint64_t baseline_hash = 0;

    for (int run = 0; run < 10; ++run) {
        const auto run_path = temp_dir / ("run_" + std::to_string(run) + ".sndl");
        const std::uint64_t hash = run_cluster(4, run_path);

        std::ifstream f(run_path, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        if (run == 0) {
            baseline_bytes = std::move(bytes);
            baseline_hash = hash;
        } else {
            // Verify bit-for-bit identical recording bytes
            assert(bytes.size() == baseline_bytes.size());
            assert(std::memcmp(bytes.data(), baseline_bytes.data(), bytes.size()) == 0);
            // Verify bit-for-bit identical final cluster hash
            assert(hash == baseline_hash);
        }
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group G PASS\n";
}

// =============================================================================
// Group H: Deterministic Fuzzing & Mutation Testing Harness
// =============================================================================
void test_group_h_deterministic_fuzzing() {
    std::cout << "[Group H] Testing Bounded Deterministic Mutation & Fuzzing Harness...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_h";
    std::filesystem::create_directories(temp_dir);
    const auto valid_p = temp_dir / "valid_seed.sndl";

    WSndlV2Header hdr{};
    hdr.core_count = 4;
    hdr.initial_global_sequence = 1;

    SndlV2Writer w(SndlV2WriterConfig{4, 100});
    assert(w.open(valid_p, hdr).is_success());
    for (std::uint64_t i = 1; i <= 16; ++i) {
        SndlV2Record r{};
        r.global_event_sequence = i;
        r.core_id = static_cast<std::uint32_t>(i % 4);
        r.event_type = SndlV2EventType::CoreSelected;
        assert(w.write_record(r).is_success());
    }
    assert(w.finalize().is_success());

    std::vector<std::uint8_t> seed_bytes;
    {
        std::ifstream f(valid_p, std::ios::binary);
        seed_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    // Deterministic PRNG with fixed seed
    std::mt19937_64 rng(0x123456789ABCDEF0ULL);

    // Run 100 bit-flip and byte-tamper mutations
    for (int iter = 0; iter < 100; ++iter) {
        auto mutated = seed_bytes;
        const std::size_t target_idx = rng() % mutated.size();
        mutated[target_idx] ^= static_cast<std::uint8_t>(1 << (rng() % 8));

        const auto mut_path = temp_dir / "fuzz.sndl";
        std::ofstream out(mut_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(mutated.data()), static_cast<std::streamsize>(mutated.size()));
        out.close();

        // Validator must never crash or throw unhandled exceptions
        const auto rep = SndlV2Validator::validate_file(mut_path);
        // Any single bit-flip corrupts CRC or payload and must be detected
        assert(!rep.is_valid);
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group H PASS\n";
}

// =============================================================================
// Group I: Performance & Resource Limits Validation
// =============================================================================
void test_group_i_performance_and_limits() {
    std::cout << "[Group I] Testing Performance & Bounded Resource Limits...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_i";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_p = temp_dir / "perf.sndl";

    WSndlV2Header hdr{};
    hdr.core_count = 4;
    hdr.initial_global_sequence = 1;

    SndlV2Writer w(SndlV2WriterConfig{64, 10000});
    assert(w.open(sndl_p, hdr).is_success());

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint64_t i = 1; i <= 5000; ++i) {
        SndlV2Record r{};
        r.global_event_sequence = i;
        r.core_id = static_cast<std::uint32_t>(i % 4);
        r.event_type = SndlV2EventType::CoreSelected;
        assert(w.write_record(r).is_success());
    }
    assert(w.finalize().is_success());
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double write_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    const auto val_t0 = std::chrono::high_resolution_clock::now();
    const auto val_rep = SndlV2Validator::validate_file(sndl_p);
    const auto val_t1 = std::chrono::high_resolution_clock::now();
    const double val_ms = std::chrono::duration<double, std::milli>(val_t1 - val_t0).count();

    assert(val_rep.is_valid);
    assert(val_rep.total_events == 5000);
    std::cout << "    [Perf] 5000 events write: " << write_ms << " ms | validate: " << val_ms << " ms\n";

    // Test block resource limit check
    SndlV2ValidationOptions opts{};
    opts.max_block_payload_bytes = 100; // Tiny 100-byte limit (our blocks are 64 * 80 = 5120 bytes)
    const auto rep_limit = SndlV2Validator::validate_file(sndl_p, std::nullopt, opts);
    assert(!rep_limit.is_valid);
    assert(rep_limit.code == SndlV2ErrorCode::ResourceLimitExceeded);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group I PASS\n";
}

// =============================================================================
// Group J: Compatibility & Mandatory SMP Replay Rejection Guardrail
// =============================================================================
void test_group_j_compatibility_and_rejection() {
    std::cout << "[Group J] Testing Compatibility & Replay Safety Guardrails...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_test_j";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "smp.wsnap";
    const auto sndl_path = temp_dir / "smp.sndl";

    // 1. Create a 4-core snapshot
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024);
    SmpScheduler sched(mem, 4, 100);
    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }
    assert(SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);

    // 2. Create a 4-core SNDL v2 log
    SndlV2Recorder recorder(SndlV2RecorderOptions{4, 1024 * 1024, 0, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());
    recorder.record_quantum_start(0, 100, 0x1000);
    assert(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());

    // 3. MANDATORY SAFETY GUARDRAIL: Deterministic replay session MUST explicitly reject
    // multi-core configurations with ReplayErrorCode::UnsupportedConfiguration!
    ReplaySession session;
    ReplayConfig config{};
    const auto res = session.prepare(wsnap_path, sndl_path, config);

    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group J PASS\n";
}

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
#endif
    std::cout << "===============================================================\n";
    std::cout << "  Phase 52, Stage 25: SNDL v2 Hardening & Readiness Suite      \n";
    std::cout << "===============================================================\n";

    test_group_a_source_audit();
    test_group_b_ordering_and_retirement();
    test_group_c_payload_semantics();
    test_group_d_snapshot_binding();
    test_group_e_adversarial_validation();
    test_group_f_overflow_and_recovery();
    test_group_g_independent_determinism();
    test_group_h_deterministic_fuzzing();
    test_group_i_performance_and_limits();
    test_group_j_compatibility_and_rejection();

    std::cout << "\n===============================================================\n";
    std::cout << "  ALL 10 STAGE 25 TEST GROUPS PASSED (100% SUCCESS)            \n";
    std::cout << "===============================================================\n";
    return 0;
}
