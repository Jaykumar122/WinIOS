// ==============================================================================
// WinIOS Campaign 200 Stage 7: Memory Attributes and Cache Integration Tests
// ==============================================================================
// Verifies:
// 1. Normal vs Device memory attributes decoding from MAIR_EL1
// 2. Cacheability and Shareability domain decoding
// 3. DC ZVA 64-byte block zeroing conformance
// 4. Cache maintenance range checks (DC CVAU / IC IVAU)
// 5. Exclusive reservation 64-byte cache-line granule
// 6. Cross-core store invalidation of exclusive reservation inside granule
// 7. Store outside reservation granule preserves reservation
// 8. DMA write coherency and monitor invalidation
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/smp_exclusive_monitor.h"
#include "memory/memory.h"
#include "memory/memory_attributes.h"
#include "memory/unified_memory.h"

using namespace winios;
using namespace winios::memory;

static void test_memory_attribute_decoding() {
    // Normal Memory: Write-Back, Write-Allocate (0xFF)
    auto norm_wb = decode_memory_attributes(0xFF, 0b11);
    assert(norm_wb.type == MemoryType::Normal);
    assert(norm_wb.inner_cacheability == Cacheability::WriteBack);
    assert(norm_wb.outer_cacheability == Cacheability::WriteBack);
    assert(norm_wb.shareability == ShareabilityDomain::InnerShareable);
    assert(!norm_wb.is_device);
    assert(norm_wb.allow_dc_zva);
    assert(norm_wb.allow_atomics);

    // Device Memory: Device_nGnRnE (0x00)
    auto dev_ngnrne = decode_memory_attributes(0x00, 0b10);
    assert(dev_ngnrne.type == MemoryType::Device_nGnRnE);
    assert(dev_ngnrne.is_device);
    assert(!dev_ngnrne.allow_dc_zva);

    // Normal Write-Through (0xBB)
    auto norm_wt = decode_memory_attributes(0xBB, 0b11);
    assert(norm_wt.type == MemoryType::Normal);
    assert(norm_wt.inner_cacheability == Cacheability::WriteThrough);
    assert(!norm_wt.is_device);

    std::cout << "[PASS] test_memory_attribute_decoding\n";
}

static void test_dc_zva_block_clearing() {
    Memory mem(1024 * 1024 * 2);
    UnifiedMemoryView uview(mem, nullptr, 1, false);

    const std::uint64_t block_addr = 0x1000ULL;
    // Fill 64-byte block with pattern 0x5A
    for (std::size_t i = 0; i < 64; ++i) {
        mem.write_u8(static_cast<std::size_t>(block_addr + i), 0x5A);
    }
    // Verify filled
    assert(mem.read_u8(static_cast<std::size_t>(block_addr)) == 0x5A);
    assert(mem.read_u8(static_cast<std::size_t>(block_addr + 63)) == 0x5A);

    // Execute DC ZVA (64-byte cache block zeroing)
    uview.cache_zero_block(block_addr, 64);

    // Verify all 64 bytes are zeroed
    for (std::size_t i = 0; i < 64; ++i) {
        assert(mem.read_u8(static_cast<std::size_t>(block_addr + i)) == 0x00);
    }

    std::cout << "[PASS] test_dc_zva_block_clearing\n";
}

static void test_cache_maintenance_range_checks() {
    Memory mem(1024 * 1024 * 2);
    UnifiedMemoryView uview(mem, nullptr, 1, false);

    const std::uint64_t va = 0x2000ULL;
    // In raw mode, check_cache_range on valid range should not throw
    bool ok = true;
    try {
        uview.check_cache_range(va, 128, AccessType::Read, false);
    } catch (...) {
        ok = false;
    }
    assert(ok);

    std::cout << "[PASS] test_cache_maintenance_range_checks\n";
}

static void test_exclusive_monitor_granule_and_invalidation() {
    SmpExclusiveMonitorManager monitor(4); // 4 cores, 64-byte granule
    assert(monitor.granule_size() == 64);

    const std::uint64_t line_addr = 0x8000ULL;

    // Core 0 sets exclusive reservation on 8 bytes at 0x8000
    monitor.load_exclusive(0, line_addr, 8);
    assert(monitor.is_reservation_valid(0, line_addr, 8));

    // Core 1 writes to another address within the same 64-byte cache line (0x8018)
    monitor.invalidate_reservations(1, line_addr + 0x18, 4);

    // Core 0's reservation must be invalidated due to 64-byte cache line granule!
    assert(!monitor.is_reservation_valid(0, line_addr, 8));

    // Now re-reserve line 0x8000 on Core 0
    monitor.load_exclusive(0, line_addr, 8);
    assert(monitor.is_reservation_valid(0, line_addr, 8));

    // Core 1 writes outside the cache line (0x8040 = next cache line)
    monitor.invalidate_reservations(1, line_addr + 0x40, 4);

    // Core 0's reservation must remain valid!
    assert(monitor.is_reservation_valid(0, line_addr, 8));

    // Core 0 store_exclusive succeeds
    bool executed = false;
    bool store_ok = monitor.store_exclusive(0, line_addr, 8, [&]() {
        executed = true;
    });
    assert(store_ok);
    (void)store_ok;
    assert(executed);
    // Reservation cleared after store_exclusive
    assert(!monitor.is_reservation_valid(0, line_addr, 8));

    std::cout << "[PASS] test_exclusive_monitor_granule_and_invalidation\n";
}

static void test_dma_cache_coherency_and_monitor() {
    Memory mem(1024 * 1024 * 2);
    SmpExclusiveMonitorManager monitor(4);
    UnifiedMemoryView uview(mem, nullptr, 1, false);
    uview.set_exclusive_monitor(&monitor);

    const std::uint64_t target_pa = 0x5000ULL;
    uview.write_phys_u32(target_pa, 0x11223344);

    // Core 0 sets reservation
    monitor.load_exclusive(0, target_pa, 4);
    assert(monitor.is_reservation_valid(0, target_pa, 4));

    // Simulated DMA engine writes to physical memory and invalidates reservation
    uview.write_phys_u32(target_pa, 0x99887766);
    uview.invalidate_reservation(999, target_pa, 4); // writer id 999 (DMA)

    // CPU reads new coherently updated value
    assert(uview.read_phys_u32(target_pa) == 0x99887766);

    // Core 0's reservation was invalidated by DMA write
    assert(!monitor.is_reservation_valid(0, target_pa, 4));

    std::cout << "[PASS] test_dma_cache_coherency_and_monitor\n";
}

int main() {
    std::cout << "=== Running Stage 200.7 Memory Attributes and Cache Integration Tests ===\n";
    test_memory_attribute_decoding();
    test_dc_zva_block_clearing();
    test_cache_maintenance_range_checks();
    test_exclusive_monitor_granule_and_invalidation();
    test_dma_cache_coherency_and_monitor();
    std::cout << "All Stage 200.7 tests passed successfully!\n";
    return 0;
}
