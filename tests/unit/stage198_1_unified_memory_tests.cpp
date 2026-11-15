// =============================================================================
// WinIOS Campaign 198 Stage 198.1: Unified Guest Memory Access Architecture Tests
// =============================================================================

#include "memory/unified_memory.h"
#include "cpu/mmu.h"
#include "cpu/smp_exclusive_monitor.h"

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
using namespace winios::memory;

namespace {

// Helper to configure a basic 16KB granule MMU table
void setup_identity_mmu(Memory& mem, mmu::MMU& mmu, std::uint64_t ttbr_base = 0x10000) {
    // 16KB page tables: Granule16KB (TCR_EL1 TG0 = 0b10)
    // Attr0 in MAIR_EL1 = 0xFF (Normal Inner/Outer Write-Back RW-Allocate)
    // Attr1 in MAIR_EL1 = 0x00 (Device-nGnRnE)
    const std::uint64_t mair = 0x00FFULL;
    mmu.set_mair_el1(mair);

    // TCR_EL1: TG0=16KB (bit 14=0, 15=1 -> 0b10 << 14 = 0x8000), T0SZ=28 (bits 5:0 = 28 -> 36-bit VA)
    const std::uint64_t tcr = (2ULL << 14u) | (28ULL);
    mmu.set_tcr_el1(tcr);
    mmu.set_ttbr0_el1(ttbr_base);

    // SCTLR_EL1: M=1 (MMU enabled), C=1, I=1, A=0 (unaligned normal allowed)
    mmu.set_sctlr_el1(0x5ULL); // M=1, C=1, A=0

    // For 16KB granule with T0SZ=28 (36-bit VA), walk starts at Level 2 table at TTBR0
    // L2 table at ttbr_base: entry 0 points to L3 table at ttbr_base + 0x4000
    const std::uint64_t l3_base = ttbr_base + 0x4000;
    const std::uint64_t l2_desc = l3_base | 0x3ULL; // Table descriptor (bits [1:0] = 0b11)
    mem.write_u64(ttbr_base, l2_desc);

    // L3 table covers 16KB pages:
    // Page 0 (VA 0x0000 - 0x3FFF) -> PA 0x40000, Normal RW (AF=1, Page)
    const std::uint64_t l3_page0_desc = 0x40000ULL | 0x3ULL | (1ULL << 10u);
    mem.write_u64(l3_base, l3_page0_desc);

    // Page 1 (VA 0x4000 - 0x7FFF) -> PA 0x44000, Normal RW (AF=1, Page)
    const std::uint64_t l3_page1_desc = 0x44000ULL | 0x3ULL | (1ULL << 10u);
    mem.write_u64(l3_base + 8, l3_page1_desc);

    // Page 2 (VA 0x8000 - 0xBFFF) -> PA 0x48000, Device-nGnRnE (AttrIndx=1 -> bits [4:2] = 0b001, AF=1, Page)
    const std::uint64_t l3_page2_desc = 0x48000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 2u);
    mem.write_u64(l3_base + 16, l3_page2_desc);
}

void test_direct_physical_access() {
    std::cout << "[Test 1] Direct physical memory access...\n";
    Memory raw_mem(1024 * 1024);
    UnifiedMemoryView view(raw_mem, nullptr, 0, false);

    view.write_phys_u8(0x100, 0xAB);
    TEST_ASSERT(view.read_phys_u8(0x100) == 0xAB);

    view.write_phys_u16(0x200, 0x1234);
    TEST_ASSERT(view.read_phys_u16(0x200) == 0x1234);

    view.write_phys_u32(0x300, 0xDEADBEEF);
    TEST_ASSERT(view.read_phys_u32(0x300) == 0xDEADBEEF);

    view.write_phys_u64(0x400, 0x0123456789ABCDEFULL);
    TEST_ASSERT(view.read_phys_u64(0x400) == 0x0123456789ABCDEFULL);

    const std::uint8_t src[5] = {1, 2, 3, 4, 5};
    view.write_phys_bytes(0x500, src, 5);
    std::uint8_t dst[5] = {0};
    view.read_phys_bytes(0x500, dst, 5);
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(dst[i] == src[i]);
    }
    std::cout << "  -> PASSED\n";
}

