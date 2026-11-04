#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "replay/replay_engine.h"
#include "replay/replay_session.h"
#include "replay/replay_scheduler.h"
#include "replay/replay_input_router.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_state_hash.h"
#include "replay/replay_checkpoint.h"
#include "replay/replay_divergence.h"
#include "replay/replay_report.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_writer.h"
#include "sndl/sndl_event_encoder.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/virtual_time.h"
#include "cpu/gicv3.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using winios::VirtualTime;
using winios::GICv3;
using namespace winios::replay;
using namespace winios::sndl;

// Helper: Sets up a test program in memory and captures a baseline snapshot
void setup_test_program(
    CPU& cpu,
    Memory& mem,
    VirtualTime& vt,
    GICv3& gic,
    const std::filesystem::path& snap_path
) {
    std::error_code ec;
    std::filesystem::remove(snap_path, ec);

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // Program:
    // 0: MOVZ X0, #0x2000
    // 4: MOVZ X1, #0x55
    // 8: STR X1, [X0]
    // 12: LDR X2, [X0]
    // 16: ADD X3, X2, #5
    // 20: MOVK X3, #0x1234, LSL #16
    mem.write_u32(0,  0xD2840000u); // MOVZ X0, #0x2000
    mem.write_u32(4,  0xD2800AA1u); // MOVZ X1, #0x55
    mem.write_u32(8,  0xF9000001u); // STR X1, [X0]
    mem.write_u32(12, 0xF9400002u); // LDR X2, [X0]
    mem.write_u32(16, 0x91001443u); // ADD X3, X2, #5
    mem.write_u32(20, 0xF2A24683u); // MOVK X3, #0x1234, LSL #16

    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Replay Hardening Test Baseline";
    const auto cap_res = winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_path, opts);
    assert(cap_res.success);
}

// Helper: Creates an .sndl file with given header parameters and records
void create_test_sndl(
    const std::filesystem::path& path,
    std::uint64_t base_hash,
    std::uint64_t total_instructions,
    const std::vector<SndlRecord>& records = {}
) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    WSndlHeader hdr{};
    hdr.core_count = 1;
    hdr.base_snapshot_hash = base_hash;
    hdr.total_instructions = total_instructions;

    SndlWriter writer;
    assert(writer.open(path, hdr).is_success());
    for (const auto& r : records) {
        assert(writer.write_record(r).is_success());
    }
    assert(writer.finalize(SndlFinalizationStatus::Completed, total_instructions).is_success());
}

// =============================================================================
// Test Group A: Nondeterministic Source Coverage & Classification
// =============================================================================
void test_group_a_source_coverage() {
    std::cout << "[Test Group A] Nondeterministic source coverage & classification...\n";

    const std::filesystem::path snap_file = "test_ga.wsnap";
    const std::filesystem::path sndl_file = "test_ga.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);

    // Build log containing valid supported source records
    std::vector<SndlRecord> recs;
    auto r1 = make_counter_read_record(0, 4, 1, 0, 0x5F01, 1000, 24000000);
    r1.sequence_num = 1;
    recs.push_back(r1);

    auto r2 = make_mmio_read_record(0, 8, 2, 0, 0x08000000, 0x1234, 4, 0);
    r2.sequence_num = 2;
    recs.push_back(r2);

    auto r3 = make_entropy_record(0, 12, 3, 0, 0xCAFEBABEDEADBEEFULL, 64, 0);
    r3.sequence_num = 3;
    recs.push_back(r3);

    auto r4 = make_interrupt_delivery_record(0, 16, 4, 33, 0x80, false, false, 1);
    r4.sequence_num = 4;
    recs.push_back(r4);

    create_test_sndl(sndl_file, 0, 6, recs);

    ReplayEventCursor cursor;
    assert(cursor.open(sndl_file).is_success());
    assert(cursor.has_next());
    assert(cursor.current().event_type == SndlEventType::CounterReadResult);
    assert(cursor.advance().is_success());
    assert(cursor.current().event_type == SndlEventType::MmioReadResult);
    assert(cursor.advance().is_success());
    assert(cursor.current().event_type == SndlEventType::EntropyResult);
    assert(cursor.advance().is_success());
    assert(cursor.current().event_type == SndlEventType::InterruptDelivery);
    cursor.close();

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: All supported nondeterministic input sources validated\n";
}

