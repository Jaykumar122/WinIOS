#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_writer.h"
#include "snapshot/smp_snapshot_manager.h"
#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "replay/replay_session.h"
#include "replay/replay_engine.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

// =============================================================================
// Group A: Binary Format (Header, Block, Footer, Record round-trips, CRC32C)
// =============================================================================
void test_group_a_binary_format() {
    std::cout << "[Group A] Testing SNDL v2 Binary Format and Wire Layout...\n";

    // A1: Header round-trip
    {
        winios::sndl::WSndlV2Header hdr{};
        hdr.magic = winios::sndl::kWSndlV2Magic;
        hdr.version = 2;
        hdr.core_count = 4;
        hdr.flags = 0x55AA;
        hdr.compression_type = 0;
        hdr.base_snapshot_hash = 0x0123456789ABCDEFULL;
        hdr.base_snapshot_file_size = 1048576;
        hdr.memory_capacity = 67108864;
        hdr.initial_global_sequence = 100;
        hdr.total_events = 5000;
        hdr.total_blocks = 10;

        std::uint8_t buffer[winios::sndl::kWSndlV2HeaderSize]{};
        winios::sndl::encode_sndl_v2_header(hdr, buffer);

        winios::sndl::WSndlV2Header decoded{};
        const auto res = winios::sndl::decode_sndl_v2_header(buffer, sizeof(buffer), decoded);
        assert(res.is_success());
        assert(decoded.magic == hdr.magic);
        assert(decoded.version == 2);
        assert(decoded.core_count == 4);
        assert(decoded.flags == 0x55AA);
        assert(decoded.base_snapshot_hash == 0x0123456789ABCDEFULL);
        assert(decoded.base_snapshot_file_size == 1048576);
        assert(decoded.memory_capacity == 67108864);
        assert(decoded.initial_global_sequence == 100);
        assert(decoded.total_events == 5000);
        assert(decoded.total_blocks == 10);

        // CRC check
        buffer[20] ^= 0xFF;
        const auto bad_crc = winios::sndl::decode_sndl_v2_header(buffer, sizeof(buffer), decoded);
        assert(bad_crc.code == winios::sndl::SndlV2ErrorCode::HeaderCrcMismatch);
    }

    // A2: Block Header round-trip
    {
        winios::sndl::WSndlV2BlockHeader blk{};
        blk.magic = winios::sndl::kWSndlV2BlockMagic;
        blk.version = 2;
        blk.flags = 0;
        blk.block_index = 3;
        blk.record_count = 512;
        blk.payload_bytes = 512 * 80;
        blk.first_global_sequence = 1537;
        blk.last_global_sequence = 2048;
        blk.payload_crc32c = 0xCAFEBABE;

        std::uint8_t buffer[winios::sndl::kWSndlV2BlockHeaderSize]{};
        winios::sndl::encode_sndl_v2_block_header(blk, buffer);

        winios::sndl::WSndlV2BlockHeader decoded{};
        const auto res = winios::sndl::decode_sndl_v2_block_header(buffer, sizeof(buffer), decoded);
        assert(res.is_success());
        assert(decoded.magic == blk.magic);
        assert(decoded.version == 2);
        assert(decoded.block_index == 3);
        assert(decoded.record_count == 512);
        assert(decoded.payload_bytes == 512 * 80);
        assert(decoded.first_global_sequence == 1537);
        assert(decoded.last_global_sequence == 2048);
        assert(decoded.payload_crc32c == 0xCAFEBABE);

        // Corrupted block CRC
        buffer[10] ^= 0x42;
        const auto bad_crc = winios::sndl::decode_sndl_v2_block_header(buffer, sizeof(buffer), decoded);
        assert(bad_crc.code == winios::sndl::SndlV2ErrorCode::BlockHeaderCrcMismatch);
    }

    // A3: Record round-trip
    {
        winios::sndl::SndlV2Record rec{};
        rec.global_event_sequence = 42;
        rec.global_retirement_sequence = 1000;
        rec.core_retirement_count = 250;
        rec.pc = 0x80001000ULL;
        rec.payload1 = 0x12345678ULL;
        rec.payload2 = 0x9ABCDEF0ULL;
        rec.payload3 = 8;
        rec.core_id = 2;
        rec.source_id = 1;
        rec.flags = 0x80;
        rec.sub_instruction_ordinal = 0;
        rec.event_type = winios::sndl::SndlV2EventType::MmioReadResult;
        rec.boundary_type = winios::sndl::SndlV2BoundaryType::DuringInstruction;
        rec.payload_size = 8;

        std::uint8_t buffer[winios::sndl::kWSndlV2RecordSize]{};
        winios::sndl::encode_sndl_v2_record(rec, buffer);

        winios::sndl::SndlV2Record decoded{};
        const auto res = winios::sndl::decode_sndl_v2_record(buffer, sizeof(buffer), decoded);
        assert(res.is_success());
        assert(decoded == rec);

        // Corrupted record CRC
        buffer[35] ^= 0x01;
        const auto bad_crc = winios::sndl::decode_sndl_v2_record(buffer, sizeof(buffer), decoded);
        assert(bad_crc.code == winios::sndl::SndlV2ErrorCode::RecordCrcMismatch);
    }

    // A4: Footer round-trip
    {
        winios::sndl::WSndlV2Footer ftr{};
        ftr.magic = winios::sndl::kWSndlV2FooterMagic;
        ftr.version = 2;
        ftr.completion_status = winios::sndl::SndlV2CompletionStatus::Completed;
        ftr.total_blocks = 20;
        ftr.total_events = 10240;
        ftr.last_global_sequence = 10240;
        ftr.last_global_retirement_sequence = 50000;
        ftr.dropped_events = 0;

        std::uint8_t buffer[winios::sndl::kWSndlV2FooterSize]{};
        winios::sndl::encode_sndl_v2_footer(ftr, buffer);

        winios::sndl::WSndlV2Footer decoded{};
        const auto res = winios::sndl::decode_sndl_v2_footer(buffer, sizeof(buffer), decoded);
        assert(res.is_success());
        assert(decoded.magic == ftr.magic);
        assert(decoded.version == 2);
        assert(decoded.completion_status == winios::sndl::SndlV2CompletionStatus::Completed);
        assert(decoded.total_blocks == 20);
        assert(decoded.total_events == 10240);
        assert(decoded.last_global_sequence == 10240);
        assert(decoded.last_global_retirement_sequence == 50000);
        assert(decoded.dropped_events == 0);

        // Corrupted footer CRC
        buffer[20] ^= 0x77;
        const auto bad_crc = winios::sndl::decode_sndl_v2_footer(buffer, sizeof(buffer), decoded);
        assert(bad_crc.code == winios::sndl::SndlV2ErrorCode::FooterCrcMismatch);

        // Corrupted footer magic
        buffer[2] ^= 0xFF;
        const auto bad_magic = winios::sndl::decode_sndl_v2_footer(buffer, sizeof(buffer), decoded);
        assert(bad_magic.code == winios::sndl::SndlV2ErrorCode::InvalidMagic);
    }
    std::cout << "  -> Group A PASS\n";
}