void test_stage1_translation_normal_aligned() {
    std::cout << "[Test 2] Stage 1 translation normal aligned access...\n";
    Memory raw_mem(1024 * 1024);
    mmu::MMU mmu_inst;
    setup_identity_mmu(raw_mem, mmu_inst);

    UnifiedMemoryView view(raw_mem, &mmu_inst, 1 /* EL1 */, true);

    // VA 0x100 is in Page 0 -> PA 0x40100
    view.write_u64(0x100, 0xCAFEBABE11223344ULL);
    TEST_ASSERT(view.read_u64(0x100) == 0xCAFEBABE11223344ULL);
    TEST_ASSERT(raw_mem.read_u64(0x40100) == 0xCAFEBABE11223344ULL);

    view.write_u32(0x200, 0x99887766u);
    TEST_ASSERT(view.read_u32(0x200) == 0x99887766u);
    TEST_ASSERT(raw_mem.read_u32(0x40200) == 0x99887766u);
    std::cout << "  -> PASSED\n";
}

void test_stage1_translation_unaligned_cross_page() {
    std::cout << "[Test 3] Stage 1 translation unaligned cross-page access...\n";
    Memory raw_mem(1024 * 1024);
    mmu::MMU mmu_inst;
    setup_identity_mmu(raw_mem, mmu_inst);

    UnifiedMemoryView view(raw_mem, &mmu_inst, 1 /* EL1 */, true);

    // 16KB boundary is at VA 0x4000 (Page 0: [0x0000, 0x3FFF] -> PA [0x40000, 0x43FFF])
    // Page 1: [0x4000, 0x7FFF] -> PA [0x44000, 0x47FFF]
    // Write 8 bytes across boundary at VA 0x3FFC (4 bytes in Page 0, 4 bytes in Page 1)
    const std::uint64_t val = 0x8877665544332211ULL;
    view.write_u64(0x3FFC, val);

    // Read back via virtual address
    TEST_ASSERT(view.read_u64(0x3FFC) == val);

    // Verify physical memory layout:
    // First 4 bytes (0x44332211) at end of Page 0 (PA 0x43FFC)
    TEST_ASSERT(raw_mem.read_u32(0x43FFC) == 0x44332211u);
    // Next 4 bytes (0x88776655) at start of Page 1 (PA 0x44000)
    TEST_ASSERT(raw_mem.read_u32(0x44000) == 0x88776655u);

    std::cout << "  -> PASSED\n";
}

void test_strict_alignment_fault() {
    std::cout << "[Test 4] Strict alignment fault enforcement...\n";
    Memory raw_mem(1024 * 1024);
    mmu::MMU mmu_inst;
    setup_identity_mmu(raw_mem, mmu_inst);

    // Enable strict alignment check: SCTLR_EL1.A = 1 (bit 1)
    mmu_inst.set_sctlr_el1(mmu_inst.get_sctlr_el1() | 0x2ULL);

    UnifiedMemoryView view(raw_mem, &mmu_inst, 1, true);

    bool caught = false;
    try {
        (void)view.read_u64(0x101); // Misaligned 8-byte access with SCTLR.A = 1
    } catch (const SynchronousExceptionSignal& sig) {
        caught = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u); // Alignment fault FSC 0x21
        TEST_ASSERT(sig.exception.fault_address == 0x101);
    }
    TEST_ASSERT(caught);
    std::cout << "  -> PASSED\n";
}

