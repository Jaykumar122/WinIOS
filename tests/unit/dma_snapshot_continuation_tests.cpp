// =============================================================================
// WinIOS Phase 52 Stage 33: Snapshot Continuation Safety Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/snapshot_types.h"
#include "trace/trace_serializer.h"

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
using namespace winios::snapshot;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_snap_continuation_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Snapshot Capture During Idle State (Baseline Sanity)
// -----------------------------------------------------------------------------
void test_group_1_idle_snapshot_capture() {
    std::cout << "[Group 1] Testing Snapshot Capture During Idle State...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g1_idle.wsnap";

    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 2);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        shared_mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    TEST_ASSERT(!dma->is_busy());
    TEST_ASSERT(!bus->has_busy_device());

    const auto res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.error_code == SnapshotErrorCode::Success);

    // Verify snapshot file inspects cleanly
    snapshot::SmpSnapshotInspection inspect{};
    const auto inspect_res = SmpSnapshotManager::inspect_file(snap_path, inspect);
    TEST_ASSERT(inspect_res.success);
    TEST_ASSERT(inspect.header.core_count == 2);

    std::cout << "  -> Group 1 Passed.\n";
}

// -----------------------------------------------------------------------------
// Group 2: Atomic Snapshot Rejection During Active/Busy DMA State
// -----------------------------------------------------------------------------
void test_group_2_busy_dma_snapshot_rejection() {
    std::cout << "[Group 2] Testing Atomic Snapshot Rejection During Active/Busy DMA State...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g2_busy.wsnap";

    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 2);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        shared_mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Force DMA device to busy state
    dma->set_status(VirtualDeviceStatus::Active);
    TEST_ASSERT(dma->is_busy());
    TEST_ASSERT(bus->has_busy_device());

    // Snapshot capture MUST fail atomically with SnapshotErrorCode::DeviceBusy
    const auto res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(!res.success);
    TEST_ASSERT(res.error_code == SnapshotErrorCode::DeviceBusy);

    // Ensure no corrupt snapshot file was created
    TEST_ASSERT(!std::filesystem::exists(snap_path));

    std::cout << "  -> Group 2 Passed. Atomically rejected with DeviceBusy.\n";
}

// -----------------------------------------------------------------------------
// Group 3: Snapshot Restoration and DMA Device State Parity
// -----------------------------------------------------------------------------
void test_group_3_snapshot_restore_parity() {
    std::cout << "[Group 3] Testing Snapshot Restoration and DMA Device State Parity...\n";
    setup_test_dir();
    const auto snap_path = kTestDir / "g3_restore.wsnap";

    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 2);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x2000 + c * 0x200;
        shared_mem->raw_memory().write_u32(code_addr, 0x14000000u);
        cpu->set_program_counter(code_addr);
        sched->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Run a DMA fill
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.dst_paddr = 0x20000;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xEEEEEEEEu;
    shared_mem->write_bytes(0, 0x10000, reinterpret_cast<const std::uint8_t*>(&desc), sizeof(desc));
    (void)dma->trigger_transfer(0x10000, 0);

    const std::uint64_t pre_snap_hash = sched->compute_canonical_multicore_hash();

    // Capture snapshot
    const auto cap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(cap_res.success);

    // Create a new cluster and restore
    auto shared_mem2 = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    auto sched2 = std::make_unique<SmpScheduler>(shared_mem2, 2);
    auto bus2 = std::make_shared<VirtualDeviceBus>();
    auto dma2 = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 32);
    dma2->set_shared_memory(shared_mem2);
    bus2->register_device(dma2);
    sched2->attach_device_bus(bus2);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        sched2->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    const auto restore_res = SmpSnapshotManager::restore_from_file(snap_path, *sched2);
    TEST_ASSERT(restore_res.success);

    const std::uint64_t post_restore_hash = sched2->compute_canonical_multicore_hash();
    TEST_ASSERT(pre_snap_hash == post_restore_hash);

    // Verify DMA device registers were restored
    TEST_ASSERT(dma2->read_register(0x18, 8) == dma->read_register(0x18, 8)); // transfer count
    TEST_ASSERT(dma2->read_register(0x20, 8) == dma->read_register(0x20, 8)); // bytes transferred

    std::cout << "  -> Group 3 Passed. Canonical hash and device state match identically.\n";
}

// -----------------------------------------------------------------------------
// Group 4: Rejection of Deserializing Mid-DMA Busy State
// -----------------------------------------------------------------------------
void test_group_4_rejection_of_busy_deserialization() {
    std::cout << "[Group 4] Testing Rejection of Deserializing Mid-DMA Busy State...\n";
    VirtualDmaDevice dma(0x1000, 0x40, 32);

    // Create valid serialized state
    auto state = dma.serialize_state();
    TEST_ASSERT(dma.deserialize_state(state.data(), state.size()));

    // Tamper reg_status to include kStatusBusy (bit 0)
    // In serialized format: magic at 0..7, reg_control at 8..15, reg_status at 16..23
    std::uint64_t status = trace::decode_u64_le(state.data() + 16);
    status |= 1ULL; // kStatusBusy
    trace::encode_u64_le(state.data() + 16, status);

    // Recompute valid CRC so checksum passes but busy check catches it
    const std::uint32_t new_crc = trace::calculate_crc32c(state.data(), 60);
    trace::encode_u32_le(state.data() + 60, new_crc);

    // deserialize_state MUST reject
    TEST_ASSERT(!dma.deserialize_state(state.data(), state.size()));

    std::cout << "  -> Group 4 Passed. Deserialization of busy state rejected.\n";
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 33: Snapshot Continuation Tests\n";
    std::cout << "=================================================================\n";

    test_group_1_idle_snapshot_capture();
    test_group_2_busy_dma_snapshot_rejection();
    test_group_3_snapshot_restore_parity();
    test_group_4_rejection_of_busy_deserialization();

    cleanup_test_dir();
    std::cout << "All Snapshot Continuation Tests PASSED successfully.\n";
    return 0;
}
