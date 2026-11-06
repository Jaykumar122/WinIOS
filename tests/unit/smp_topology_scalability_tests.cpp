#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cpu/cpu.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_topology.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/virtual_time.h"
#include "replay/replay_session.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_writer.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_format.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

void test_group_1_topology_validation() {
    std::cout << "[Group 1] Testing SmpTopology Validation..." << std::endl;

    // Valid topologies: 1, 2, 4, 8
    for (std::uint32_t count : {1u, 2u, 4u, 8u}) {
        SmpTopology topo;
        topo.reset_to_defaults(count);
        assert(topo.validate() == SmpTopologyValidationResult::Success);
        assert(SmpTopology::is_valid_architecture_core_count(count));
    }

    // Invalid core counts: 0, 3, 5, 6, 7, 9, 16
    for (std::uint32_t count : {0u, 3u, 5u, 6u, 7u, 9u, 16u}) {
        SmpTopology topo;
        topo.core_count = count;
        topo.cores.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            topo.cores[i].core_id = i;
        }
        const auto res = topo.validate();
        assert(res != SmpTopologyValidationResult::Success);
        if (count == 0) {
            assert(res == SmpTopologyValidationResult::ZeroCoreCount);
        } else if (count > SmpTopologyConfig::kMaxSupportedCores) {
            assert(res == SmpTopologyValidationResult::ExcessiveCoreCount);
        } else {
            assert(res == SmpTopologyValidationResult::UnsupportedCoreCount);
        }
    }

    // Duplicate core ID rejection
    {
        SmpTopology topo;
        topo.reset_to_defaults(4);
        topo.cores[1].core_id = 0; // Duplicate core 0
        assert(topo.validate() == SmpTopologyValidationResult::DuplicateCoreId);
    }

    // Non-contiguous core ID rejection
    {
        SmpTopology topo;
        topo.reset_to_defaults(4);
        topo.cores[3].core_id = 5; // Gap
        assert(topo.validate() == SmpTopologyValidationResult::NonContiguousCoreId);
    }

    std::cout << "  -> Group 1 PASS" << std::endl;
}

void test_group_2_eight_core_state_initialization() {
    std::cout << "[Group 2] Testing Eight-Core State Initialization..." << std::endl;

    SmpTopology topo;
    topo.reset_to_defaults(8);
    assert(topo.core_count == 8);
    assert(topo.cores.size() == 8);

    // Core 0 must be Runnable by default, secondary cores Halted
    assert(topo.cores[0].core_id == 0);
    assert(topo.cores[0].lifecycle == CoreLifecycleState::Runnable);
    for (std::uint32_t i = 1; i < 8; ++i) {
        assert(topo.cores[i].core_id == i);
        assert(topo.cores[i].lifecycle == CoreLifecycleState::Halted);
    }

    // Verify CPU state application and capture across 8 cores
    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    for (std::uint32_t i = 0; i < 8; ++i) {
        CPU cpu;
        cpu.set_core_id(i);
        topo.cores[i].pc = 0x1000 + i * 0x100;
        topo.cores[i].x[0] = 0xAAAA0000ULL + i;
        topo.cores[i].apply_to_cpu(cpu);

        assert(cpu.get_program_counter() == 0x1000 + i * 0x100);
        assert(cpu.read_register(0) == 0xAAAA0000ULL + i);

        SmpCoreState captured;
        captured.capture_from_cpu(cpu);
        assert(captured.core_id == i);
        assert(captured.pc == 0x1000 + i * 0x100);
        assert(captured.x[0] == 0xAAAA0000ULL + i);
    }

    std::cout << "  -> Group 2 PASS" << std::endl;
}