// =============================================================================
// Group B: Global Ordering & Monotonicity
// =============================================================================
void test_group_b_global_ordering() {
    std::cout << "[Group B] Testing Global Event Ordering and Monotonicity...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_b";
    std::filesystem::create_directories(temp_dir);
    const auto file_path = temp_dir / "ordering_test.sndl";

    winios::sndl::WSndlV2Header hdr{};
    hdr.core_count = 4;
    hdr.initial_global_sequence = 0;

    winios::sndl::SndlV2Writer writer(winios::sndl::SndlV2WriterConfig{4, 100});
    const auto open_res = writer.open(file_path, hdr);
    assert(open_res.is_success());

    // Interleave events across 4 cores with strictly increasing global sequence
    std::uint64_t global_seq = 1;
    std::uint64_t global_ret = 10;
    std::uint64_t core_rets[4] = {0, 0, 0, 0};

    for (int step = 0; step < 12; ++step) {
        const std::uint32_t c = step % 4;
        core_rets[c] += 5;
        global_ret += 5;

        winios::sndl::SndlV2Record r{};
        r.global_event_sequence = global_seq++;
        r.global_retirement_sequence = global_ret;
        r.core_retirement_count = core_rets[c];
        r.core_id = c;
        r.event_type = winios::sndl::SndlV2EventType::QuantumStart;
        r.boundary_type = winios::sndl::SndlV2BoundaryType::SchedulerBoundary;
        assert(writer.write_record(r).is_success());
    }

    assert(writer.finalize(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // Validate ordering
    const auto val = winios::sndl::SndlV2Validator::validate_file(file_path);
    assert(val.is_valid);
    assert(val.total_events == 12);
    assert(val.first_global_sequence == 1);
    assert(val.last_global_sequence == 12);

    // Test rejection of non-monotonic sequence
    const auto bad_path = temp_dir / "bad_ordering.sndl";
    winios::sndl::SndlV2Writer bad_writer(winios::sndl::SndlV2WriterConfig{4, 100});
    assert(bad_writer.open(bad_path, hdr).is_success());

    winios::sndl::SndlV2Record r1{};
    r1.global_event_sequence = 5;
    r1.core_id = 0;
    r1.event_type = winios::sndl::SndlV2EventType::CoreStarted;
    assert(bad_writer.write_record(r1).is_success());

    winios::sndl::SndlV2Record r2{};
    r2.global_event_sequence = 4; // Non-monotonic!
    r2.core_id = 0;
    r2.event_type = winios::sndl::SndlV2EventType::CoreHalted;
    assert(bad_writer.write_record(r2).is_success());

    assert(bad_writer.finalize(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto bad_val = winios::sndl::SndlV2Validator::validate_file(bad_path);
    assert(!bad_val.is_valid);
    assert(bad_val.code == winios::sndl::SndlV2ErrorCode::InvalidSequence);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group B PASS\n";
}

// =============================================================================
// Group C: Scheduler Recording & Canonical Hash Parity
// =============================================================================
void test_group_c_scheduler_recording() {
    std::cout << "[Group C] Testing Scheduler Recording and Canonical Hash Parity...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_c";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "sched_recording.sndl";

    // Setup 4 cores
    auto mem1 = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched1(mem1, 4, 50);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        // Write simple arithmetic sequence
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem1->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>(c) << 5)); // MOVZ X0, #c
        mem1->raw_memory().write_u32(code_addr + 4, 0x91000400u); // ADD X0, X0, #1
        mem1->raw_memory().write_u32(code_addr + 8, 0x14000000u); // B . (loop)
        cpu->set_program_counter(code_addr);
        sched1.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    // Run without recording
    sched1.run_deterministic(400);
    const std::uint64_t hash_unrecorded = sched1.compute_canonical_multicore_hash();

    // Now run with recording attached
    auto mem2 = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched2(mem2, 4, 50);

    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        const std::uint64_t code_addr = 0x1000 + c * 0x100;
        mem2->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>(c) << 5));
        mem2->raw_memory().write_u32(code_addr + 4, 0x91000400u);
        mem2->raw_memory().write_u32(code_addr + 8, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched2.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{4, 1024 * 1024, 0, 0, 0, 64});
    assert(recorder.start_recording(sndl_path).is_success());
    sched2.attach_sndl_recorder(&recorder);

    sched2.run_deterministic(400);
    const std::uint64_t hash_recorded = sched2.compute_canonical_multicore_hash();

    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // Zero divergence! Canonical state hashes MUST be identical
    assert(hash_unrecorded == hash_recorded);

    // Validate generated SNDL file
    const auto val = winios::sndl::SndlV2Validator::validate_file(sndl_path);
    assert(val.is_valid);
    assert(val.total_events > 0);
    assert(val.core_count == 4);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group C PASS\n";
}

