// =============================================================================
// WinIOS Campaign 198 Stage 198.10: DMA Concurrency & Fault Recovery Tests
// =============================================================================

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;

namespace {

void test_simultaneous_cpu_and_dma_concurrency() {
    std::cout << "[Test 1] Concurrent CPU and DMA operations on shared memory...\n";
    const std::size_t mem_size = 2 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    VirtualDmaEngine engine(shared_mem);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x20000;
    desc.dst_paddr = 0x21000;
    desc.length_bytes = 1024;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x12345678u;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    std::atomic<bool> start_flag{false};
    std::atomic<int> dma_cycles{0};

    // CPU thread operating on 0x10000 - 0x10FFF
    std::thread cpu_thread([&]() {
        while (!start_flag.load()) {}
        for (std::uint64_t iter = 0; iter < 1000; ++iter) {
            shared_mem->write_u64(1, 0x10000, iter);
            const auto readback = shared_mem->read_u64(1, 0x10000);
            TEST_ASSERT(readback == iter);
        }
    });

    // DMA thread operating on 0x20000 - 0x2FFFF
    std::thread dma_thread([&]() {
        while (!start_flag.load()) {}
        for (int i = 0; i < 100; ++i) {
            auto res = engine.execute_descriptor_chain(0x800, 0);
            TEST_ASSERT(res.is_success());
            dma_cycles.fetch_add(1);
        }
    });

    start_flag.store(true);
    cpu_thread.join();
    dma_thread.join();

    TEST_ASSERT(dma_cycles.load() == 100);
    TEST_ASSERT(shared_mem->read_u32(0, 0x21000) == 0x12345678u);
    std::cout << "  -> PASSED\n";
}

void test_dma_cyclic_chain_fault_recovery() {
    std::cout << "[Test 2] Cyclic descriptor chain detection and safe recovery...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // D1 at 0x800 -> points to D2 at 0x840
    VirtualDmaDescriptor d1{};
    d1.descriptor_id = 101;
    d1.src_paddr = 0x1000;
    d1.dst_paddr = 0x2000;
    d1.length_bytes = 64;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d1.flags = kDmaFlagChainNext;
    d1.next_descriptor_paddr = 0x840;
    shared_mem->write_bytes(0, 0x800, &d1, sizeof(d1));

    // D2 at 0x840 -> points back to D1 at 0x800 (CYCLE!)
    VirtualDmaDescriptor d2{};
    d2.descriptor_id = 102;
    d2.src_paddr = 0x2000;
    d2.dst_paddr = 0x3000;
    d2.length_bytes = 64;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d2.flags = kDmaFlagChainNext;
    d2.next_descriptor_paddr = 0x800; // Loop back
    shared_mem->write_bytes(0, 0x840, &d2, sizeof(d2));

    const auto pre_rejected = engine.total_rejected_submissions();
    auto res = engine.execute_descriptor_chain(0x800, 0);

    // Engine must not hang, must return ErrorCyclicChain
    TEST_ASSERT(!res.is_success());
    TEST_ASSERT(res.status == VirtualDmaDescriptorStatus::ErrorCyclicChain);
    TEST_ASSERT(engine.total_rejected_submissions() == pre_rejected + 1);

    std::cout << "  -> PASSED\n";
}

void test_dma_chain_too_deep_fault_recovery() {
    std::cout << "[Test 3] Descriptor chain depth limit enforcement...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaLimits limits{};
    limits.max_descriptors_per_chain = 4; // Bounded limit
    VirtualDmaEngine engine(shared_mem, limits);

    // Create chain of 5 descriptors (0x800 -> 0x840 -> 0x880 -> 0x8C0 -> 0x900)
    for (std::uint64_t i = 0; i < 5; ++i) {
        VirtualDmaDescriptor d{};
        d.descriptor_id = 200 + i;
        d.src_paddr = 0x1000 + i * 64;
        d.dst_paddr = 0x2000 + i * 64;
        d.length_bytes = 64;
        d.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        d.flags = (i < 4) ? kDmaFlagChainNext : 0;
        d.next_descriptor_paddr = 0x800 + (i + 1) * 0x40;
        shared_mem->write_bytes(0, 0x800 + i * 0x40, &d, sizeof(d));
    }

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(!res.is_success());
    TEST_ASSERT(res.status == VirtualDmaDescriptorStatus::ErrorChainTooDeep);

    std::cout << "  -> PASSED\n";
}

void test_dma_interrupted_descriptor_chain() {
    std::cout << "[Test 4] Interrupted descriptor chain recovery on invalid address...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // D1 at 0x800: Valid Copy
    VirtualDmaDescriptor d1{};
    d1.descriptor_id = 301;
    d1.src_paddr = 0x1000;
    d1.dst_paddr = 0x2000;
    d1.length_bytes = 64;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d1.flags = kDmaFlagChainNext;
    d1.next_descriptor_paddr = 0x840;
    shared_mem->write_bytes(0, 0x800, &d1, sizeof(d1));

    // D2 at 0x840: Points to out-of-bounds address 0x5000000 (beyond 1MB RAM)
    VirtualDmaDescriptor d2{};
    d2.descriptor_id = 302;
    d2.src_paddr = 0x2000;
    d2.dst_paddr = 0x3000;
    d2.length_bytes = 64;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d2.flags = kDmaFlagChainNext;
    d2.next_descriptor_paddr = 0x5000000; // Out-of-bounds next descriptor
    shared_mem->write_bytes(0, 0x840, &d2, sizeof(d2));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    // D1 and D2 executed, next lookup failed with ErrorOutOfBounds
    TEST_ASSERT(!res.is_success());
    TEST_ASSERT(res.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);
    TEST_ASSERT(res.descriptors_processed == 2);
    TEST_ASSERT(res.bytes_transferred == 128);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.10: DMA Concurrency & Fault Recovery Tests\n";
    std::cout << "====================================================\n";

    test_simultaneous_cpu_and_dma_concurrency();
    test_dma_cyclic_chain_fault_recovery();
    test_dma_chain_too_deep_fault_recovery();
    test_dma_interrupted_descriptor_chain();

    std::cout << "\nAll Stage 198.10 DMA Concurrency tests passed successfully!\n";
    return 0;
}
