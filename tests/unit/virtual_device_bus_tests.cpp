// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual Device Bus & MMIO Unit Tests
// =============================================================================

#include "device/virtual_device.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_device_types.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <iostream>
#include <memory>
#include <vector>

using namespace winios::device;

namespace {

class DummyDevice : public VirtualDevice {
public:
    DummyDevice(std::uint32_t id, std::uint64_t base, std::uint64_t size)
        : VirtualDevice(id, "DummyDevice", VirtualDeviceType::GenericMmio, base, size) {}

    std::uint64_t read_register(std::uint64_t offset, unsigned width) override {
        (void)width;
        reads_count++;
        if (offset == 0x00) return reg0_;
        if (offset == 0x08) return reg1_;
        return 0;
    }

    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override {
        (void)width;
        writes_count++;
        if (offset == 0x00) reg0_ = value;
        if (offset == 0x08) reg1_ = value;
    }

    void reset() noexcept override {
        reg0_ = 0;
        reg1_ = 0;
        reads_count = 0;
        writes_count = 0;
    }

    std::vector<std::uint8_t> serialize_state() const override {
        return {0x01, 0x02, 0x03};
    }

    bool deserialize_state(const std::uint8_t* data, std::size_t size) override {
        return data && size == 3;
    }

    std::uint64_t reg0_{0};
    std::uint64_t reg1_{0};
    std::uint32_t reads_count{0};
    std::uint32_t writes_count{0};
};

// -----------------------------------------------------------------------------
// Group 1: Device Registration and Query
// -----------------------------------------------------------------------------
void test_group_1_registration() {
    std::cout << "[Group 1] Testing Device Registration and Query...\n";
    VirtualDeviceBus bus;

    auto dev1 = std::make_shared<DummyDevice>(1, 0x10000000, 0x1000);
    auto dev2 = std::make_shared<DummyDevice>(2, 0x20000000, 0x2000);

    assert(bus.register_device(dev1));
    assert(bus.register_device(dev2));
    assert(bus.device_count() == 2);

    assert(bus.find_device_by_id(1) == dev1);
    assert(bus.find_device_by_id(2) == dev2);
    assert(bus.find_device_by_id(99) == nullptr);

    assert(bus.is_device_address(0x10000000));
    assert(bus.is_device_address(0x10000FFF));
    assert(!bus.is_device_address(0x10001000));

    assert(bus.is_device_address(0x20000000));
    assert(bus.is_device_address(0x20001FFF));
    assert(!bus.is_device_address(0x20002000));

    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Duplicate ID and Overlapping Range Rejection
// -----------------------------------------------------------------------------
void test_group_2_collision_rejection() {
    std::cout << "[Group 2] Testing Collision Rejection...\n";
    VirtualDeviceBus bus;

    auto dev1 = std::make_shared<DummyDevice>(1, 0x10000000, 0x1000);
    assert(bus.register_device(dev1));

    // Duplicate ID
    auto dev_dup_id = std::make_shared<DummyDevice>(1, 0x30000000, 0x1000);
    assert(!bus.register_device(dev_dup_id));

    // Overlapping ranges:
    // Exact match
    auto dev_exact = std::make_shared<DummyDevice>(2, 0x10000000, 0x1000);
    assert(!bus.register_device(dev_exact));

    // Partial overlap (start inside existing)
    auto dev_overlap_start = std::make_shared<DummyDevice>(3, 0x10000800, 0x1000);
    assert(!bus.register_device(dev_overlap_start));

    // Partial overlap (end inside existing)
    auto dev_overlap_end = std::make_shared<DummyDevice>(4, 0x0FFFF800, 0x1000);
    assert(!bus.register_device(dev_overlap_end));

    // Enclosing range
    auto dev_enclosing = std::make_shared<DummyDevice>(5, 0x0FFFF000, 0x3000);
    assert(!bus.register_device(dev_enclosing));

    assert(bus.device_count() == 1);
    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: MMIO Read and Write Routing
// -----------------------------------------------------------------------------
void test_group_3_mmio_routing() {
    std::cout << "[Group 3] Testing MMIO Read/Write Routing...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<DummyDevice>(1, 0x40000000, 0x1000);
    assert(bus.register_device(dev));

    // Write to register 0x00
    bus.write_mmio(0x40000000, 0x12345678, 4);
    assert(dev->writes_count == 1);
    assert(dev->reg0_ == 0x12345678);

    // Read back from register 0x00
    const std::uint64_t val = bus.read_mmio(0x40000000, 4);
    assert(dev->reads_count == 1);
    assert(val == 0x12345678);

    // Write to register 0x08
    bus.write_mmio(0x40000008, 0xDEADBEEFCAFE, 8);
    assert(dev->reg1_ == 0xDEADBEEFCAFE);
    assert(bus.read_mmio(0x40000008, 8) == 0xDEADBEEFCAFE);

    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Misaligned MMIO Access Rejection
// -----------------------------------------------------------------------------
void test_group_4_alignment_checks() {
    std::cout << "[Group 4] Testing Misaligned MMIO Access Rejection...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<DummyDevice>(1, 0x50000000, 0x1000);
    assert(bus.register_device(dev));

    // 2-byte access to odd address
    bus.write_mmio(0x50000001, 0xAA, 2);
    assert(dev->writes_count == 0); // Must be rejected before touching device!

    // 4-byte access to non-4-aligned address
    bus.write_mmio(0x50000002, 0xBBBB, 4);
    assert(dev->writes_count == 0);

    // 8-byte access to non-8-aligned address
    bus.write_mmio(0x50000004, 0xCCCCCCCC, 8);
    assert(dev->writes_count == 0);

    // Read alignment rejection
    assert(bus.read_mmio(0x50000003, 4) == 0);
    assert(dev->reads_count == 0);

    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: Device Status, Enablement, and Reset
// -----------------------------------------------------------------------------
void test_group_5_status_and_reset() {
    std::cout << "[Group 5] Testing Device Status and Reset...\n";
    VirtualDeviceBus bus;

    auto dev = std::make_shared<DummyDevice>(1, 0x60000000, 0x1000);
    assert(bus.register_device(dev));

    // Disable device
    dev->set_enabled(false);
    assert(!dev->is_enabled());

    // Access to disabled device is rejected
    bus.write_mmio(0x60000000, 0x999, 4);
    assert(dev->writes_count == 0);
    assert(bus.read_mmio(0x60000000, 4) == 0);
    assert(dev->reads_count == 0);

    // Re-enable device
    dev->set_enabled(true);
    assert(dev->is_enabled());
    bus.write_mmio(0x60000000, 0x777, 4);
    assert(dev->writes_count == 1);
    assert(bus.read_mmio(0x60000000, 4) == 0x777);

    // Reset all
    bus.reset_all();
    assert(dev->reg0_ == 0);
    assert(dev->reg1_ == 0);

    // Unregister
    assert(bus.unregister_device(1));
    assert(bus.device_count() == 0);
    assert(!bus.is_device_address(0x60000000));

    std::cout << "  -> Group 5 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 6: Capability Gate Enforcement
// -----------------------------------------------------------------------------
void test_group_6_capability_gate() {
    std::cout << "[Group 6] Testing Capability Gate Enforcement...\n";
    VirtualDeviceCapabilities caps;
    caps.allow_virtual_devices = false; // Disallowed
    VirtualDeviceBus bus(caps);

    auto dev = std::make_shared<DummyDevice>(1, 0x70000000, 0x1000);
    assert(bus.register_device(dev));

    // Access rejected when allow_virtual_devices is false
    bus.write_mmio(0x70000000, 0x555, 4);
    assert(dev->writes_count == 0);
    assert(bus.read_mmio(0x70000000, 4) == 0);
    assert(dev->reads_count == 0);

    std::cout << "  -> Group 6 PASS\n";
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual Device Bus Tests               \n";
    std::cout << "===================================================================\n\n";

    test_group_1_registration();
    test_group_2_collision_rejection();
    test_group_3_mmio_routing();
    test_group_4_alignment_checks();
    test_group_5_status_and_reset();
    test_group_6_capability_gate();

    std::cout << "\n>>> ALL VIRTUAL DEVICE BUS TEST GROUPS PASSED <<<\n";
    return 0;
}
