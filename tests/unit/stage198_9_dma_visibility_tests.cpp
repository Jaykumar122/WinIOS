// =============================================================================
// WinIOS Campaign 198 Stage 198.9: CPU-DMA Visibility & Synchronization Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "memory/memory.h"

#include <cassert>
#include <iostream>
#include <memory>
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

void test_cpu_write_dc_clean_dma_read() {
    std::cout << "[Test 1] CPU writes -> DC CVAC -> DMA reads and copies...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // CPU at EL1 writes 64 bytes at 0x2000
    for (std::size_t i = 0; i < 64; ++i) {
        shared_mem->raw_memory().write_u8(0x2000 + i, static_cast<std::uint8_t>(i + 1));
    }

    // CPU executes DC CVAC on 0x2000, then DSB ISH
    // DC CVAC, X0: 0xD50B7A20
    // DSB ISH:     0xD5033BBF
    shared_mem->raw_memory().write_u32(0x1000, 0xD50B7A20u);
    shared_mem->raw_memory().write_u32(0x1004, 0xD5033BBFu);
    cpu.set_current_el(1);
    cpu.write_register(0, 0x2000);
    cpu.set_program_counter(0x1000);

    cpu.step(shared_mem->raw_memory());
    cpu.step(shared_mem->raw_memory());

    // DMA descriptor at 0x800: Copy 64 bytes from 0x2000 to 0x3000
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x2000;
    desc.dst_paddr = 0x3000;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());
    TEST_ASSERT(res.bytes_transferred == 64);

    // Verify DMA observed exact CPU data
    for (std::size_t i = 0; i < 64; ++i) {
        TEST_ASSERT(shared_mem->raw_memory().read_u8(0x3000 + i) == static_cast<std::uint8_t>(i + 1));
    }

    std::cout << "  -> PASSED\n";
}

void test_dma_write_cpu_invalidate_read() {
    std::cout << "[Test 2] DMA writes -> DC CIVAC -> CPU reads...\n";
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // DMA fills 128 bytes at 0x4000 with 0xBEEFCAFE
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 2;
    desc.dst_paddr = 0x4000;
    desc.length_bytes = 128;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xBEEFCAFEu;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());

    // CPU executes DC CIVAC on 0x4000 and 0x4040, then DSB ISH, then reads
    // DC CIVAC, X0: 0xD50B7E20
    // DC CIVAC, X1: 0xD50B7E21
    // DSB ISH:     0xD5033BBF
    shared_mem->raw_memory().write_u32(0x1000, 0xD50B7E20u);
    shared_mem->raw_memory().write_u32(0x1004, 0xD50B7E21u);
    shared_mem->raw_memory().write_u32(0x1008, 0xD5033BBFu);

    cpu.set_current_el(1);
    cpu.write_register(0, 0x4000);
    cpu.write_register(1, 0x4040);
    cpu.set_program_counter(0x1000);

    cpu.step(shared_mem->raw_memory());
    cpu.step(shared_mem->raw_memory());
    cpu.step(shared_mem->raw_memory());

    // CPU reads from 0x4000 and 0x4040
    TEST_ASSERT(shared_mem->raw_memory().read_u32(0x4000) == 0xBEEFCAFEu);
    TEST_ASSERT(shared_mem->raw_memory().read_u32(0x4040) == 0xBEEFCAFEu);

    std::cout << "  -> PASSED\n";
}

void test_dma_exclusive_reservation_invalidation() {
    std::cout << "[Test 3] DMA writes invalidate CPU exclusive reservations...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    auto monitor = std::make_shared<SmpExclusiveMonitorManager>(4, 64);
    shared_mem->attach_exclusive_monitor(monitor.get());
    VirtualDmaEngine engine(shared_mem);

    const std::uint64_t res_addr = 0x5000;
    shared_mem->write_u64(1, res_addr, 12345);

    // Core 1 registers exclusive reservation on 0x5000
    monitor->load_exclusive(1, res_addr, 8);
    TEST_ASSERT(monitor->is_reservation_valid(1, res_addr, 8));

    // DMA engine (acting as core 0) writes 64 bytes to 0x5000
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 3;
    desc.dst_paddr = res_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x99999999u;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());

    // Core 1's exclusive reservation MUST be invalidated by DMA write
    TEST_ASSERT(!monitor->is_reservation_valid(1, res_addr, 8));

    // Core 1's store_exclusive must FAIL
    bool committed = false;
    bool status = monitor->store_exclusive(1, res_addr, 8, [&]() {
        committed = true;
    });
    TEST_ASSERT(!status);
    TEST_ASSERT(!committed);

    std::cout << "  -> PASSED\n";
}

void test_dma_non_overlapping_reservation_preserved() {
    std::cout << "[Test 4] DMA writes to disjoint address preserve exclusive reservation...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    auto monitor = std::make_shared<SmpExclusiveMonitorManager>(4, 64);
    shared_mem->attach_exclusive_monitor(monitor.get());
    VirtualDmaEngine engine(shared_mem);

    const std::uint64_t res_addr = 0x6000;
    const std::uint64_t dma_addr = 0x7000; // Distinct 64-byte granule

    // Core 1 registers exclusive reservation on 0x6000
    monitor->load_exclusive(1, res_addr, 8);
    TEST_ASSERT(monitor->is_reservation_valid(1, res_addr, 8));

    // DMA engine writes to 0x7000
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 4;
    desc.dst_paddr = dma_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x55555555u;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());

    // Core 1's exclusive reservation on 0x6000 MUST remain valid
    TEST_ASSERT(monitor->is_reservation_valid(1, res_addr, 8));

    // Core 1's store_exclusive must SUCCEED
    bool committed = false;
    bool status = monitor->store_exclusive(1, res_addr, 8, [&]() {
        committed = true;
    });
    TEST_ASSERT(status);
    TEST_ASSERT(committed);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.9: CPU-DMA Visibility & Synchronization Tests\n";
    std::cout << "====================================================\n";

    test_cpu_write_dc_clean_dma_read();
    test_dma_write_cpu_invalidate_read();
    test_dma_exclusive_reservation_invalidation();
    test_dma_non_overlapping_reservation_preserved();

    std::cout << "\nAll Stage 198.9 CPU-DMA Visibility tests passed successfully!\n";
    return 0;
}
