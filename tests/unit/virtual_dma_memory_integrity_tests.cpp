// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual DMA Memory Integrity Tests
// =============================================================================

#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_dma_types.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::device;

namespace {

struct Fixture {
    std::shared_ptr<SmpSharedMemory> mem;
    std::unique_ptr<SmpExclusiveMonitorManager> monitor;
    std::unique_ptr<VirtualDmaEngine> engine;
    const std::size_t ram_size{1024 * 1024}; // 1 MB
};

Fixture create_fixture() {
    Fixture f;
    f.mem = std::make_shared<SmpSharedMemory>(f.ram_size, 8);
    f.monitor = std::make_unique<SmpExclusiveMonitorManager>(8);
    f.mem->attach_exclusive_monitor(f.monitor.get());
    f.engine = std::make_unique<VirtualDmaEngine>(f.mem);
    return f;
}

// -----------------------------------------------------------------------------
// Group 1: End-to-End Guest-to-Guest Memory Copy
// -----------------------------------------------------------------------------
void test_group_1_copy_integrity() {
    std::cout << "[Group 1] Testing End-to-End Memory Copy Integrity...\n";
    auto f = create_fixture();

    const std::size_t transfer_len = 1024;
    std::vector<std::uint8_t> src_data(transfer_len);
    for (std::size_t i = 0; i < transfer_len; ++i) {
        src_data[i] = static_cast<std::uint8_t>((i * 17 + 3) & 0xFF);
    }

    const std::uint64_t src_addr = 0x10000;
    const std::uint64_t dst_addr = 0x20000;
    const std::uint64_t desc_addr = 0x5000;

    f.mem->write_bytes(0, src_addr, src_data.data(), transfer_len);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 101;
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = transfer_len;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = f.engine->execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());
    assert(res.descriptors_processed == 1);
    assert(res.bytes_transferred == transfer_len);

    std::vector<std::uint8_t> dst_data(transfer_len, 0);
    f.mem->read_bytes(0, dst_addr, dst_data.data(), transfer_len);
    assert(dst_data == src_data);

    // Verify descriptor status committed to memory
    VirtualDmaDescriptor updated_desc{};
    assert(f.engine->read_descriptor(desc_addr, updated_desc));
    assert(updated_desc.status == static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::Success));

    std::cout << "  -> Group 1 PASS (1024 bytes copied bit-identical)\n";
}

// -----------------------------------------------------------------------------
// Group 2: Pattern Fill Memory Integrity
// -----------------------------------------------------------------------------
void test_group_2_fill_integrity() {
    std::cout << "[Group 2] Testing Pattern Fill Memory Integrity...\n";
    auto f = create_fixture();

    const std::size_t fill_len = 300; // non-power-of-two length
    const std::uint32_t pattern = 0xCAFEBABEu;
    const std::uint64_t dst_addr = 0x30000;
    const std::uint64_t desc_addr = 0x6000;

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 202;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = fill_len;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = pattern;
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = f.engine->execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());

    std::vector<std::uint8_t> actual_dst(fill_len, 0);
    f.mem->read_bytes(0, dst_addr, actual_dst.data(), fill_len);

    const std::uint8_t pat_bytes[4] = {
        0xBE, 0xBA, 0xFE, 0xCA // Little-endian bytes of 0xCAFEBABE
    };
    for (std::size_t i = 0; i < fill_len; ++i) {
        assert(actual_dst[i] == pat_bytes[i % 4]);
    }

    std::cout << "  -> Group 2 PASS (Pattern fill verified)\n";
}

