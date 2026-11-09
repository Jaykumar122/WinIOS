#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_ans2_storage.h"
#include "device/apple_devicetree.h"
#include "device/virtual_device_bus.h"
#include "memory/memory.h"
#include "runtime/darwin_platform_expert.h"
#include "runtime/iokit_registry.h"

#define STORAGE_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Test 1: ANS2 Controller MMIO Registration & VirtualDeviceBus Routing
// -----------------------------------------------------------------------------
bool test_ans2_bus_registration_and_routing() {
    std::cout << "[RUN] Test 1: ANS2 Controller Registration & Bus Routing ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();

    STORAGE_TEST_ASSERT(bus.register_device(storage), "ANS2 device must register successfully on bus");
    STORAGE_TEST_ASSERT(bus.is_device_address(AppleANS2StorageDevice::kDefaultAns2Base), "MMIO base must be recognized");
    STORAGE_TEST_ASSERT(bus.find_device_by_address(AppleANS2StorageDevice::kDefaultAns2Base) == storage, "Bus must route to ANS2");

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;
    const std::uint64_t ver = bus.read_mmio(base + AppleANS2StorageDevice::kRegVersion, 4);
    STORAGE_TEST_ASSERT(ver == 0x00010400u, "Version must match NVMe 1.4 (0x00010400)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Controller Capabilities & Ready Handshake
// -----------------------------------------------------------------------------
bool test_ans2_capabilities_and_ready_handshake() {
    std::cout << "[RUN] Test 2: Controller Capabilities & Ready Handshake ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;
    const std::uint64_t cap = bus.read_mmio(base + AppleANS2StorageDevice::kRegCap, 8);
    STORAGE_TEST_ASSERT(cap != 0, "Capabilities register must be non-zero");

    const std::uint64_t csts = bus.read_mmio(base + AppleANS2StorageDevice::kRegStatus, 4);
    STORAGE_TEST_ASSERT((csts & 0x01u) != 0, "Controller status ready bit must be asserted by default");

    // Disable controller via CC
    bus.write_mmio(base + AppleANS2StorageDevice::kRegControl, 0x00u, 4);
    STORAGE_TEST_ASSERT(!storage->is_controller_ready(), "Controller must transition to not ready when disabled");

    // Re-enable controller
    bus.write_mmio(base + AppleANS2StorageDevice::kRegControl, 0x01u, 4);
    STORAGE_TEST_ASSERT(storage->is_controller_ready(), "Controller must report ready when enabled");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Backing Storage Attachment & LBA Geometry Conformance
// -----------------------------------------------------------------------------
bool test_ans2_backing_storage_and_geometry() {
    std::cout << "[RUN] Test 3: Backing Storage & LBA Geometry Conformance ... ";

    auto storage = std::make_shared<AppleANS2StorageDevice>();
    STORAGE_TEST_ASSERT(storage->sector_size() == 4096u, "Apple Silicon standard LBA size must be 4096 bytes");

    // Load 4 MB synthetic disk image
    std::vector<std::uint8_t> disk_image(4 * 1024 * 1024, 0xAA);
    STORAGE_TEST_ASSERT(storage->load_image(disk_image.data(), disk_image.size()), "Disk image load must succeed");
    STORAGE_TEST_ASSERT(storage->total_capacity_bytes() == 4 * 1024 * 1024, "Total capacity must be 4 MB");
    STORAGE_TEST_ASSERT(storage->total_lba_count() == 1024, "4 MB storage must contain exactly 1024 LBAs");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Direct Block Read via DMA to Physical RAM
// -----------------------------------------------------------------------------
bool test_ans2_direct_block_read_dma() {
    std::cout << "[RUN] Test 4: Direct Block Read via DMA to Physical RAM ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    Memory mem(0x10000000); // 256 MB RAM at 0x80000000
    storage->attach_memory(&mem);

    // Initialize LBA 10 in backing storage with test pattern
    std::vector<std::uint8_t> disk_image(1024 * 4096, 0);
    const std::uint32_t pattern = 0x12345678u;
    std::memcpy(disk_image.data() + 10 * 4096, &pattern, 4);
    storage->load_image(disk_image.data(), disk_image.size());

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;
    const std::uint64_t dma_target = 0x85000000ULL;

    // Trigger DMA Read
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 10ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdBlockCnt, 1u, 4);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, dma_target, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);

    const std::uint64_t status = bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4);
    STORAGE_TEST_ASSERT(status == 0, "DMA read command status must be 0 (Success)");

    // Verify RAM contents
    const std::uint64_t mem_off = dma_target - 0x80000000ULL;
    const std::uint32_t read_back = mem.read_u32(static_cast<std::size_t>(mem_off));
    STORAGE_TEST_ASSERT(read_back == pattern, "Target RAM must contain test pattern from LBA 10");
    STORAGE_TEST_ASSERT(storage->total_dma_bytes_transferred() == 4096, "Transferred bytes count must match 4096");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Multi-Block Sequential DMA Read
// -----------------------------------------------------------------------------
bool test_ans2_multi_block_sequential_dma_read() {
    std::cout << "[RUN] Test 5: Multi-Block Sequential DMA Read ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    Memory mem(0x10000000); // 256 MB RAM
    storage->attach_memory(&mem);

    // Populate LBAs 0, 1, 2, 3 with sequence markers
    std::vector<std::uint8_t> disk_image(16 * 4096, 0);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t tag = 0xBEEF0000u | i;
        std::memcpy(disk_image.data() + i * 4096, &tag, 4);
    }
    storage->load_image(disk_image.data(), disk_image.size());

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;
    const std::uint64_t dma_target = 0x86000000ULL;

    // Read 4 contiguous blocks starting at LBA 0
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 0ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdBlockCnt, 4u, 4);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, dma_target, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);

    STORAGE_TEST_ASSERT(bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4) == 0, "Multi-block read must succeed");

    const std::uint64_t mem_off = dma_target - 0x80000000ULL;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t expected = 0xBEEF0000u | i;
        const std::uint32_t val = mem.read_u32(static_cast<std::size_t>(mem_off + i * 4096));
        STORAGE_TEST_ASSERT(val == expected, "Sequential block contents must match tag");
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Block Write via DMA from Physical RAM
// -----------------------------------------------------------------------------
bool test_ans2_block_write_dma() {
    std::cout << "[RUN] Test 6: Block Write via DMA from Physical RAM ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    Memory mem(0x10000000); // 256 MB RAM
    storage->attach_memory(&mem);

    // Populate RAM at 0x87000000 with pattern
    const std::uint64_t dma_src = 0x87000000ULL;
    const std::uint64_t mem_off = dma_src - 0x80000000ULL;
    const std::uint32_t write_tag = 0xCAFE1234u;
    mem.write_u32(static_cast<std::size_t>(mem_off), write_tag);

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;

    // Trigger DMA Write to LBA 20
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 20ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdBlockCnt, 1u, 4);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, dma_src, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdWrite, 4);

    STORAGE_TEST_ASSERT(bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4) == 0, "DMA write command must succeed");

    // Inspect backing data at LBA 20
    const auto& backing = storage->backing_data();
    std::uint32_t written_back = 0;
    std::memcpy(&written_back, backing.data() + 20 * 4096, 4);
    STORAGE_TEST_ASSERT(written_back == write_tag, "Backing storage must contain data written from RAM");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: DMA Boundary Protection & Out-of-Bounds Rejection
