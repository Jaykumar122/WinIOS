// ============================================================================
// Stage 199.14: Multicore Interrupt and Device Integration Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_dma_types.h"
#include "device/virtual_iommu.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;

// 1. Validate DMA completion interrupt triggering SPI line in GICv3
static bool test_dma_completion_interrupt_basic() {
    auto gic = std::make_shared<GICv3>();
    gic->reset();
    gic->set_core_count(2);

    // Configure GIC Distributor: Enable Group 1
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x2u, 4);

    // Setup SPI 48: Group 1, Enabled, Priority 0x40
    // INTID 48 is in word 1 of SPIs (word 1 in GICD_IGROUPR is offset 0x0084)
    // Bit index in word 1 is 48 - 32 = 16
    gic->write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 16u), 4); // Group 1
    gic->write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 16u), 4); // ISENABLER1
    gic->write_mmio(GICv3::kGicdBase + 0x0400u + 48u, 0x40u, 1); // Priority 0x40

    // Core 0 CPU interface
    gic->write_icc_igrpen1(1, 0);
    gic->write_icc_pmr(0xFFu, 0);

    // Shared memory & DMA engine
    auto mem = std::make_shared<SmpSharedMemory>(0x100000); // 1 MiB
    VirtualDmaEngine dma(mem);
    dma.set_gic(gic, 48);
    TEST_ASSERT(dma.is_completion_irq_enabled(), "DMA completion IRQ must be enabled");
    TEST_ASSERT(dma.completion_irq() == 48, "DMA completion IRQ must be 48");

    // Prepare a DMA copy descriptor with kDmaFlagInterruptOnComplete
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x1000;
    desc.dst_paddr = 0x2000;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;

    // Write test pattern into source
    std::uint8_t src_data[64];
    for (std::size_t i = 0; i < 64; ++i) src_data[i] = static_cast<std::uint8_t>(0xA0 + i);
    mem->write_bytes(0, 0x1000, src_data, 64);

    // Place descriptor in memory at 0x8000
    std::uint8_t desc_bytes[sizeof(VirtualDmaDescriptor)];
    std::memcpy(desc_bytes, &desc, sizeof(desc));
    mem->write_bytes(0, 0x8000, desc_bytes, sizeof(desc));

    // Initially no interrupt
    auto sig_pre = gic->evaluate(1, 0, 0);
    TEST_ASSERT(!sig_pre.has_interrupt, "No interrupt should be pending before DMA execution");

    // Execute descriptor chain
    auto res = dma.execute_descriptor_chain(0x8000, 0);
    TEST_ASSERT(res.is_success(), "DMA copy must succeed");

    // Interrupt should now be signaled on Core 0!
    auto sig_post = gic->evaluate(1, 0, 0);
    TEST_ASSERT(sig_post.has_interrupt, "Interrupt must be signaled on completion");
    TEST_ASSERT(sig_post.intid == 48u, "Signaled INTID must be 48");
    TEST_ASSERT(!sig_post.is_fiq, "SPI 48 must be Group 1 (IRQ)");

    // Acknowledge interrupt
    std::uint32_t ack = gic->acknowledge_group1(1, 0);
    TEST_ASSERT(ack == 48u, "Acknowledged INTID must be 48");

    // Acknowledge DMA engine line
    dma.acknowledge_completion_irq();
    gic->end_of_interrupt_group1(48, 1, 0);

    // Verify interrupt is now cleared
    auto sig_cleared = gic->evaluate(1, 0, 0);
    TEST_ASSERT(!sig_cleared.has_interrupt, "Interrupt must be cleared after ack & EOI");

    std::cout << "[PASS] test_dma_completion_interrupt_basic\n";
    return true;
}

// 2. Validate multicore affinity routing across 8 cores in SMP cluster
static bool test_dma_multicore_affinity_routing() {
    auto gic = std::make_shared<GICv3>();
    gic->reset();
    gic->set_core_count(8);

    // Enable Group 1 in Distributor
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x2u, 4);

    // Setup SPI 50: Group 1, Enabled, Priority 0x30
    // INTID 50 word 1 bit 18
    gic->write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 18u), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 18u), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0400u + 50u, 0x30u, 1);

    // Configure all 8 cores CPU interfaces
    for (std::size_t c = 0; c < 8; ++c) {
        gic->write_icc_igrpen1(1, c);
        gic->write_icc_pmr(0xFFu, c);
    }

    // Configure GICD_IROUTER for SPI 50 (INTID 50 - 32 = index 18)
    // Route exclusively to Core 5 (Affinity 0.0.0.5)
    // GICD_IROUTER offset for SPIs starts at 0x6100: 0x6100 + index * 8
    std::uint64_t target_core = 5;
    gic->write_mmio(GICv3::kGicdBase + 0x6100u + 18u * 8u, target_core, 8);

    // Shared memory and DMA engine
    auto mem = std::make_shared<SmpSharedMemory>(0x200000);
    VirtualDmaEngine dma(mem);
    dma.set_gic(gic, 50);

    // Prepare DMA Fill descriptor
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 2;
    desc.dst_paddr = 0x10000;
    desc.length_bytes = 128;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xDEADBEEFu;
    desc.flags = kDmaFlagInterruptOnComplete;

    std::uint8_t desc_bytes[sizeof(VirtualDmaDescriptor)];
    std::memcpy(desc_bytes, &desc, sizeof(desc));
    mem->write_bytes(0, 0x9000, desc_bytes, sizeof(desc));

    // Submit DMA from Core 1
    auto res = dma.execute_descriptor_chain(0x9000, 1);
    TEST_ASSERT(res.is_success(), "DMA fill must succeed");

    // Check all cores: ONLY Core 5 should have received the interrupt!
    for (std::size_t c = 0; c < 8; ++c) {
        auto sig = gic->evaluate(1, 0, c);
        if (c == 5) {
            TEST_ASSERT(sig.has_interrupt, "Target Core 5 must receive interrupt");
            TEST_ASSERT(sig.intid == 50u, "Core 5 received intid must be 50");
        } else {
            TEST_ASSERT(!sig.has_interrupt, "Non-targeted core must not receive interrupt");
        }
    }

    // Acknowledge on Core 5
    std::uint32_t ack = gic->acknowledge_group1(1, 5);
    TEST_ASSERT(ack == 50u, "Core 5 ack must be 50");
    dma.acknowledge_completion_irq();
    gic->end_of_interrupt_group1(50, 1, 5);

    // Verify now cleared on Core 5
    auto sig5_cleared = gic->evaluate(1, 0, 5);
    TEST_ASSERT(!sig5_cleared.has_interrupt, "Core 5 interrupt must be cleared");

    std::cout << "[PASS] test_dma_multicore_affinity_routing\n";
    return true;
}

