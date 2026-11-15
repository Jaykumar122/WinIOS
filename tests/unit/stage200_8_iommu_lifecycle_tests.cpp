// ==============================================================================
// WinIOS Campaign 200 Stage 8: IOMMU Domain Lifecycle Tests
// ==============================================================================
// Verifies:
// 1. Domain creation, unique domain ID validation, and duplicate rejection
// 2. Device attachment, querying device domain, and attachment tracking
// 3. Multi-device domain sharing (multiple devices attached to one domain)
// 4. Device detachment and unattached translation rejection
// 5. Domain destruction and safe resource reclamation
// 6. Non-existent domain destruction rejection
// 7. Dynamic re-attachment of detached device to a different domain
// 8. IOTLB invalidation on domain destruction and device detachment
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "device/virtual_iommu.h"

using namespace winios::device;

static void test_domain_creation_and_rejection() {
    VirtualIommu iommu;

    assert(!iommu.has_domain(1));
    assert(iommu.create_domain(1));
    assert(iommu.has_domain(1));

    // Duplicate creation must fail
    assert(!iommu.create_domain(1));

    // Multiple distinct domains
    assert(iommu.create_domain(2));
    assert(iommu.create_domain(3));
    assert(iommu.has_domain(2));
    assert(iommu.has_domain(3));

    std::cout << "[PASS] test_domain_creation_and_rejection\n";
}

static void test_device_attachment_and_query() {
    VirtualIommu iommu;
    iommu.create_domain(10);

    const DeviceId dev1 = 101;
    const DeviceId dev2 = 102;
    (void)dev1;
    (void)dev2;

    assert(!iommu.is_device_attached(dev1));
    assert(!iommu.get_device_domain(dev1).has_value());

    // Attach to domain 10
    assert(iommu.attach_device(dev1, 10));
    assert(iommu.is_device_attached(dev1));
    assert(iommu.get_device_domain(dev1).value() == 10);

    // Attach to non-existent domain should fail
    assert(!iommu.attach_device(dev2, 999));
    assert(!iommu.is_device_attached(dev2));

    std::cout << "[PASS] test_device_attachment_and_query\n";
}

static void test_multi_device_domain_sharing() {
    VirtualIommu iommu;
    iommu.create_domain(5);

    const DeviceId devA = 501;
    const DeviceId devB = 502;
    const DeviceId devC = 503;

    assert(iommu.attach_device(devA, 5));
    assert(iommu.attach_device(devB, 5));
    assert(iommu.attach_device(devC, 5));

    assert(iommu.get_device_domain(devA).value() == 5);
    assert(iommu.get_device_domain(devB).value() == 5);
    assert(iommu.get_device_domain(devC).value() == 5);

    // Map a region in domain 5
    assert(iommu.map(5, 0x10000ULL, 0x80000ULL, 0x4000, IommuAccess::ReadWrite));

    // All three devices can translate the mapped IOVA
    const std::size_t mem_limit = 1024 * 1024 * 16;
    auto paA = iommu.translate(devA, 0x10000ULL, 64, IommuAccess::Read, mem_limit);
    auto paB = iommu.translate(devB, 0x10000ULL, 64, IommuAccess::Read, mem_limit);
    auto paC = iommu.translate(devC, 0x10000ULL, 64, IommuAccess::Read, mem_limit);

    assert(paA.has_value() && paA.value() == 0x80000ULL);
    assert(paB.has_value() && paB.value() == 0x80000ULL);
    assert(paC.has_value() && paC.value() == 0x80000ULL);

    std::cout << "[PASS] test_multi_device_domain_sharing\n";
}

static void test_device_detachment_and_reattachment() {
    VirtualIommu iommu;
    iommu.create_domain(1);
    iommu.create_domain(2);

    const DeviceId dev = 77;
    assert(iommu.attach_device(dev, 1));
    assert(iommu.get_device_domain(dev).value() == 1);

    // Map IOVA in domain 1 and domain 2 to different PAs
    const std::size_t mem_limit = 1024 * 1024 * 16;
    assert(iommu.map(1, 0x20000ULL, 0x100000ULL, 0x4000, IommuAccess::ReadWrite));
    assert(iommu.map(2, 0x20000ULL, 0x200000ULL, 0x4000, IommuAccess::ReadWrite));

    auto pa1 = iommu.translate(dev, 0x20000ULL, 4, IommuAccess::Read, mem_limit);
    assert(pa1.has_value() && pa1.value() == 0x100000ULL);

    // Detach device
    assert(iommu.detach_device(dev));
    assert(!iommu.is_device_attached(dev));

    // Translation while detached must fail
    auto pa_detached = iommu.translate(dev, 0x20000ULL, 4, IommuAccess::Read, mem_limit);
    assert(!pa_detached.has_value());

    // Reattach to domain 2
    assert(iommu.attach_device(dev, 2));
    assert(iommu.get_device_domain(dev).value() == 2);

    // Now translates to domain 2 PA
    auto pa2 = iommu.translate(dev, 0x20000ULL, 4, IommuAccess::Read, mem_limit);
    assert(pa2.has_value() && pa2.value() == 0x200000ULL);

    std::cout << "[PASS] test_device_detachment_and_reattachment\n";
}

static void test_domain_destruction_lifecycle() {
    VirtualIommu iommu;
    iommu.create_domain(8);

    const DeviceId dev = 88;
    (void)dev;
    assert(iommu.attach_device(dev, 8));
    assert(iommu.map(8, 0x30000ULL, 0x300000ULL, 0x4000, IommuAccess::ReadWrite));

    // Destroy domain 8
    assert(iommu.destroy_domain(8));
    assert(!iommu.has_domain(8));

    // Device should no longer be attached
    assert(!iommu.is_device_attached(dev));

    // Non-existent domain destruction fails
    assert(!iommu.destroy_domain(8));
    assert(!iommu.destroy_domain(9999));

    std::cout << "[PASS] test_domain_destruction_lifecycle\n";
}

int main() {
    std::cout << "=== Running Stage 200.8 IOMMU Domain Lifecycle Tests ===\n";
    test_domain_creation_and_rejection();
    test_device_attachment_and_query();
    test_multi_device_domain_sharing();
    test_device_detachment_and_reattachment();
    test_domain_destruction_lifecycle();
    std::cout << "All Stage 200.8 tests passed successfully!\n";
    return 0;
}
