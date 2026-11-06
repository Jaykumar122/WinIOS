#include "trace/trace_serializer.h"
// =============================================================================
// WinIOS Phase 52 Stage 32: End-to-End DMA Recording and Replay Unit Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_rec_rep_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: End-to-End DMA Fill Recording and Deterministic Replay
// -----------------------------------------------------------------------------
void test_group_1_fill_recording_and_replay() {
    std::cout << "[Group 1] Testing End-to-End DMA Fill Recording & Deterministic Replay...\n";
    setup_test_dir(); const auto snap_path = kTestDir / "fill_test.wsnap";
    const auto sndl_path = kTestDir / "fill_test.sndl";

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
    sched->attach_device_bus(bus); const std::uint64_t desc_addr = 0x10000;
    const std::uint64_t dst_addr = 0x20000;
    const std::uint64_t length = 256;
    const std::uint32_t fill_val = 0xDEADBEEF;

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.status = static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::Pending);
    desc.flags = 0;
    desc.src_paddr = 0;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = length;
    desc.fill_pattern = fill_val;
    desc.next_descriptor_paddr = 0;

    mem->write_bytes(0, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    auto snap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(snap_res.success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlIrqEnable, 8);
    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();

    std::vector<std::uint8_t> read_buf(length);
    mem->read_bytes(0, dst_addr, read_buf.data(), length);
    for (std::size_t i = 0; i < length; i += 4) {
        std::uint32_t val = 0;
        std::memcpy(&val, read_buf.data() + i, 4);
        TEST_ASSERT(val == fill_val);
    }

    const std::uint64_t recorded_final_hash = sched->compute_canonical_multicore_hash();

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    auto prep_res = coordinator.prepare(snap_path, sndl_path);
    TEST_ASSERT(prep_res.is_success());
    TEST_ASSERT(coordinator.state() == SmpReplayState::Ready);

    auto run_res = coordinator.run();
    if (run_res.is_failure()) {
        std::cerr << "Replay failed: " << run_res.message
                  << ", DivType: " << smp_replay_divergence_type_name(coordinator.divergence().type)
                  << ", DivMsg: " << coordinator.divergence().message
                  << ", Seq: " << coordinator.divergence().global_event_sequence << std::endl;
    }
    TEST_ASSERT(run_res.is_success());
    TEST_ASSERT(coordinator.state() == SmpReplayState::Completed);

    std::vector<std::uint8_t> replayed_buf(length);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst_addr, replayed_buf.data(), length);
    TEST_ASSERT(replayed_buf == read_buf);
    
    
    std::cout << "Replay final hash: 0x" << std::hex << coordinator.stats().final_canonical_hash
              << " vs Recorded: 0x" << recorded_final_hash << std::dec << std::endl;
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
    TEST_ASSERT(coordinator.stats().final_canonical_hash == recorded_final_hash);



    std::cout << "  -> PASSED (Canonical final hash: 0x" << std::hex << recorded_final_hash << std::dec << ")\n";
}

// -----------------------------------------------------------------------------
// Group 2: End-to-End DMA Copy Recording and Replay with Companion Payload
// -----------------------------------------------------------------------------
void test_group_2_copy_recording_and_replay() {
    std::cout << "[Group 2] Testing End-to-End DMA Copy Recording & Replay with Companion Store...\n";
    const auto snap_path = kTestDir / "copy_test.wsnap";
    const auto sndl_path = kTestDir / "copy_test.sndl";

    const std::size_t ram_size = 2 * 1024 * 1024;
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
    sched->attach_device_bus(bus); const std::uint64_t desc_addr = 0x10000;
    const std::uint64_t src_addr = 0x30000;
    const std::uint64_t dst_addr = 0x50000;
    const std::uint64_t length = 1024;

    std::vector<std::uint8_t> source_data(length);
    for (std::size_t i = 0; i < length; ++i) {
        source_data[i] = static_cast<std::uint8_t>((i * 37 + 0x55) & 0xFF);
    }
    mem->write_bytes(0, src_addr, source_data.data(), length);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 42;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.status = static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::Pending);
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = length;
    mem->write_bytes(0, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();

    const std::uint64_t live_final_hash = sched->compute_canonical_multicore_hash();

    auto companion_path = sndl_path;
    companion_path.concat(".dma");
    TEST_ASSERT(std::filesystem::exists(companion_path));

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());
    TEST_ASSERT(coordinator.run().is_success());
    TEST_ASSERT(coordinator.state() == SmpReplayState::Completed);

    std::vector<std::uint8_t> replayed_dst(length);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst_addr, replayed_dst.data(), length);
    TEST_ASSERT(replayed_dst == source_data);
    TEST_ASSERT(coordinator.stats().final_canonical_hash == live_final_hash);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 3: Small Transfers with Inline DmaDataChunk