// -----------------------------------------------------------------------------
bool test_ans2_boundary_protection() {
    std::cout << "[RUN] Test 7: DMA Boundary Protection & Out-of-Bounds Rejection ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    Memory mem(0x10000000); // 256 MB RAM (0x80000000..0x90000000)
    storage->attach_memory(&mem);

    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;

    // 1. LBA out of bounds (LBA 99999999)
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 99999999ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdBlockCnt, 1u, 4);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, 0x85000000ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);
    STORAGE_TEST_ASSERT(bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4) == 2,
                        "Read past storage capacity must return Error (2)");

    // 2. RAM address below RAM base (0x70000000ULL)
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 0ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, 0x70000000ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);
    STORAGE_TEST_ASSERT(bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4) == 2,
                        "DMA below RAM base must return Error (2)");

    // 3. RAM address above RAM end (0x95000000ULL)
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, 0x95000000ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);
    STORAGE_TEST_ASSERT(bus.read_mmio(base + AppleANS2StorageDevice::kRegCmdStatus, 4) == 2,
                        "DMA above RAM capacity must return Error (2)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Storage Interrupt Line Assertion & Clear
// -----------------------------------------------------------------------------
bool test_ans2_interrupt_signaling() {
    std::cout << "[RUN] Test 8: Storage Interrupt Line Assertion & Clear ... ";

    VirtualDeviceBus bus;
    auto storage = std::make_shared<AppleANS2StorageDevice>();
    bus.register_device(storage);

    Memory mem(0x10000000);
    storage->attach_memory(&mem);

    STORAGE_TEST_ASSERT(!storage->is_interrupt_asserted(), "Interrupt must be deasserted on reset");

    // Perform successful DMA read to trigger interrupt
    const std::uint64_t base = AppleANS2StorageDevice::kDefaultAns2Base;
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdLba, 0ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdBlockCnt, 1u, 4);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdDmaAddr, 0x85000000ULL, 8);
    bus.write_mmio(base + AppleANS2StorageDevice::kRegCmdTrigger, AppleANS2StorageDevice::kCmdRead, 4);

    STORAGE_TEST_ASSERT(storage->is_interrupt_asserted(), "Interrupt must be asserted upon I/O completion");

    storage->clear_interrupt();
    STORAGE_TEST_ASSERT(!storage->is_interrupt_asserted(), "Interrupt must be cleared");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: DeviceTree Node Binding & Darwin Driver Matching for ANS2