// -----------------------------------------------------------------------------
// Group 3: Zero-Partial-Writes on Rejected Transfers
// -----------------------------------------------------------------------------
void test_group_3_no_partial_writes() {
    std::cout << "[Group 3] Testing Zero-Partial-Writes on Rejected Transfers...\n";
    auto f = create_fixture();

    const std::uint64_t dst_addr = 0x40000;
    const std::size_t test_len = 512;

    // Fill destination with known marker bytes 0x55
    std::vector<std::uint8_t> marker(test_len, 0x55);
    f.mem->write_bytes(0, dst_addr, marker.data(), test_len);

    // Create a descriptor that starts inside guest RAM but extends out-of-bounds
    const std::uint64_t desc_addr = 0x7000;
    VirtualDmaDescriptor invalid_desc{};
    invalid_desc.descriptor_id = 303;
    invalid_desc.src_paddr = f.ram_size - 100; // Only 100 bytes left!
    invalid_desc.dst_paddr = dst_addr;
    invalid_desc.length_bytes = test_len; // 512 bytes exceeds 100 bytes!
    invalid_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    f.mem->write_bytes(0, desc_addr, &invalid_desc, sizeof(invalid_desc));

    auto res = f.engine->execute_descriptor_chain(desc_addr, 0);
    assert(res.is_failure());
    assert(res.status == VirtualDmaDescriptorStatus::ErrorOutOfBounds);
    assert(res.bytes_transferred == 0);

    // Crucial check: Destination memory MUST be 100% untouched!
    std::vector<std::uint8_t> check_dst(test_len, 0);
    f.mem->read_bytes(0, dst_addr, check_dst.data(), test_len);
    assert(check_dst == marker);

    std::cout << "  -> Group 3 PASS (Destination completely untouched on failure)\n";
}

// -----------------------------------------------------------------------------
// Group 4: Exact Memory Boundary Transfers
// -----------------------------------------------------------------------------
void test_group_4_boundary_transfers() {
    std::cout << "[Group 4] Testing Exact Memory Boundary Transfers...\n";
    auto f = create_fixture();

    const std::uint64_t length = 256;
    const std::uint64_t src_addr = 0x1000;
    const std::uint64_t dst_addr = f.ram_size - length; // Ends exactly at ram_size!
    const std::uint64_t desc_addr = 0x8000;

    std::vector<std::uint8_t> data(length, 0x7E);
    f.mem->write_bytes(0, src_addr, data.data(), length);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 404;
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = length;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = f.engine->execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());

    std::vector<std::uint8_t> read_back(length, 0);
    f.mem->read_bytes(0, dst_addr, read_back.data(), length);
    assert(read_back == data);

    std::cout << "  -> Group 4 PASS (Transfer to end of RAM succeeded)\n";
}

// -----------------------------------------------------------------------------
// Group 5: Exclusive Monitor Invalidation on DMA Store
// -----------------------------------------------------------------------------
void test_group_5_exclusive_monitor_invalidation() {
    std::cout << "[Group 5] Testing Exclusive Monitor Invalidation...\n";
    auto f = create_fixture();

    const std::uint64_t target_addr = 0x60000;

    // Core 1 sets exclusive reservation at target_addr
    f.monitor->load_exclusive(1, target_addr, 64);
    assert(f.monitor->is_reservation_valid(1, target_addr, 64));

    // Core 2 sets exclusive reservation at target_addr
    f.monitor->load_exclusive(2, target_addr, 64);
    assert(f.monitor->is_reservation_valid(2, target_addr, 64));

    // Core 0 executes DMA write to target_addr
    const std::uint64_t desc_addr = 0x9000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 505;
    desc.dst_paddr = target_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x11223344u;
    f.mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    auto res = f.engine->execute_descriptor_chain(desc_addr, 0);
    assert(res.is_success());

    // Both Core 1 and Core 2 reservations MUST have been invalidated by the DMA store!
    assert(!f.monitor->is_reservation_valid(1, target_addr, 64));
    assert(!f.monitor->is_reservation_valid(2, target_addr, 64));

    std::cout << "  -> Group 5 PASS (Reservations atomically invalidated)\n";
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual DMA Memory Integrity Tests      \n";
    std::cout << "===================================================================\n\n";

    test_group_1_copy_integrity();
    test_group_2_fill_integrity();
    test_group_3_no_partial_writes();
    test_group_4_boundary_transfers();
    test_group_5_exclusive_monitor_invalidation();

    std::cout << "\n>>> ALL VIRTUAL DMA MEMORY INTEGRITY TEST GROUPS PASSED <<<\n";
    return 0;
}
