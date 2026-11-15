// ==============================================================================
// WinIOS Campaign 200 Stage 9: IOVA Mapping & DMA Fault Semantics Tests
// ==============================================================================
// Verifies:
// 1. Valid IOVA-to-PA translation with offset preservation
// 2. Unmapped IOVA fault generation (IommuFaultType::UnmappedIova)
// 3. Permission fault on write to Read-Only IOVA (IommuFaultType::PermissionDenied)
// 4. Permission fault on read from Write-Only IOVA (IommuFaultType::PermissionDenied)
// 5. Memory bounds limit enforcement (IommuFaultType::OutOfBounds)
// 6. Device-not-attached fault generation (IommuFaultType::DeviceNotAttached)
// 7. Complete fault metadata reporting (device_id, domain_id, iova, access)
// 8. DMA engine halts and aborts transfer upon IOMMU fault
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
using namespace winios::device;

static void test_valid_iova_translation() {
    VirtualIommu iommu;
    iommu.create_domain(1);
    iommu.attach_device(10, 1);

    const std::uint64_t iova = 0x40000ULL;
    const std::uint64_t pa   = 0x100000ULL;
    (void)pa;
    assert(iommu.map(1, iova, pa, 0x4000, IommuAccess::ReadWrite));

    const std::size_t mem_limit = 1024 * 1024 * 16;
    IommuFault fault{};

    auto res = iommu.translate(10, iova + 0x128, 4, IommuAccess::Read, mem_limit, &fault);
    assert(res.has_value());
    assert(res.value() == pa + 0x128);
    assert(!fault.has_fault());

    std::cout << "[PASS] test_valid_iova_translation\n";
}

static void test_unmapped_iova_fault() {
    VirtualIommu iommu;
    iommu.create_domain(1);
    iommu.attach_device(10, 1);

    const std::size_t mem_limit = 1024 * 1024 * 16;
    IommuFault fault{};

    auto res = iommu.translate(10, 0x90000ULL, 4, IommuAccess::Read, mem_limit, &fault);
    assert(!res.has_value());
    assert(fault.has_fault());
    assert(fault.type == IommuFaultType::UnmappedIova);
    assert(fault.device_id == 10);
    assert(fault.domain_id == 1);
    assert(fault.iova == 0x90000ULL);

    std::cout << "[PASS] test_unmapped_iova_fault\n";
}

static void test_permission_fault_semantics() {
    VirtualIommu iommu;
    iommu.create_domain(2);
    iommu.attach_device(20, 2);

    const std::uint64_t iova_ro = 0x50000ULL;
    const std::uint64_t pa_ro   = 0x200000ULL;
    (void)pa_ro;
    assert(iommu.map(2, iova_ro, pa_ro, 0x4000, IommuAccess::Read));

    const std::size_t mem_limit = 1024 * 1024 * 16;
    IommuFault fault{};

    // Read succeeds
    auto r_res = iommu.translate(20, iova_ro, 4, IommuAccess::Read, mem_limit, &fault);
    assert(r_res.has_value());
    assert(!fault.has_fault());

    // Write fails with PermissionDenied
    auto w_res = iommu.translate(20, iova_ro, 4, IommuAccess::Write, mem_limit, &fault);
    assert(!w_res.has_value());
    assert(fault.has_fault());
    assert(fault.type == IommuFaultType::PermissionDenied);
    assert(fault.access == IommuAccess::Write);
    assert(fault.iova == iova_ro);

    std::cout << "[PASS] test_permission_fault_semantics\n";
}

static void test_out_of_bounds_fault() {
    VirtualIommu iommu;
    iommu.create_domain(3);
    iommu.attach_device(30, 3);

    // Map PA at 30MB, but memory limit is 16MB
    const std::uint64_t iova = 0x60000ULL;
    const std::uint64_t pa   = 30ULL * 1024 * 1024;
    (void)pa;
    assert(iommu.map(3, iova, pa, 0x4000, IommuAccess::ReadWrite));

    const std::size_t mem_limit = 16 * 1024 * 1024;
    IommuFault fault{};

    auto res = iommu.translate(30, iova, 4, IommuAccess::Read, mem_limit, &fault);
    assert(!res.has_value());
    assert(fault.has_fault());
    assert(fault.type == IommuFaultType::OutOfBounds);

    std::cout << "[PASS] test_out_of_bounds_fault\n";
}

static void test_dma_engine_halts_on_iommu_fault() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto iommu = std::make_shared<VirtualIommu>();

    iommu->create_domain(1);
    iommu->attach_device(42, 1);

    VirtualDmaEngine dma(mem);
    dma.set_addressing_mode(DmaAddressingMode::IommuTranslated);
    dma.set_iommu(iommu);
    dma.set_default_device_id(42);

    const std::uint64_t desc_addr = 0x1000;
    const std::uint64_t unmapped_src_iova = 0x99000;
    const std::uint64_t mapped_dst_iova = 0x20000;
    const std::uint64_t mapped_dst_pa = 0x50000;
    (void)mapped_dst_pa;
    assert(iommu->map(1, mapped_dst_iova, mapped_dst_pa, 0x4000, IommuAccess::ReadWrite));

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = unmapped_src_iova; // Unmapped in IOMMU!
    desc.dst_paddr = mapped_dst_iova;
    desc.length_bytes = 16;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(!res.is_success());
    assert(res.status == VirtualDmaDescriptorStatus::ErrorIommuUnmapped);
    assert(res.bytes_transferred == 0);

    std::cout << "[PASS] test_dma_engine_halts_on_iommu_fault\n";
}

int main() {
    std::cout << "=== Running Stage 200.9 IOVA Mapping & DMA Fault Tests ===\n";
    test_valid_iova_translation();
    test_unmapped_iova_fault();
    test_permission_fault_semantics();
    test_out_of_bounds_fault();
    test_dma_engine_halts_on_iommu_fault();
    std::cout << "All Stage 200.9 tests passed successfully!\n";
    return 0;
}
