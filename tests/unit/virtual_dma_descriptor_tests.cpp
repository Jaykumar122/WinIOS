// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual DMA Descriptor Validation Tests
// =============================================================================

#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_dma_types.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <iostream>
#include <memory>

using namespace winios;
using namespace winios::device;

namespace {

struct Fixture {
    std::shared_ptr<SmpSharedMemory> mem;
    std::unique_ptr<VirtualDmaEngine> engine;
    const std::size_t ram_size{1024 * 1024}; // 1 MB
};

Fixture create_fixture() {
    Fixture f;
    f.mem = std::make_shared<SmpSharedMemory>(f.ram_size, 8);
    f.engine = std::make_unique<VirtualDmaEngine>(f.mem);
    return f;
}

// -----------------------------------------------------------------------------
// Group 1: Valid Descriptor Validation (Copy and Fill)
// -----------------------------------------------------------------------------
void test_group_1_valid_descriptors() {
    std::cout << "[Group 1] Testing Valid Descriptors...\n";
    auto f = create_fixture();

    VirtualDmaDescriptor copy_desc{};
    copy_desc.descriptor_id = 1;
    copy_desc.src_paddr = 0x1000;
    copy_desc.dst_paddr = 0x2000;
    copy_desc.length_bytes = 1024;
    copy_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);

    assert(f.engine->validate_descriptor(copy_desc, f.ram_size) == VirtualDmaDescriptorStatus::Success);

    VirtualDmaDescriptor fill_desc{};
    fill_desc.descriptor_id = 2;
    fill_desc.dst_paddr = 0x3000;
    fill_desc.length_bytes = 512;
    fill_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    fill_desc.fill_pattern = 0xAA55AA55u;

    assert(f.engine->validate_descriptor(fill_desc, f.ram_size) == VirtualDmaDescriptorStatus::Success);