void test_device_memory_unaligned_fault() {
    std::cout << "[Test 5] Device memory unaligned access fault (regardless of SCTLR.A)...\n";
    Memory raw_mem(1024 * 1024);
    mmu::MMU mmu_inst;
    setup_identity_mmu(raw_mem, mmu_inst);

    // Keep SCTLR_EL1.A = 0
    mmu_inst.set_sctlr_el1(mmu_inst.get_sctlr_el1() & ~0x2ULL);

    UnifiedMemoryView view(raw_mem, &mmu_inst, 1, true);

    // VA Page 2 (0x8000) is mapped to Device-nGnRnE memory
    // Aligned access should succeed
    view.write_u32(0x8000, 0x11223344);
    TEST_ASSERT(view.read_u32(0x8000) == 0x11223344);

    // Unaligned access to Device memory MUST fault with Alignment Fault (FSC 0x21)
    bool caught = false;
    try {
        (void)view.read_u32(0x8001);
    } catch (const SynchronousExceptionSignal& sig) {
        caught = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u); // Alignment fault
        TEST_ASSERT(sig.exception.fault_address == 0x8001);
    }
    TEST_ASSERT(caught);
    std::cout << "  -> PASSED\n";
}

void test_dc_zva_cache_zero_block() {
    std::cout << "[Test 6] DC ZVA cache zero block...\n";
    Memory raw_mem(1024 * 1024);
    mmu::MMU mmu_inst;
    setup_identity_mmu(raw_mem, mmu_inst);

    SmpExclusiveMonitorManager monitor(4, 64);
    UnifiedMemoryView view(raw_mem, &mmu_inst, 1, true);
    view.set_exclusive_monitor(&monitor);

    // Populate Page 0 with non-zero bytes
    for (std::size_t offset = 0; offset < 64; ++offset) {
        view.write_u8(0x200 + offset, 0xFF);
    }
    // Verify non-zero
    TEST_ASSERT(raw_mem.read_u64(0x40200) == 0xFFFFFFFFFFFFFFFFULL);

    // Set an exclusive reservation on core 1 overlapping VA 0x200 (PA 0x40200)
    monitor.load_exclusive(1, 0x40200, 8);
    TEST_ASSERT(monitor.is_reservation_valid(1, 0x40200, 8));

    // Execute cache_zero_block at VA 0x200 (64 bytes)
    view.cache_zero_block(0x200, 64);

    // Verify 64 bytes in physical memory at 0x40200 are now 0
    for (std::size_t offset = 0; offset < 64; offset += 8) {
        TEST_ASSERT(raw_mem.read_u64(0x40200 + offset) == 0ULL);
    }

    // Verify reservation on core 1 was invalidated
    TEST_ASSERT(!monitor.is_reservation_valid(1, 0x40200, 8));

    // DC ZVA to Device memory (VA 0x8000) MUST fault with Alignment
    bool caught_device = false;
    try {
        view.cache_zero_block(0x8000, 64);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_device = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u); // Alignment fault
    }
    TEST_ASSERT(caught_device);

    std::cout << "  -> PASSED\n";
}

void test_out_of_bounds_faults() {
    std::cout << "[Test 7] Out-of-bounds and external abort handling...\n";
    Memory raw_mem(1024 * 1024);
    UnifiedMemoryView view(raw_mem, nullptr, 1, true);

    // Access beyond memory size without MMU
    bool caught = false;
    try {
        (void)view.read_u64(2 * 1024 * 1024);
    } catch (const SynchronousExceptionSignal& sig) {
        caught = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x10u); // Synchronous external abort
    }
    TEST_ASSERT(caught);

    // Address exceeding physical limit 2^52
    bool caught_addr_size = false;
    try {
        (void)view.read_u64(kPhysicalAddressLimit + 0x1000);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_addr_size = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x00u); // Address size fault Level 0
    }
    TEST_ASSERT(caught_addr_size);
    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << "Stage 198.1: Unified Guest Memory Access Tests\n";
    std::cout << "====================================================\n";

    test_direct_physical_access();
    test_stage1_translation_normal_aligned();
    test_stage1_translation_unaligned_cross_page();
    test_strict_alignment_fault();
    test_device_memory_unaligned_fault();
    test_dc_zva_cache_zero_block();
    test_out_of_bounds_faults();

    std::cout << "\nAll Stage 198.1 Unified Memory tests passed successfully!\n";
    return 0;
}