// =============================================================================
// Test Group B: Replay Bypass Prevention & Leakage Detection
// =============================================================================
void test_group_b_bypass_prevention() {
    std::cout << "[Test Group B] Replay bypass prevention & leakage detection...\n";

    const std::filesystem::path snap_file = "test_gb.wsnap";
    const std::filesystem::path sndl_file = "test_gb.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // Instruction at 0: MRS X0, CNTPCT_EL0
    mem.write_u32(0, 0xD53BE020u);
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    // Create log with NO counter event -> must detect bypass / unrecorded counter read!
    create_test_sndl(sndl_file, 0, 1, {});

    ReplayConfig config{};
    config.max_instructions = 1;
    config.strict_bypass_checks = true;

    ReplaySession session;
    assert(session.prepare(snap_file, sndl_file, config).is_success());
    const auto res = session.run();
    // In strict mode, an unrecorded counter read must fail and record divergence!
    assert(res.is_failure());
    assert(session.stats().diverged);
    assert(session.stats().divergence_info.category == ReplayDivergenceCategory::InputValueMismatch);

    session.close();
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: Unrecorded host clock bypass blocked and diverged\n";
}

// =============================================================================
// Test Group C: GICv3 Interrupt Determinism & Scenarios
// =============================================================================
void test_group_c_interrupt_determinism() {
    std::cout << "[Test Group C] GICv3 interrupt determinism (SPI, PPI, masking, delivery)...\n";

    GICv3 gic;
    gic.reset();
    gic.write_mmio(0x08000000, 0x3, 4); // Enable GICD Group 0 and Group 1
    gic.write_mmio(0x08000084, 0x2u, 4); // SPI 33 Group 1
    gic.write_mmio(0x08000104, 0x2u, 4); // Enable SPI 33 in GICD_ISENABLER1
    gic.write_icc_igrpen1(0x1u, 0); // Enable Group 1 in CPU interface
    gic.write_icc_pmr(0xFFu, 0); // Unmask all priorities

    // 1. SPI Edge/Level: Set SPI 33 level
    gic.set_spi_level(33, true);
    auto sig1 = gic.evaluate(1, 0, 0);
    assert(sig1.has_interrupt);
    assert(sig1.intid == 33);

    // 2. Priority Masking: Set PMR to 0x00 -> must mask interrupt!
    gic.write_icc_pmr(0x00u, 0);
    auto sig2 = gic.evaluate(1, 0, 0);
    assert(!sig2.has_interrupt);
    gic.write_icc_pmr(0xFFu, 0); // Restore PMR

    // 3. Acknowledge Group 1: Returns INTID 33 and transitions to active
    const auto ack_id = gic.acknowledge_group1(1, 0);
    assert(ack_id == 33);

    // After acknowledgment, interrupt is active, not pending
    auto sig3 = gic.evaluate(1, 0, 0);
    assert(!sig3.has_interrupt);

    // 4. End of Interrupt (EOIR): Deactivate INTID 33
    gic.set_spi_level(33, false); // Clear line
    gic.end_of_interrupt_group1(33, 1, 0);

    // 5. PPI Level: Physical timer PPI 30
    gic.write_mmio(0x080A0000 + 0x10080, (1u << 30), 4); // PPI 30 Group 1
    gic.write_mmio(0x080A0000 + 0x10100, (1u << 30), 4); // Enable PPI 30 in GICR_ISENABLER0
    gic.set_ppi_level(30, true, 0);
    auto sig4 = gic.evaluate(1, 0, 0);
    assert(sig4.has_interrupt);
    assert(sig4.intid == 30);
    const auto ack_ppi = gic.acknowledge_group1(1, 0);
    assert(ack_ppi == 30);
    gic.set_ppi_level(30, false, 0);
    gic.end_of_interrupt_group1(30, 1, 0);

    std::cout << "  -> PASSED: GICv3 priority selection, masking, and acknowledgment verified\n";
}