// =============================================================================
// Group D: Interrupt and Exception Recording
// =============================================================================
void test_group_d_interrupt_recording() {
    std::cout << "[Group D] Testing Interrupt and Exception Event Recording...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_d";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "interrupts.sndl";

    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{2, 1024 * 1024, 0, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());

    // Record various interrupt and exception events
    recorder.record_external_interrupt_asserted(1, 32, 0);
    recorder.record_interrupt_routed(32, 0, 1);
    recorder.record_interrupt_accepted(1, 32, 1);
    recorder.record_exception_taken(1, 0x92000000ULL, 1, 0x80004000ULL);
    recorder.record_exception_returned(1, 0, 0x80001004ULL);

    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto val = winios::sndl::SndlV2Validator::validate_file(sndl_path);
    assert(val.is_valid);
    assert(val.total_events == 5);

    std::vector<winios::sndl::SndlV2Record> records;
    assert(winios::sndl::SndlV2Validator::extract_events(sndl_path, records).is_success());
    assert(records.size() == 5);
    assert(records[0].event_type == winios::sndl::SndlV2EventType::ExternalInterruptAsserted);
    assert(records[1].event_type == winios::sndl::SndlV2EventType::InterruptRouted);
    assert(records[2].event_type == winios::sndl::SndlV2EventType::InterruptAccepted);
    assert(records[3].event_type == winios::sndl::SndlV2EventType::ExceptionTaken);
    assert(records[4].event_type == winios::sndl::SndlV2EventType::ExceptionReturned);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group D PASS\n";
}

