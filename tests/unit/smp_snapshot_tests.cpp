#include "snapshot/smp_snapshot_format.h"
#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/smp_snapshot_validation.h"
#include "snapshot/snapshot_manager.h"
#include "replay/replay_engine.h"
#include "replay/replay_session.h"
#include "trace/trace_serializer.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ::Memory;

namespace {

int g_pass_count = 0;
int g_fail_count = 0;

void record_test(const std::string& test_name, bool passed, const std::string& msg = "") {
    if (passed) {
        std::cout << "  [PASS] " << test_name << "\n";
        g_pass_count++;
    } else {
        std::cerr << "  [FAIL] " << test_name << " - " << msg << "\n";
        g_fail_count++;
    }
}

// Helper to create a test instruction sequence:
// Core repeatedly executes ADD X0, X0, #1 (0x91000400)
void write_add_loop(::Memory& mem, std::uint64_t addr, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        // ADD X0, X0, #1 : 0x91000400
        mem.write_u32(addr + i * 4, 0x91000400u);
    }
}

} // namespace

// =============================================================================
// Group A: Cluster Snapshot Metadata
// =============================================================================
void test_group_a_metadata() {
    std::cout << "[Group A: Cluster Snapshot Metadata]\n";

    // A1: Metadata round-trip
    {
        const std::string desc = "Test Cluster 4-Core Snapshot";
        const std::string creator = "Antigravity Test Harness";
        const std::string topo = "4-Core Shared L2 Cluster Topology";

        const auto buf = winios::snapshot::serialize_cluster_metadata(desc, creator, topo);
        std::string d, c, t;
        const auto res = winios::snapshot::deserialize_cluster_metadata(buf.data(), buf.size(), d, c, t);

        record_test("A1_MetadataRoundTrip", res.success && d == desc && c == creator && t == topo,
                    res.error_message);
    }

    // A2: CoreDirectory round-trip
    {
        std::vector<winios::snapshot::CoreDirectoryEntry> entries = {
            {0, winios::CoreLifecycleState::Runnable, true},
            {1, winios::CoreLifecycleState::WaitingForInterrupt, true},
            {2, winios::CoreLifecycleState::Halted, false},
            {3, winios::CoreLifecycleState::Runnable, true}
        };

        const auto buf = winios::snapshot::serialize_core_directory(entries);
        std::vector<winios::snapshot::CoreDirectoryEntry> restored;
        const auto res = winios::snapshot::deserialize_core_directory(buf.data(), buf.size(), restored);

        bool match = (res.success && restored.size() == entries.size());
        if (match) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (restored[i].core_id != entries[i].core_id ||
                    restored[i].lifecycle != entries[i].lifecycle ||
                    restored[i].active != entries[i].active) {
                    match = false;
                    break;
                }
            }
        }
        record_test("A2_CoreDirectoryRoundTrip", match, res.error_message);
    }
}

// =============================================================================
// Group B: Per-Core State Round-Trip
// =============================================================================
void test_group_b_core_state() {
    std::cout << "[Group B: Per-Core State Round-Trip]\n";

    winios::SmpCoreState orig;
    orig.core_id = 3;
    orig.lifecycle = winios::CoreLifecycleState::Runnable;
    orig.instruction_retirement_count = 12345678ULL;
    orig.current_budget = 5000;
    orig.executed_in_quantum = 1200;
    orig.pending_interrupt = true;

    for (std::size_t i = 0; i < 31; ++i) {
        orig.x[i] = 0x1122334400000000ULL | (i * 0x101ULL);
    }
    orig.pc = 0x80001000ULL;
    orig.sp_el0 = 0x7FFF0000ULL;
    orig.sp_el1 = 0xFFFF0000ULL;
    orig.sp_el2 = 0xEEEE0000ULL;
    orig.sp_el3 = 0xDDDD0000ULL;
    orig.nzcv = 0x8u; // Negative flag set
    orig.daif = 0x2u; // IRQ masked
    orig.sp_sel = true;
    orig.il = false;
    orig.pstate_ss = true;
    orig.current_el = 1;

    for (std::size_t i = 0; i < 32; ++i) {
        orig.v[i].low = 0x0123456789ABCDEFULL ^ (i * 0x1111ULL);
        orig.v[i].high = 0xFEDCBA9876543210ULL ^ (i * 0x2222ULL);
    }
    orig.fpcr = 0x01000000u;
    orig.fpsr = 0x00000008u;

    orig.elr_el1 = 0x80004000ULL;
    orig.spsr_el1 = 0x3C5u;
    orig.esr_el1 = 0x56000000u;
    orig.far_el1 = 0x7FFF1234ULL;
    orig.vbar_el1 = 0x80000000ULL;

    orig.sctlr_el1 = 0x30D00800ULL;
    orig.tcr_el1 = 0x10000000ULL;
    orig.ttbr0_el1 = 0x40000000ULL;
    orig.ttbr1_el1 = 0x50000000ULL;
    orig.mair_el1 = 0xFF440400ULL;
    orig.cpacr_el1 = 0x00300000ULL;
    orig.cntvoff_el2 = 0x10000ULL;

    orig.cntp_cval_el0 = 9999999ULL;
    orig.cntp_ctl_el0 = 1; // Enabled
    orig.cntv_cval_el0 = 8888888ULL;
    orig.cntv_ctl_el0 = 0;

    const auto buf = winios::snapshot::serialize_core_architectural_state(orig);
    winios::SmpCoreState restored;
    const auto res = winios::snapshot::deserialize_core_architectural_state(buf.data(), buf.size(), restored);

    const bool eq = (orig == restored);
    const bool hash_eq = (orig.compute_canonical_hash() == restored.compute_canonical_hash());

    record_test("B1_SmpCoreStateEquality", res.success && eq, res.error_message);
    record_test("B2_SmpCoreStateCanonicalHashExact", res.success && hash_eq, "Hash mismatch");
}

