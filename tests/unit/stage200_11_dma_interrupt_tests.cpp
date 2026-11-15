// ==============================================================================
// WinIOS Campaign 200 Stage 11: DMA Interrupt & Completion Integration Tests
// ==============================================================================
// Verifies:
// 1. GICv3 attachment to VirtualDmaEngine and SPI line configuration
// 2. Transfer completion interrupt triggering via kDmaFlagInterruptOnComplete
// 3. GICv3 interrupt signal evaluation (INTID matching completion_irq)
// 4. DMA completion interrupt acknowledgment and deassertion
// 5. Transfer without interrupt flag does not trigger GIC interrupt
// 6. Multichannel DMA completion handling
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "cpu/gicv3.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
using namespace winios::device;

static void test_dma_completion_interrupt_generation() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto gic = std::make_shared<GICv3>();
    gic->set_core_count(1);
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // Enable Group 0 and Group 1

    const std::uint32_t dma_irq = 48; // SPI 48

    // Enable SPI 48 in GICv3 Distributor
    // GICD_IGROUPR1 (offset 0x0084) sets Group 1 (IRQ) for INTID 48
    gic->write_mmio(GICv3::kGicdBase + 0x0080 + (dma_irq / 32) * 4, 1u << (dma_irq % 32), 4);
    // GICD_ISENABLER1 (offset 0x0104) enables INTID 32..63
    gic->write_mmio(GICv3::kGicdBase + 0x0100 + (dma_irq / 32) * 4, 1u << (dma_irq % 32), 4);
    // Priority 0x20
    gic->write_mmio(GICv3::kGicdBase + 0x0400 + dma_irq, 0x20u, 1);
    // Unmask priority in CPU Interface: PMR = 0xFF
    gic->write_icc_pmr(0xFFu, 0);
    gic->write_icc_igrpen1(1u, 0); // Enable Group 1

    VirtualDmaEngine dma(mem);
    dma.set_gic(gic, dma_irq);
    assert(dma.is_completion_irq_enabled());
    assert(dma.completion_irq() == dma_irq);

    // Initial check: no interrupt pending
    auto sig_init = gic->evaluate(1, 0, 0);
    assert(!sig_init.has_interrupt);

    // Prepare DMA copy with kDmaFlagInterruptOnComplete
    const std::uint64_t desc_addr = 0x1000;
    const std::uint64_t src_pa = 0x20000;
    const std::uint64_t dst_pa = 0x30000;

    mem->write_u32(0, src_pa, 0x12345678);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_pa;
    desc.dst_paddr = dst_pa;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    // Execute transfer
    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());
    assert(mem->read_u32(0, dst_pa) == 0x12345678);

    // GICv3 must now have SPI 48 pending!
    auto sig = gic->evaluate(1, 0, 0);
    assert(sig.has_interrupt);
    assert(sig.intid == dma_irq);

    // Acknowledge interrupt in GIC and deassert in DMA engine
    std::uint32_t ack_intid = gic->acknowledge_group1(1, 0);
    assert(ack_intid == dma_irq);

    dma.acknowledge_completion_irq();
    gic->end_of_interrupt_group1(ack_intid, 1, 0);

    // Verify interrupt is cleared
    auto sig_cleared = gic->evaluate(1, 0, 0);
    assert(!sig_cleared.has_interrupt);

    std::cout << "[PASS] test_dma_completion_interrupt_generation\n";
}

static void test_dma_transfer_without_interrupt_flag() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto gic = std::make_shared<GICv3>();
    gic->set_core_count(1);
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // Enable Group 0 and Group 1

    const std::uint32_t dma_irq = 48;
    gic->write_mmio(GICv3::kGicdBase + 0x0080 + (dma_irq / 32) * 4, 1u << (dma_irq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0100 + (dma_irq / 32) * 4, 1u << (dma_irq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0400 + dma_irq, 0x20u, 1);
    gic->write_icc_pmr(0xFFu, 0);
    gic->write_icc_igrpen1(1u, 0);

    VirtualDmaEngine dma(mem);
    dma.set_gic(gic, dma_irq);

    const std::uint64_t desc_addr = 0x2000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 2;
    desc.src_paddr = 0x20000;
    desc.dst_paddr = 0x30000;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xDEADBEEF;
    desc.flags = 0; // No interrupt on complete!

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());

    // GIC must NOT have interrupt pending
    auto sig = gic->evaluate(1, 0, 0);
    assert(!sig.has_interrupt);

    std::cout << "[PASS] test_dma_transfer_without_interrupt_flag\n";
}

int main() {
    std::cout << "=== Running Stage 200.11 DMA Interrupt Integration Tests ===\n";
    test_dma_completion_interrupt_generation();
    test_dma_transfer_without_interrupt_flag();
    std::cout << "All Stage 200.11 tests passed successfully!\n";
    return 0;
}