// =============================================================================
// Group E: Nondeterministic IO and Timer Inputs
// =============================================================================
void test_group_e_nondeterministic_inputs() {
    std::cout << "[Group E] Testing Nondeterministic IO and Architectural Timers...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_e";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "io_timers.sndl";

    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{2, 1024 * 1024, 0, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());

    recorder.record_cntpct_read(0, 1000000ULL, 0x8000ULL);
    recorder.record_cntvct_read(0, 1000050ULL, 0x8008ULL);
    recorder.record_pmccntr_read(1, 5000ULL, 0x9000ULL);
    recorder.record_mmio_read(0, 0x09000000ULL, 0x41, 1, 0x8010ULL);
    recorder.record_entropy_read(1, 0xDEADBEEF01234567ULL, 0x9010ULL);
    recorder.record_host_input(42, 0x1234ULL);

    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto val = winios::sndl::SndlV2Validator::validate_file(sndl_path);
    assert(val.is_valid);
    assert(val.total_events == 6);

    std::vector<winios::sndl::SndlV2Record> records;
    assert(winios::sndl::SndlV2Validator::extract_events(sndl_path, records).is_success());
    assert(records.size() == 6);
    assert(records[0].event_type == winios::sndl::SndlV2EventType::CntpctRead);
    assert(records[1].event_type == winios::sndl::SndlV2EventType::CntvctRead);
    assert(records[2].event_type == winios::sndl::SndlV2EventType::PmccntrRead);
    assert(records[3].event_type == winios::sndl::SndlV2EventType::MmioReadResult);
    assert(records[4].event_type == winios::sndl::SndlV2EventType::EntropyReadResult);
    assert(records[5].event_type == winios::sndl::SndlV2EventType::HostInputInjected);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group E PASS\n";
}

// =============================================================================
// Group F: Shared Memory & Exclusive Monitors
// =============================================================================
void test_group_f_shared_memory_exclusives() {
    std::cout << "[Group F] Testing Shared Memory Barriers and Exclusive Monitors...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_f";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "exclusives.sndl";

    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{4, 1024 * 1024, 0, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());

    recorder.record_memory_barrier(0, 1, 0x1000ULL);
    recorder.record_reservation_set(0, 0x20000ULL, 0x1004ULL);
    recorder.record_store_exclusive_result(0, 0x20000ULL, true, 0x1008ULL);
    recorder.record_reservation_cleared(1, 0x20000ULL, 0x3000ULL);
    recorder.record_store_exclusive_result(1, 0x20000ULL, false, 0x3004ULL);

    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto val = winios::sndl::SndlV2Validator::validate_file(sndl_path);
    assert(val.is_valid);
    assert(val.total_events == 5);

    std::vector<winios::sndl::SndlV2Record> records;
    assert(winios::sndl::SndlV2Validator::extract_events(sndl_path, records).is_success());
    assert(records.size() == 5);
    assert(records[0].event_type == winios::sndl::SndlV2EventType::MemoryBarrierSync);
    assert(records[1].event_type == winios::sndl::SndlV2EventType::ReservationSet);
    assert(records[2].event_type == winios::sndl::SndlV2EventType::StoreExclusiveResult);
    assert(records[2].payload2 == 1); // Success
    assert(records[3].event_type == winios::sndl::SndlV2EventType::ReservationCleared);
    assert(records[4].event_type == winios::sndl::SndlV2EventType::StoreExclusiveResult);
    assert(records[4].payload2 == 0); // Failure

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group F PASS\n";
}

