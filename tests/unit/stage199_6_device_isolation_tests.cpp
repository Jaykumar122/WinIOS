#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
using namespace winios::device;

void test_cross_domain_isolation() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 8); // 8 MB
    auto iommu = std::make_shared<VirtualIommu>();

    // Domain 1: Secure enclave / camera device (ID 10)
    iommu->create_domain(1);
    iommu->attach_device(10, 1);
    const std::uint64_t enclave_iova = 0x10000ULL;
    const std::uint64_t enclave_pa   = 0x400000ULL;
    iommu->map(1, enclave_iova, enclave_pa, 0x4000, IommuAccess::ReadWrite);

    // Write secret data to Enclave PA
    mem->write_u32(0, enclave_pa, 0x5EC07EC1);

    // Domain 2: Untrusted peripheral device (ID 20)
    iommu->create_domain(2);
    iommu->attach_device(20, 2);
    const std::uint64_t untrusted_dst_iova = 0x20000ULL;
    const std::uint64_t untrusted_dst_pa   = 0x600000ULL;
    iommu->map(2, untrusted_dst_iova, untrusted_dst_pa, 0x4000, IommuAccess::ReadWrite);

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(20); // Operating on behalf of untrusted device 20

    // Untrusted device attempts to read enclave_iova (which is only mapped in Domain 1)
    const std::uint64_t desc_addr = 0x2000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 101;
    desc.src_paddr = enclave_iova; // Not mapped in Domain 2
    desc.dst_paddr = untrusted_dst_iova;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_failure());
    assert(res.status == VirtualDmaDescriptorStatus::ErrorIommuUnmapped);

    // Verify untrusted destination buffer was not populated with secret
    assert(mem->read_u32(0, untrusted_dst_pa) == 0);
    // Verify enclave physical buffer remains pristine
    assert(mem->read_u32(0, enclave_pa) == 0x5EC07EC1);

    std::cout << "[PASS] test_cross_domain_isolation" << std::endl;
}

void test_unauthorized_device_rejection() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto iommu = std::make_shared<VirtualIommu>();

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(999); // Device 999 is NOT attached to any domain

    const std::uint64_t desc_addr = 0x3000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 102;
    desc.src_paddr = 0;
    desc.dst_paddr = 0x10000;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_failure());
    assert(res.status == VirtualDmaDescriptorStatus::ErrorIommuNotAttached);

    std::cout << "[PASS] test_unauthorized_device_rejection" << std::endl;
}

void test_safe_unmapping_lifecycle() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto iommu = std::make_shared<VirtualIommu>();
    iommu->create_domain(3);
    iommu->attach_device(30, 3);

    const std::uint64_t iova = 0x30000ULL;
    const std::uint64_t pa   = 0x180000ULL;
    iommu->map(3, iova, pa, 0x4000, IommuAccess::ReadWrite);

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(30);

    const std::uint64_t desc_addr = 0x4000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 103;
    desc.src_paddr = 0;
    desc.dst_paddr = iova;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x42424242;
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    // First run succeeds
    auto res1 = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res1.is_success());
    assert(mem->read_u32(0, pa) == 0x42424242);

    // Unmap the domain page
    iommu->unmap(3, iova, 0x4000);

    // Second run must fail due to unmapping
    desc.fill_pattern = 0x99999999;
    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res2 = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res2.is_failure());
    assert(res2.status == VirtualDmaDescriptorStatus::ErrorIommuUnmapped);

    // Verify physical memory was not overwritten with 0x99999999
    assert(mem->read_u32(0, pa) == 0x42424242);

    std::cout << "[PASS] test_safe_unmapping_lifecycle" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.6 Device Isolation and Domain Protection Tests ---" << std::endl;
    test_cross_domain_isolation();
    test_unauthorized_device_rejection();
    test_safe_unmapping_lifecycle();
    std::cout << "All Stage 199.6 tests PASSED!" << std::endl;
    return 0;
}
