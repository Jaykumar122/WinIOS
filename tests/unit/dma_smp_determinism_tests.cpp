// =============================================================================
// WinIOS Phase 52 Stage 32: DMA SMP Determinism Tests (1, 2, 4, 8 Cores)
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "trace/trace_serializer.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

using namespace winios;
using namespace winios::device;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_smp_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// Helper: Setup, record, and verify 10 deterministic replay runs for a given topology
std::uint64_t run_topology_determinism_test(std::size_t cores, const std::string& prefix) {
    const auto snap_path = kTestDir / (prefix + ".wsnap");
    const auto sndl_path = kTestDir / (prefix + ".sndl");

    const std::size_t ram_size = 2 * 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram_size, cores);
    auto sched = std::make_unique<SmpScheduler>(mem, cores, 50);

    for (std::size_t c = 0; c < cores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma_dev->set_shared_memory(mem);
    bus->register_device(dma_dev);
    sched->attach_device_bus(bus);

    // Setup DMA descriptor submitted by core (cores - 1)
    const std::size_t sub_core = cores - 1;
    const std::uint64_t desc_addr = 0x10000 + sub_core * 0x100;
    const std::uint64_t src_addr  = 0x40000 + sub_core * 0x1000;
    const std::uint64_t dst_addr  = 0x60000 + sub_core * 0x1000;
    const std::uint64_t length    = 512;

    std::vector<std::uint8_t> test_bytes(length);
    for (std::size_t i = 0; i < length; ++i) {
        test_bytes[i] = static_cast<std::uint8_t>((i * 13 + cores * 17) & 0xFF);
    }
    mem->write_bytes(sub_core, src_addr, test_bytes.data(), length);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = static_cast<std::uint64_t>(cores * 100);
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = length;
    mem->write_bytes(sub_core, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(cores);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    // Core (cores-1) submits DMA
    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    // Round-robin execution across all cores
    for (std::size_t c = 0; c < cores; ++c) {
        sched->step_quantum();
    }

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();
    sched->attach_sndl_recorder(nullptr);

    const std::uint64_t live_hash = sched->compute_canonical_multicore_hash();

    // 10 Repeated Replay Runs
    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;
    if (cores == 4) {
        config.allow_four_core_replay = true;
    } else if (cores == 8) {
        config.allow_experimental_smp = true;
        config.allow_eight_core_replay = true;
    }

    for (int run = 0; run < 10; ++run) {
        SmpReplayCoordinator coordinator(config);
        auto prep = coordinator.prepare(snap_path, sndl_path);
        TEST_ASSERT(prep.is_success());

        auto res = coordinator.run();
        TEST_ASSERT(res.is_success());
        TEST_ASSERT(coordinator.state() == SmpReplayState::Completed);

        const std::uint64_t rep_hash = coordinator.stats().final_canonical_hash;
        TEST_ASSERT(rep_hash == live_hash);
    }

    return live_hash;
}

// -----------------------------------------------------------------------------
// Group 1-4: SMP Topologies
// -----------------------------------------------------------------------------
void test_group_1_to_4_topologies() {
    setup_test_dir();

    std::cout << "[Group 1] Testing 1-Core SMP Determinism (10 repeated runs)...\n";
    const auto h1 = run_topology_determinism_test(1, "smp_1c");
    std::cout << "  -> 1-Core 10/10 MATCH: 0x" << std::hex << h1 << std::dec << "\n";

    std::cout << "[Group 2] Testing 2-Core SMP Determinism (10 repeated runs)...\n";
    const auto h2 = run_topology_determinism_test(2, "smp_2c");
    std::cout << "  -> 2-Core 10/10 MATCH: 0x" << std::hex << h2 << std::dec << "\n";

    std::cout << "[Group 3] Testing 4-Core SMP Determinism (10 repeated runs)...\n";
    const auto h4 = run_topology_determinism_test(4, "smp_4c");
    std::cout << "  -> 4-Core 10/10 MATCH: 0x" << std::hex << h4 << std::dec << "\n";

    std::cout << "[Group 4] Testing 8-Core SMP Determinism (10 repeated runs)...\n";
    const auto h8 = run_topology_determinism_test(8, "smp_8c");
    std::cout << "  -> 8-Core 10/10 MATCH: 0x" << std::hex << h8 << std::dec << "\n";
}

// -----------------------------------------------------------------------------
// Group 5: Exclusive Monitor Invalidation Determinism
// -----------------------------------------------------------------------------
void test_group_5_exclusive_monitor_invalidation() {
    std::cout << "[Group 5] Testing Exclusive Monitor Invalidation Determinism...\n";
    const auto snap_path = kTestDir / "monitor_test.wsnap";
    const auto sndl_path = kTestDir / "monitor_test.sndl";

    const std::size_t ram_size = 1024 * 1024;
    const std::size_t core_count = 2;
    auto mem = std::make_shared<SmpSharedMemory>(ram_size, core_count);
    auto sched = std::make_unique<SmpScheduler>(mem, core_count, 100);

    for (std::size_t c = 0; c < core_count; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma_dev->set_shared_memory(mem);
    bus->register_device(dma_dev);
    sched->attach_device_bus(bus);

    const std::uint64_t target_paddr = 0x30000;

    // Core 0 sets exclusive reservation on target_paddr
    sched->exclusive_monitor().load_exclusive(0, target_paddr, 64);
    TEST_ASSERT(sched->exclusive_monitor().is_reservation_valid(0, target_paddr, 64));

    // Setup DMA descriptor writing to target_paddr
    const std::uint64_t desc_addr = 0x10000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 77;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.dst_paddr = target_paddr;
    desc.length_bytes = 64;
    desc.fill_pattern = 0x55555555;
    mem->write_bytes(1, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    // Core 1 triggers DMA
    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    // Live: Core 0's reservation should have been invalidated by DMA commit
    TEST_ASSERT(!sched->exclusive_monitor().is_reservation_valid(0, target_paddr, 64));

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();
    sched->attach_sndl_recorder(nullptr);

    const std::uint64_t live_hash = sched->compute_canonical_multicore_hash();

    // Replay: Verify reservation invalidation in replay
    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());

    // Initially after snapshot restoration, reservation exists
    TEST_ASSERT(coordinator.scheduler()->exclusive_monitor().is_reservation_valid(0, target_paddr, 64));

    TEST_ASSERT(coordinator.run().is_success());

    // After DMA replay, reservation is invalidated!
    TEST_ASSERT(!coordinator.scheduler()->exclusive_monitor().is_reservation_valid(0, target_paddr, 64));
    std::cout << "Group 5 live_hash=0x" << std::hex << live_hash << " replay_hash=0x" << coordinator.stats().final_canonical_hash << std::dec << std::endl;
    std::cout << "Sched ret_seq: rep=" << coordinator.scheduler()->global_retirement_sequence()
              << " vs rec=" << sched->global_retirement_sequence() << std::endl;
    for (std::size_t c = 0; c < core_count; ++c) {
        std::cout << "Core " << c << ": rep_ret=" << coordinator.scheduler()->get_slot(c).total_instructions_retired
                  << " vs rec_ret=" << sched->get_slot(c).total_instructions_retired
                  << ", rep_pc=0x" << std::hex << coordinator.scheduler()->get_slot(c).cpu->get_program_counter()
                  << " vs rec_pc=0x" << sched->get_slot(c).cpu->get_program_counter()
                  << ", rep_hash=0x" << coordinator.scheduler()->get_slot(c).state.compute_canonical_hash()
                  << " vs rec_hash=0x" << sched->get_slot(c).state.compute_canonical_hash() << std::dec << std::endl;
    }
    const std::uint32_t rep_crc = trace::calculate_crc32c(coordinator.scheduler()->shared_memory().raw_memory().raw_data(), ram_size);
    const std::uint32_t rec_crc = trace::calculate_crc32c(sched->shared_memory().raw_memory().raw_data(), ram_size);
    std::cout << "Mem CRC: rep=0x" << std::hex << rep_crc << " vs rec=0x" << rec_crc << std::dec << std::endl;
    TEST_ASSERT(coordinator.stats().final_canonical_hash == live_hash);
    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "=== Running DMA SMP Determinism Tests ===\n";
    test_group_1_to_4_topologies();
    test_group_5_exclusive_monitor_invalidation();
    cleanup_test_dir();
    std::cout << "=== All DMA SMP Determinism Tests PASSED ===\n";
    return 0;
}
