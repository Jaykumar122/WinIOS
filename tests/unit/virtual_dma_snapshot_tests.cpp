// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual DMA Snapshot & Serialization Tests
// =============================================================================

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_device.h"
#include "device/virtual_dma_types.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/snapshot_types.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::device;
using namespace winios::snapshot;

namespace {

struct Fixture {
    std::shared_ptr<SmpSharedMemory> mem;
    std::unique_ptr<VirtualDmaDevice> dev;
    const std::size_t ram_size{1024 * 1024};
};

Fixture create_fixture() {
    Fixture f;
    f.mem = std::make_shared<SmpSharedMemory>(f.ram_size, 8);
    f.dev = std::make_unique<VirtualDmaDevice>(0x444D4131, 0x10000000, 0x1000, f.mem, 48);
    return f;
}

// -----------------------------------------------------------------------------
// Group 1: Idle Device Serialization & Restoration
// -----------------------------------------------------------------------------
void test_group_1_idle_snapshot() {
    std::cout << "[Group 1] Testing Idle Device State Serialization...\n";
    auto f = create_fixture();

    auto blob = f.dev->serialize_state();
    assert(blob.size() == 64);

    auto f2 = create_fixture();
    assert(f2.dev->deserialize_state(blob.data(), blob.size()));

    assert(f2.dev->device_id() == 0x444D4131);
    assert(f2.dev->read_register(dma_reg::kControl, 8) == kControlEnable);
    assert(f2.dev->read_register(dma_reg::kStatus, 8) == 0);
    assert(f2.dev->read_register(dma_reg::kTransferCount, 8) == 0);
    assert(f2.dev->read_register(dma_reg::kBytesTransferred, 8) == 0);

    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Completed Transfers State Serialization
// -----------------------------------------------------------------------------
void test_group_2_completed_state_snapshot() {
    std::cout << "[Group 2] Testing Completed State Serialization...\n";
    auto f = create_fixture();

    // Perform transfer of 256 bytes
    const std::uint64_t desc_addr = 0x2000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 99;
    desc.src_paddr = 0x10000;
    desc.dst_paddr = 0x20000;
    desc.length_bytes = 256;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x55;
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    f.dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    f.dev->write_register(dma_reg::kControl, kControlEnable | kControlStart | kControlIrqEnable, 8);

    assert(f.dev->read_register(dma_reg::kTransferCount, 8) == 1);
    assert(f.dev->read_register(dma_reg::kBytesTransferred, 8) == 256);
    assert((f.dev->read_register(dma_reg::kStatus, 8) & kStatusComplete) != 0);

    auto blob = f.dev->serialize_state();

    // Restore into a fresh device
    auto f2 = create_fixture();
    assert(f2.dev->deserialize_state(blob.data(), blob.size()));

    assert(f2.dev->read_register(dma_reg::kTransferCount, 8) == 1);
    assert(f2.dev->read_register(dma_reg::kBytesTransferred, 8) == 256);
    assert((f2.dev->read_register(dma_reg::kStatus, 8) & kStatusComplete) != 0);
    assert(f2.dev->read_register(dma_reg::kDescriptorAddr, 8) == desc_addr);

    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Active Error State Serialization
// -----------------------------------------------------------------------------
void test_group_3_error_state_snapshot() {
    std::cout << "[Group 3] Testing Error State Serialization...\n";
    auto f = create_fixture();

    // Intentionally trigger out-of-bounds error
    const std::uint64_t desc_addr = 0x3000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 100;
    desc.src_paddr = 0x1000;
    desc.dst_paddr = f.ram_size + 100; // Out of bounds!
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    f.dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    f.dev->write_register(dma_reg::kControl, kControlEnable | kControlStart, 8);

    assert((f.dev->read_register(dma_reg::kStatus, 8) & kStatusError) != 0);
    const std::uint64_t err = f.dev->read_register(dma_reg::kErrorCode, 8);
    assert(err == static_cast<std::uint64_t>(VirtualDmaDescriptorStatus::ErrorOutOfBounds));

    auto blob = f.dev->serialize_state();

    auto f2 = create_fixture();
    assert(f2.dev->deserialize_state(blob.data(), blob.size()));

    assert((f2.dev->read_register(dma_reg::kStatus, 8) & kStatusError) != 0);
    assert(f2.dev->read_register(dma_reg::kErrorCode, 8) == err);

    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Adversarial Corruption & Atomic Failure
// -----------------------------------------------------------------------------
void test_group_4_adversarial_corruption() {
    std::cout << "[Group 4] Testing Adversarial Snapshot Corruption...\n";
    auto f = create_fixture();
    auto blob = f.dev->serialize_state();

    // 1. Corrupted magic bytes
    auto corrupt_magic = blob;
    corrupt_magic[0] = 'X';
    corrupt_magic[1] = 'Y';
    assert(!f.dev->deserialize_state(corrupt_magic.data(), corrupt_magic.size()));

    // 2. Corrupted CRC32C
    auto corrupt_crc = blob;
    corrupt_crc[60] ^= 0xFF;
    assert(!f.dev->deserialize_state(corrupt_crc.data(), corrupt_crc.size()));

    // 3. Truncated payload
    assert(!f.dev->deserialize_state(blob.data(), 40));

    // 4. Null pointer
    assert(!f.dev->deserialize_state(nullptr, 64));

    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: WSnap v2 Section Serialization Integration
// -----------------------------------------------------------------------------
void test_group_5_wsnap_section_integration() {
    std::cout << "[Group 5] Testing WSnap v2 Section Integration...\n";
    auto f = create_fixture();
    auto dev_blob = f.dev->serialize_state();

    // Wrap in WSnap Section 0x18 (DeviceState)
    auto section_bytes = serialize_device_state(0x444D4131, dev_blob);
    assert(section_bytes.size() == 8 + dev_blob.size() + 4);

    std::uint32_t out_id = 0;
    std::vector<std::uint8_t> out_payload;
    auto res = deserialize_device_state(section_bytes.data(), section_bytes.size(), out_id, out_payload);
    assert(res.success);
    assert(out_id == 0x444D4131);
    assert(out_payload == dev_blob);

    // Corrupt section CRC
    section_bytes.back() ^= 0xEE;
    auto res_corrupt = deserialize_device_state(section_bytes.data(), section_bytes.size(), out_id, out_payload);
    assert(!res_corrupt.success);
    assert(res_corrupt.error_code == SnapshotErrorCode::SectionCrcMismatch);

    std::cout << "  -> Group 5 PASS\n";
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual DMA Snapshot Tests              \n";
    std::cout << "===================================================================\n\n";

    test_group_1_idle_snapshot();
    test_group_2_completed_state_snapshot();
    test_group_3_error_state_snapshot();
    test_group_4_adversarial_corruption();
    test_group_5_wsnap_section_integration();

    std::cout << "\n>>> ALL VIRTUAL DMA SNAPSHOT TEST GROUPS PASSED <<<\n";
    return 0;
}