// =============================================================================
// Group G: Baseline Snapshot Association & Validation
// =============================================================================
void test_group_g_snapshot_association() {
    std::cout << "[Group G] Testing Baseline .wsnap v2 Snapshot Association...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_g";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "cluster.wsnap";
    const auto sndl_path = temp_dir / "cluster.sndl";

    // Create a 2-core snapshot
    auto mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched(mem, 2, 100);
    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    const auto snap_res = winios::snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path);
    assert(snap_res.success);

    const std::uint64_t cluster_hash = sched.compute_canonical_multicore_hash();
    winios::snapshot::SmpSnapshotInspection snap_info{};
    assert(winios::snapshot::SmpSnapshotManager::inspect_file(wsnap_path, snap_info).success);

    // Create matching SNDL v2 recording
    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{
        2,
        snap_info.header.memory_capacity,
        cluster_hash,
        std::filesystem::file_size(wsnap_path),
        0,
        32
    });
    assert(recorder.start_recording(sndl_path).is_success());
    recorder.record_quantum_start(0, 100, 0x1000);
    recorder.record_quantum_end(0, 50, 0, 0x10c8);
    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // Validate with snapshot
    const auto val_match = winios::sndl::SndlV2Validator::validate_file(sndl_path, wsnap_path);
    assert(val_match.is_valid);
    assert(val_match.snapshot_checked);
    assert(val_match.snapshot_matched);

    // Test mismatch: 4-core SNDL with 2-core snapshot
    const auto bad_sndl_path = temp_dir / "mismatch.sndl";
    winios::sndl::SndlV2Recorder bad_recorder(winios::sndl::SndlV2RecorderOptions{
        4, // Mismatch core count!
        snap_info.header.memory_capacity,
        cluster_hash,
        std::filesystem::file_size(wsnap_path),
        0,
        32
    });
    assert(bad_recorder.start_recording(bad_sndl_path).is_success());
    bad_recorder.record_quantum_start(0, 100, 0x1000);
    assert(bad_recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto val_mismatch = winios::sndl::SndlV2Validator::validate_file(bad_sndl_path, wsnap_path);
    assert(!val_mismatch.is_valid);
    assert(val_mismatch.code == winios::sndl::SndlV2ErrorCode::SnapshotMismatch);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group G PASS\n";
}

// =============================================================================
// Group H: Integrity, Buffer Overflow & Dropped Events
// =============================================================================
void test_group_h_integrity_and_overflow() {
    std::cout << "[Group H] Testing Integrity Protection and Buffer Overflow Policy...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_h";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "overflow.sndl";

    // Configure writer with small capacity of 10 records
    winios::sndl::SndlV2Writer writer(winios::sndl::SndlV2WriterConfig{4, 10});
    winios::sndl::WSndlV2Header hdr{};
    hdr.core_count = 2;
    assert(writer.open(sndl_path, hdr).is_success());

    for (std::uint64_t i = 1; i <= 25; ++i) {
        winios::sndl::SndlV2Record r{};
        r.global_event_sequence = i;
        r.event_type = winios::sndl::SndlV2EventType::CoreSelected;
        (void)writer.write_record(r);
    }

    assert(writer.has_overflowed());
    assert(writer.dropped_events() == 15);

    assert(writer.finalize().is_success());

    // Validate that overflow is recorded in footer
    winios::sndl::SndlV2InspectionInfo info{};
    assert(winios::sndl::SndlV2Validator::inspect_file(sndl_path, info).is_success());
    assert(info.has_valid_footer);
    assert(info.footer.completion_status == winios::sndl::SndlV2CompletionStatus::Overflow);
    assert(info.footer.dropped_events == 15);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group H PASS\n";
}

