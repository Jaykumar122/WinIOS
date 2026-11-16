// ==============================================================================
// WinIOS Campaign 201 Stage 14: DMA Memory Safety & Coherency Tests
// ==============================================================================
// Verifies:
// 1. DMA memory safety through Virtual IOMMU translation
// 2. Write-permission enforcement: writing to RO memory halts DMA without writing
// 3. CPU-DMA cache coherency & exclusive reservation monitor invalidation
// 4. Fault atomicity: failed transfers leave destination memory uncorrupted
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
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
// Test 1: IOMMU-Gated DMA Transfer & Permission Enforcement
// -----------------------------------------------------------------------------
void test_iommu_gated_dma() {
    std::cout << "[Test 1] IOMMU-Gated DMA Transfer & RO Rejection...\n";
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto iommu = std::make_shared<VirtualIommu>();
    iommu->reset();

    const DomainId dom_id = 1;
    const DeviceId dma_dev_id = 8;
    iommu->create_domain(dom_id);
    iommu->attach_device(dma_dev_id, dom_id);

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(dma_dev_id);
    dma.set_addressing_mode(DmaAddressingMode::IommuTranslated);

    const std::uint64_t src_iova = 0x10000ULL;
    const std::uint64_t src_pa   = 0x40000ULL;
    const std::uint64_t dst_iova = 0x20000ULL;
    const std::uint64_t dst_pa   = 0x50000ULL;

    // Map source as Read-Only, destination as Read-Write
    iommu->map(dom_id, src_iova, src_pa, 0x4000, IommuAccess::Read);
    iommu->map(dom_id, dst_iova, dst_pa, 0x4000, IommuAccess::ReadWrite);

    // Initialize source data
    mem->write_u32(0, src_pa, 0xDEADBEEFULL);
    mem->write_u32(0, dst_pa, 0x0ULL);

    // Set up descriptor in memory at 0x1000
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_iova;
    desc.dst_paddr = dst_iova;
    desc.length_bytes = 4;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagIommuTranslate;
    mem->write_bytes(0, 0x1000, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(0x1000, 0);
    TEST_CHECK(res.is_success(), "IOMMU-translated DMA copy must succeed");
    TEST_CHECK(mem->read_u32(0, dst_pa) == 0xDEADBEEFULL, "Destination memory must match source data");

    // Case 1.2: Remap destination as Read-Only -> DMA write must fault without modifying memory!
    iommu->unmap(dom_id, dst_iova, 0x4000);
    iommu->map(dom_id, dst_iova, dst_pa, 0x4000, IommuAccess::Read); // Read-only!

    mem->write_u32(0, dst_pa, 0x11112222ULL); // Sentinel
    auto fail_res = dma.execute_descriptor_chain(0x1000, 0);
    TEST_CHECK(!fail_res.is_success(), "DMA write to Read-Only IOVA must fail");
    TEST_CHECK(mem->read_u32(0, dst_pa) == 0x11112222ULL, "Destination memory must remain unmodified on fault");

    std::cout << "  -> IOMMU-gated DMA passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Exclusive Reservation Monitor Invalidation on DMA Write
// -----------------------------------------------------------------------------
void test_exclusive_monitor_invalidation_by_dma() {
    std::cout << "[Test 2] Exclusive Monitor Invalidation on DMA Write...\n";
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4, 2);
    SmpExclusiveMonitorManager ex_mon(2);
    mem->attach_exclusive_monitor(&ex_mon);

    VirtualDmaEngine dma(mem);

    const std::uint64_t target_pa = 0x60000ULL;
    mem->write_u64(0, target_pa, 0x100ULL);

    // Core 0 sets exclusive reservation on target_pa
    ex_mon.load_exclusive(0, target_pa, 8);
    auto val = mem->read_u64(0, target_pa);
    TEST_CHECK(val == 0x100ULL, "Load must return current value");
    TEST_CHECK(ex_mon.is_reservation_valid(0, target_pa, 8), "Core 0 reservation must be active");

    // DMA engine performs write to target_pa
    const std::uint64_t src_pa = 0x70000ULL;
    mem->write_u64(0, src_pa, 0x200ULL);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_pa;
    desc.dst_paddr = target_pa;
    desc.length_bytes = 8;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0;
    mem->write_bytes(0, 0x2000, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(0x2000, 0);
    TEST_CHECK(res.is_success(), "DMA transfer must succeed");

    // Coherency check: Core 0 exclusive reservation MUST have been invalidated by DMA write!
    TEST_CHECK(!ex_mon.is_reservation_valid(0, target_pa, 8), "DMA write must invalidate exclusive monitor");

    // Attempt store-conditional from Core 0: must fail!
    bool sc_ok = mem->write_exclusive_u64(0, target_pa, 0x300ULL);
    TEST_CHECK(!sc_ok, "Store-conditional must fail after DMA invalidation");
    TEST_CHECK(mem->read_u64(0, target_pa) == 0x200ULL, "Memory must retain DMA-written value");

    std::cout << "  -> Exclusive monitor invalidation passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.14 DMA Memory Safety & Coherency Tests ===\n";
    test_iommu_gated_dma();
    test_exclusive_monitor_invalidation_by_dma();

    if (g_failures == 0) {
        std::cout << "All Stage 201.14 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.14 test(s) failed!\n";
        return 1;
    }
}
