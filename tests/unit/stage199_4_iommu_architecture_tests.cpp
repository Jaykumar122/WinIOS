#include <cassert>
#include <iostream>

#include "device/virtual_iommu.h"

using namespace winios::device;

void test_domain_lifecycle_and_device_attachment() {
    VirtualIommu iommu;

    assert(iommu.create_domain(1));
    assert(!iommu.create_domain(1)); // duplicate rejected
    assert(iommu.has_domain(1));

    assert(iommu.attach_device(100, 1));
    assert(iommu.is_device_attached(100));
    assert(iommu.get_device_domain(100) == 1);

    assert(iommu.detach_device(100));
    assert(!iommu.is_device_attached(100));

    assert(iommu.destroy_domain(1));
    assert(!iommu.has_domain(1));

    std::cout << "[PASS] test_domain_lifecycle_and_device_attachment" << std::endl;
}

void test_translation_and_permissions() {
    VirtualIommu iommu;
    iommu.create_domain(2);
    iommu.attach_device(200, 2);

    const std::uint64_t iova_rw = 0x10000ULL;
    const std::uint64_t pa_rw   = 0x40000ULL;
    assert(iommu.map(2, iova_rw, pa_rw, 0x2000, IommuAccess::ReadWrite));
    (void)pa_rw;

    const std::uint64_t iova_ro = 0x20000ULL;
    const std::uint64_t pa_ro   = 0x60000ULL;
    assert(iommu.map(2, iova_ro, pa_ro, 0x2000, IommuAccess::Read));
    (void)pa_ro;

    const std::size_t mem_limit = 1024 * 1024 * 8; // 8 MB

    // Read RW page
    auto pa1 = iommu.translate(200, iova_rw + 0x50, 4, IommuAccess::Read, mem_limit);
    assert(pa1.has_value());
    assert(pa1.value() == pa_rw + 0x50);

    // Write RW page
    auto pa2 = iommu.translate(200, iova_rw + 0x80, 8, IommuAccess::Write, mem_limit);
    assert(pa2.has_value());
    assert(pa2.value() == pa_rw + 0x80);

    // Read RO page
    auto pa3 = iommu.translate(200, iova_ro + 0x10, 4, IommuAccess::Read, mem_limit);
    assert(pa3.has_value());
    assert(pa3.value() == pa_ro + 0x10);

    // Write RO page -> PermissionDenied fault
    IommuFault fault{};
    auto pa4 = iommu.translate(200, iova_ro + 0x10, 4, IommuAccess::Write, mem_limit, &fault);
    assert(!pa4.has_value());
    assert(fault.type == IommuFaultType::PermissionDenied);
    assert(fault.device_id == 200);
    assert(fault.domain_id == 2);

    std::cout << "[PASS] test_translation_and_permissions" << std::endl;
}

void test_faults_and_overflow() {
    VirtualIommu iommu;
    iommu.create_domain(3);
    const std::size_t mem_limit = 1024 * 1024 * 8;

    // Device not attached
    IommuFault f1{};
    auto res1 = iommu.translate(999, 0x1000, 4, IommuAccess::Read, mem_limit, &f1);
    assert(!res1.has_value());
    assert(f1.type == IommuFaultType::DeviceNotAttached);

    iommu.attach_device(300, 3);

    // Unmapped IOVA
    IommuFault f2{};
    auto res2 = iommu.translate(300, 0x90000ULL, 4, IommuAccess::Read, mem_limit, &f2);
    assert(!res2.has_value());
    assert(f2.type == IommuFaultType::UnmappedIova);

    // Address overflow
    IommuFault f3{};
    auto res3 = iommu.translate(300, 0xFFFFFFFFFFFFFFFFULL, 4, IommuAccess::Read, mem_limit, &f3);
    assert(!res3.has_value());
    assert(f3.type == IommuFaultType::AddressOverflow);

    std::cout << "[PASS] test_faults_and_overflow" << std::endl;
}

void test_iotlb_caching_and_invalidation() {
    VirtualIommu iommu;
    iommu.create_domain(4);
    iommu.attach_device(400, 4);

    const std::uint64_t iova = 0x50000ULL;
    const std::uint64_t pa1  = 0x100000ULL;
    const std::uint64_t pa2  = 0x200000ULL;

    iommu.map(4, iova, pa1, 0x4000, IommuAccess::ReadWrite);

    // First translation -> IOTLB miss
    auto r1 = iommu.translate(400, iova, 64, IommuAccess::Read, 0);
    assert(r1.has_value() && r1.value() == pa1);
    assert(iommu.iotlb_misses() == 1);
    assert(iommu.iotlb_hits() == 0);

    // Second translation -> IOTLB hit
    auto r2 = iommu.translate(400, iova + 16, 64, IommuAccess::Read, 0);
    assert(r2.has_value() && r2.value() == pa1 + 16);
    assert(iommu.iotlb_hits() == 1);

    // Re-map with new PA (this automatically invalidates the domain's IOTLB)
    iommu.map(4, iova, pa2, 0x4000, IommuAccess::ReadWrite);

    // Next translation must produce pa2!
    auto r3 = iommu.translate(400, iova, 64, IommuAccess::Read, 0);
    assert(r3.has_value() && r3.value() == pa2);
    assert(iommu.iotlb_misses() == 2);

    std::cout << "[PASS] test_iotlb_caching_and_invalidation" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.4 Virtual IOMMU Architecture Tests ---" << std::endl;
    test_domain_lifecycle_and_device_attachment();
    test_translation_and_permissions();
    test_faults_and_overflow();
    test_iotlb_caching_and_invalidation();
    std::cout << "All Stage 199.4 tests PASSED!" << std::endl;
    return 0;
}
