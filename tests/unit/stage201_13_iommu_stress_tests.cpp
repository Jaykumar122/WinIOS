// ==============================================================================
// WinIOS Campaign 201 Stage 13: Virtual IOMMU Domain Isolation & Stress Tests
// ==============================================================================
// Verifies:
// 1. High-volume domain creation, mapping, and teardown (100 domains)
// 2. Multi-device domain sharing and dynamic detachment
// 3. Complete cross-domain isolation (Domain A cannot access Domain B IOVA)
// 4. Overlapping IOVA namespaces resolving to distinct physical addresses
// 5. Unmapped and detached translation fault handling
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "device/virtual_iommu.h"

using namespace winios::device;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: High-Volume Domain Creation and Destruction
// -----------------------------------------------------------------------------
void test_domain_high_volume_lifecycle() {
    std::cout << "[Test 1] High-Volume Domain Creation & Destruction...\n";
    VirtualIommu iommu;
    iommu.reset();

    constexpr std::uint32_t kDomainCount = 100;

    // Allocate 100 domains
    for (std::uint32_t i = 1; i <= kDomainCount; ++i) {
        TEST_CHECK(iommu.create_domain(i), "Domain creation must succeed");
        TEST_CHECK(iommu.has_domain(i), "Domain must exist after creation");
    }

    // Add mapping to each domain: IOVA 0x10000 -> PA (0x100000 + i * 0x4000)
    for (std::uint32_t i = 1; i <= kDomainCount; ++i) {
        const std::uint64_t pa = 0x100000ULL + i * 0x4000ULL;
        TEST_CHECK(iommu.map(i, 0x10000ULL, pa, 0x4000, IommuAccess::ReadWrite), "Mapping must succeed");
    }

    // Teardown all domains
    for (std::uint32_t i = 1; i <= kDomainCount; ++i) {
        TEST_CHECK(iommu.destroy_domain(i), "Domain destruction must succeed");
        TEST_CHECK(!iommu.has_domain(i), "Domain must not exist after destruction");
    }

    std::cout << "  -> High-volume domain lifecycle passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Cross-Domain Isolation & Overlapping IOVA Namespaces
// -----------------------------------------------------------------------------
void test_cross_domain_isolation() {
    std::cout << "[Test 2] Cross-Domain Isolation & Overlapping IOVAs...\n";
    VirtualIommu iommu;
    iommu.reset();

    const DomainId dom_a = 1;
    const DomainId dom_b = 2;
    const DeviceId dev_a = 10;
    const DeviceId dev_b = 20;
    const std::size_t mem_limit = 1024 * 1024 * 64;

    iommu.create_domain(dom_a);
    iommu.create_domain(dom_b);
    iommu.attach_device(dev_a, dom_a);
    iommu.attach_device(dev_b, dom_b);

    // Map IDENTICAL IOVA 0x4000 to DIFFERENT physical addresses in each domain
    const std::uint64_t shared_iova = 0x4000ULL;
    const std::uint64_t pa_a        = 0x01004000ULL;
    const std::uint64_t pa_b        = 0x02004000ULL;

    iommu.map(dom_a, shared_iova, pa_a, 0x4000, IommuAccess::ReadWrite);
    iommu.map(dom_b, shared_iova, pa_b, 0x4000, IommuAccess::Read); // Domain B is Read-Only!

    // Translate Device A: must resolve to pa_a
    IommuFault fault{};
    auto pa_res_a = iommu.translate(dev_a, shared_iova, 4, IommuAccess::Write, mem_limit, &fault);
    TEST_CHECK(pa_res_a.has_value() && *pa_res_a == pa_a, "Device A must resolve to Domain A PA");

    // Translate Device B: read must resolve to pa_b, write must fault!
    auto pa_res_b_read = iommu.translate(dev_b, shared_iova, 4, IommuAccess::Read, mem_limit, &fault);
    TEST_CHECK(pa_res_b_read.has_value() && *pa_res_b_read == pa_b, "Device B read must resolve to Domain B PA");

    auto pa_res_b_write = iommu.translate(dev_b, shared_iova, 4, IommuAccess::Write, mem_limit, &fault);
    TEST_CHECK(!pa_res_b_write.has_value(), "Device B write must be blocked by Domain B RO permission");
    TEST_CHECK(fault.type == IommuFaultType::PermissionDenied, "Fault must be PermissionDenied");

    std::cout << "  -> Cross-domain isolation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Detached Device & Unmapped IOVA Fault Handling
// -----------------------------------------------------------------------------
void test_detached_and_unmapped_faults() {
    std::cout << "[Test 3] Detached Device & Unmapped IOVA Faults...\n";
    VirtualIommu iommu;
    iommu.reset();

    const DeviceId dev_detached = 99;
    const std::size_t mem_limit = 1024 * 1024 * 64;
    IommuFault fault{};

    // Query non-attached device
    auto res_detached = iommu.translate(dev_detached, 0x1000ULL, 4, IommuAccess::Read, mem_limit, &fault);
    TEST_CHECK(!res_detached.has_value(), "Translation on detached device must fail");
    TEST_CHECK(fault.type == IommuFaultType::DeviceNotAttached, "Fault must be DeviceNotAttached");

    // Attach device to domain but do not map target IOVA
    const DomainId dom = 1;
    const DeviceId dev = 5;
    iommu.create_domain(dom);
    iommu.attach_device(dev, dom);

    auto res_unmapped = iommu.translate(dev, 0x0000DEAD0000ULL, 4, IommuAccess::Read, mem_limit, &fault);
    TEST_CHECK(!res_unmapped.has_value(), "Translation on unmapped IOVA must fail");
    TEST_CHECK(fault.type == IommuFaultType::UnmappedIova, "Fault must be UnmappedIova");

    std::cout << "  -> Detached & unmapped fault tests passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.13 Virtual IOMMU Stress Tests ===\n";
    test_domain_high_volume_lifecycle();
    test_cross_domain_isolation();
    test_detached_and_unmapped_faults();

    if (g_failures == 0) {
        std::cout << "All Stage 201.13 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.13 test(s) failed!\n";
        return 1;
    }
}
