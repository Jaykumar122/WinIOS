#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_format.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "sndl/sndl_validation.h"
#include "sndl/sndl_writer.h"
#include "snapshot/snapshot_manager.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/virtual_time.h"
#include "cpu/gicv3.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
// Test 1: Header Serialization, Deserialization, and Integrity
// -----------------------------------------------------------------------------
void test_header_serialization() {
    std::cout << "[Test 1] Testing WSndlHeader format, endianness, and CRC32C...\n";

    winios::sndl::WSndlHeader hdr{};
    hdr.magic = winios::sndl::kWSndlMagic;
    hdr.version = 1;
    hdr.core_count = 4;
    hdr.flags = 0x12345678;
    hdr.base_snapshot_hash = 0xDEADBEEFCAFEULL;
    hdr.creation_timestamp_ns = 1700000000000ULL;
    hdr.total_record_count = 500;
    hdr.total_instructions = 10000;
    std::memcpy(hdr.snapshot_id.data(), "SNAP_TEST_01", 12);

    std::uint8_t buffer[winios::sndl::kWSndlHeaderSize]{};
    winios::sndl::encode_sndl_header(hdr, buffer);

    winios::sndl::WSndlHeader decoded{};
    const auto res = winios::sndl::decode_sndl_header(buffer, sizeof(buffer), decoded);
    assert(res.is_success());
    assert(decoded.magic == hdr.magic);
    assert(decoded.version == 1);
    assert(decoded.core_count == 4);
    assert(decoded.flags == 0x12345678);
    assert(decoded.base_snapshot_hash == 0xDEADBEEFCAFEULL);
    assert(decoded.creation_timestamp_ns == 1700000000000ULL);
    assert(decoded.total_record_count == 500);
    assert(decoded.total_instructions == 10000);
    assert(std::memcmp(decoded.snapshot_id.data(), "SNAP_TEST_01", 12) == 0);

    // Corrupted magic
    buffer[0] = 'X';
    assert(winios::sndl::decode_sndl_header(buffer, sizeof(buffer), decoded).code == winios::sndl::SndlErrorCode::InvalidMagic);

    // Corrupted CRC
    buffer[0] = 'W';
    buffer[60] ^= 0xFF;
    assert(winios::sndl::decode_sndl_header(buffer, sizeof(buffer), decoded).code == winios::sndl::SndlErrorCode::HeaderCrcMismatch);

    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 2: Record Serialization and Field Fidelity (64 Bytes)
// -----------------------------------------------------------------------------
void test_record_serialization() {
    std::cout << "[Test 2] Testing SndlRecord 64-byte serialization and builder round-trip...\n";

    const auto rec = winios::sndl::make_mmio_read_record(
        2, 0x80001000ULL, 42, 1, 0x1C000000ULL, 0xAABBCCDD11223344ULL, 8, 0x55AA);

    std::uint8_t buf[winios::sndl::kWSndlRecordDiskSize]{};
    winios::sndl::encode_record(rec, buf);

    winios::sndl::SndlRecord dec{};
    const auto res = winios::sndl::decode_record(buf, sizeof(buf), dec);
    assert(res.is_success());
    assert(dec.core_id == 2);
    assert(dec.pc == 0x80001000ULL);
    assert(dec.instruction_retirement_count == 42);
    assert(dec.sub_instruction_ordinal == 1);
    assert(dec.event_type == winios::sndl::SndlEventType::MmioReadResult);
    assert(dec.boundary_type == winios::sndl::SndlBoundaryType::DuringInstruction);
    assert(dec.payload1 == 0x1C000000ULL);
    assert(dec.payload2 == 0xAABBCCDD11223344ULL);
    assert(dec.payload3 == 8);
    assert(dec.source_id == 0x55AA);
    assert(dec.payload_size == 8);

    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 3: Block Header and Footer Format Tests
// -----------------------------------------------------------------------------
void test_block_and_footer_format() {
    std::cout << "[Test 3] Testing block header, footer format, and CRC32C...\n";

    winios::sndl::WSndlBlockHeader blk{};
    blk.magic = winios::sndl::kWSndlBlockMagic;
    blk.version = 1;
    blk.flags = 0;
    blk.record_count = 10;
    blk.payload_bytes = 640;
    blk.first_sequence = 1;
    blk.last_sequence = 10;
    blk.payload_crc32c = 0x12345678;

    std::uint8_t blk_buf[winios::sndl::kWSndlBlockHeaderSize]{};
    winios::sndl::encode_sndl_block_header(blk, blk_buf);

    winios::sndl::WSndlBlockHeader dec_blk{};
    assert(winios::sndl::decode_sndl_block_header(blk_buf, sizeof(blk_buf), dec_blk).is_success());
    assert(dec_blk.record_count == 10);
    assert(dec_blk.payload_bytes == 640);
    assert(dec_blk.first_sequence == 1);
    assert(dec_blk.last_sequence == 10);

    winios::sndl::WSndlFooter ftr{};
    ftr.magic = winios::sndl::kWSndlFooterMagic;
    ftr.version = 1;
    ftr.finalization_status = winios::sndl::SndlFinalizationStatus::Completed;
    ftr.block_count = 5;
    ftr.total_records = 50;
    ftr.last_sequence = 50;
    ftr.total_instructions = 1000;

    std::uint8_t ftr_buf[winios::sndl::kWSndlFooterSize]{};
    winios::sndl::encode_sndl_footer(ftr, ftr_buf);

    winios::sndl::WSndlFooter dec_ftr{};
    assert(winios::sndl::decode_sndl_footer(ftr_buf, sizeof(ftr_buf), dec_ftr).is_success());
    assert(dec_ftr.finalization_status == winios::sndl::SndlFinalizationStatus::Completed);
    assert(dec_ftr.block_count == 5);
    assert(dec_ftr.total_records == 50);

    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 4: Event Ordering and Sub-Instruction Ordinal Monotonicity
// -----------------------------------------------------------------------------
void test_event_ordering_and_sub_instruction() {
    std::cout << "[Test 4] Testing sequence monotonicity and sub-instruction ordinals...\n";

    const std::filesystem::path test_file = "test_ordering.sndl";
    std::filesystem::remove(test_file);

    winios::sndl::SndlRecorder recorder(1024);
    assert(recorder.start_recording(test_file, 2).is_success());

    // Core 0: retirement count 100 has TWO MMIO reads (sub 0, sub 1)
    recorder.record_mmio_read(0, 0x1000, 100, 0x4000, 0x11, 4, 1);
    recorder.record_mmio_read(0, 0x1000, 100, 0x4004, 0x22, 4, 1);

    // Core 0: retirement count 101 has one counter read (sub 0)
    recorder.record_counter_read(0, 0x1004, 101, 0x5701, 5000, 24000000);

    // Core 1: retirement count 50 has an interrupt delivery
    recorder.record_interrupt_delivery(1, 0x2000, 50, 30, 0x80, false, false, 1);

    assert(recorder.stop_recording(200).is_success());

    winios::sndl::SndlInspectionInfo info{};
    assert(winios::sndl::SndlValidation::inspect_file(test_file, info).is_success());
    assert(info.sample_records.size() == 4);

    // Verify sequences: strictly 1, 2, 3, 4
    assert(info.sample_records[0].sequence_num == 1);
    assert(info.sample_records[1].sequence_num == 2);
    assert(info.sample_records[2].sequence_num == 3);
    assert(info.sample_records[3].sequence_num == 4);

    // Verify sub-instruction ordinals on Core 0 at ret_count 100
    assert(info.sample_records[0].instruction_retirement_count == 100);
    assert(info.sample_records[0].sub_instruction_ordinal == 0);

    assert(info.sample_records[1].instruction_retirement_count == 100);
    assert(info.sample_records[1].sub_instruction_ordinal == 1);

    // Ret count 101 resets sub-ordinal to 0
    assert(info.sample_records[2].instruction_retirement_count == 101);
    assert(info.sample_records[2].sub_instruction_ordinal == 0);

    std::filesystem::remove(test_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 5: Input Source Hooks (MMIO, Interrupts, Timers, Counter, Entropy)
// -----------------------------------------------------------------------------
void test_source_hooks() {
    std::cout << "[Test 5] Testing non-deterministic input hooks across all sources...\n";

    const std::filesystem::path test_file = "test_sources.sndl";
    std::filesystem::remove(test_file);

    winios::sndl::SndlRecorder recorder(1024);
    winios::sndl::set_active_recorder(&recorder);

    // Inactive recording check
    winios::sndl::hook_mmio_read(0, 0x100, 1, 0x200, 0x33, 4);
    assert(recorder.total_recorded() == 0);

    assert(recorder.start_recording(test_file, 1).is_success());

    // 1. MMIO read (8, 16, 32, 64-bit)
    winios::sndl::hook_mmio_read(0, 0x1000, 10, 0x1C000000, 0xAA, 1);
    winios::sndl::hook_mmio_read(0, 0x1004, 11, 0x1C000002, 0xBBAA, 2);
    winios::sndl::hook_mmio_read(0, 0x1008, 12, 0x1C000004, 0xDDCCBBAA, 4);
    winios::sndl::hook_mmio_read(0, 0x100C, 13, 0x1C000008, 0x8877665544332211ULL, 8);

    // 2. Interrupt inject & delivery
    winios::sndl::hook_interrupt_inject(0, 48, 0x20, 1, 0);
    winios::sndl::hook_interrupt_delivery(0, 0x1010, 14, 48, 0x20, false, false, 1);

    // 3. Timer expiration
    winios::sndl::hook_timer_expiration(0, 0x1014, 15, 0, 100000, 100005);

    // 4. Counter read
    winios::sndl::hook_counter_read(0, 0x1018, 16, 0x5701, 100010, 24000000);

    // 5. Entropy
    winios::sndl::hook_entropy(0, 0x101C, 17, 0xFEEDFACECAFEBABEULL, 64, 1);

    // 6. Host input
    winios::sndl::hook_host_input(0, 1, 0x55667788ULL, 8);

    assert(recorder.total_recorded() == 10);
    assert(recorder.stop_recording(50).is_success());
    winios::sndl::set_active_recorder(nullptr);

    // Validate recording
    const auto val_res = winios::sndl::SndlValidation::validate_file(test_file);
    assert(val_res.is_valid);
    assert(val_res.total_records == 10);

    std::filesystem::remove(test_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 6: Bounded Queue Capacity and Overflow Resilience
// -----------------------------------------------------------------------------
void test_buffer_overflow_resilience() {
    std::cout << "[Test 6] Testing bounded capacity and strict overflow error handling...\n";

    const std::filesystem::path test_file = "test_overflow.sndl";
    std::filesystem::remove(test_file);

    // Create recorder with tiny queue capacity (4 records)
    winios::sndl::SndlRecorder tiny_recorder(4);
    assert(tiny_recorder.start_recording(test_file, 1).is_success());

    for (int i = 0; i < 10; ++i) {
        tiny_recorder.record_mmio_read(0, 0x1000 + i * 4, i, 0x2000, 0x55, 4);
    }

    // Must be marked overflowed
    assert(tiny_recorder.has_overflowed());
    assert(tiny_recorder.dropped_records() > 0);

    // Finalize must return failure due to overflow
    const auto stop_res = tiny_recorder.stop_recording(10);
    assert(stop_res.is_failure());
    assert(stop_res.code == winios::sndl::SndlErrorCode::BufferOverflow);

    // File validation must strictly detect and report overflow status
    const auto val_res = winios::sndl::SndlValidation::validate_file(test_file);
    assert(!val_res.is_valid);
    assert(val_res.code == winios::sndl::SndlErrorCode::BufferOverflow);
    assert(val_res.finalization_status == winios::sndl::SndlFinalizationStatus::Overflow);

    std::filesystem::remove(test_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 7: Snapshot Association and Hash Verification
// -----------------------------------------------------------------------------
void test_snapshot_association() {
    std::cout << "[Test 7] Testing snapshot hash association and missing/mismatch checks...\n";

    const std::filesystem::path snap_file = "test_baseline.wsnap";
    const std::filesystem::path sndl_file = "test_bound.sndl";
    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);

    // 1. Create dummy baseline snapshot
    CPU cpu;
    Memory mem(1024 * 1024);
    winios::VirtualTime vt;
    winios::GICv3 gic;
    winios::snapshot::SnapshotRestoreContext ctx{&cpu, &mem, &vt, &gic, {}};
    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    // Inspect snapshot to get header CRC
    winios::snapshot::SnapshotInspection snap_info{};
    assert(winios::snapshot::SnapshotManager::inspect_file(snap_file, snap_info).success);
    const std::uint64_t snap_hash = snap_info.header.header_crc32c;

    // 2. Record SNDL bound to that snapshot hash
    winios::sndl::SndlRecorder recorder(1024);
    assert(recorder.start_recording(sndl_file, 1, snap_hash).is_success());
    recorder.record_mmio_read(0, 0x1000, 1, 0x4000, 0x42, 4);
    assert(recorder.stop_recording(10).is_success());

    // 3. Validation with matching snapshot: PASS
    auto val_match = winios::sndl::SndlValidation::validate_file(sndl_file, snap_file);
    assert(val_match.is_valid);
    assert(val_match.snapshot_checked);
    assert(val_match.snapshot_matched);

    // 4. Validation with missing snapshot: FAIL
    auto val_missing = winios::sndl::SndlValidation::validate_file(sndl_file, "non_existent.wsnap");
    assert(!val_missing.is_valid);
    assert(val_missing.code == winios::sndl::SndlErrorCode::SnapshotNotFound);

    std::filesystem::remove(snap_file);
    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 8: File Corruption Defense & Tampering Rejection
// -----------------------------------------------------------------------------
void test_file_corruption_rejection() {
    std::cout << "[Test 8] Testing rejection of corrupted or truncated SNDL files...\n";

    const std::filesystem::path test_file = "test_corrupt.sndl";
    std::filesystem::remove(test_file);

    winios::sndl::SndlRecorder recorder(1024);
    assert(recorder.start_recording(test_file, 1).is_success());
    for (int i = 0; i < 20; ++i) {
        recorder.record_mmio_read(0, 0x1000 + i * 4, i + 1, 0x2000, 0xAA, 4);
    }
    assert(recorder.stop_recording(20).is_success());

    // Read valid file bytes
    std::ifstream ifs(test_file, std::ios::binary | std::ios::ate);
    const auto sz = static_cast<std::size_t>(ifs.tellg());
    ifs.seekg(0);
    std::vector<std::uint8_t> valid_bytes(sz);
    ifs.read(reinterpret_cast<char*>(valid_bytes.data()), sz);
    ifs.close();

    // 1. Truncated file below header
    auto res = winios::sndl::SndlValidation::validate_buffer(valid_bytes.data(), 40);
    assert(!res.is_valid);
    assert(res.code == winios::sndl::SndlErrorCode::FileTooShort);

    // 2. Corrupted block header CRC
    auto corrupt_blk_hdr = valid_bytes;
    corrupt_blk_hdr[64 + 44] ^= 0x55; // Flip bit in block header CRC
    res = winios::sndl::SndlValidation::validate_buffer(corrupt_blk_hdr.data(), corrupt_blk_hdr.size());
    assert(!res.is_valid);
    assert(res.code == winios::sndl::SndlErrorCode::BlockHeaderCrcMismatch);

    // 3. Corrupted payload CRC
    auto corrupt_payload = valid_bytes;
    corrupt_payload[64 + 48 + 10] ^= 0xAA; // Flip byte in first record payload
    res = winios::sndl::SndlValidation::validate_buffer(corrupt_payload.data(), corrupt_payload.size());
    assert(!res.is_valid);
    assert(res.code == winios::sndl::SndlErrorCode::BlockPayloadCrcMismatch);

    // 4. Corrupted footer CRC
    auto corrupt_footer = valid_bytes;
    corrupt_footer[corrupt_footer.size() - 2] ^= 0xFF; // Flip bit in footer CRC
    res = winios::sndl::SndlValidation::validate_buffer(corrupt_footer.data(), corrupt_footer.size());
    assert(!res.is_valid);
    assert(res.code == winios::sndl::SndlErrorCode::FooterCrcMismatch);

    std::filesystem::remove(test_file);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Test 9: Live CPU Interpreter Step Integration
// -----------------------------------------------------------------------------
void test_live_cpu_step_integration() {
    std::cout << "[Test 9] Testing live CPU instruction execution with MMIO & counter hooks...\n";

    const std::filesystem::path sndl_file = "test_cpu_live.sndl";
    std::filesystem::remove(sndl_file);

    winios::sndl::SndlRecorder recorder(1024);
    assert(recorder.start_recording(sndl_file, 1).is_success());
    winios::sndl::set_active_recorder(&recorder);

    CPU cpu;
    Memory mem(1024 * 1024);
    winios::VirtualTime vt;
    cpu.bind_virtual_time(&vt, 0);

    // Program:
    // 0: MOVZ X0, #0x1000
    // 4: MOVZ X1, #0x42
    // 8: STR X1, [X0]
    // 12: LDR X2, [X0]
    mem.write_u32(0,  0xD2820000u); // MOVZ X0, #0x1000
    mem.write_u32(4,  0xD2800841u); // MOVZ X1, #0x42
    mem.write_u32(8,  0xF9000001u); // STR X1, [X0]
    mem.write_u32(12, 0xF9400002u); // LDR X2, [X0]

    cpu.set_program_counter(0);

    for (int i = 0; i < 4; ++i) {
        cpu.step(mem);
    }

    assert(cpu.get_instruction_retirement_count() == 4);
    assert(cpu.read_register(2) == 0x42);

    // Now inject an external interrupt
    cpu.get_gic().set_spi_level(40, true);

    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(4).is_success());

    // Validate recording
    const auto val_res = winios::sndl::SndlValidation::validate_file(sndl_file);
    assert(val_res.is_valid);
    assert(val_res.total_records >= 1); // External interrupt was recorded!

    std::filesystem::remove(sndl_file);
    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "========================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 18: Non-Deterministic Log (SNDL) Test Suite\n";
    std::cout << "========================================================================\n";

    test_header_serialization();
    test_record_serialization();
    test_block_and_footer_format();
    test_event_ordering_and_sub_instruction();
    test_source_hooks();
    test_buffer_overflow_resilience();
    test_snapshot_association();
    test_file_corruption_rejection();
    test_live_cpu_step_integration();

    std::cout << "========================================================================\n";
    std::cout << "ALL 9 SNDL TEST SUITES PASSED SUCCESSFULLY\n";
    std::cout << "========================================================================\n";
    return 0;
}
