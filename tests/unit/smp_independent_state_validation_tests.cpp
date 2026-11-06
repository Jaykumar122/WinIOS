// =============================================================================
// WinIOS Phase 52 Stage 30: Independent Expected-State Validation Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_topology.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::replay;

namespace {

struct Fixture {
    std::filesystem::path dir;
    std::filesystem::path wsnap_path;
    std::filesystem::path sndl_path;
    std::uint64_t initial_cluster_hash{0};
    std::uint64_t final_recorded_cluster_hash{0};
    std::size_t cores{8};
};

Fixture setup_analytical_fixture(const std::string& name, std::size_t cores = 8, std::uint64_t quantum = 25) {
    Fixture fix;
    fix.cores = cores;
    fix.dir = std::filesystem::temp_directory_path() / ("winios_indep_" + name);
    std::filesystem::create_directories(fix.dir);
    fix.wsnap_path = fix.dir / "snap.wsnap";
    fix.sndl_path = fix.dir / "rec.sndl";

    const std::size_t ram = 2 * 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram, cores);
    SmpScheduler sched(mem, cores, quantum);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);

        // Core c initial register: Xc = 100 * (c + 1)
        cpu->write_register(c, 100 * (c + 1));

        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        // ADD Xc, Xc, #5
        // STR Xc, [data_addr]
        // B .-8
        const std::uint32_t data_offset = static_cast<std::uint32_t>(0x4000 + c * 64);
        // ADD Xc, Xc, #(c + 1)
        mem->raw_memory().write_u32(code_addr + 0, 0x91000000u | (static_cast<std::uint32_t>(c + 1) << 10) | (static_cast<std::uint32_t>(c) << 5) | static_cast<std::uint32_t>(c));
        mem->raw_memory().write_u32(code_addr + 4, 0xD280000Au | (data_offset << 5)); // MOVZ X10, #data_offset
        mem->raw_memory().write_u32(code_addr + 8, 0xF9000140u | static_cast<std::uint32_t>(c)); // STR Xc, [X10]
        mem->raw_memory().write_u32(code_addr + 12, 0x17FFFFFDu); // B .-12 (jumps to +0)

        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(snapshot::SmpSnapshotManager::capture_to_file(sched, fix.wsnap_path).success);
    fix.initial_cluster_hash = sched.compute_canonical_multicore_hash();

    sndl::SndlV2RecorderOptions opts{
        static_cast<std::uint16_t>(cores),
        ram,
        fix.initial_cluster_hash,
        0, 0, 64
    };
    sndl::SndlV2Recorder recorder(opts);
    assert(recorder.start_recording(fix.sndl_path).is_success());
    sched.attach_sndl_recorder(&recorder);

    // Run exact 800 total instructions (100 instructions per core across 8 cores)
    sched.run_deterministic(cores * 100);
    fix.final_recorded_cluster_hash = sched.compute_canonical_multicore_hash();
    assert(recorder.stop_recording(sndl::SndlV2CompletionStatus::Completed).is_success());

    return fix;
}

void cleanup(const Fixture& f) {
    std::error_code ec;
    std::filesystem::remove_all(f.dir, ec);
}

SmpReplayCapabilityConfig make_cfg(std::size_t cores = 8) {
    SmpReplayCapabilityConfig cfg;
    cfg.max_supported_cores = cores;
    cfg.allow_experimental_smp = true;
    cfg.allow_four_core_replay = true;
    cfg.allow_eight_core_replay = true;
    return cfg;
}

} // namespace