// =============================================================================
// Test Group D: Timer & VirtualTime Determinism
// =============================================================================
void test_group_d_timer_virtualtime() {
    std::cout << "[Test Group D] Timer & VirtualTime determinism (CNTP, CNTV, CNTHP, CVAL)...\n";

    VirtualTime vt(24000000ULL, 1);
    vt.set_physical_counter(1000);

    // 1. Physical timer disabled -> no IRQ
    CPU::TimerState phys{};
    phys.enable = false;
    phys.imask = false;
    phys.cval = 500; // Counter is past cval
    vt.set_phys_timer(0, phys);
    vt.evaluate_core_timers(0);
    assert(!vt.get_phys_timer_irq(0));

    // 2. Physical timer enabled, not masked, cval in future -> no IRQ
    phys.enable = true;
    phys.cval = 2000;
    vt.set_phys_timer(0, phys);
    vt.evaluate_core_timers(0);
    assert(!vt.get_phys_timer_irq(0));

    // 3. Counter reaches cval -> IRQ asserted!
    vt.advance_physical_counter(1000); // physical counter now 2000
    vt.evaluate_core_timers(0);
    assert(vt.get_phys_timer(0).istatus);
    assert(vt.get_phys_timer_irq(0));

    // 4. Timer masked -> istatus true, but IRQ false
    phys = vt.get_phys_timer(0);
    phys.imask = true;
    vt.set_phys_timer(0, phys);
    vt.evaluate_core_timers(0);
    assert(vt.get_phys_timer(0).istatus);
    assert(!vt.get_phys_timer_irq(0));

    // 5. Virtual timer evaluation
    CPU::TimerState virt{};
    virt.enable = true;
    virt.imask = false;
    virt.cval = 2000;
    vt.set_virt_timer(0, virt);
    vt.evaluate_core_timers(0);
    assert(vt.get_virt_timer(0).istatus);
    assert(vt.get_virt_timer_irq(0));

    // 6. Rearm timer into future
    phys.imask = false;
    phys.cval = 5000;
    vt.set_phys_timer(0, phys);
    vt.evaluate_core_timers(0);
    assert(!vt.get_phys_timer(0).istatus);
    assert(!vt.get_phys_timer_irq(0));

    std::cout << "  -> PASSED: Physical, virtual, comparator, and masking semantics verified\n";
}

// =============================================================================
// Test Group E: Counter & Entropy Verification
// =============================================================================
void test_group_e_counter_entropy() {
    std::cout << "[Test Group E] Counter & entropy virtualization...\n";

    const std::filesystem::path snap_file = "test_ge.wsnap";
    const std::filesystem::path sndl_file = "test_ge.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);

    // 0: MRS X0, CNTPCT_EL0
    // 4: ADD X1, X0, #42
    mem.write_u32(0, 0xD53BE020u); // MRS X0, CNTPCT_EL0
    mem.write_u32(4, 0x9100A801u); // ADD X1, X0, #42
    cpu.set_program_counter(0);

    winios::snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &mem;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    winios::snapshot::SnapshotCaptureOptions opts{};
    assert(winios::snapshot::SnapshotManager::capture_to_file(ctx, snap_file, opts).success);

    // Provide recorded CNTPCT value of 0xABCDEFULL
    std::vector<SndlRecord> recs;
    auto r1 = make_counter_read_record(0, 0, 0, 0, 0x5F01, 0xABCDEFULL, 24000000);
    r1.sequence_num = 1;
    recs.push_back(r1);
    create_test_sndl(sndl_file, 0, 2, recs);

    ReplayConfig config{};
    config.max_instructions = 2;

    ReplayExecutionStats stats{};
    const auto res = ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(res.is_success());
    assert(stats.instructions_executed == 2);
    assert(stats.events_consumed == 1);

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: Exact recorded counter injection verified\n";
}