// =============================================================================
// Group C: Shared Memory State (Single Sparse Instance)
// =============================================================================
void test_group_c_shared_memory() {
    std::cout << "[Group C: Shared Memory State (Single Instance & Sparse Chunks)]\n";

    constexpr std::size_t kMemSize = 4 * 1024 * 1024; // 4 MB
    ::Memory orig_mem(kMemSize);

    // Write sparse non-zero data at distinct chunk addresses
    orig_mem.write_u64(0x0, 0x123456789ABCDEF0ULL);
    orig_mem.write_u64(4096, 0xDEADBEEFCAFEBABEULL);
    orig_mem.write_u64(1024 * 1024, 0xAA55AA5511223344ULL);
    orig_mem.write_u64(kMemSize - 64, 0x8877665544332211ULL);

    const auto buf = winios::snapshot::serialize_shared_memory_state(orig_mem, 4096, true);

    // Verify buffer is substantially smaller than dense 4MB because zero chunks were omitted
    const bool sparse_compressed = (buf.size() < 128 * 1024);
    record_test("C1_SparseZeroChunkOmission", sparse_compressed,
                "Serialized size (" + std::to_string(buf.size()) + ") not sparse");

    // Restore into clean memory
    ::Memory restored_mem(kMemSize);
    const auto res = winios::snapshot::deserialize_shared_memory_state(buf.data(), buf.size(), restored_mem);

    bool bytes_match = (res.success && std::memcmp(orig_mem.raw_data(), restored_mem.raw_data(), kMemSize) == 0);
    record_test("C2_SharedMemoryBitExactRestore", bytes_match, res.error_message);
}

// =============================================================================
// Group D: Scheduler State Round-Trip
// =============================================================================
void test_group_d_scheduler_state() {
    std::cout << "[Group D: Scheduler State Round-Trip]\n";

    winios::snapshot::SchedulerSnapshotData data;
    data.quantum_instructions = 5000;
    data.global_retirement_sequence = 99887766ULL;
    data.registered_core_count = 4;
    data.current_core_index = 2;
    data.slot_metrics = {
        {0, true, 25000ULL, 5, 1},
        {1, true, 24800ULL, 5, 2},
        {2, true, 25000ULL, 5, 0},
        {3, true, 25076ULL, 6, 0}
    };

    const auto buf = winios::snapshot::serialize_scheduler_state(data);
    winios::snapshot::SchedulerSnapshotData restored;
    const auto res = winios::snapshot::deserialize_scheduler_state(buf.data(), buf.size(), restored);

    bool match = (res.success &&
                  restored.quantum_instructions == data.quantum_instructions &&
                  restored.global_retirement_sequence == data.global_retirement_sequence &&
                  restored.registered_core_count == data.registered_core_count &&
                  restored.current_core_index == data.current_core_index &&
                  restored.slot_metrics.size() == data.slot_metrics.size());

    record_test("D1_SchedulerStateRoundTrip", match, res.error_message);
}

