#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "devices/device_types.h"
#include "devices/virtual_device_interface.h"
#include "devices/device_registry.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

class MockDevice : public VirtualDeviceBase {
public:
    MockDevice(uint32_t id, DeviceClass cls, std::string name)
        : VirtualDeviceBase(id, 1, cls, std::move(name), "MockProvider", 0x00010900, 0) {}
};

int main() {
    std::cout << "[Test Stage 225.3] Device Registry & Handle Management...\n" << std::flush;

    auto& registry = DeviceRegistry::get();
    registry.reset();
    TEST_ASSERT(registry.device_count() == 0);

    // 1. Register Mock Devices
    auto dev1 = std::make_shared<MockDevice>(101, DeviceClass::Display, "MockDisplay");
    auto dev2 = std::make_shared<MockDevice>(102, DeviceClass::AudioOutput, "MockAudio");
    auto dev3 = std::make_shared<MockDevice>(103, DeviceClass::Sensor, "MockSensor");

    TEST_ASSERT(registry.register_device(dev1));
    TEST_ASSERT(registry.register_device(dev2));
    TEST_ASSERT(registry.register_device(dev3));
    TEST_ASSERT(registry.device_count() == 3);

    // Duplicate registration should fail
    TEST_ASSERT(!registry.register_device(dev1));

    // 2. Lookup & Handle Verification
    uint64_t h1 = dev1->handle();
    TEST_ASSERT(device_handle_id(h1) == 101);
    TEST_ASSERT(device_handle_gen(h1) == 1);
    TEST_ASSERT(registry.is_handle_valid(h1));

    auto found1 = registry.get_by_handle(h1);
    TEST_ASSERT(found1 != nullptr);
    TEST_ASSERT(found1->name() == "MockDisplay");

    // 3. Enumeration by Class
    auto all_devs = registry.enumerate_all();
    TEST_ASSERT(all_devs.size() == 3);

    auto audio_devs = registry.enumerate_by_class(DeviceClass::AudioOutput);
    TEST_ASSERT(audio_devs.size() == 1);
    TEST_ASSERT(audio_devs[0].device_id == 102);

    // 4. Lifecycle Transitions
    registry.initialize_all();
    TEST_ASSERT(dev1->state() == DeviceState::Ready);
    TEST_ASSERT(dev2->state() == DeviceState::Ready);

    registry.start_all();
    TEST_ASSERT(dev1->state() == DeviceState::Running);
    TEST_ASSERT(dev2->state() == DeviceState::Running);

    registry.suspend_all();
    TEST_ASSERT(dev1->state() == DeviceState::Suspended);

    registry.resume_all();
    TEST_ASSERT(dev1->state() == DeviceState::Running);

    registry.stop_all();
    TEST_ASSERT(dev1->state() == DeviceState::Stopped);

    // 5. Unregistration & ABA Generation Defense
    TEST_ASSERT(registry.unregister_device(101));
    TEST_ASSERT(registry.device_count() == 2);
    TEST_ASSERT(!registry.is_handle_valid(h1)); // Old handle is now invalid
    TEST_ASSERT(dev1->state() == DeviceState::Removed);

    // Re-register same device ID - generation must increment
    auto dev1_new = std::make_shared<MockDevice>(101, DeviceClass::Display, "MockDisplayV2");
    TEST_ASSERT(registry.register_device(dev1_new));
    uint64_t h1_new = dev1_new->handle();
    TEST_ASSERT(device_handle_id(h1_new) == 101);
    TEST_ASSERT(device_handle_gen(h1_new) == 2); // Generation advanced!
    TEST_ASSERT(h1_new != h1);
    TEST_ASSERT(!registry.is_handle_valid(h1)); // Stale handle still rejected
    TEST_ASSERT(registry.is_handle_valid(h1_new)); // New handle valid

    registry.reset();
    std::cout << "[Test Stage 225.3] PASS: Registry & Handle lifecycle verified.\n";
    return 0;
}