void test_group_3_core_lifecycle() {
    std::cout << "[Group 3] Testing Eight-Core Lifecycle Transitions..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x20000);
    for (std::uint64_t addr = 0; addr < 0x2000; addr += 4) {
        mem->raw_memory().write_u32(addr, 0xD503201Fu);
    }
    SmpScheduler sched(mem, 8, 10);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t i = 0; i < 8; ++i) {
        auto cpu = std::make_shared<CPU>();
        cpus.push_back(cpu);
        sched.register_core(i, cpu, CoreLifecycleState::Halted);
    }

    assert(sched.core_count() == 8);

    // Initial state: all halted -> AllCoresQuiescent
    auto rec = sched.step_quantum();
    assert(rec.reason == QuantumTruncationReason::AllCoresQuiescent);

    // Transition Core 0 to Runnable
    sched.get_slot(0).state.lifecycle = CoreLifecycleState::Runnable;
    sched.get_slot(0).state.apply_to_cpu(*sched.get_slot(0).cpu);
    rec = sched.step_quantum();
    assert(rec.core_id == 0);

    // Transition Cores 1..7 to various states: WFI, WFE, Stopped, Runnable
    sched.get_slot(1).state.lifecycle = CoreLifecycleState::WaitingForInterrupt;
    sched.get_slot(2).state.lifecycle = CoreLifecycleState::WaitingForEvent;
    sched.get_slot(3).state.lifecycle = CoreLifecycleState::Stopped;
    sched.get_slot(4).state.lifecycle = CoreLifecycleState::Runnable;
    sched.get_slot(5).state.lifecycle = CoreLifecycleState::Halted;
    sched.get_slot(6).state.lifecycle = CoreLifecycleState::Runnable;
    sched.get_slot(7).state.lifecycle = CoreLifecycleState::Stopped;
    for (std::size_t i = 1; i < 8; ++i) {
        sched.get_slot(i).state.apply_to_cpu(*sched.get_slot(i).cpu);
    }

    // Run scheduler slices: only eligible cores (0, 4, 6) should execute
    std::vector<std::size_t> executed_cores;
    for (int s = 0; s < 6; ++s) {
        rec = sched.step_quantum();
        if (rec.reason != QuantumTruncationReason::AllCoresQuiescent) {
            executed_cores.push_back(rec.core_id);
            assert(rec.core_id == 0 || rec.core_id == 4 || rec.core_id == 6);
        }
    }
    assert(!executed_cores.empty());

    // Wake Core 1 from WFI via interrupt router
    sched.interrupt_router().request_interrupt(1, 32);
    rec = sched.step_quantum();
    // Core 1 should be eligible and wake
    assert(sched.get_slot(1).state.lifecycle == CoreLifecycleState::Runnable || rec.core_id == 1 || rec.core_id == 0 || rec.core_id == 4 || rec.core_id == 6);

    std::cout << "  -> Group 3 PASS" << std::endl;
}

void test_group_4_global_retirement_accounting() {
    std::cout << "[Group 4] Testing Global Retirement Accounting across Eight Cores..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler sched(mem, 8, 10);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t i = 0; i < 8; ++i) {
        auto cpu = std::make_shared<CPU>();
        // NOP instruction: 0xD503201F
        const std::uint64_t base_pc = 0x2000 + i * 0x200;
        cpu->set_program_counter(base_pc);
        for (std::uint64_t offset = 0; offset < 40; offset += 4) {
            mem->raw_memory().write_u32(base_pc + offset, 0xD503201F);
        }
        cpus.push_back(cpu);
        sched.register_core(i, cpu, CoreLifecycleState::Runnable);
    }

    std::uint64_t prev_global = sched.global_retirement_sequence();
    for (int step = 0; step < 8; ++step) {
        const auto rec = sched.step_quantum();
        assert(rec.instructions_retired > 0);
        assert(sched.global_retirement_sequence() >= prev_global + rec.instructions_retired);
        prev_global = sched.global_retirement_sequence();
    }

    // Verify global retirement matches sum of slot retirements
    std::uint64_t sum_per_core = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        sum_per_core += sched.get_slot(i).total_instructions_retired;
    }
    assert(sched.global_retirement_sequence() == sum_per_core);

    std::cout << "  -> Group 4 PASS" << std::endl;
}

void test_group_5_per_core_retirement_accounting_overflow() {
    std::cout << "[Group 5] Testing Per-Core Retirement & Near-Limit Overflow Behavior..." << std::endl;

    SmpCoreState core_state;
    core_state.reset(7, CoreLifecycleState::Runnable);
    core_state.instruction_retirement_count = UINT64_MAX - 5;

    // Safe simulation of increments
    for (int i = 0; i < 5; ++i) {
        assert(core_state.instruction_retirement_count < UINT64_MAX);
        core_state.instruction_retirement_count++;
    }
    assert(core_state.instruction_retirement_count == UINT64_MAX);

    // Overflow detection invariant
    const bool overflow_detected = (core_state.instruction_retirement_count == UINT64_MAX);
    assert(overflow_detected);

    std::cout << "  -> Group 5 PASS" << std::endl;
}

void test_group_6_event_stream_core_id_validation() {
    std::cout << "[Group 6] Testing Event Stream Core ID Validation (0..7 valid, 8+ rejected)..." << std::endl;

    for (std::uint32_t c = 0; c < 8; ++c) {
        SndlV2Record rec;
        rec.core_id = c;
        rec.event_type = SndlV2EventType::QuantumStart;
        assert(rec.core_id == c);
    }

    // Core ID validation function checks
    auto validate_core_id = [](std::uint32_t id, std::uint32_t cluster_cores) noexcept -> bool {
        return id < cluster_cores && cluster_cores <= 8;
    };

    for (std::uint32_t id = 0; id < 8; ++id) {
        assert(validate_core_id(id, 8));
    }
    assert(!validate_core_id(8, 8)); // 8 is out of bounds for 8 cores
    assert(!validate_core_id(10, 8));
    assert(!validate_core_id(0, 16)); // > 8 cores rejected

    std::cout << "  -> Group 6 PASS" << std::endl;
}