// =============================================================================
// Group E: Exclusive Monitor State Round-Trip
// =============================================================================
void test_group_e_exclusive_monitor() {
    std::cout << "[Group E: Exclusive Monitor State Round-Trip]\n";

    winios::snapshot::ExclusiveMonitorSnapshotData data;
    data.max_cores = 4;
    data.reservations = {
        {0, 0x1000, 0x1008, 8, true},
        {1, 0x2000, 0x2004, 4, true},
        {2, 0, 0, 0, false},
        {3, 0x4000, 0x4010, 16, true}
    };

    const auto buf = winios::snapshot::serialize_exclusive_monitor_state(data);
    winios::snapshot::ExclusiveMonitorSnapshotData restored;
    const auto res = winios::snapshot::deserialize_exclusive_monitor_state(buf.data(), buf.size(), restored);

    bool match = (res.success && restored.max_cores == 4 && restored.reservations.size() == 4);
    if (match) {
        for (std::size_t i = 0; i < 4; ++i) {
            if (restored.reservations[i].core_id != data.reservations[i].core_id ||
                restored.reservations[i].granule_address != data.reservations[i].granule_address ||
                restored.reservations[i].exact_address != data.reservations[i].exact_address ||
                restored.reservations[i].size != data.reservations[i].size ||
                restored.reservations[i].valid != data.reservations[i].valid) {
                match = false;
                break;
            }
        }
    }
    record_test("E1_ExclusiveMonitorRoundTrip", match, res.error_message);
}

// =============================================================================
// Group F: Interrupt Router State Round-Trip
// =============================================================================
void test_group_f_interrupt_router() {
    std::cout << "[Group F: Interrupt Router State Round-Trip]\n";

    winios::snapshot::InterruptRouterSnapshotData data;
    data.max_cores = 2;
    data.next_sequence = 42;
    data.core_queues.resize(2);

    winios::InterruptRequest r1;
    r1.target_core = 0;
    r1.source_core = 1;
    r1.intid = 5; // SGI
    r1.priority = 0x20;
    r1.type = winios::InterruptType::SGI;
    r1.is_fiq = false;
    r1.request_sequence = 10;
    data.core_queues[0].push_back(r1);

    winios::InterruptRequest r2;
    r2.target_core = 1;
    r2.source_core = 0;
    r2.intid = 35; // SPI
    r2.priority = 0x80;
    r2.type = winios::InterruptType::SPI;
    r2.is_fiq = true;
    r2.request_sequence = 11;
    data.core_queues[1].push_back(r2);

    const auto buf = winios::snapshot::serialize_interrupt_router_state(data);
    winios::snapshot::InterruptRouterSnapshotData restored;
    const auto res = winios::snapshot::deserialize_interrupt_router_state(buf.data(), buf.size(), restored);

    bool match = (res.success && restored.max_cores == 2 && restored.next_sequence == 42);
    if (match) {
        match = (restored.core_queues[0].size() == 1 && restored.core_queues[1].size() == 1 &&
                 restored.core_queues[0][0].intid == 5 && restored.core_queues[1][0].intid == 35);
    }
    record_test("F1_InterruptRouterRoundTrip", match, res.error_message);
}

// =============================================================================
// Group G: Integrity and Adversarial Validation
// =============================================================================
void test_group_g_adversarial_validation() {
    std::cout << "[Group G: Integrity and Adversarial Validation]\n";

    // Setup valid cluster snapshot buffer
    constexpr std::size_t kCores = 2;
    auto shared_mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024, kCores);
    winios::SmpScheduler sched(shared_mem, kCores, 1000);
    for (std::size_t c = 0; c < kCores; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    std::vector<std::uint8_t> valid_buf;
    const auto cap_res = winios::snapshot::SmpSnapshotManager::capture_to_buffer(sched, valid_buf);
    assert(cap_res.success);

    // G1: Valid buffer validates successfully
    {
        const auto val = winios::snapshot::SmpSnapshotManager::validate_buffer(valid_buf.data(), valid_buf.size());
        record_test("G1_ValidBufferPasses", val.valid, val.error_message);
    }

    // G2: Truncated buffer
    {
        const auto val = winios::snapshot::SmpSnapshotManager::validate_buffer(valid_buf.data(), 40);
        record_test("G2_TruncatedBufferRejected", !val.valid && val.error_code == winios::snapshot::SnapshotErrorCode::TruncatedFile);
    }

    // G3: Header CRC32C corruption
    {
        auto corrupt = valid_buf;
        corrupt[10] ^= 0xFF; // Flip core_count byte without updating CRC
        const auto val = winios::snapshot::SmpSnapshotManager::validate_buffer(corrupt.data(), corrupt.size());
        record_test("G3_HeaderCrcCorruptionRejected", !val.valid && val.error_code == winios::snapshot::SnapshotErrorCode::HeaderCrcMismatch);
    }

    // G4: Section payload CRC32C corruption
    {
        auto corrupt = valid_buf;
        // Corrupt first byte of section payload (offset 64)
        corrupt[64] ^= 0xAA;
        const auto val = winios::snapshot::SmpSnapshotManager::validate_buffer(corrupt.data(), corrupt.size());
        record_test("G4_SectionPayloadCrcCorruptionRejected", !val.valid && val.error_code == winios::snapshot::SnapshotErrorCode::SectionCrcMismatch);
    }

    // G5: Unsupported version (e.g. version 99)
    {
        auto corrupt = valid_buf;
        winios::trace::encode_u16_le(corrupt.data() + 8, 99);
        // Recompute header CRC
        const auto crc = winios::trace::calculate_crc32c(corrupt.data(), 60);
        winios::trace::encode_u32_le(corrupt.data() + 60, crc);
        const auto val = winios::snapshot::SmpSnapshotManager::validate_buffer(corrupt.data(), corrupt.size());
        record_test("G5_UnsupportedVersionRejected", !val.valid && val.error_code == winios::snapshot::SnapshotErrorCode::UnsupportedVersion);
    }
}