    std::cout << "  -> Group 1 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 2: Misalignment and Descriptor Boundary Checks
// -----------------------------------------------------------------------------
void test_group_2_alignment_and_bounds() {
    std::cout << "[Group 2] Testing Misaligned & Out-of-Bounds Descriptors...\n";
    auto f = create_fixture();

    // 1. Misaligned descriptor address (e.g. 0x1001, not 8-byte aligned)
    auto res_mis = f.engine->execute_descriptor_chain(0x1001);
    assert(res_mis.status == VirtualDmaDescriptorStatus::ErrorMisaligned);

    // 2. Descriptor outside guest memory (e.g. 0x200000 > 1 MB)
    auto res_oob = f.engine->execute_descriptor_chain(0x200000);
    assert(res_oob.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    // 3. Descriptor spanning across the end of guest memory (ram_size - 32)
    auto res_span = f.engine->execute_descriptor_chain(f.ram_size - 32);
    assert(res_span.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    std::cout << "  -> Group 2 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 3: Length Limits and Zero-Length Transfers
// -----------------------------------------------------------------------------
void test_group_3_length_limits() {
    std::cout << "[Group 3] Testing Length Limits...\n";
    auto f = create_fixture();

    // Zero length rejected
    VirtualDmaDescriptor zero_desc{};
    zero_desc.src_paddr = 0x1000;
    zero_desc.dst_paddr = 0x2000;
    zero_desc.length_bytes = 0;
    zero_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(zero_desc, f.ram_size) == VirtualDmaDescriptorStatus::ErrorLengthLimit);

    // Max transfer limit (64 KiB default)
    VirtualDmaDescriptor max_desc = zero_desc;
    max_desc.length_bytes = 65536; // Exact limit
    assert(f.engine->validate_descriptor(max_desc, f.ram_size) == VirtualDmaDescriptorStatus::Success);

    VirtualDmaDescriptor over_desc = zero_desc;
    over_desc.length_bytes = 65537; // Exceeds limit
    assert(f.engine->validate_descriptor(over_desc, f.ram_size) == VirtualDmaDescriptorStatus::ErrorLengthLimit);

    std::cout << "  -> Group 3 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 4: Address Arithmetic Overflow Checks
// -----------------------------------------------------------------------------
void test_group_4_address_overflow() {
    std::cout << "[Group 4] Testing Address Arithmetic Overflow...\n";
    auto f = create_fixture();

    // Source arithmetic overflow: src + length wraps 64-bit integer
    VirtualDmaDescriptor src_ovf{};
    src_ovf.src_paddr = UINT64_MAX - 100;
    src_ovf.dst_paddr = 0x1000;
    src_ovf.length_bytes = 200;
    src_ovf.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(src_ovf, f.ram_size) == VirtualDmaDescriptorStatus::ErrorAddressOverflow);

    // Destination arithmetic overflow
    VirtualDmaDescriptor dst_ovf{};
    dst_ovf.src_paddr = 0x1000;
    dst_ovf.dst_paddr = UINT64_MAX - 50;
    dst_ovf.length_bytes = 100;
    dst_ovf.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(dst_ovf, f.ram_size) == VirtualDmaDescriptorStatus::ErrorAddressOverflow);

    std::cout << "  -> Group 4 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 5: Memory Capacity Bounds Checks
// -----------------------------------------------------------------------------
void test_group_5_memory_bounds() {
    std::cout << "[Group 5] Testing Memory Capacity Bounds Checks...\n";
    auto f = create_fixture();

    // Source out-of-bounds (starts inside, extends past ram_size)
    VirtualDmaDescriptor src_oob{};
    src_oob.src_paddr = f.ram_size - 100;
    src_oob.dst_paddr = 0x1000;
    src_oob.length_bytes = 200;
    src_oob.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(src_oob, f.ram_size) == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    // Destination out-of-bounds
    VirtualDmaDescriptor dst_oob{};
    dst_oob.src_paddr = 0x1000;
    dst_oob.dst_paddr = f.ram_size - 50;
    dst_oob.length_bytes = 100;
    dst_oob.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(dst_oob, f.ram_size) == VirtualDmaDescriptorStatus::ErrorOutOfBounds);

    // Transfer right up to end of RAM succeeds
    VirtualDmaDescriptor edge_desc{};
    edge_desc.src_paddr = 0x1000;
    edge_desc.dst_paddr = f.ram_size - 100;
    edge_desc.length_bytes = 100;
    edge_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    assert(f.engine->validate_descriptor(edge_desc, f.ram_size) == VirtualDmaDescriptorStatus::Success);

    std::cout << "  -> Group 5 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 6: Chain Depth Limits and Cycle Detection
// -----------------------------------------------------------------------------
void test_group_6_chaining_and_cycles() {
    std::cout << "[Group 6] Testing Chain Depth and Cycle Detection...\n";
    auto f = create_fixture();

    // Setup cyclic chain: Desc A (0x1000) -> Desc B (0x2000) -> Desc A (0x1000)
    VirtualDmaDescriptor descA{};
    descA.descriptor_id = 1;
    descA.src_paddr = 0x10000;
    descA.dst_paddr = 0x20000;
    descA.length_bytes = 64;
    descA.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    descA.flags = kDmaFlagChainNext;
    descA.next_descriptor_paddr = 0x2000;

    VirtualDmaDescriptor descB{};
    descB.descriptor_id = 2;
    descB.src_paddr = 0x30000;
    descB.dst_paddr = 0x40000;
    descB.length_bytes = 64;
    descB.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    descB.flags = kDmaFlagChainNext;
    descB.next_descriptor_paddr = 0x1000; // Loops back to Desc A!

    f.mem->write_bytes(0, 0x1000, &descA, sizeof(descA));
    f.mem->write_bytes(0, 0x2000, &descB, sizeof(descB));

    auto res_cycle = f.engine->execute_descriptor_chain(0x1000);
    assert(res_cycle.status == VirtualDmaDescriptorStatus::ErrorCyclicChain);

    // Deep chain exceeding max_descriptors_per_chain (default 16)
    // Create chain of 20 descriptors
    for (std::uint64_t i = 0; i < 20; ++i) {
        VirtualDmaDescriptor d{};
        d.descriptor_id = i + 1;
        d.src_paddr = 0x50000 + i * 128;
        d.dst_paddr = 0x60000 + i * 128;
        d.length_bytes = 32;
        d.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        d.flags = kDmaFlagChainNext;
        d.next_descriptor_paddr = 0x8000 + (i + 1) * 64;
        f.mem->write_bytes(0, 0x8000 + i * 64, &d, sizeof(d));
    }

    auto res_deep = f.engine->execute_descriptor_chain(0x8000);
    assert(res_deep.status == VirtualDmaDescriptorStatus::ErrorChainTooDeep);

    std::cout << "  -> Group 6 PASS\n";
}

// -----------------------------------------------------------------------------
// Group 7: Unsupported Operations and Disabled Engine
// -----------------------------------------------------------------------------
void test_group_7_unsupported_and_disabled() {
    std::cout << "[Group 7] Testing Unsupported Op & Disabled Engine...\n";
    auto f = create_fixture();

    VirtualDmaDescriptor bad_op{};
    bad_op.src_paddr = 0x1000;
    bad_op.dst_paddr = 0x2000;
    bad_op.length_bytes = 64;
    bad_op.operation_type = 99; // Unsupported
    assert(f.engine->validate_descriptor(bad_op, f.ram_size) == VirtualDmaDescriptorStatus::ErrorUnsupportedOp);

    // Disabled engine rejects
    f.engine->set_enabled(false);
    assert(!f.engine->is_enabled());

    VirtualDmaDescriptor valid_desc{};
    valid_desc.src_paddr = 0x1000;
    valid_desc.dst_paddr = 0x2000;
    valid_desc.length_bytes = 64;
    valid_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    f.mem->write_bytes(0, 0x3000, &valid_desc, sizeof(valid_desc));

    auto res_dis = f.engine->execute_descriptor_chain(0x3000);
    assert(res_dis.status == VirtualDmaDescriptorStatus::ErrorDeviceDisabled);

    std::cout << "  -> Group 7 PASS\n";
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual DMA Descriptor Validation Tests \n";
    std::cout << "===================================================================\n\n";

    test_group_1_valid_descriptors();
    test_group_2_alignment_and_bounds();
    test_group_3_length_limits();
    test_group_4_address_overflow();
    test_group_5_memory_bounds();
    test_group_6_chaining_and_cycles();
    test_group_7_unsupported_and_disabled();

    std::cout << "\n>>> ALL VIRTUAL DMA DESCRIPTOR TEST GROUPS PASSED <<<\n";
    return 0;
}
