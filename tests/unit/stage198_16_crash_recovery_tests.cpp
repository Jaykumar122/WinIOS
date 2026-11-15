// ==============================================================================
// WinIOS Campaign 198 Stage 16: Crash Recovery and Boundary Stress Tests
// ==============================================================================
// Verifies:
// 1. Aborted DMA transfer recovery (out-of-bounds, misaligned, arithmetic overflow)
// 2. Corrupted snapshot buffer detection and rejection (bad magic, CRC mismatch, truncation)
// 3. Cyclic descriptor chains and depth limit recovery
// 4. Memory boundary stress at the upper edge of physical RAM
// ==============================================================================

#include "device/virtual_dma_engine.h"
#include "cpu/smp_shared_memory.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/wsnap_format.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::snapshot;
using namespace winios::device;

void test_aborted_dma_recovery() {
    std::cout << "[Test 1] Aborted DMA recovery on invalid descriptors...\n";
    const std::size_t mem_size = 512 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // 1. Descriptor pointing outside RAM
    VirtualDmaDescriptor d_oob{};
    d_oob.descriptor_id = 1;
    d_oob.src_paddr = 0x1000;
    d_oob.dst_paddr = 0x900000; // Far beyond 512KB
    d_oob.length_bytes = 128;
    d_oob.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    shared_mem->write_bytes(0, 0x1000, &d_oob, sizeof(d_oob));

    auto res1 = engine.execute_descriptor_chain(0x1000, 0);
    TEST_ASSERT(!res1.is_success());
    TEST_ASSERT(res1.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);
    TEST_ASSERT(engine.total_rejected_submissions() > 0);

    // 2. Misaligned descriptor address
    auto res2 = engine.execute_descriptor_chain(0x1003, 0);
    TEST_ASSERT(!res2.is_success());
    TEST_ASSERT(res2.status == VirtualDmaDescriptorStatus::ErrorMisaligned);

    // 3. Address arithmetic overflow (dst + length wraps 64-bit space)
    VirtualDmaDescriptor d_overflow{};
    d_overflow.descriptor_id = 2;
    d_overflow.src_paddr = 0x1000;
    d_overflow.dst_paddr = 0xFFFFFFFFFFFFFF00ULL;
    d_overflow.length_bytes = 0x200;
    d_overflow.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    shared_mem->write_bytes(0, 0x1100, &d_overflow, sizeof(d_overflow));

    auto res3 = engine.execute_descriptor_chain(0x1100, 0);
    TEST_ASSERT(!res3.is_success());
    TEST_ASSERT(res3.status == VirtualDmaDescriptorStatus::ErrorAddressOverflow);

    std::cout << "  -> PASSED\n";
}

void test_corrupted_snapshot_rejection() {
    std::cout << "[Test 2] Corrupted snapshot detection and rejection...\n";
    std::vector<std::uint8_t> valid_buf(128, 0);
    WSnapHeader hdr{};
    hdr.magic = kWSnapMagic;
    hdr.version = 1;
    hdr.core_count = 1;
    hdr.memory_capacity = 64 * 1024;
    hdr.section_dir_offset = 64;
    hdr.section_count = 1;
    hdr.total_file_size = 128;
    encode_wsnap_header(hdr, valid_buf.data());

    // 1. Truncated buffer
    WSnapHeader decoded{};
    auto res_trunc = decode_wsnap_header(valid_buf.data(), 16, decoded);
    TEST_ASSERT(!res_trunc.success);
    TEST_ASSERT(res_trunc.error_code == SnapshotErrorCode::TruncatedFile);

    // 2. Corrupted magic
    auto bad_magic_buf = valid_buf;
    bad_magic_buf[0] = 'X';
    auto res_magic = decode_wsnap_header(bad_magic_buf.data(), bad_magic_buf.size(), decoded);
    TEST_ASSERT(!res_magic.success);
    TEST_ASSERT(res_magic.error_code == SnapshotErrorCode::InvalidMagic);

    // 3. Corrupted header CRC32C
    auto bad_crc_buf = valid_buf;
    bad_crc_buf[60] ^= 0xAA;
    auto res_crc = decode_wsnap_header(bad_crc_buf.data(), bad_crc_buf.size(), decoded);
    TEST_ASSERT(!res_crc.success);
    TEST_ASSERT(res_crc.error_code == SnapshotErrorCode::HeaderCrcMismatch);

    std::cout << "  -> PASSED\n";
}

void test_cyclic_chain_safety() {
    std::cout << "[Test 3] Cyclic DMA chain safety & recovery...\n";
    const std::size_t mem_size = 512 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // D1 points to D2, D2 points back to D1
    VirtualDmaDescriptor d1{};
    d1.descriptor_id = 11;
    d1.src_paddr = 0x2000;
    d1.dst_paddr = 0x3000;
    d1.length_bytes = 64;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d1.flags = kDmaFlagChainNext;
    d1.next_descriptor_paddr = 0x600;

    VirtualDmaDescriptor d2{};
    d2.descriptor_id = 12;
    d2.src_paddr = 0x2000;
    d2.dst_paddr = 0x3040;
    d2.length_bytes = 64;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d2.flags = kDmaFlagChainNext;
    d2.next_descriptor_paddr = 0x500; // loop back to D1

    shared_mem->write_bytes(0, 0x500, &d1, sizeof(d1));
    shared_mem->write_bytes(0, 0x600, &d2, sizeof(d2));

    auto res = engine.execute_descriptor_chain(0x500, 0);
    TEST_ASSERT(!res.is_success());
    TEST_ASSERT(res.status == VirtualDmaDescriptorStatus::ErrorCyclicChain);

    std::cout << "  -> PASSED\n";
}

void test_ram_upper_boundary_access() {
    std::cout << "[Test 4] Boundary stress at the highest byte of RAM...\n";
    const std::size_t mem_size = 64 * 1024; // 64 KB
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 2);
    VirtualDmaEngine engine(shared_mem);

    // Write exactly up to the last byte (mem_size - 64 to mem_size)
    const std::uint64_t last_block_addr = mem_size - 64;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 20;
    desc.dst_paddr = last_block_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0xABABABABu;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));

    auto res_ok = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(res_ok.is_success());
    TEST_ASSERT(shared_mem->read_u32(0, last_block_addr) == 0xABABABABu);
    TEST_ASSERT(shared_mem->read_u32(0, mem_size - 4) == 0xABABABABu);

    // One byte past the boundary must be rejected cleanly
    desc.dst_paddr = mem_size - 63;
    shared_mem->write_bytes(0, 0x800, &desc, sizeof(desc));
    auto res_fail = engine.execute_descriptor_chain(0x800, 0);
    TEST_ASSERT(!res_fail.is_success());
    TEST_ASSERT(res_fail.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.16: Crash Recovery and Boundary Stress Tests\n";
    std::cout << "========================================================\n";

    test_aborted_dma_recovery();
    test_corrupted_snapshot_rejection();
    test_cyclic_chain_safety();
    test_ram_upper_boundary_access();

    std::cout << "\nAll Stage 198.16 tests passed successfully.\n";
    return 0;
}