// =============================================================================
// Group H: Atomic Capture and Restore Semantics
// =============================================================================
void test_group_h_atomicity() {
    std::cout << "[Group H: Atomic Capture and Restore Semantics]\n";

    constexpr std::size_t kCores = 2;
    auto shared_mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024, kCores);
    winios::SmpScheduler sched(shared_mem, kCores, 1000);
    for (std::size_t c = 0; c < kCores; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
    }

    const fs::path test_snap = "test_atomicity_cluster.wsnap";
    winios::snapshot::SmpSnapshotCaptureOptions opts;
    const auto cap_res = winios::snapshot::SmpSnapshotManager::capture_to_file(sched, test_snap, opts);
    record_test("H1_CaptureToFileSuccess", cap_res.success, cap_res.error_message);

    // H2: Target scheduler core count mismatch fails restore without modifying target
    {
        auto other_mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024, 4);
        winios::SmpScheduler target_sched(other_mem, 4, 1000);
        for (std::size_t c = 0; c < 4; ++c) {
            target_sched.register_core(c, std::make_shared<CPU>());
        }
        const std::uint64_t hash_before = target_sched.compute_canonical_multicore_hash();

        const auto res = winios::snapshot::SmpSnapshotManager::restore_from_file(test_snap, target_sched);
        const std::uint64_t hash_after = target_sched.compute_canonical_multicore_hash();

        record_test("H2_CoreCountMismatchFailsCleanly", !res.success && hash_before == hash_after,
                    "Target modified despite error");
    }

    if (fs::exists(test_snap)) {
        fs::remove(test_snap);
    }
}

// =============================================================================
// Group I: Deterministic Execution Continuation (10 repetitions on 2, 4, 8 cores)
// =============================================================================
void test_group_i_deterministic_continuation() {
    std::cout << "[Group I: Deterministic Execution Continuation (10 Runs on 2, 4, 8 Cores)]\n";

    const std::vector<std::size_t> core_configs = {2, 4, 8};

    for (std::size_t num_cores : core_configs) {
        bool all_runs_matched = true;

        for (int run = 0; run < 10; ++run) {
            constexpr std::size_t kMemBytes = 1024 * 1024;
            constexpr std::uint64_t kQuantum = 100;
            constexpr std::uint64_t kPhase1Instructions = 400;
            constexpr std::uint64_t kPhase2Instructions = 400;

            // Base run: continuous execution of Phase 1 + Phase 2
            auto base_mem = std::make_shared<winios::SmpSharedMemory>(kMemBytes, num_cores);
            winios::SmpScheduler base_sched(base_mem, num_cores, kQuantum);
            for (std::size_t c = 0; c < num_cores; ++c) {
                auto cpu = std::make_shared<CPU>();
                base_sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
                write_add_loop(base_mem->raw_memory(), c * 0x1000, 2000);
                cpu->set_program_counter(c * 0x1000);
                base_sched.get_slot(c).state.pc = c * 0x1000;
            }

            base_sched.run_deterministic(kPhase1Instructions);

            // Snapshot at boundary
            std::vector<std::uint8_t> snap_buffer;
            const auto cap_res = winios::snapshot::SmpSnapshotManager::capture_to_buffer(base_sched, snap_buffer);
            assert(cap_res.success);

            // Continue base run through Phase 2
            base_sched.run_deterministic(kPhase2Instructions);
            const std::uint64_t expected_hash = base_sched.compute_canonical_multicore_hash();
            const std::uint64_t expected_seq = base_sched.global_retirement_sequence();

            // Branch run: restore from snapshot, then run Phase 2
            auto branch_mem = std::make_shared<winios::SmpSharedMemory>(kMemBytes, num_cores);
            winios::SmpScheduler branch_sched(branch_mem, num_cores, kQuantum);
            for (std::size_t c = 0; c < num_cores; ++c) {
                branch_sched.register_core(c, std::make_shared<CPU>());
            }

            const auto res_rest = winios::snapshot::SmpSnapshotManager::restore_from_buffer(
                snap_buffer.data(), snap_buffer.size(), branch_sched
            );
            assert(res_rest.success);

            branch_sched.run_deterministic(kPhase2Instructions);
            const std::uint64_t branch_hash = branch_sched.compute_canonical_multicore_hash();
            const std::uint64_t branch_seq = branch_sched.global_retirement_sequence();

            if (branch_hash != expected_hash || branch_seq != expected_seq) {
                all_runs_matched = false;
                break;
            }
        }

        record_test("I_DeterministicContinuation_" + std::to_string(num_cores) + "Cores_10Runs",
                    all_runs_matched, "Divergence detected in continuation run");
    }
}

