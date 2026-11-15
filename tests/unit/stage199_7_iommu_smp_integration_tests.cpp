#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "cpu/smp_cluster.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"

using namespace winios;
using namespace winios::device;

void test_translated_dma_invalidates_smp_exclusive_monitor() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 4);
    auto mon = std::make_shared<SmpExclusiveMonitorManager>(8, 64);
    mem->attach_exclusive_monitor(mon.get());

    auto iommu = std::make_shared<VirtualIommu>();
    iommu->create_domain(1);
    iommu->attach_device(11, 1);

    const std::uint64_t target_iova = 0x10000ULL;
    const std::uint64_t target_pa   = 0x40000ULL;
    iommu->map(1, target_iova, target_pa, 0x4000, IommuAccess::ReadWrite);

    // Core 0 places exclusive reservation on target_pa
    mon->load_exclusive(0, target_pa, 64);
    assert(mon->is_reservation_valid(0, target_pa, 64));

    // Core 2 places reservation on another unrelated address
    mon->load_exclusive(2, 0x50000ULL, 64);
    assert(mon->is_reservation_valid(2, 0x50000ULL, 64));

    VirtualDmaEngine dma(mem);
    dma.set_iommu(iommu);
    dma.set_default_device_id(11);

    // Execute DMA write via translated IOVA targeting target_pa
    const std::uint64_t desc_addr = 0x2000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 201;
    desc.src_paddr = 0;
    desc.dst_paddr = target_iova;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x12345678;
    desc.flags = kDmaFlagIommuTranslate;

    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = dma.execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());

    // Core 0's reservation must be invalidated by the DMA write!
    assert(!mon->is_reservation_valid(0, target_pa, 64));
    // Core 2's unrelated reservation must still be valid!
    assert(mon->is_reservation_valid(2, 0x50000ULL, 64));

    std::cout << "[PASS] test_translated_dma_invalidates_smp_exclusive_monitor" << std::endl;
}

void test_concurrent_multicore_iommu_dma() {
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024 * 16); // 16 MB
    auto iommu = std::make_shared<VirtualIommu>();

    // Set up 4 distinct domains for 4 devices
    for (std::uint32_t i = 1; i <= 4; ++i) {
        iommu->create_domain(i);
        iommu->attach_device(i * 10, i);
        // Map IOVA 0x10000 to PA (i * 0x100000)
        iommu->map(i, 0x10000ULL, i * 0x100000ULL, 0x10000, IommuAccess::ReadWrite);
    }

    std::atomic<bool> start_flag{false};
    std::atomic<int> completed_threads{0};

    auto dma_worker = [&](std::uint32_t dev_id, std::uint64_t desc_addr, std::uint32_t pattern) {
        VirtualDmaEngine dma(mem);
        dma.set_iommu(iommu);
        dma.set_default_device_id(dev_id);

        VirtualDmaDescriptor desc{};
        desc.descriptor_id = dev_id;
        desc.src_paddr = 0;
        desc.dst_paddr = 0x10000ULL;
        desc.length_bytes = 1024;
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
        desc.fill_pattern = pattern;
        desc.flags = kDmaFlagIommuTranslate;

        mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

        while (!start_flag.load(std::memory_order_acquire)) {}

        for (int iter = 0; iter < 50; ++iter) {
            auto res = dma.execute_descriptor_chain(desc_addr, dev_id % 8);
            assert(res.is_success());
        }
        completed_threads.fetch_add(1, std::memory_order_release);
    };

    std::vector<std::thread> threads;
    threads.emplace_back(dma_worker, 10, 0x1000, 0x11111111);
    threads.emplace_back(dma_worker, 20, 0x2000, 0x22222222);
    threads.emplace_back(dma_worker, 30, 0x3000, 0x33333333);
    threads.emplace_back(dma_worker, 40, 0x4000, 0x44444444);

    start_flag.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    assert(completed_threads.load() == 4);

    // Verify each physical domain buffer received its corresponding pattern
    assert(mem->read_u32(0, 1 * 0x100000ULL) == 0x11111111);
    assert(mem->read_u32(0, 2 * 0x100000ULL) == 0x22222222);
    assert(mem->read_u32(0, 3 * 0x100000ULL) == 0x33333333);
    assert(mem->read_u32(0, 4 * 0x100000ULL) == 0x44444444);

    std::cout << "[PASS] test_concurrent_multicore_iommu_dma" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.7 IOMMU and SMP Integration Tests ---" << std::endl;
    test_translated_dma_invalidates_smp_exclusive_monitor();
    test_concurrent_multicore_iommu_dma();
    std::cout << "All Stage 199.7 tests PASSED!" << std::endl;
    return 0;
}
