#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/virtual_device.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_device_types.h"

#define STAGE66_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::device;

namespace {

class TestMmioDevice : public VirtualDevice {
public:
    TestMmioDevice(std::uint32_t id, const std::string& name, std::uint64_t base, std::uint64_t size)
        : VirtualDevice(id, name, VirtualDeviceType::GenericMmio, base, size) {}

    std::uint64_t read_register(std::uint64_t offset, unsigned width) override {
        (void)width;
        reads_count++;
        if (offset == 0x00) return reg0_;
        if (offset == 0x08) return reg1_;
        if (offset == 0x10) return reg2_;
        return 0;
    }

    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override {
        (void)width;
        writes_count++;
        if (offset == 0x00) reg0_ = value;
        if (offset == 0x08) reg1_ = value;
        if (offset == 0x10) reg2_ = value;
    }

    void reset() noexcept override {
        reg0_ = 0;
        reg1_ = 0;
        reg2_ = 0;
        reads_count = 0;
        writes_count = 0;
        status_ = VirtualDeviceStatus::Idle;
    }

    std::vector<std::uint8_t> serialize_state() const override {
        std::vector<std::uint8_t> data(24);
        std::memcpy(data.data(), &reg0_, 8);
        std::memcpy(data.data() + 8, &reg1_, 8);
        std::memcpy(data.data() + 16, &reg2_, 8);
        return data;
    }

    bool deserialize_state(const std::uint8_t* data, std::size_t size) override {
        if (!data || size != 24) return false;
        std::memcpy(&reg0_, data, 8);
        std::memcpy(&reg1_, data + 8, 8);
        std::memcpy(&reg2_, data + 16, 8);
        return true;
    }

