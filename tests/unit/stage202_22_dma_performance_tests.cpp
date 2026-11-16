// ==============================================================================
// WinIOS Campaign 202 Stage 22: DMA Ring Performance Engineering Tests
// ==============================================================================
// Verifies:
// 1. Contiguous direct memory zero-copy transfer correctness and byte fidelity
// 2. Batched ring descriptor processing (execute_ring_batch) & circular rollover
// 3. GIC completion interrupt and exclusive monitor coherency preservation
// 4. Virtual IOMMU IOVA translation and fault reporting within descriptor rings
// 5. Performance benchmark comparison: Baseline (bounce buffer) vs Zero-Copy vs Ring Batch
// ==============================================================================

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/gicv3.h"
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
// Test 1: Contiguous Direct Memory Zero-Copy Transfer Correctness
// -----------------------------------------------------------------------------
void test_zero_copy_direct_transfer_correctness() {
    std::cout << "[Test 1] Contiguous Direct Memory Zero-Copy Transfer Correctness...\n";

    constexpr std::size_t kMemSize = 4 * 1024 * 1024; // 4 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(kMemSize);
    VirtualDmaEngine dma(shared_mem);

    TEST_CHECK(dma.is_zero_copy_enabled(), "Zero-copy should be enabled by default");

    const std::uint64_t src_pa = 0x20000ULL;
    const std::uint64_t dst_pa = 0x60000ULL;
    constexpr std::size_t kTransferBytes = 64 * 1024; // 64 KB

    // Write deterministically seeded pattern to source memory
    std::uint8_t* raw = shared_mem->raw_memory().raw_data();
    for (std::size_t i = 0; i < kTransferBytes; ++i) {
        raw[src_pa + i] = static_cast<std::uint8_t>((i * 13u + 7u) & 0xFFu);
    }
    std::memset(raw + dst_pa, 0, kTransferBytes);

    // Setup descriptor at 0x10000
    const std::uint64_t desc_pa = 0x10000ULL;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_pa;
    desc.dst_paddr = dst_pa;
    desc.length_bytes = kTransferBytes;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0;

    std::memcpy(raw + desc_pa, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_pa);
    TEST_CHECK(res.is_success(), "Descriptor execution must succeed");
    TEST_CHECK(res.descriptors_processed == 1, "Must process 1 descriptor");
    TEST_CHECK(res.bytes_transferred == kTransferBytes, "Bytes transferred must match 64 KB");

    // Verify destination byte fidelity
    bool match = (std::memcmp(raw + src_pa, raw + dst_pa, kTransferBytes) == 0);
    TEST_CHECK(match, "Destination buffer must match source buffer byte-for-byte");

    // Test Fill operation
    const std::uint64_t fill_dst_pa = 0xA0000ULL;
    constexpr std::size_t kFillBytes = 16 * 1024; // 16 KB
    VirtualDmaDescriptor fill_desc{};
    fill_desc.descriptor_id = 2;
    fill_desc.dst_paddr = fill_dst_pa;
    fill_desc.length_bytes = kFillBytes;
    fill_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    fill_desc.fill_pattern = 0xA5A5A5A5u;

    const std::uint64_t fill_desc_pa = 0x10100ULL;
    std::memcpy(raw + fill_desc_pa, &fill_desc, sizeof(fill_desc));

    auto res_fill = dma.execute_descriptor_chain(fill_desc_pa);
    TEST_CHECK(res_fill.is_success(), "Fill execution must succeed");
    bool fill_match = true;
    for (std::size_t i = 0; i < kFillBytes; ++i) {
        if (raw[fill_dst_pa + i] != 0xA5u) {
            fill_match = false;
            break;
        }
    }
    TEST_CHECK(fill_match, "Destination buffer must match fill pattern");

    std::cout << "  -> Zero-copy direct transfer correctness passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Batched Ring Descriptor Processing & Circular Rollover
// -----------------------------------------------------------------------------
void test_batched_ring_descriptor_processing() {
    std::cout << "[Test 2] Batched Ring Descriptor Processing (execute_ring_batch)...\n";

    constexpr std::size_t kMemSize = 8 * 1024 * 1024; // 8 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(kMemSize);
    VirtualDmaEngine dma(shared_mem);

    std::uint8_t* raw = shared_mem->raw_memory().raw_data();

    // Configure 16-slot descriptor ring at physical address 0x40000
    const std::uint64_t ring_base = 0x40000ULL;
    constexpr std::uint32_t kRingCapacity = 16;
    constexpr std::size_t kBlockSize = 4096; // 4 KB per block

    // Setup source buffers at 0x100000 + i*0x1000
    // Setup destination buffers at 0x200000 + i*0x1000
    for (std::uint32_t i = 0; i < kRingCapacity; ++i) {
        const std::uint64_t s = 0x100000ULL + i * kBlockSize;
        const std::uint64_t d = 0x200000ULL + i * kBlockSize;
        for (std::size_t b = 0; b < kBlockSize; ++b) {
            raw[s + b] = static_cast<std::uint8_t>((i * 17u + b) & 0xFFu);
        }
        std::memset(raw + d, 0, kBlockSize);

        VirtualDmaDescriptor desc{};
        desc.descriptor_id = 100 + i;
        desc.src_paddr = s;
        desc.dst_paddr = d;
        desc.length_bytes = kBlockSize;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        desc.flags = 0;
        desc.status = static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::Pending);

        std::memcpy(raw + ring_base + i * sizeof(VirtualDmaDescriptor), &desc, sizeof(desc));
    }

    // Batch 1: Process 8 descriptors starting at index 0 (slots 0..7)
    VirtualDmaRingConfig cfg1{};
    cfg1.ring_base_paddr = ring_base;
    cfg1.ring_capacity = kRingCapacity;
    cfg1.start_index = 0;
    cfg1.batch_count = 8;

    auto res1 = dma.execute_ring_batch(cfg1);
    TEST_CHECK(res1.is_success(), "Batch 1 execution must succeed");
    TEST_CHECK(res1.descriptors_processed == 8, "Must process 8 descriptors");
    TEST_CHECK(res1.bytes_transferred == 8 * kBlockSize, "Must transfer 32 KB");

    // Verify slots 0..7 data
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::uint64_t s = 0x100000ULL + i * kBlockSize;
        const std::uint64_t d = 0x200000ULL + i * kBlockSize;
        TEST_CHECK(std::memcmp(raw + s, raw + d, kBlockSize) == 0, "Slot " + std::to_string(i) + " data mismatch");

        VirtualDmaDescriptor check_desc{};
        std::memcpy(&check_desc, raw + ring_base + i * sizeof(VirtualDmaDescriptor), sizeof(check_desc));
        TEST_CHECK(check_desc.status == static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::Success),
                   "Descriptor status in RAM must be Success");
    }

    // Batch 2: Circular rollover test!
    // Start at index 12, process 6 descriptors -> wraps around slots 12, 13, 14, 15, 0, 1
    VirtualDmaRingConfig cfg2{};
    cfg2.ring_base_paddr = ring_base;
    cfg2.ring_capacity = kRingCapacity;
    cfg2.start_index = 12;
    cfg2.batch_count = 6;

    auto res2 = dma.execute_ring_batch(cfg2);
    TEST_CHECK(res2.is_success(), "Batch 2 circular rollover execution must succeed");
    TEST_CHECK(res2.descriptors_processed == 6, "Must process 6 descriptors with rollover");
    TEST_CHECK(res2.bytes_transferred == 6 * kBlockSize, "Must transfer 24 KB");

    // Verify wrapped slot 14 data
    const std::uint64_t s14 = 0x100000ULL + 14 * kBlockSize;
    const std::uint64_t d14 = 0x200000ULL + 14 * kBlockSize;
    TEST_CHECK(std::memcmp(raw + s14, raw + d14, kBlockSize) == 0, "Slot 14 data mismatch");

    std::cout << "  -> Batched ring descriptor processing passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Interrupt Delivery & Exclusive Monitor Coherency Preservation