// -----------------------------------------------------------------------------
bool test_ans2_devicetree_and_driver_matching() {
    std::cout << "[RUN] Test 9: DeviceTree Node Binding & Driver Matching (ANS2) ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    std::string err;
    STORAGE_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "Driver matching must succeed");

    const auto* ans2_driver = pe.find_driver_by_path("/arm-io/ans2");
    STORAGE_TEST_ASSERT(ans2_driver != nullptr, "ANS2 driver must be matched");
    STORAGE_TEST_ASSERT(ans2_driver->personality.io_class == "AppleANS2NVMeController",
                        "Driver class must be AppleANS2NVMeController");
    STORAGE_TEST_ASSERT(ans2_driver->final_score >= 50000, "ANS2 probe score must be >= 50000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Storage Controller State Serialization & Snapshot Restoration
// -----------------------------------------------------------------------------
bool test_ans2_serialization_and_restore() {
    std::cout << "[RUN] Test 10: Storage Serialization & Snapshot Restoration ... ";

    auto storage_orig = std::make_shared<AppleANS2StorageDevice>();
    storage_orig->write_register(AppleANS2StorageDevice::kRegCmdLba, 42ULL, 8);
    storage_orig->write_register(AppleANS2StorageDevice::kRegCmdBlockCnt, 5u, 4);

    auto snapshot = storage_orig->serialize_state();
    STORAGE_TEST_ASSERT(!snapshot.empty(), "Serialized snapshot must be non-empty");

    auto storage_restored = std::make_shared<AppleANS2StorageDevice>();
    STORAGE_TEST_ASSERT(storage_restored->deserialize_state(snapshot.data(), snapshot.size()),
                        "Deserialization must succeed");

    STORAGE_TEST_ASSERT(storage_restored->read_register(AppleANS2StorageDevice::kRegCmdLba, 8) == 42ULL,
                        "Restored LBA must match 42ULL");
    STORAGE_TEST_ASSERT(storage_restored->read_register(AppleANS2StorageDevice::kRegCmdBlockCnt, 4) == 5u,
                        "Restored block count must match 5u");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 119: Storage Device Emulation & Block I/O Suite          \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_ans2_bus_registration_and_routing()) passed++;
    if (test_ans2_capabilities_and_ready_handshake()) passed++;
    if (test_ans2_backing_storage_and_geometry()) passed++;
    if (test_ans2_direct_block_read_dma()) passed++;
    if (test_ans2_multi_block_sequential_dma_read()) passed++;
    if (test_ans2_block_write_dma()) passed++;
    if (test_ans2_boundary_protection()) passed++;
    if (test_ans2_interrupt_signaling()) passed++;
    if (test_ans2_devicetree_and_driver_matching()) passed++;
    if (test_ans2_serialization_and_restore()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