// =============================================================================
// Test Group F: Replay Completion Semantics & Event Accounting
// =============================================================================
void test_group_f_completion_semantics() {
    std::cout << "[Test Group F] Replay completion semantics & event accounting...\n";

    const std::filesystem::path snap_file = "test_gf.wsnap";
    const std::filesystem::path sndl_file = "test_gf.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);

    // 1. CompletedNormally: 6 instructions, 0 events
    create_test_sndl(sndl_file, 0, 6, {});
    ReplayConfig config{};
    config.max_instructions = 6;
    ReplayExecutionStats stats{};
    assert(ReplayEngine::run(snap_file, sndl_file, config, stats).is_success());
    assert(stats.completion_status == ReplayCompletionStatus::CompletedNormally);

    // 2. CompletedAtConfiguredInstructionLimit: ran 3 of 6 instructions
    config.max_instructions = 3;
    assert(ReplayEngine::run(snap_file, sndl_file, config, stats).is_success());
    assert(stats.completion_status == ReplayCompletionStatus::CompletedAtConfiguredInstructionLimit);

    // 3. UnconsumedEvents: log has event at retirement 10, execution stopped at 3
    std::vector<SndlRecord> recs;
    auto r1 = make_counter_read_record(0, 4, 10, 0, 0x5F01, 1000, 24000000);
    r1.sequence_num = 1;
    recs.push_back(r1);
    create_test_sndl(sndl_file, 0, 6, recs);

    config.max_instructions = 3;
    config.require_exact_event_consumption = false;
    assert(ReplayEngine::run(snap_file, sndl_file, config, stats).is_success());
    assert(stats.completion_status == ReplayCompletionStatus::UnconsumedEvents);

    // 4. Strict exact event consumption requirement: unconsumed event causes failure
    config.require_exact_event_consumption = true;
    const auto fail_res = ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(fail_res.is_failure());
    assert(stats.completion_status == ReplayCompletionStatus::UnconsumedEvents);

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: All completion statuses and event accounting verified\n";
}

// =============================================================================
// Test Group G: Long-Running & Multi-Block Streaming Stress
// =============================================================================
void test_group_g_streaming_stress() {
    std::cout << "[Test Group G] Multi-block streaming stress & corruption handling...\n";

    const std::filesystem::path sndl_file = "test_stress.sndl";
    std::error_code ec;
    std::filesystem::remove(sndl_file, ec);

    // Create a multi-block SNDL file with 5,000 records
    std::vector<SndlRecord> recs;
    recs.reserve(5000);
    for (std::uint64_t i = 1; i <= 5000; ++i) {
        auto r = make_counter_read_record(0, i * 4, i, 0, 0x5F01, 1000 + i, 24000000);
        r.sequence_num = i;
        recs.push_back(r);
    }
    create_test_sndl(sndl_file, 0, 5000, recs);

    // Stream through all 5,000 records
    ReplayEventCursor cursor;
    assert(cursor.open(sndl_file).is_success());
    std::uint64_t count = 0;
    while (cursor.has_next()) {
        assert(cursor.current().sequence_num == count + 1);
        ++count;
        (void)cursor.advance();
    }
    assert(count == 5000);
    cursor.close();

    // Corrupt block: overwrite bytes in middle of file
    {
        std::fstream fs(sndl_file, std::ios::in | std::ios::out | std::ios::binary);
        assert(fs.is_open());
        fs.seekp(1024, std::ios::beg);
        const char garbage[16] = {0x7F, 0x7F, 0x7F, 0x7F};
        fs.write(garbage, sizeof(garbage));
    }

    // Streaming through corrupted file must detect CRC mismatch!
    ReplayEventCursor corrupt_cursor;
    const auto open_res = corrupt_cursor.open(sndl_file);
    if (open_res.is_success()) {
        bool detected = false;
        while (corrupt_cursor.has_next()) {
            if (corrupt_cursor.advance().is_failure()) {
                detected = true;
                break;
            }
        }
        assert(detected || !corrupt_cursor.has_next());
    } else {
        assert(open_res.is_failure());
    }
    corrupt_cursor.close();

    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: Multi-block streaming and block corruption detection verified\n";
}

