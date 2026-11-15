#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
using namespace winios::device;

void test_physical_mode_compatibility() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024); // 1 MB
    VirtualDmaEngine dma(mem);

    assert(dma.addressing_mode() == DmaAddressingMode::Physical);

    const std::uint64_t desc_addr = 0x1000;
    const std::uint64_t src_pa = 0x20000;
    const std::uint64_t dst_pa = 0x30000;

    mem->write_u32(0, src_pa, 0x11223344);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_pa;
    desc.dst_paddr = dst_pa;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0; // Physical mode

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());
    assert(res.bytes_transferred == 4);
    assert(mem->read_u32(0, dst_pa) == 0x11223344);

    std::cout << "[PASS] test_physical_mode_compatibility" << std::endl;
}

void test_iommu_translated_dma_copy_and_fill() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4); // 4 MB
    auto iommu = std::make_shared<VirtualIommu>();
    VirtualIommu& iommu_ref = *iommu;

    iommu_ref.create_domain(1);
    iommu_ref.attach_device(42, 1);

    // Map source IOVA 0x10000 -> PA 0x100000 (Read)
    const std::uint64_t src_iova = 0x10000ULL;
    const std::uint64_t src_pa   = 0x100000ULL;
    assert(iommu_ref.map(1, src_iova, src_pa, 0x4000, IommuAccess::Read));

    // Map destination IOVA 0x20000 -> PA 0x200000 (Write)
    const std::uint64_t dst_iova = 0x20000ULL;
    const std::uint64_t dst_pa   = 0x200000ULL;
    assert(iommu_ref.map(1, dst_iova, dst_pa, 0x4000, IommuAccess::Write));
    (void)dst_pa;

    mem->write_u32(0, src_pa, 0xFEEDFACE);

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(42);

    const std::uint64_t desc_addr = 0x2000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 2;
    desc.src_paddr = src_iova;
    desc.dst_paddr = dst_iova;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());
    assert(mem->read_u32(0, dst_pa) == 0xFEEDFACE);

    // Test Fill through IOMMU
    desc.descriptor_id = 3;
    desc.src_paddr = 0;
    desc.dst_paddr = dst_iova;
    desc.length_bytes = 8;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xAA55AA55;
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res_fill = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res_fill.is_success());
    assert(mem->read_u32(0, dst_pa) == 0xAA55AA55);
    assert(mem->read_u32(0, dst_pa + 4) == 0xAA55AA55);

    std::cout << "[PASS] test_iommu_translated_dma_copy_and_fill" << std::endl;
}

void test_iommu_fault_handling() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto iommu = std::make_shared<VirtualIommu>();
    iommu->create_domain(2);
    iommu->attach_device(50, 2);

    // Map IOVA as Read-Only
    const std::uint64_t ro_iova = 0x30000ULL;
    const std::uint64_t ro_pa   = 0x150000ULL;
    iommu->map(2, ro_iova, ro_pa, 0x4000, IommuAccess::Read);

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(50);

    const std::uint64_t desc_addr = 0x3000;

    // Attempting to write to Read-Only destination IOVA
    VirtualDmaDescriptor desc_ro{};
    desc_ro.descriptor_id = 4;
    desc_ro.src_paddr = 0;
    desc_ro.dst_paddr = ro_iova;
    desc_ro.length_bytes = 4;
    desc_ro.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc_ro.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc_ro, sizeof(desc_ro));

    auto res_ro = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res_ro.is_failure());
    assert(res_ro.status == VirtualDmaDescriptorStatus::ErrorIommuPermission);

    // Descriptor status in memory should be updated
    VirtualDmaDescriptor updated{};
    bool read_ok = dma.read_descriptor(desc_addr, updated, 0);
    assert(read_ok);
    (void)read_ok;
    assert(updated.status == static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::ErrorIommuPermission));

    // Attempting to read from unmapped IOVA
    VirtualDmaDescriptor desc_unmapped{};
    desc_unmapped.descriptor_id = 5;
    desc_unmapped.src_paddr = 0x990000ULL; // unmapped
    desc_unmapped.dst_paddr = ro_pa;
    desc_unmapped.length_bytes = 4;
    desc_unmapped.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc_unmapped.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc_unmapped, sizeof(desc_unmapped));

    auto res_unmapped = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res_unmapped.is_failure());
    assert(res_unmapped.status == VirtualDmaDescriptorStatus::ErrorIommuUnmapped);

    std::cout << "[PASS] test_iommu_fault_handling" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.5 DMA Address Translation Tests ---" << std::endl;
    test_physical_mode_compatibility();
    test_iommu_translated_dma_copy_and_fill();
    test_iommu_fault_handling();
    std::cout << "All Stage 199.5 tests PASSED!" << std::endl;
    return 0;
}