// 3. Validate IOMMU translated DMA submission with interrupt signaling
static bool test_dma_iommu_translated_completion_interrupt() {
    auto gic = std::make_shared<GICv3>();
    gic->reset();
    gic->set_core_count(4);

    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x2u, 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0084u, (1u << 16u), 4); // SPI 48 Grp 1
    gic->write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 16u), 4); // SPI 48 Enable
    gic->write_mmio(GICv3::kGicdBase + 0x0400u + 48u, 0x20u, 1); // Priority 0x20

    // Target Core 2
    gic->write_mmio(GICv3::kGicdBase + 0x6100u + 16u * 8u, 2ULL, 8);
    gic->write_icc_igrpen1(1, 2);
    gic->write_icc_pmr(0xFFu, 2);

    auto mem = std::make_shared<SmpSharedMemory>(0x200000);
    auto iommu = std::make_shared<VirtualIommu>();
    TEST_ASSERT(iommu->create_domain(10), "Domain 10 creation must succeed");
    TEST_ASSERT(iommu->attach_device(1, 10), "Attaching device 1 to domain 10 must succeed");

    // Map IOVA 0x40000 -> PA 0x10000 (Read) and IOVA 0x50000 -> PA 0x20000 (Write)
    TEST_ASSERT(iommu->map(10, 0x40000, 0x10000, 0x4000, IommuAccess::Read), "Map Read must succeed");
    TEST_ASSERT(iommu->map(10, 0x50000, 0x20000, 0x4000, IommuAccess::ReadWrite), "Map ReadWrite must succeed");

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_addressing_mode(DmaAddressingMode::IommuTranslated);
    dma.set_default_device_id(1);
    dma.set_gic(gic, 48);

    // Initialize source data
    std::uint8_t pattern[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    mem->write_bytes(0, 0x10000, pattern, sizeof(pattern));

    // Create descriptor referencing IOVAs
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 100;
    desc.src_paddr = 0x40000; // IOVA
    desc.dst_paddr = 0x50000; // IOVA
    desc.length_bytes = sizeof(pattern);
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete | kDmaFlagIommuTranslate;

    std::uint8_t desc_bytes[sizeof(VirtualDmaDescriptor)];
    std::memcpy(desc_bytes, &desc, sizeof(desc));
    mem->write_bytes(0, 0x8000, desc_bytes, sizeof(desc));

    auto res = dma.execute_descriptor_chain(0x8000, 0);
    TEST_ASSERT(res.is_success(), "IOMMU translated DMA copy must succeed");

    // Verify interrupt delivered to Core 2
    auto sig = gic->evaluate(1, 0, 2);
    TEST_ASSERT(sig.has_interrupt && sig.intid == 48u, "Core 2 must receive DMA completion IRQ 48");

    // Verify destination data
    std::uint8_t dst_data[16]{};
    mem->read_bytes(0, 0x20000, dst_data, sizeof(dst_data));
    for (std::size_t i = 0; i < 16; ++i) {
        TEST_ASSERT(dst_data[i] == pattern[i], "Data must match copied pattern");
    }

    dma.acknowledge_completion_irq();
    gic->acknowledge_group1(1, 2);
    gic->end_of_interrupt_group1(48, 1, 2);

    std::cout << "[PASS] test_dma_iommu_translated_completion_interrupt\n";
    return true;
}

int main() {
    std::cout << "--- Stage 199.14: Multicore Interrupt and Device Integration Tests ---\n";
    if (!test_dma_completion_interrupt_basic()) return 1;
    if (!test_dma_multicore_affinity_routing()) return 1;
    if (!test_dma_iommu_translated_completion_interrupt()) return 1;

    std::cout << "All Stage 199.14 tests passed successfully!\n";
    return 0;
}