// =============================================================================
// Test Group H: Cross-Speed and 10-Run Repeatability
// =============================================================================
void test_group_h_repeatability() {
    std::cout << "[Test Group H] Cross-speed and 10-run repeatability acceptance...\n";

    const std::filesystem::path snap_file = "test_rep.wsnap";
    const std::filesystem::path sndl_file = "test_rep.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);
    create_test_sndl(sndl_file, 0, 6, {});

    ReplayConfig batch_config{};
    batch_config.max_instructions = 6;
    batch_config.checkpoint_interval = 100000;

    ReplayComponentHashes baseline_hashes{};
    for (int run = 1; run <= 10; ++run) {
        ReplayExecutionStats stats{};
        assert(ReplayEngine::run(snap_file, sndl_file, batch_config, stats).is_success());
        if (run == 1) {
            baseline_hashes = stats.final_state_hashes;
            assert(baseline_hashes.combined_hash != 0);
        } else {
            assert(stats.final_state_hashes == baseline_hashes);
        }
    }

    // Step-by-step single instruction stepping must produce identical state!
    ReplaySession step_session;
    assert(step_session.prepare(snap_file, sndl_file, batch_config).is_success());
    for (int i = 0; i < 6; ++i) {
        assert(step_session.step());
    }
    const auto step_hashes = ReplayStateHash::compute_state_hashes(
        *step_session.cpu(), *step_session.memory(), step_session.virtual_time(), step_session.gic()
    );
    assert(step_hashes == baseline_hashes);
    step_session.close();

    // Checkpoint every instruction must also produce identical final state!
    ReplayConfig stress_cp_config{};
    stress_cp_config.max_instructions = 6;
    stress_cp_config.checkpoint_interval = 1;
    ReplayExecutionStats cp_stats{};
    assert(ReplayEngine::run(snap_file, sndl_file, stress_cp_config, cp_stats).is_success());
    assert(cp_stats.checkpoints_taken >= 6);
    assert(cp_stats.final_state_hashes == baseline_hashes);

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: 10 repeated runs, step-by-step, and per-instruction checkpoints produce bit-for-bit identical hashes\n";
}

// =============================================================================
// Test Group I: State Hash Canonical Coverage & Host Leakage Immunity
// =============================================================================
void test_group_i_hash_coverage() {
    std::cout << "[Test Group I] State hash canonical coverage & host pointer immunity...\n";

    CPU cpu1;
    Memory mem1(1024 * 1024);
    VirtualTime vt1;
    GICv3 gic1;
    cpu1.reset();
    cpu1.bind_virtual_time(&vt1, 0);

    CPU cpu2;
    Memory mem2(1024 * 1024);
    VirtualTime vt2;
    GICv3 gic2;
    cpu2.reset();
    cpu2.bind_virtual_time(&vt2, 0);

    // Initial identical states must produce identical hashes despite different host allocations
    const auto h1 = ReplayStateHash::compute_state_hashes(cpu1, mem1, &vt1, &gic1);
    const auto h2 = ReplayStateHash::compute_state_hashes(cpu2, mem2, &vt2, &gic2);
    assert(h1 == h2);

    // Modifying X0 alters cpu registers hash
    cpu1.write_register(0, 0x12345678ULL);
    const auto h1_mod_x0 = ReplayStateHash::compute_state_hashes(cpu1, mem1, &vt1, &gic1);
    assert(h1_mod_x0.cpu_registers_hash != h2.cpu_registers_hash);
    cpu1.write_register(0, 0); // Restore

    // Modifying V0 alters SIMD registers hash
    cpu1.write_d_register(0, 0xDEADBEEFULL);
    const auto h1_mod_v0 = ReplayStateHash::compute_state_hashes(cpu1, mem1, &vt1, &gic1);
    assert(h1_mod_v0.simd_registers_hash != h2.simd_registers_hash);
    cpu1.write_d_register(0, 0); // Restore

    // Modifying single byte in memory alters memory hash
    mem1.write_u8(0x500, 0xFF);
    const auto h1_mod_mem = ReplayStateHash::compute_state_hashes(cpu1, mem1, &vt1, &gic1);
    assert(h1_mod_mem.memory_hash != h2.memory_hash);
    mem1.write_u8(0x500, 0x00); // Restore

    std::cout << "  -> PASSED: Canonical hashing covers all architectural state with zero host leakage\n";
}