// -----------------------------------------------------------------------------
void test_interrupt_delivery_and_exclusive_monitor() {
    std::cout << "[Test 3] GIC Interrupt Delivery & Exclusive Monitor Invalidation...\n";

    constexpr std::size_t kMemSize = 2 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kMemSize, 4);

    // Attach SmpExclusiveMonitorManager
    SmpExclusiveMonitorManager monitor(4);
    shared_mem->attach_exclusive_monitor(&monitor);

    // Attach GICv3 interrupt controller
    auto gic = std::make_shared<GICv3>();
    constexpr std::uint32_t kDmaIrq = 48;

    // Configure GIC
    gic->set_core_count(1);
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // Enable Group 0 and Group 1
    gic->write_mmio(GICv3::kGicdBase + 0x0080 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0100 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0400 + kDmaIrq, 0x20u, 1);
    gic->write_icc_pmr(0xFFu, 0);
    gic->write_icc_igrpen1(1u, 0);

    VirtualDmaEngine dma(shared_mem);
    dma.set_gic(gic, kDmaIrq);

    // Core 1 sets exclusive reservation on physical address 0x70000
    const std::uint64_t res_addr = 0x70000ULL;
    monitor.load_exclusive(1, res_addr, 64);
    TEST_CHECK(monitor.is_reservation_valid(1, res_addr, 64), "Core 1 must hold reservation");

    // Configure DMA ring descriptor targeting 0x70000 with interrupt-on-complete flag
    const std::uint64_t ring_base = 0x30000ULL;
    std::uint8_t* raw = shared_mem->raw_memory().raw_data();
    std::memset(raw + 0x50000, 0x55, 64);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x50000ULL;
    desc.dst_paddr = res_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;

    std::memcpy(raw + ring_base, &desc, sizeof(desc));

    VirtualDmaRingConfig cfg{};
    cfg.ring_base_paddr = ring_base;
    cfg.ring_capacity = 4;
    cfg.start_index = 0;
    cfg.batch_count = 1;

    auto res = dma.execute_ring_batch(cfg);
    TEST_CHECK(res.is_success(), "Ring execution must succeed");

    // 1. Exclusive reservation on Core 1 must be invalidated by the DMA write
    TEST_CHECK(!monitor.is_reservation_valid(1, res_addr, 64),
               "DMA destination write must invalidate exclusive reservations");

    // 2. GIC SPI completion interrupt must be asserted
    auto sig = gic->evaluate(1, 0, 0);
    TEST_CHECK(sig.has_interrupt && sig.intid == kDmaIrq, "GIC SPI 48 must be pending upon DMA completion");

    dma.acknowledge_completion_irq();
    auto sig_ack = gic->evaluate(1, 0, 0);
    TEST_CHECK(!sig_ack.has_interrupt, "GIC SPI 48 must be cleared after acknowledge");

    std::cout << "  -> Interrupt delivery & exclusive monitor coherency passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Virtual IOMMU IOVA Translation & Protection within Ring Batch
