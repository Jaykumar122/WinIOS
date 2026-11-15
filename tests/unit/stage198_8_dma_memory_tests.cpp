// =============================================================================
// WinIOS Campaign 198 Stage 198.8: Virtual DMA Memory Integration Tests
// =============================================================================

#include "device/virtual_dma_engine.h"
#include "cpu/smp_shared_memory.h"

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

void test_dma_copy_aligned_and_unaligned() {
    std::cout << "[Test 1] Virtual DMA Copy operation (aligned and unaligned)...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // Populate source data at 0x1000
    for (std::size_t i = 0; i < 256; ++i) {
        shared_mem->write_u8(0, 0x1000 + i, static_cast<std::uint8_t>(i ^ 0x55));
    }

    // Descriptor at 0x800: Copy 256 bytes from 0x1000 to 0x2000
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x1000;
    desc.dst_paddr = 0x2000;
    desc.length_bytes = 256;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = 0;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());
    TEST_ASSERT(res.bytes_transferred == 256);
    TEST_ASSERT(res.descriptors_processed == 1);

    // Verify destination data
    for (std::size_t i = 0; i < 256; ++i) {
        TEST_ASSERT(shared_mem->read_u8(0, 0x2000 + i) == static_cast<std::uint8_t>(i ^ 0x55));
    }

    // Unaligned transfer: 137 bytes from 0x1003 to 0x3005
    VirtualDmaDescriptor desc2{};
    desc2.descriptor_id = 2;
    desc2.src_paddr = 0x1003;
    desc2.dst_paddr = 0x3005;
    desc2.length_bytes = 137;
    desc2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x840, &desc2, sizeof(desc2));

    auto res2 = engine.execute_descriptor_chain(0x840, 0);
    TEST_ASSERT(res2.is_success());
    TEST_ASSERT(res2.bytes_transferred == 137);

    for (std::size_t i = 0; i < 137; ++i) {
        TEST_ASSERT(shared_mem->read_u8(0, 0x3005 + i) == shared_mem->read_u8(0, 0x1003 + i));
    }

    std::cout << "  -> PASSED\n";
}

void test_dma_fill_operation() {
    std::cout << "[Test 2] Virtual DMA Fill operation...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // Fill 512 bytes at 0x5000 with 32-bit pattern 0xA5A55A5A
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 10;
    desc.dst_paddr = 0x5000;
    desc.length_bytes = 512;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xA5A55A5Au;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());
    TEST_ASSERT(res.bytes_transferred == 512);

    for (std::size_t offset = 0; offset < 512; offset += 4) {
        TEST_ASSERT(shared_mem->read_u32(0, 0x5000 + offset) == 0xA5A55A5Au);
    }

    std::cout << "  -> PASSED\n";
}

void test_dma_boundary_validation() {
    std::cout << "[Test 3] Virtual DMA boundary and limit validations...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // 1. Out-of-bounds destination
    VirtualDmaDescriptor oob_desc{};
    oob_desc.descriptor_id = 20;
    oob_desc.src_paddr = 0x1000;
    oob_desc.dst_paddr = mem_size - 10;
    oob_desc.length_bytes = 100; // Overflows memory bound
    oob_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x800, &oob_desc, sizeof(oob_desc));

    auto r1 = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(!r1.is_success());
    TEST_ASSERT(r1.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    // 2. Length exceeding max limit
    VirtualDmaDescriptor len_desc{};
    len_desc.descriptor_id = 21;
    len_desc.src_paddr = 0x1000;
    len_desc.dst_paddr = 0x2000;
    len_desc.length_bytes = 70000; // Limit is 65536
    len_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x800, &len_desc, sizeof(len_desc));

    auto r2 = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(!r2.is_success());
    TEST_ASSERT(r2.status == VirtualDmaDescriptorStatus::ErrorLengthLimit);

    // 3. Address overflow
    VirtualDmaDescriptor ovf_desc{};
    ovf_desc.descriptor_id = 22;
    ovf_desc.src_paddr = 0xFFFFFFFFFFFFFFFFULL;
    ovf_desc.dst_paddr = 0x2000;
    ovf_desc.length_bytes = 16;
    ovf_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x800, &ovf_desc, sizeof(ovf_desc));

    auto r3 = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(!r3.is_success());
    TEST_ASSERT(r3.status == VirtualDmaDescriptorStatus::ErrorAddressOverflow);

    std::cout << "  -> PASSED\n";
}

void test_dma_chained_descriptors() {
    std::cout << "[Test 4] Virtual DMA chained descriptor execution...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // Init source 1
    shared_mem->write_u64(0, 0x1000, 0x1111222233334444ULL);

    // Descriptor 1 at 0x800: Copy 8 bytes 0x1000 -> 0x2000, next=0x840
    VirtualDmaDescriptor d1{};
    d1.descriptor_id = 31;
    d1.src_paddr = 0x1000;
    d1.dst_paddr = 0x2000;
    d1.length_bytes = 8;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d1.flags = kDmaFlagChainNext;
    d1.next_descriptor_paddr = 0x840;
    shared_mem->write_bytes(0, 0x800, &d1, sizeof(d1));

    // Descriptor 2 at 0x840: Fill 16 bytes at 0x2008 with 0x77777777, next=0x880
    VirtualDmaDescriptor d2{};
    d2.descriptor_id = 32;
    d2.dst_paddr = 0x2008;
    d2.length_bytes = 16;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d2.fill_pattern = 0x77777777u;
    d2.flags = kDmaFlagChainNext;
    d2.next_descriptor_paddr = 0x880;
    shared_mem->write_bytes(0, 0x840, &d2, sizeof(d2));

    // Descriptor 3 at 0x880: Copy 24 bytes 0x2000 -> 0x3000, final
    VirtualDmaDescriptor d3{};
    d3.descriptor_id = 33;
    d3.src_paddr = 0x2000;
    d3.dst_paddr = 0x3000;
    d3.length_bytes = 24;
    d3.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d3.flags = 0; // End of chain
    shared_mem->write_bytes(0, 0x880, &d3, sizeof(d3));

    auto res = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res.is_success());
    TEST_ASSERT(res.descriptors_processed == 3);
    TEST_ASSERT(res.bytes_transferred == 8 + 16 + 24);

    // Verify final destination at 0x3000
    TEST_ASSERT(shared_mem->read_u64(0, 0x3000) == 0x1111222233334444ULL);
    TEST_ASSERT(shared_mem->read_u32(0, 0x3008) == 0x77777777u);
    TEST_ASSERT(shared_mem->read_u32(0, 0x300C) == 0x77777777u);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "====================================================\n";
    std::cout << "Stage 198.8: Virtual DMA Memory Integration Tests\n";
    std::cout << "====================================================\n";

    test_dma_copy_aligned_and_unaligned();
    test_dma_fill_operation();
    test_dma_boundary_validation();
    test_dma_chained_descriptors();

    std::cout << "\nAll Stage 198.8 DMA Memory tests passed successfully!\n";
    return 0;
}
