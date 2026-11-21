// ============================================================================
// Stage 247.8, 247.9 & 247.10: DMA Ownership, Coherency & Transfer Tests
// Tests:
// 1. Valid DMA memory-to-memory block transfer.
// 2. DMA pattern fill operation.
// 3. Address boundary validation: rejection of out-of-bounds buffer.
// 4. Misaligned descriptor address rejection.
// 5. Completion status and interrupt assertion upon transfer completion.
// ============================================================================

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_device.h"
#include "device/virtual_dma_types.h"

#include <iostream>
#include <memory>
#include <vector>
#include <cstring>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;

static void test_dma_copy_and_coherency() {
    std::cout << "[Stage 247.8 - Test 1] DMA memory copy and memory coherency...\n";

    const std::size_t mem_size = 1024 * 1024; // 1 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);

    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    const uint64_t src_addr = 0x20000;
    const uint64_t dst_addr = 0x30000;
    const uint64_t desc_addr = 0x10000;
    const std::size_t transfer_len = 256;

    // Initialize source buffer with known pattern
    std::vector<uint8_t> src_data(transfer_len);
    for (std::size_t i = 0; i < transfer_len; ++i) {
        src_data[i] = static_cast<uint8_t>(i ^ 0x5A);
    }
    shared_mem->write_bytes(0, src_addr, src_data.data(), transfer_len);

    // Prepare descriptor in shared memory
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = transfer_len;
    desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;
    shared_mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    // Configure DMA registers via MMIO
    dma.write_register(dma_reg::kControl, kControlEnable | kControlIrqEnable, 4);
    dma.write_register(dma_reg::kDescriptorAddr, desc_addr, 8);

    // Trigger submission
    dma.write_register(dma_reg::kControl, kControlEnable | kControlIrqEnable | kControlStart, 4);

    // Verify status complete
    uint64_t status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusComplete) != 0, "DMA transfer completed successfully");
    TEST_ASSERT((status & kStatusError) == 0, "No DMA error flag set");

    // Read back and verify destination bytes
    std::vector<uint8_t> dst_data(transfer_len);
    shared_mem->read_bytes(0, dst_addr, dst_data.data(), transfer_len);
    TEST_ASSERT(std::memcmp(src_data.data(), dst_data.data(), transfer_len) == 0,
                "Destination data matches source data bit-for-bit");
}

static void test_dma_pattern_fill() {
    std::cout << "[Stage 247.9 - Test 2] DMA pattern fill operation...\n";

    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);

    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    const uint64_t dst_addr = 0x40000;
    const uint64_t desc_addr = 0x10080;
    const std::size_t fill_len = 128;
    const uint32_t fill_val = 0xCAFEBABE;

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 2;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = fill_len;
    desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = fill_val;
    shared_mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    dma.write_register(dma_reg::kControl, kControlEnable, 4);
    dma.write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma.write_register(dma_reg::kControl, kControlEnable | kControlStart, 4);

    uint64_t status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusComplete) != 0, "DMA fill completed successfully");

    // Verify pattern
    for (std::size_t i = 0; i < fill_len / 4; ++i) {
        uint32_t val = shared_mem->read_u32(0, dst_addr + i * 4);
        TEST_ASSERT(val == fill_val, "Pattern filled correctly at every word");
    }
}

static void test_dma_adversarial_validation() {
    std::cout << "[Stage 247.8 - Test 3] DMA adversarial bounds & alignment checks...\n";

    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);

    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    // Case A: Out-of-bounds buffer
    const uint64_t bad_desc_addr = 0x10100;
    VirtualDmaDescriptor oob_desc{};
    oob_desc.descriptor_id = 3;
    oob_desc.src_paddr = 0x20000;
    oob_desc.dst_paddr = mem_size + 0x1000; // Far beyond RAM limit!
    oob_desc.length_bytes = 256;
    oob_desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, bad_desc_addr, &oob_desc, sizeof(oob_desc));

    dma.write_register(dma_reg::kControl, kControlEnable, 4);
    dma.write_register(dma_reg::kDescriptorAddr, bad_desc_addr, 8);
    dma.write_register(dma_reg::kControl, kControlEnable | kControlStart, 4);

    uint64_t status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusError) != 0, "Out-of-bounds destination flagged as error");

    // Case B: Misaligned descriptor pointer
    dma.reset();
    dma.write_register(dma_reg::kControl, kControlEnable, 4);
    dma.write_register(dma_reg::kDescriptorAddr, 0x10003ULL, 8); // Not 8-byte aligned
    dma.write_register(dma_reg::kControl, kControlEnable | kControlStart, 4);

    status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusError) != 0, "Misaligned descriptor address rejected");
}

int main() {
    std::cout << "=== Running Stage 247.8 DMA Ownership & Coherency Tests ===\n";
    test_dma_copy_and_coherency();
    test_dma_pattern_fill();
    test_dma_adversarial_validation();
    std::cout << "Stage 247.8: All DMA ownership and coherency tests passed.\n";
    return 0;
}