// =============================================================================
// Group I: Streaming Validation & Summary Formatting
// =============================================================================
void test_group_i_streaming_validation() {
    std::cout << "[Group I] Testing Multi-Block Streaming Validation and Summary...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_i";
    std::filesystem::create_directories(temp_dir);
    const auto sndl_path = temp_dir / "stream.sndl";

    // Write 500 events in 10 blocks (50 records per block)
    winios::sndl::SndlV2Writer writer(winios::sndl::SndlV2WriterConfig{50, 1000});
    winios::sndl::WSndlV2Header hdr{};
    hdr.core_count = 8;
    hdr.memory_capacity = 32 * 1024 * 1024;
    assert(writer.open(sndl_path, hdr).is_success());

    for (std::uint64_t i = 1; i <= 500; ++i) {
        winios::sndl::SndlV2Record r{};
        r.global_event_sequence = i;
        r.core_id = static_cast<std::uint32_t>((i - 1) % 8);
        r.event_type = winios::sndl::SndlV2EventType::QuantumStart;
        assert(writer.write_record(r).is_success());
    }

    assert(writer.finalize(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    const auto val = winios::sndl::SndlV2Validator::validate_file(sndl_path);
    assert(val.is_valid);
    assert(val.total_blocks == 10);
    assert(val.total_events == 500);

    winios::sndl::SndlV2InspectionInfo info{};
    assert(winios::sndl::SndlV2Validator::inspect_file(sndl_path, info).is_success());
    assert(!info.formatted_summary.empty());
    assert(info.formatted_summary.find("Virtual Core Count:        8") != std::string::npos);
    assert(info.formatted_summary.find("Footer Total Events:       500") != std::string::npos);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group I PASS\n";
}

// =============================================================================
// Group J: Legacy Compatibility & Mandatory SMP Replay Rejection
// =============================================================================
void test_group_j_compatibility_and_safety() {
    std::cout << "[Group J] Testing Legacy Format Preservation and Mandatory Replay Guardrails...\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_test_j";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "smp.wsnap";
    const auto sndl_path = temp_dir / "smp.sndl";

    // 1. Create a 4-core snapshot
    auto mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
    winios::SmpScheduler sched(mem, 4, 100);
    for (std::size_t c = 0; c < 4; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }
    assert(winios::snapshot::SmpSnapshotManager::capture_to_file(sched, wsnap_path).success);

    // 2. Create a 4-core SNDL v2 log
    winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{4, 1024 * 1024, 0, 0, 0, 32});
    assert(recorder.start_recording(sndl_path).is_success());
    recorder.record_quantum_start(0, 100, 0x1000);
    assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

    // 3. MANDATORY SAFETY GUARDRAIL: Deterministic replay session MUST explicitly reject
    // multi-core configurations with ReplayErrorCode::UnsupportedConfiguration!
    winios::replay::ReplaySession session;
    winios::replay::ReplayConfig config{};
    const auto res = session.prepare(wsnap_path, sndl_path, config);

    assert(res.is_failure());
    assert(res.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
    std::cout << "  -> Group J PASS\n";
}

} // namespace

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
    std::cout << "  Phase 52, Stage 24: Multi-Core SNDL v2 Verification Suite     \n";
    std::cout << "===============================================================\n";

    test_group_a_binary_format();
    test_group_b_global_ordering();
    test_group_c_scheduler_recording();
    test_group_d_interrupt_recording();
    test_group_e_nondeterministic_inputs();
    test_group_f_shared_memory_exclusives();
    test_group_g_snapshot_association();
    test_group_h_integrity_and_overflow();
    test_group_i_streaming_validation();
    test_group_j_compatibility_and_safety();

    std::cout << "\n===============================================================\n";
    std::cout << "  ALL 10 TEST GROUPS PASSED (24/24 assertion suites valid)     \n";
    std::cout << "===============================================================\n";
    return 0;
}