// -----------------------------------------------------------------------------
void test_iommu_translation_within_ring_batch() {
    std::cout << "[Test 4] Virtual IOMMU IOVA Translation within Ring Batch...\n";

    constexpr std::size_t kMemSize = 4 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kMemSize);
    auto iommu = std::make_shared<VirtualIommu>();

    constexpr DeviceId kDevId = 7;
    constexpr DomainId kDomId = 1;

    TEST_CHECK(iommu->create_domain(kDomId), "Create IOMMU domain");
    TEST_CHECK(iommu->attach_device(kDevId, kDomId), "Attach device to domain");

    // Map Source IOVA 0x10000 -> PA 0x80000 (Read-Only)
    const std::uint64_t src_iova = 0x10000ULL;
    const std::uint64_t src_pa   = 0x80000ULL;
    TEST_CHECK(iommu->map(kDomId, src_iova, src_pa, 4096, IommuAccess::Read), "Map source IOVA");

    // Map Destination IOVA 0x20000 -> PA 0x90000 (Read/Write)
    const std::uint64_t dst_iova = 0x20000ULL;
    const std::uint64_t dst_pa   = 0x90000ULL;
    TEST_CHECK(iommu->map(kDomId, dst_iova, dst_pa, 4096, IommuAccess::ReadWrite), "Map dest IOVA");

    VirtualDmaEngine dma(shared_mem);
    dma.set_iommu(iommu);
    dma.set_addressing_mode(DmaAddressingMode::IommuTranslated);
    dma.set_default_device_id(kDevId);

    std::uint8_t* raw = shared_mem->raw_memory().raw_data();
    std::memset(raw + src_pa, 0x77, 4096);
    std::memset(raw + dst_pa, 0, 4096);

    const std::uint64_t ring_base = 0x50000ULL;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_iova;
    desc.dst_paddr = dst_iova;
    desc.length_bytes = 4096;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0;
    desc.reserved = kDevId;

    std::memcpy(raw + ring_base, &desc, sizeof(desc));

    VirtualDmaRingConfig cfg{};
    cfg.ring_base_paddr = ring_base;
    cfg.ring_capacity = 2;
    cfg.start_index = 0;
    cfg.batch_count = 1;

    auto res = dma.execute_ring_batch(cfg);
    TEST_CHECK(res.is_success(), "IOMMU translated ring copy must succeed");
    TEST_CHECK(std::memcmp(raw + src_pa, raw + dst_pa, 4096) == 0, "Data must reach backing physical RAM");

    // Permission fault test: Attempting to write into Read-Only mapped IOVA (src_iova)
    VirtualDmaDescriptor bad_desc{};
    bad_desc.descriptor_id = 2;
    bad_desc.src_paddr = dst_iova;
    bad_desc.dst_paddr = src_iova; // Read-only IOVA as destination!
    bad_desc.length_bytes = 4096;
    bad_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    bad_desc.reserved = kDevId;

    std::memcpy(raw + ring_base, &bad_desc, sizeof(bad_desc));
    auto res_fault = dma.execute_ring_batch(cfg);
    TEST_CHECK(!res_fault.is_success(), "Write to read-only IOVA must fail");
    TEST_CHECK(res_fault.status == VirtualDmaDescriptorStatus::ErrorIommuPermission, "Must report ErrorIommuPermission");

    std::cout << "  -> Virtual IOMMU IOVA translation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Benchmark Comparison: Baseline (Bounce Buffer) vs Zero-Copy vs Ring Batch