// -----------------------------------------------------------------------------
void test_group_3_inline_chunks() {
    std::cout << "[Group 3] Testing Small Transfers with Inline DmaDataChunk...\n";
    const auto snap_path = kTestDir / "inline_test.wsnap";
    const auto sndl_path = kTestDir / "inline_test.sndl";

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
    sched->attach_device_bus(bus); const std::uint64_t desc_addr = 0x10000;
    const std::uint64_t src_addr = 0x20000;
    const std::uint64_t dst_addr = 0x30000;
    const std::uint64_t length = 64;

    std::vector<std::uint8_t> test_data(length);
    for (std::size_t i = 0; i < length; ++i) {
        test_data[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    mem->write_bytes(0, src_addr, test_data.data(), length);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 101;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = length;
    mem->write_bytes(0, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();

    const std::uint64_t live_hash = sched->compute_canonical_multicore_hash();

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());
    TEST_ASSERT(coordinator.run().is_success());

    std::vector<std::uint8_t> replayed_data(length);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst_addr, replayed_data.data(), length);
    TEST_ASSERT(replayed_data == test_data);
    TEST_ASSERT(coordinator.stats().final_canonical_hash == live_hash);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 4: Chained Multi-Descriptor Transfers
// -----------------------------------------------------------------------------
void test_group_4_chained_descriptors() {
    std::cout << "[Group 4] Testing Chained Multi-Descriptor DMA Transfers...\n";
    const auto snap_path = kTestDir / "chain_test.wsnap";
    const auto sndl_path = kTestDir / "chain_test.sndl";

    const std::size_t ram_size = 2 * 1024 * 1024;
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
    sched->attach_device_bus(bus); const std::uint64_t d0_addr = 0x10000;
    const std::uint64_t d1_addr = 0x10040;
    const std::uint64_t d2_addr = 0x10080;

    const std::uint64_t src1 = 0x40000;
    const std::uint64_t dst0 = 0x50000;
    const std::uint64_t dst1 = 0x60000;
    const std::uint64_t dst2 = 0x70000;

    std::vector<std::uint8_t> copy_data(512);
    for (std::size_t i = 0; i < copy_data.size(); ++i) copy_data[i] = static_cast<std::uint8_t>(i & 0xFF);
    mem->write_bytes(0, src1, copy_data.data(), copy_data.size());

    VirtualDmaDescriptor d0{}, d1{}, d2{};
    d0.descriptor_id = 1;
    d0.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d0.flags = kDmaFlagChainNext;
    d0.dst_paddr = dst0;
    d0.length_bytes = 128;
    d0.fill_pattern = 0x11111111;
    d0.next_descriptor_paddr = d1_addr;

    d1.descriptor_id = 2;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d1.flags = kDmaFlagChainNext;
    d1.src_paddr = src1;
    d1.dst_paddr = dst1;
    d1.length_bytes = 512;
    d1.next_descriptor_paddr = d2_addr;

    d2.descriptor_id = 3;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d2.dst_paddr = dst2;
    d2.length_bytes = 64;
    d2.fill_pattern = 0x22222222;
    d2.next_descriptor_paddr = 0;

    mem->write_bytes(0, d0_addr, reinterpret_cast<const std::uint8_t*>(&d0), sizeof(d0));
    mem->write_bytes(0, d1_addr, reinterpret_cast<const std::uint8_t*>(&d1), sizeof(d1));
    mem->write_bytes(0, d2_addr, reinterpret_cast<const std::uint8_t*>(&d2), sizeof(d2));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kDescriptorAddr, d0_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();

    const std::uint64_t live_hash = sched->compute_canonical_multicore_hash();

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());
    TEST_ASSERT(coordinator.run().is_success());

    std::vector<std::uint8_t> r0(128), r1(512), r2(64);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst0, r0.data(), 128);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst1, r1.data(), 512);
    coordinator.scheduler()->shared_memory().read_bytes(0, dst2, r2.data(), 64);

    for (std::size_t i = 0; i < 128; i += 4) {
        std::uint32_t val = 0;
        std::memcpy(&val, r0.data() + i, 4);
        TEST_ASSERT(val == 0x11111111);
    }
    TEST_ASSERT(r1 == copy_data);
    for (std::size_t i = 0; i < 64; i += 4) {
        std::uint32_t val = 0;
        std::memcpy(&val, r2.data() + i, 4);
        TEST_ASSERT(val == 0x22222222);
    }

    TEST_ASSERT(coordinator.stats().final_canonical_hash == live_hash);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 5: Rejection and Error Paths
// -----------------------------------------------------------------------------
void test_group_5_rejection_and_errors() {
    std::cout << "[Group 5] Testing DMA Rejection and Error Handling Replay...\n";
    const auto snap_path = kTestDir / "error_test.wsnap";
    const auto sndl_path = kTestDir / "error_test.sndl";

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
    sched->attach_device_bus(bus); const std::uint64_t desc_addr = 0x10000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 999;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.dst_paddr = ram_size + 0x1000; // invalid out-of-bounds
    desc.length_bytes = 128;
    desc.fill_pattern = 0xBAD0BAD0;
    mem->write_bytes(0, desc_addr, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));

    TEST_ASSERT(SmpSnapshotManager::capture_to_file(*sched, snap_path).success);

    SndlV2RecorderOptions opts{};
    opts.core_count = static_cast<std::uint16_t>(core_count);
    opts.memory_capacity = ram_size;
    opts.base_snapshot_hash = sched->compute_canonical_multicore_hash();
    SndlV2Recorder recorder(opts);
    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    dma_dev->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    sched->step_quantum();

    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());
    dma_dev->detach_sndl_recorder();

    std::uint64_t st = dma_dev->read_register(dma_reg::kStatus, 8);
    TEST_ASSERT((st & kStatusError) != 0);

    SmpReplayCapabilityConfig config{};
    config.allow_dma_recording = true;
    config.allow_dma_replay = true;

    SmpReplayCoordinator coordinator(config);
    TEST_ASSERT(coordinator.prepare(snap_path, sndl_path).is_success());
    TEST_ASSERT(coordinator.run().is_success());
    TEST_ASSERT(coordinator.state() == SmpReplayState::Completed);
    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::cout << "=== Running End-to-End DMA Recording and Replay Unit Tests ===\n" << std::flush;
    test_group_1_fill_recording_and_replay();
    test_group_2_copy_recording_and_replay();
    test_group_3_inline_chunks();
    test_group_4_chained_descriptors();
    test_group_5_rejection_and_errors();
    cleanup_test_dir();
    std::cout << "=== All End-to-End DMA Recording and Replay Unit Tests PASSED ===\n";
    return 0;
}