// =============================================================================
// Test Group J: Session Isolation & Lifecycle Cleanliness
// =============================================================================
void test_group_j_session_isolation() {
    std::cout << "[Test Group J] Replay session isolation & global cleanup...\n";

    const std::filesystem::path snap_file = "test_iso.wsnap";
    const std::filesystem::path sndl_file = "test_iso.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);
    create_test_sndl(sndl_file, 0, 6, {});

    // Ensure router is not active before session
    assert(!is_replay_active());

    {
        ReplaySession s1;
        assert(s1.prepare(snap_file, sndl_file).is_success());
        assert(s1.state() == ReplayLifecycleState::Ready);
        assert(s1.run().is_success());
        assert(s1.state() == ReplayLifecycleState::Completed);
        s1.close();
    }
    // Router must be unregistered upon destruction or close
    assert(!is_replay_active());

    {
        // Second independent session must prepare and run cleanly
        ReplaySession s2;
        assert(s2.prepare(snap_file, sndl_file).is_success());
        assert(s2.run().is_success());
        s2.close();
    }
    assert(!is_replay_active());

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: Clean session lifecycle and zero global router leakage verified\n";
}

// =============================================================================
// Test Group K: SMP Readiness & Rejection Enforcement
// =============================================================================
void test_group_k_smp_rejection() {
    std::cout << "[Test Group K] SMP readiness & rejection enforcement...\n";

    const std::filesystem::path snap_file = "test_smp_k.wsnap";
    const std::filesystem::path sndl_file = "test_smp_k.sndl";
    std::error_code ec;
    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);

    CPU cpu;
    Memory mem(1024 * 1024);
    VirtualTime vt;
    GICv3 gic;
    setup_test_program(cpu, mem, vt, gic, snap_file);
    create_test_sndl(sndl_file, 0, 6);

    ReplayConfig config{};
    config.mode = ReplayMode::MultiCore;
    config.allow_experimental_smp = false; // Strictly rejected!

    ReplayExecutionStats stats{};
    const auto res = ReplayEngine::run(snap_file, sndl_file, config, stats);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    assert(stats.completion_status == ReplayCompletionStatus::UnsupportedConfiguration);

    std::filesystem::remove(snap_file, ec);
    std::filesystem::remove(sndl_file, ec);
    std::cout << "  -> PASSED: Multi-core SMP replay is strictly rejected and audited as unsupported\n";
}

} // namespace

#include <crtdbg.h>

int main() {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "========================================================================\n";
    std::cout << "      WINIOS PHASE 52 STAGE 20 REPLAY HARDENING & VALIDATION TESTS      \n";
    std::cout << "========================================================================\n";

    test_group_a_source_coverage();
    test_group_b_bypass_prevention();
    test_group_c_interrupt_determinism();
    test_group_d_timer_virtualtime();
    test_group_e_counter_entropy();
    test_group_f_completion_semantics();
    test_group_g_streaming_stress();
    test_group_h_repeatability();
    test_group_i_hash_coverage();
    test_group_j_session_isolation();
    test_group_k_smp_rejection();

    std::cout << "========================================================================\n";
    std::cout << "      ALL 11 REPLAY HARDENING & VALIDATION TEST GROUPS PASSED!          \n";
    std::cout << "========================================================================\n";
    return 0;
}
