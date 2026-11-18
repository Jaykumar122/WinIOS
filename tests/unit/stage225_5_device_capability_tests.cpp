#include <iostream>
#include <cstdint>
#include <vector>
#include <string>

#include "security/capability_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::security;

int main() {
    std::cout << "[Test Stage 225.5] Device Identity & Capability Integration...\n" << std::flush;

    CapabilityManager cm;
    const uint32_t pid = 77;
    cm.bind_identity(pid, "com.winios.devicetest");

    // Initially has no device capabilities
    TEST_ASSERT(!cm.check(pid, Capability::DeviceEnumerate));
    TEST_ASSERT(!cm.check(pid, Capability::AudioOutput));
    TEST_ASSERT(!cm.check(pid, Capability::SensorAccess));
    TEST_ASSERT(!cm.check(pid, Capability::NetworkAccess));
    TEST_ASSERT(!cm.check(pid, Capability::NotificationPost));

    // Grant DeviceEnumerate & AudioOutput
    cm.grant(pid, Capability::DeviceEnumerate);
    cm.grant(pid, Capability::AudioOutput);

    TEST_ASSERT(cm.check(pid, Capability::DeviceEnumerate));
    TEST_ASSERT(cm.check(pid, Capability::AudioOutput));
    TEST_ASSERT(!cm.check(pid, Capability::SensorAccess));

    // Test permission name parsing
    TEST_ASSERT(CapabilityManager::parse_capability_name("device.enum") == Capability::DeviceEnumerate);
    TEST_ASSERT(CapabilityManager::parse_capability_name("device.input") == Capability::DeviceInput);
    TEST_ASSERT(CapabilityManager::parse_capability_name("audio.out") == Capability::AudioOutput);
    TEST_ASSERT(CapabilityManager::parse_capability_name("audio.in") == Capability::AudioInput);
    TEST_ASSERT(CapabilityManager::parse_capability_name("sensor") == Capability::SensorAccess);
    TEST_ASSERT(CapabilityManager::parse_capability_name("network") == Capability::NetworkAccess);
    TEST_ASSERT(CapabilityManager::parse_capability_name("notification") == Capability::NotificationPost);

    // Grant named permissions
    cm.grant_named(pid, {"sensor", "network", "notification"});
    TEST_ASSERT(cm.check(pid, Capability::SensorAccess));
    TEST_ASSERT(cm.check(pid, Capability::NetworkAccess));
    TEST_ASSERT(cm.check(pid, Capability::NotificationPost));

    // Check audit stats
    auto stats = cm.get_audit_stats();
    TEST_ASSERT(stats.total_checks > 0);

    std::cout << "[Test Stage 225.5] PASS: Device capabilities verified.\n";
    return 0;
}