// -----------------------------------------------------------------------------
void test_benchmark_dma_performance_comparison() {
    std::cout << "[Test 5] Performance Benchmark: Baseline vs Zero-Copy vs Ring Batch...\n";

    constexpr std::size_t kMemSize = 16 * 1024 * 1024; // 16 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(kMemSize);
    VirtualDmaEngine dma(shared_mem);

    constexpr std::size_t kTransferSize = 32 * 1024; // 32 KB per transfer
    constexpr std::uint32_t kIterations = 500;

    const std::uint64_t src_pa = 0x100000ULL;
    const std::uint64_t dst_pa = 0x200000ULL;
    const std::uint64_t desc_pa = 0x10000ULL;

    std::uint8_t* raw = shared_mem->raw_memory().raw_data();
    for (std::size_t i = 0; i < kTransferSize; ++i) {
        raw[src_pa + i] = static_cast<std::uint8_t>(i & 0xFFu);
    }

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_pa;
    desc.dst_paddr = dst_pa;
    desc.length_bytes = kTransferSize;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    std::memcpy(raw + desc_pa, &desc, sizeof(desc));

    // 1. Baseline Benchmark: zero_copy_enabled = false (bounce buffer allocating)
    dma.set_zero_copy_enabled(false);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (std::uint32_t i = 0; i < kIterations; ++i) {
        dma.execute_descriptor_chain(desc_pa);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double baseline_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // 2. Zero-Copy Descriptor Chain Benchmark: zero_copy_enabled = true (direct std::memmove)
    dma.set_zero_copy_enabled(true);
    auto t2 = std::chrono::high_resolution_clock::now();
    for (std::uint32_t i = 0; i < kIterations; ++i) {
        dma.execute_descriptor_chain(desc_pa);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double zerocopy_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // 3. Batched Ring Benchmark: execute_ring_batch with zero-copy
    const std::uint64_t ring_base = 0x50000ULL;
    constexpr std::uint32_t kRingCap = 64;
    for (std::uint32_t r = 0; r < kRingCap; ++r) {
        VirtualDmaDescriptor rdesc{};
        rdesc.descriptor_id = r;
        rdesc.src_paddr = src_pa;
        rdesc.dst_paddr = dst_pa;
        rdesc.length_bytes = kTransferSize;
        rdesc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        std::memcpy(raw + ring_base + r * sizeof(VirtualDmaDescriptor), &rdesc, sizeof(rdesc));
    }

    VirtualDmaRingConfig ring_cfg{};
    ring_cfg.ring_base_paddr = ring_base;
    ring_cfg.ring_capacity = kRingCap;
    ring_cfg.start_index = 0;
    ring_cfg.batch_count = 32;

    auto t4 = std::chrono::high_resolution_clock::now();
    const std::uint32_t ring_batches = kIterations / 32;
    for (std::uint32_t i = 0; i < ring_batches; ++i) {
        dma.execute_ring_batch(ring_cfg);
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    double ring_ms = std::chrono::duration<double, std::milli>(t5 - t4).count();

    const double total_mb = static_cast<double>(kIterations * kTransferSize) / (1024.0 * 1024.0);
    const double baseline_bw = (total_mb / (baseline_ms / 1000.0));
    const double zerocopy_bw = (total_mb / (zerocopy_ms / 1000.0));
    const double ring_bw     = (total_mb / (ring_ms / 1000.0));

    std::cout << "  -> Benchmark Results (" << total_mb << " MB total):\n";
    std::cout << "     Baseline (Bounce Buffer):  " << baseline_ms << " ms (" << baseline_bw << " MB/s)\n";
    std::cout << "     Optimized (Zero-Copy):    " << zerocopy_ms << " ms (" << zerocopy_bw << " MB/s)\n";
    std::cout << "     Batched Ring Descriptor:  " << ring_ms << " ms (" << ring_bw << " MB/s)\n";

    TEST_CHECK(baseline_ms > 0 && zerocopy_ms > 0, "Benchmark timings must be positive");
    std::cout << "  -> Performance benchmark comparison passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 202.22 DMA Ring Performance Engineering Tests ===\n";
    test_zero_copy_direct_transfer_correctness();
    test_batched_ring_descriptor_processing();
    test_interrupt_delivery_and_exclusive_monitor();
    test_iommu_translation_within_ring_batch();
    test_benchmark_dma_performance_comparison();

    if (g_failures != 0) {
        std::cerr << "Stage 202.22 tests FAILED with " << g_failures << " error(s).\n";
        return 1;
    }
    std::cout << "All Stage 202.22 tests passed successfully!\n";
    return 0;
}