    std::uint64_t reg0_{0};
    std::uint64_t reg1_{0};
    std::uint64_t reg2_{0};
    std::uint32_t reads_count{0};
    std::uint32_t writes_count{0};
};

// -----------------------------------------------------------------------------
// Suite 1: Device Registration and ID / Address Lookup
// -----------------------------------------------------------------------------
bool test_device_registration_and_lookup() {
    std::cout << "[1] test_device_registration_and_lookup...\n";
    VirtualDeviceBus bus;

    auto dev1 = std::make_shared<TestMmioDevice>(1, "Dev1", 0x10000000, 0x1000);
    auto dev2 = std::make_shared<TestMmioDevice>(2, "Dev2", 0x20000000, 0x2000);
    auto dev3 = std::make_shared<TestMmioDevice>(3, "Dev3", 0x30000000, 0x1000);

    STAGE66_ASSERT(bus.register_device(dev1), "Register Dev1");
    STAGE66_ASSERT(bus.register_device(dev2), "Register Dev2");
    STAGE66_ASSERT(bus.register_device(dev3), "Register Dev3");
    STAGE66_ASSERT(bus.device_count() == 3, "Total registered devices must be 3");

    STAGE66_ASSERT(bus.find_device_by_id(1) == dev1, "Lookup Dev1");
    STAGE66_ASSERT(bus.find_device_by_id(2) == dev2, "Lookup Dev2");
    STAGE66_ASSERT(bus.find_device_by_id(3) == dev3, "Lookup Dev3");
    STAGE66_ASSERT(bus.find_device_by_id(999) == nullptr, "Lookup nonexistent returns null");

    STAGE66_ASSERT(bus.is_device_address(0x10000000), "0x10000000 is Dev1 base");
    STAGE66_ASSERT(bus.is_device_address(0x10000FFF), "0x10000FFF is Dev1 end");
    STAGE66_ASSERT(!bus.is_device_address(0x10001000), "0x10001000 outside Dev1");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Duplicate Device ID Rejection
// -----------------------------------------------------------------------------
bool test_duplicate_id_rejection() {
    std::cout << "[2] test_duplicate_id_rejection...\n";
    VirtualDeviceBus bus;

    auto dev1 = std::make_shared<TestMmioDevice>(10, "Dev10", 0x10000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev1), "First registration of ID 10 succeeds");

    // Second device with duplicate ID 10 on different range
    auto dev10_dup = std::make_shared<TestMmioDevice>(10, "Dev10_Dup", 0x20000000, 0x1000);
    STAGE66_ASSERT(!bus.register_device(dev10_dup), "Duplicate device ID 10 must be rejected");
    STAGE66_ASSERT(bus.device_count() == 1, "Device count remains 1");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Overlapping MMIO Range Rejection
// -----------------------------------------------------------------------------
bool test_overlapping_range_rejection() {
    std::cout << "[3] test_overlapping_range_rejection...\n";
    VirtualDeviceBus bus;

    auto dev_base = std::make_shared<TestMmioDevice>(1, "Base", 0x40000000, 0x4000); // 0x40000000..0x40003FFF
    STAGE66_ASSERT(bus.register_device(dev_base), "Base device registered");

    // A. Exact overlap
    auto dev_exact = std::make_shared<TestMmioDevice>(2, "Exact", 0x40000000, 0x4000);
    STAGE66_ASSERT(!bus.register_device(dev_exact), "Exact range overlap rejected");

    // B. Partial overlap: start inside base
    auto dev_start_inside = std::make_shared<TestMmioDevice>(3, "StartInside", 0x40002000, 0x4000);
    STAGE66_ASSERT(!bus.register_device(dev_start_inside), "Partial overlap (start inside) rejected");

    // C. Partial overlap: end inside base
    auto dev_end_inside = std::make_shared<TestMmioDevice>(4, "EndInside", 0x3FFFF000, 0x2000);
    STAGE66_ASSERT(!bus.register_device(dev_end_inside), "Partial overlap (end inside) rejected");

    // D. Enclosing overlap: 0x3FFF0000 .. 0x40010000 encloses 0x40000000 .. 0x40004000
    auto dev_enclosing = std::make_shared<TestMmioDevice>(5, "Enclosing", 0x3FFF0000, 0x20000);
    STAGE66_ASSERT(!bus.register_device(dev_enclosing), "Enclosing range overlap rejected");

    STAGE66_ASSERT(bus.device_count() == 1, "Only base device remains registered");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: MMIO Read / Write Routing & Width Handling
// -----------------------------------------------------------------------------
bool test_mmio_read_write_routing() {
    std::cout << "[4] test_mmio_read_write_routing...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<TestMmioDevice>(1, "Device", 0x50000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    // 4-byte write to offset 0x00
    bus.write_mmio(0x50000000, 0x12345678, 4);
    STAGE66_ASSERT(dev->writes_count == 1, "Write count incremented");
    STAGE66_ASSERT(dev->reg0_ == 0x12345678, "Reg0 updated with 0x12345678");

    // 4-byte read from offset 0x00
    std::uint64_t val = bus.read_mmio(0x50000000, 4);
    STAGE66_ASSERT(dev->reads_count == 1, "Read count incremented");
    STAGE66_ASSERT(val == 0x12345678, "Read value matches written value");

    // 8-byte write to offset 0x08
    bus.write_mmio(0x50000008, 0xCAFEBABEDEADBEEFULL, 8);
    STAGE66_ASSERT(dev->reg1_ == 0xCAFEBABEDEADBEEFULL, "Reg1 updated with 64-bit value");
    STAGE66_ASSERT(bus.read_mmio(0x50000008, 8) == 0xCAFEBABEDEADBEEFULL, "Read 64-bit matches");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Strict Alignment Rejection
// -----------------------------------------------------------------------------
bool test_strict_alignment_rejection() {
    std::cout << "[5] test_strict_alignment_rejection...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<TestMmioDevice>(1, "AlignedDev", 0x60000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    // 2-byte access to odd address
    bus.write_mmio(0x60000001, 0xAA, 2);
    STAGE66_ASSERT(dev->writes_count == 0, "Misaligned 2-byte write rejected");
    STAGE66_ASSERT(bus.read_mmio(0x60000001, 2) == 0, "Misaligned 2-byte read returns 0");

    // 4-byte access to non-4-aligned address (offset 2)
    bus.write_mmio(0x60000002, 0xBBBB, 4);
    STAGE66_ASSERT(dev->writes_count == 0, "Misaligned 4-byte write rejected");
    STAGE66_ASSERT(bus.read_mmio(0x60000002, 4) == 0, "Misaligned 4-byte read returns 0");

    // 8-byte access to non-8-aligned address (offset 4)
    bus.write_mmio(0x60000004, 0xCCCCCCCC, 8);
    STAGE66_ASSERT(dev->writes_count == 0, "Misaligned 8-byte write rejected");
    STAGE66_ASSERT(bus.read_mmio(0x60000004, 8) == 0, "Misaligned 8-byte read returns 0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Unmapped MMIO Access Handling
// -----------------------------------------------------------------------------
bool test_unmapped_access_handling() {
    std::cout << "[6] test_unmapped_access_handling...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<TestMmioDevice>(1, "Dev", 0x70000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    // Write to completely unmapped address 0x80000000
    bus.write_mmio(0x80000000, 0xDEAD, 4);
    STAGE66_ASSERT(dev->writes_count == 0, "No device write for unmapped address");

    // Read from unmapped address
    std::uint64_t val = bus.read_mmio(0x80000000, 4);
    STAGE66_ASSERT(val == 0, "Unmapped read returns 0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Disabled Device Behavior
// -----------------------------------------------------------------------------
bool test_disabled_device_behavior() {
    std::cout << "[7] test_disabled_device_behavior...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<TestMmioDevice>(1, "ToggleDev", 0x75000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    // Initially active/idle (enabled)
    STAGE66_ASSERT(dev->is_enabled(), "Device initially enabled");

    // Disable device
    dev->set_enabled(false);
    STAGE66_ASSERT(!dev->is_enabled(), "Device reported as disabled");

    // Transactions must be ignored while disabled
    bus.write_mmio(0x75000000, 0x999, 4);
    STAGE66_ASSERT(dev->writes_count == 0, "Write dropped for disabled device");
    STAGE66_ASSERT(bus.read_mmio(0x75000000, 4) == 0, "Read returns 0 for disabled device");

    // Re-enable device
    dev->set_enabled(true);
    STAGE66_ASSERT(dev->is_enabled(), "Device re-enabled");

    bus.write_mmio(0x75000000, 0x555, 4);
    STAGE66_ASSERT(dev->writes_count == 1, "Write accepted after re-enable");
    STAGE66_ASSERT(bus.read_mmio(0x75000000, 4) == 0x555, "Read returns 0x555 after re-enable");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Capability Gate Enforcement
// -----------------------------------------------------------------------------
bool test_capability_gate_enforcement() {
    std::cout << "[8] test_capability_gate_enforcement...\n";
    VirtualDeviceCapabilities caps;
    caps.allow_virtual_devices = false; // System gate blocks all devices
    VirtualDeviceBus bus(caps);

    auto dev = std::make_shared<TestMmioDevice>(1, "GatedDev", 0x80000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    // Access must be blocked by capability gate
    bus.write_mmio(0x80000000, 0x4321, 4);
    STAGE66_ASSERT(dev->writes_count == 0, "Write blocked by allow_virtual_devices=false");
    STAGE66_ASSERT(bus.read_mmio(0x80000000, 4) == 0, "Read returns 0 when blocked");

    // Update capabilities to allow
    caps.allow_virtual_devices = true;
    bus.set_capabilities(caps);

    bus.write_mmio(0x80000000, 0x4321, 4);
    STAGE66_ASSERT(dev->writes_count == 1, "Write succeeds once capability enabled");
    STAGE66_ASSERT(bus.read_mmio(0x80000000, 4) == 0x4321, "Read succeeds once capability enabled");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Device Reset and Unregistration
// -----------------------------------------------------------------------------
bool test_device_reset_and_unregistration() {
    std::cout << "[9] test_device_reset_and_unregistration...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<TestMmioDevice>(1, "ResetDev", 0x85000000, 0x1000);
    STAGE66_ASSERT(bus.register_device(dev), "Device registered");

    bus.write_mmio(0x85000000, 0xAAAA, 4);
    bus.write_mmio(0x85000008, 0xBBBB, 8);
    STAGE66_ASSERT(dev->reg0_ == 0xAAAA && dev->reg1_ == 0xBBBB, "Registers configured");

    // Bus reset_all()
    bus.reset_all();
    STAGE66_ASSERT(dev->reg0_ == 0 && dev->reg1_ == 0, "Registers reset to 0");
    STAGE66_ASSERT(dev->writes_count == 0 && dev->reads_count == 0, "Access counters reset");

    // Unregister
    STAGE66_ASSERT(bus.unregister_device(1), "Unregister device 1 succeeds");
    STAGE66_ASSERT(bus.device_count() == 0, "Device count is 0 after unregister");
    STAGE66_ASSERT(!bus.is_device_address(0x85000000), "Address no longer recognized");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Memory Map Layout & Snapshot Serialization
// -----------------------------------------------------------------------------
bool test_memory_map_and_serialization() {
    std::cout << "[10] test_memory_map_and_serialization...\n";
    VirtualDeviceBus bus;

    // Architectural System Memory Map validation:
    // GIC:    0x08000000, 0x00020000
    // UART:   0x09000000, 0x00001000
    // DMA:    0x10000000, 0x00001000
    // RAM:    0x40000000+
    auto dev_gic  = std::make_shared<TestMmioDevice>(1, "GIC",  0x08000000, 0x00020000);
    auto dev_uart = std::make_shared<TestMmioDevice>(2, "UART", 0x09000000, 0x00001000);
    auto dev_dma  = std::make_shared<TestMmioDevice>(3, "DMA",  0x10000000, 0x00001000);

    STAGE66_ASSERT(bus.register_device(dev_gic), "Register GIC in memory map");
    STAGE66_ASSERT(bus.register_device(dev_uart), "Register UART in memory map");
    STAGE66_ASSERT(bus.register_device(dev_dma), "Register DMA in memory map");

    // Write state to DMA device
    bus.write_mmio(0x10000000, 0x1111111122222222ULL, 8);
    bus.write_mmio(0x10000008, 0x3333333344444444ULL, 8);
    bus.write_mmio(0x10000010, 0x5555555566666666ULL, 8);

    // Serialize DMA state
    std::vector<std::uint8_t> state = dev_dma->serialize_state();
    STAGE66_ASSERT(state.size() == 24, "Serialized state size is 24 bytes");

    // Reset DMA device
    dev_dma->reset();
    STAGE66_ASSERT(dev_dma->reg0_ == 0 && dev_dma->reg1_ == 0 && dev_dma->reg2_ == 0, "DMA reset");

    // Restore state
    STAGE66_ASSERT(dev_dma->deserialize_state(state.data(), state.size()), "Deserialization succeeded");
    STAGE66_ASSERT(dev_dma->reg0_ == 0x1111111122222222ULL, "Reg0 restored");
    STAGE66_ASSERT(dev_dma->reg1_ == 0x3333333344444444ULL, "Reg1 restored");
    STAGE66_ASSERT(dev_dma->reg2_ == 0x5555555566666666ULL, "Reg2 restored");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 66 Virtual Device Framework Tests ===" << std::endl;

    if (!test_device_registration_and_lookup()) return 1;
    if (!test_duplicate_id_rejection()) return 2;
    if (!test_overlapping_range_rejection()) return 3;
    if (!test_mmio_read_write_routing()) return 4;
    if (!test_strict_alignment_rejection()) return 5;
    if (!test_unmapped_access_handling()) return 6;
    if (!test_disabled_device_behavior()) return 7;
    if (!test_capability_gate_enforcement()) return 8;
    if (!test_device_reset_and_unregistration()) return 9;
    if (!test_memory_map_and_serialization()) return 10;

    std::cout << "stage66_virtual_device_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