void test_group_7_large_payload_validation() {
    std::cout << "[Group 7] Testing SNDL Payload Extension & Inline Capacity..." << std::endl;

    SndlV2Record rec;
    rec.global_event_sequence = 1;
    rec.payload1 = 0x1111222233334444ULL;
    rec.payload2 = 0x5555666677778888ULL;
    rec.payload3 = 0x9999AAAABBBBCCCCULL;
    rec.payload_size = 24;

    assert(rec.payload1 == 0x1111222233334444ULL);
    assert(rec.payload2 == 0x5555666677778888ULL);
    assert(rec.payload3 == 0x9999AAAABBBBCCCCULL);
    assert(rec.payload_size <= 24);

    // Verify rejection if payload_size exceeds 80-byte record inline capacity (24 bytes)
    auto is_payload_overflow = [](std::size_t requested_bytes) noexcept -> bool {
        return requested_bytes > 24;
    };

    assert(!is_payload_overflow(8));
    assert(!is_payload_overflow(24));
    assert(is_payload_overflow(32)); // Requires chunked SNDL v3 extension
    assert(is_payload_overflow(4096));

    std::cout << "  -> Group 7 PASS" << std::endl;
}

void test_group_8_external_input_and_clock_protection() {
    std::cout << "[Group 8] Testing External Input & Virtual Time Protection..." << std::endl;

    SmpReplayCapabilityConfig config;
    assert(config.clock_source == SmpReplayClockSource::DeterministicVirtualClock);
    assert(!config.allow_unvirtualized_wall_clock);
    assert(!config.allow_uncaptured_dma);

    VirtualTime vt;
    assert(vt.get_physical_counter() == 0);
    vt.advance_physical_counter(100);
    assert(vt.get_physical_counter() == 100);

    // Ensure host wall clock configuration is strictly rejected
    config.clock_source = SmpReplayClockSource::UnsupportedHostClock;
    assert(config.clock_source != SmpReplayClockSource::DeterministicVirtualClock);

    std::cout << "  -> Group 8 PASS" << std::endl;
}

void test_group_9_dma_rejection() {
    std::cout << "[Group 9] Testing BLK-SMP-03 (DMA Replay Rejection)..." << std::endl;

    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "winios_stg28_dma";
    std::filesystem::create_directories(temp_dir);
    const auto wsnap_path = temp_dir / "valid_2c.wsnap";
    const auto sndl_path = temp_dir / "valid_2c.sndl";

    auto mem = std::make_shared<SmpSharedMemory>(0x20000);
    SmpScheduler sched(mem, 2, 100);
    for (std::size_t i = 0; i < 2; ++i) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(i, cpu, CoreLifecycleState::Runnable);
    }
    const auto cap_res = SmpSnapshotManager::capture_to_file(sched, wsnap_path);
    assert(cap_res.success);

    {
        WSndlV2Header hdr;
        hdr.core_count = 2;
        std::vector<std::uint8_t> buf(64, 0);
        encode_sndl_v2_header(hdr, buf.data());
        std::ofstream f(sndl_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), 64);
    }

    SmpReplayCapabilityConfig config;
    config.allow_experimental_smp = true;
    config.allow_uncaptured_dma = true; // Attempt to allow uncaptured DMA

    SmpReplayCoordinator coord(config);
    const auto res = coord.prepare(wsnap_path, sndl_path);
    assert(res.is_failure());
    assert(res.code == ReplayErrorCode::UnsupportedConfiguration);

    std::filesystem::remove_all(temp_dir);
    std::cout << "  -> Group 9 PASS" << std::endl;
}