// -----------------------------------------------------------------------------
// Group 1: General-Purpose Registers Independent Verification
// -----------------------------------------------------------------------------
void test_group_1_independent_gprs() {
    std::cout << "[Group 1] Testing Independent GPR Analytical Ground Truth...\n";
    auto fix = setup_analytical_fixture("gpr", 8, 20);

    SmpReplayCoordinator coord(make_cfg(8));
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    // Verify each core's register against independent analytical formula:
    // In our loop of 4 instructions (ADD, MOV, STR, B):
    // 100 instructions per core = 25 loop iterations.
    // Each iteration added 5 to Xc.
    // Analytical expected: 25 * (c + 1)
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = coord.scheduler()->get_slot(c).cpu;
        assert(cpu != nullptr);
        const std::uint64_t actual_val = cpu->read_register(c);
        const std::uint64_t expected_val = 25 * (c + 1);
        assert(actual_val == expected_val);
    }

    cleanup(fix);
    std::cout << "  -> Group 1 PASS (All 8 Core GPRs Match Analytical Formula)\n";
}

// -----------------------------------------------------------------------------
// Group 2: Shared Memory Independent Ground Truth Check
// -----------------------------------------------------------------------------
void test_group_2_independent_shared_memory() {
    std::cout << "[Group 2] Testing Independent Shared Memory Ground Truth...\n";
    auto fix = setup_analytical_fixture("mem", 8, 20);

    SmpReplayCoordinator coord(make_cfg(8));
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    // Verify shared memory data addresses independently
    auto& ram = coord.scheduler()->shared_memory().raw_memory();
    for (std::size_t c = 0; c < 8; ++c) {
        const std::uint64_t data_addr = 0x4000 + c * 64;
        const std::uint64_t stored_val = ram.read_u64(data_addr);
        const std::uint64_t expected_val = 25 * (c + 1);
        assert(stored_val == expected_val);
    }

    cleanup(fix);
    std::cout << "  -> Group 2 PASS (Shared Memory Bytes Match Ground Truth)\n";
}

// -----------------------------------------------------------------------------
// Group 3: Per-Core Retirement and Global Retirement Synchronization
// -----------------------------------------------------------------------------
void test_group_3_independent_retirement_totals() {
    std::cout << "[Group 3] Testing Per-Core Retirement & Global Totals...\n";
    auto fix = setup_analytical_fixture("ret", 8, 25);

    SmpReplayCoordinator coord(make_cfg(8));
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    assert(coord.stats().total_instructions_retired == 800);
    assert(coord.stats().per_core_retired.size() == 8);
    for (std::size_t c = 0; c < 8; ++c) {
        assert(coord.stats().per_core_retired[c] == 100);
    }

    cleanup(fix);
    std::cout << "  -> Group 3 PASS (8x100 = 800 Instructions Verified)\n";
}

// -----------------------------------------------------------------------------
// Group 4: Program Counters and Lifecycle States
// -----------------------------------------------------------------------------
void test_group_4_pcs_and_lifecycle() {
    std::cout << "[Group 4] Testing Core PCs and Lifecycle States...\n";
    auto fix = setup_analytical_fixture("pc", 8, 20);

    SmpReplayCoordinator coord(make_cfg(8));
    assert(coord.prepare(fix.wsnap_path, fix.sndl_path).is_success());
    assert(coord.run().is_success());

    for (std::size_t c = 0; c < 8; ++c) {
        auto& slot = coord.scheduler()->get_slot(c);
        assert(slot.state.lifecycle == CoreLifecycleState::Runnable);
        // Completed 25 iterations of 4 instructions -> PC wraps back to 0x2000 + c * 0x200
        const std::uint64_t expected_pc = 0x2000 + c * 0x200;
        assert(slot.cpu->get_program_counter() == expected_pc);
    }

    cleanup(fix);
    std::cout << "  -> Group 4 PASS (PCs and Lifecycle Validated)\n";
}

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 30: Independent Expected-State Tests        \n";
    std::cout << "===================================================================\n\n";

    test_group_1_independent_gprs();
    test_group_2_independent_shared_memory();
    test_group_3_independent_retirement_totals();
    test_group_4_pcs_and_lifecycle();

    std::cout << "\n>>> ALL INDEPENDENT STATE VALIDATION TEST GROUPS PASSED <<<\n";
    return 0;
}