// =============================================================================
// Group J: Single-Core Compatibility & SMP Replay Rejection
// =============================================================================
void test_group_j_compatibility_and_rejection() {
    std::cout << "[Group J: Single-Core Compatibility & Mandatory SMP Replay Rejection]\n";

    // J1: Existing single-core snapshot .wsnap v1 creates and restores cleanly
    {
        CPU cpu;
        ::Memory mem(1024 * 1024);
        winios::VirtualTime vt;
        winios::GICv3 gic;
        winios::snapshot::SnapshotRestoreContext ctx{&cpu, &mem, &vt, &gic, {}};

        const fs::path v1_path = "test_v1_baseline.wsnap";
        winios::snapshot::SnapshotCaptureOptions opts;
        opts.description = "V1 Baseline Single-Core";
        const auto cap_res = winios::snapshot::SnapshotManager::capture_to_file(ctx, v1_path, opts);
        record_test("J1_SingleCoreV1Capture", cap_res.success, cap_res.error_message);

        winios::snapshot::SnapshotInspection info;
        const auto ins_res = winios::snapshot::SnapshotManager::inspect_file(v1_path, info);
        record_test("J2_SingleCoreV1InspectionVersion1", ins_res.success && info.header.version == 1,
                    ins_res.error_message);

        if (fs::exists(v1_path)) {
            fs::remove(v1_path);
        }
    }

    // J2: Mandatory Replay Rejection for Multi-Core Snapshots
    {
        constexpr std::size_t kCores = 2;
        auto shared_mem = std::make_shared<winios::SmpSharedMemory>(1024 * 1024, kCores);
        winios::SmpScheduler sched(shared_mem, kCores, 1000);
        for (std::size_t c = 0; c < kCores; ++c) {
            sched.register_core(c, std::make_shared<CPU>());
        }

        const fs::path smp_snap = "test_smp_reject.wsnap";
        (void)winios::snapshot::SmpSnapshotManager::capture_to_file(sched, smp_snap);

        // Dummy SNDL file path
        const fs::path dummy_sndl = "non_existent.sndl";

        winios::replay::ReplayConfig cfg;
        cfg.mode = winios::replay::ReplayMode::SingleCore;
        cfg.allow_experimental_smp = false;

        winios::replay::ReplaySession session;
        const auto res = session.prepare(smp_snap, dummy_sndl, cfg);

        // Replay MUST reject the multi-core snapshot with UnsupportedConfiguration
        const bool rejected = (res.is_failure() &&
                              res.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);

        record_test("J3_MandatorySmpReplayRejection", rejected,
                    "Expected UnsupportedConfiguration, got: " + std::string(winios::replay::replay_error_name(res.code)));

        if (fs::exists(smp_snap)) {
            fs::remove(smp_snap);
        }
    }
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 23: Multi-Core Snapshot Verification Suite\n";
    std::cout << "================================================================================\n";

    test_group_a_metadata();
    test_group_b_core_state();
    test_group_c_shared_memory();
    test_group_d_scheduler_state();
    test_group_e_exclusive_monitor();
    test_group_f_interrupt_router();
    test_group_g_adversarial_validation();
    test_group_h_atomicity();
    test_group_i_deterministic_continuation();
    test_group_j_compatibility_and_rejection();

    std::cout << "================================================================================\n";
    std::cout << "Results: " << g_pass_count << " passed, " << g_fail_count << " failed\n";
    std::cout << "================================================================================\n";

    return (g_fail_count == 0) ? 0 : 1;
}