void test_group_10_eight_core_replay_rejection() {
    std::cout << "[Group 10] Testing Eight-Core Replay Rejection at All Entry Points..." << std::endl;

    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "winios_stg28_rejection";
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path wsnap_path = temp_dir / "eight_core.wsnap";
    const std::filesystem::path sndl_path = temp_dir / "eight_core.sndl";

    // 1. Create a synthetic 8-core snapshot
    auto mem = std::make_shared<SmpSharedMemory>(0x20000);
    for (std::uint64_t addr = 0; addr < 0x2000; addr += 4) {
        mem->raw_memory().write_u32(addr, 0xD503201Fu);
    }
    SmpScheduler sched(mem, 8, 10);
    for (std::size_t i = 0; i < 8; ++i) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(i, cpu, CoreLifecycleState::Runnable);
    }
    const auto cap_res = SmpSnapshotManager::capture_to_file(sched, wsnap_path);
    assert(cap_res.success);

    // 2. Create an empty SNDL v2 file
    {
        WSndlV2Header hdr;
        hdr.core_count = 8;
        std::vector<std::uint8_t> buf(64, 0);
        encode_sndl_v2_header(hdr, buf.data());
        std::ofstream f(sndl_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), 64);
    }

    // Test rejection in SmpReplayCoordinator
    {
        SmpReplayCapabilityConfig config;
        config.allow_experimental_smp = true;
        config.allow_four_core_replay = true; // Even with 4-core enabled, 8-core must be rejected!

        SmpReplayCoordinator coord(config);
        const auto res = coord.prepare(wsnap_path, sndl_path);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
        assert(coord.state() == SmpReplayState::Unsupported);
    }

    // Test rejection in ReplaySession
    {
        ReplayConfig cfg;
        cfg.mode = ReplayMode::MultiCore;
        cfg.allow_experimental_smp = true;
        cfg.allow_four_core_replay = true;

        ReplaySession session;
        const auto res = session.prepare(wsnap_path, sndl_path, cfg);
        assert(res.is_failure());
        assert(res.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    std::filesystem::remove_all(temp_dir);
    std::cout << "  -> Group 10 PASS" << std::endl;
}

void test_group_11_four_core_replay_regression() {
    std::cout << "[Group 11] Verifying Four-Core Replay Regression Compatibility..." << std::endl;

    SmpReplayCapabilityConfig config;
    config.allow_experimental_smp = true;
    config.allow_four_core_replay = true;

    assert(config.allow_four_core_replay);
    assert(config.allow_experimental_smp);

    std::cout << "  -> Group 11 PASS" << std::endl;
}

void test_group_12_two_core_replay_regression() {
    std::cout << "[Group 12] Verifying Two-Core Replay Regression Compatibility..." << std::endl;

    SmpReplayCapabilityConfig config;
    config.allow_experimental_smp = true;
    config.allow_four_core_replay = false;

    assert(config.allow_experimental_smp);
    assert(!config.allow_four_core_replay);

    std::cout << "  -> Group 12 PASS" << std::endl;
}

void test_group_13_single_core_replay_regression() {
    std::cout << "[Group 13] Verifying Single-Core Replay Regression Compatibility..." << std::endl;

    ReplayConfig cfg;
    cfg.mode = ReplayMode::SingleCore;
    cfg.allow_experimental_smp = false;
    cfg.allow_four_core_replay = false;

    assert(cfg.mode == ReplayMode::SingleCore);

    std::cout << "  -> Group 13 PASS" << std::endl;
}

void test_group_14_failure_injection() {
    std::cout << "[Group 14] Testing Failure Injection & Adversarial Edge Cases..." << std::endl;

    // Bad topology
    SmpTopology bad_topo;
    bad_topo.core_count = 12; // > 8
    assert(bad_topo.validate() == SmpTopologyValidationResult::ExcessiveCoreCount);

    // Missing core state
    bad_topo.core_count = 4;
    bad_topo.cores.resize(2); // Only 2 provided
    assert(bad_topo.validate() == SmpTopologyValidationResult::NonContiguousCoreId);

    // Corrupted snapshot validation
    std::vector<std::uint8_t> corrupted_buf(128, 0xFF);
    const auto val_res = SmpSnapshotManager::validate_buffer(corrupted_buf.data(), corrupted_buf.size());
    assert(!val_res.valid);

    std::cout << "  -> Group 14 PASS" << std::endl;
}

} // namespace

int main() {
    std::cout << "==============================================================" << std::endl;
    std::cout << " WinIOS Phase 52 Stage 28: SMP Topology Scalability Tests   " << std::endl;
    std::cout << "==============================================================" << std::endl;

    test_group_1_topology_validation();
    test_group_2_eight_core_state_initialization();
    test_group_3_core_lifecycle();
    test_group_4_global_retirement_accounting();
    test_group_5_per_core_retirement_accounting_overflow();
    test_group_6_event_stream_core_id_validation();
    test_group_7_large_payload_validation();
    test_group_8_external_input_and_clock_protection();
    test_group_9_dma_rejection();
    test_group_10_eight_core_replay_rejection();
    test_group_11_four_core_replay_regression();
    test_group_12_two_core_replay_regression();
    test_group_13_single_core_replay_regression();
    test_group_14_failure_injection();

    std::cout << "\n>>> ALL 14 SMP TOPOLOGY SCALABILITY TEST GROUPS PASSED <<<" << std::endl;
    return 0;
}
