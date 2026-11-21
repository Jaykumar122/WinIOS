// ============================================================================
// Stage 252.0 - 252.4: Baseline Audit, Canonical Physical Memory Map,
// Frame Ownership Model, Allocator Reconciliation & Alias Consistency Tests
// ============================================================================

#include "cpu/cpu.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 252.0: Baseline Audit & Frame Overlap Reproduction
// ----------------------------------------------------------------------------
static void test_stage252_0_defect_reproduction() {
    std::cout << "[Stage 252.0] Reproducing Historical Frame Overlap Defect...\n";

    // In Campaign 250/251, the bootstrap stack was placed at 0x38000-0x48000
    // and page tables at 0x48000-0x50000.
    // Claiming that the dynamic pool started at Frame 16 (0x40000) to yield
    // 496 free frames resulted in an undisputable collision:
    // Frame 16 (0x40000-0x44000) and Frame 17 (0x44000-0x48000) were inside the stack!
    // Frame 18 (0x48000-0x4C000) and Frame 19 (0x4C000-0x50000) were inside page tables!

    MemoryInterval stack{"Stack", IntervalType::BootstrapStack, 0x38000ULL, 0x10000ULL, 0x3, 0x3, true, "Stack"};
    MemoryInterval pt{"PageTables", IntervalType::PageTables, 0x48000ULL, 0x8000ULL, 0x3, 0x3, true, "MMU"};
    MemoryInterval conflicting_pool{"ConflictingPool", IntervalType::DynamicPool, 0x40000ULL, 0x7C0000ULL, 0x3, 0x3, false, "Pool"};

    // Confirm that the conflicting pool directly overlaps stack and page tables
    TEST_ASSERT(conflicting_pool.overlaps_with(stack.base, stack.size), "Conflicting pool overlaps bootstrap stack");
    TEST_ASSERT(conflicting_pool.overlaps_with(pt.base, pt.size), "Conflicting pool overlaps page tables");

    std::cout << "[Stage 252.0] Defect successfully reproduced and documented.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 252.1: Canonical Physical Memory Map
// ----------------------------------------------------------------------------
static void test_stage252_1_canonical_physical_memory_map() {
    std::cout << "[Stage 252.1] Testing Canonical Physical Memory Map...\n";

    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);

    // Register all canonical intervals in half-open form [start, end)
    std::string err;
    TEST_ASSERT(pmap.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true, "Mach-O __TEXT"}, &err), "Add __TEXT");
    TEST_ASSERT(pmap.add_interval({"ReservedScratch", IntervalType::GenericReserved, 0x00004000ULL, 0x0000C000ULL, 0x3, 0x3, true, "Inter-segment Padding"}, &err), "Add Scratch");
    TEST_ASSERT(pmap.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true, "Mach-O __DATA"}, &err), "Add __DATA");
    TEST_ASSERT(pmap.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x00030000ULL, 0x00004000ULL, 0x3, 0x3, true, "Boot Metadata"}, &err), "Add DeviceTree");
    TEST_ASSERT(pmap.add_interval({"BootArgs", IntervalType::BootArgs, 0x00034000ULL, 0x00004000ULL, 0x3, 0x3, true, "Boot Metadata"}, &err), "Add BootArgs");
    TEST_ASSERT(pmap.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x00038000ULL, 0x00010000ULL, 0x3, 0x3, true, "Stack"}, &err), "Add Stack");
    TEST_ASSERT(pmap.add_interval({"PageTables", IntervalType::PageTables, 0x00048000ULL, 0x00008000ULL, 0x3, 0x3, true, "MMU Tables"}, &err), "Add PageTables");
    // DynamicPool begins strictly at 0x50000 (Frame 20)
    TEST_ASSERT(pmap.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x00050000ULL, 0x007B0000ULL, 0x3, 0x3, false, "Free Pool"}, &err), "Add DynamicPool");
    // External MMIO
    TEST_ASSERT(pmap.add_interval({"MmioAic", IntervalType::MmioAic, 0x23B100000ULL, 0x00100000ULL, 0x3, 0x3, true, "Apple AIC2"}, &err), "Add AIC2");
    TEST_ASSERT(pmap.add_interval({"MmioUart", IntervalType::MmioUart, 0x09000000ULL, 0x00001000ULL, 0x3, 0x3, true, "Virtual UART"}, &err), "Add UART");

    // Verify exactly 0 overlaps across entire map
    for (std::size_t i = 0; i < pmap.intervals().size(); ++i) {
        for (std::size_t j = i + 1; j < pmap.intervals().size(); ++j) {
            const auto& a = pmap.intervals()[i];
            const auto& b = pmap.intervals()[j];
            // MMIO outside DRAM does not overlap DRAM internal regions
            if (a.base >= 0x800000ULL || b.base >= 0x800000ULL) {
                TEST_ASSERT(a.base != b.base, "Distinct MMIO bases");
            } else {
                TEST_ASSERT(!a.overlaps_with(b.base, b.size), "Zero overlap between canonical intervals");
            }
        }
    }

    // Verify machine-readable export
    const std::string json = pmap.export_json();
    TEST_ASSERT(json.find("\"name\": \"DynamicPool\"") != std::string::npos, "JSON contains DynamicPool");
    TEST_ASSERT(json.find("\"base\": 327680") != std::string::npos, "DynamicPool base is 0x50000 (327680)");

    std::cout << "[Stage 252.1] Canonical Physical Memory Map verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 252.2: Physical Frame Ownership & Reservation Model
// ----------------------------------------------------------------------------
static void test_stage252_2_frame_ownership_model() {
    std::cout << "[Stage 252.2] Testing Physical Frame Ownership (Exhaustive 512 Frames)...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize 8 MiB allocator");
    TEST_ASSERT(alloc.total_pages() == 512, "Total pages is 512");

    // Reserve canonical firmware/kernel regions: Frames 0..19 (0x0 to 0x50000)
    TEST_ASSERT(alloc.reserve_range(0x00000000ULL, 0x00004000ULL, "KernelText", &err), "Reserve Text");
    TEST_ASSERT(alloc.reserve_range(0x00004000ULL, 0x0000C000ULL, "Scratch", &err), "Reserve Scratch");
    TEST_ASSERT(alloc.reserve_range(0x00010000ULL, 0x00020000ULL, "KernelData", &err), "Reserve Data");
    TEST_ASSERT(alloc.reserve_range(0x00030000ULL, 0x00004000ULL, "DeviceTree", &err), "Reserve DT");
    TEST_ASSERT(alloc.reserve_range(0x00034000ULL, 0x00004000ULL, "BootArgs", &err), "Reserve BootArgs");
    TEST_ASSERT(alloc.reserve_range(0x00038000ULL, 0x00010000ULL, "Stack", &err), "Reserve Stack");
    TEST_ASSERT(alloc.reserve_range(0x00048000ULL, 0x00008000ULL, "PageTables", &err), "Reserve PT");

    // Exhaustive frame audit across all 512 frames:
    // Frames 0..19 must be strictly Reserved!
    for (std::size_t f = 0; f < 20; ++f) {
        TEST_ASSERT(alloc.is_frame_reserved(f), "Frame in 0..19 is strictly Reserved");
        TEST_ASSERT(!alloc.is_frame_free(f), "Reserved frame cannot be Free");
        TEST_ASSERT(!alloc.is_frame_allocated(f), "Reserved frame cannot be Allocated");
    }

    // Frames 20..511 must be strictly Free!
    for (std::size_t f = 20; f < 512; ++f) {
        TEST_ASSERT(alloc.is_frame_free(f), "Frame in 20..511 is Free");
        TEST_ASSERT(!alloc.is_frame_reserved(f), "Free frame is not Reserved");
        TEST_ASSERT(!alloc.is_frame_allocated(f), "Free frame is not Allocated");
    }

    TEST_ASSERT(alloc.reserved_pages_count() == 20, "Reserved page count is exactly 20");
    TEST_ASSERT(alloc.free_pages_count() == 492, "Free page count is derived as exactly 492");

    std::cout << "[Stage 252.2] Frame ownership model verified across all 512 frames.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 252.3: Allocator Capacity Reconciliation & Boundary Tests
// ----------------------------------------------------------------------------
static void test_stage252_3_allocator_reconciliation() {
    std::cout << "[Stage 252.3] Testing Allocator Capacity Reconciliation & Boundary Tests...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x50000ULL, "StolenBootstrap"); // 20 frames reserved

    TEST_ASSERT(alloc.free_pages_count() == 492, "Exactly 492 free pages available");

    // 1. First allocation: must be Frame 20 (0x50000)
    auto p1 = alloc.allocate_pages(1);
    TEST_ASSERT(p1.has_value(), "First page allocated");
    TEST_ASSERT(*p1 == 0x50000ULL, "First allocated address is 0x50000 (Frame 20)");
    TEST_ASSERT(alloc.free_pages_count() == 491, "Free count decremented to 491");
    TEST_ASSERT(alloc.is_frame_allocated(20), "Frame 20 is Allocated");

    // 2. Allocate remainder of the 492 free pages
    auto p_all = alloc.allocate_pages(491);
    TEST_ASSERT(p_all.has_value(), "Allocated remaining 491 pages");
    TEST_ASSERT(alloc.free_pages_count() == 0, "Free page pool is completely exhausted");

    // 3. Exhaustion: next allocation must fail
    auto p_none = alloc.allocate_pages(1);
    TEST_ASSERT(!p_none.has_value(), "Allocation fails when exhausted");

    // 4. Double free rejection
    // Free p1 once: succeeds
    TEST_ASSERT(alloc.free_pages(*p1, 1, &err), "Free p1 succeeds");
    TEST_ASSERT(alloc.is_frame_free(20), "Frame 20 returned to Free");
    // Free p1 again: must fail as double free
    TEST_ASSERT(!alloc.free_pages(*p1, 1, &err), "Double free detected and rejected");
    TEST_ASSERT(err.find("Double free") != std::string::npos, "Double free error message emitted");

    // 5. Reserved-frame free rejection
    TEST_ASSERT(!alloc.free_pages(0x0ULL, 1, &err), "Freeing Frame 0 (KernelText) rejected");
    TEST_ASSERT(!alloc.free_pages(0x38000ULL, 1, &err), "Freeing Frame 14 (Stack) rejected");
    TEST_ASSERT(!alloc.free_pages(0x48000ULL, 1, &err), "Freeing Frame 18 (PageTables) rejected");

    // 6. Reallocation after free
    auto p_realloc = alloc.allocate_pages(1);
    TEST_ASSERT(p_realloc.has_value() && *p_realloc == 0x50000ULL, "Reallocated freed Frame 20");

    std::cout << "[Stage 252.3] Allocator capacity reconciliation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 252.4: Virtual Memory & Physical Alias Consistency
// ----------------------------------------------------------------------------
static void test_stage252_4_virtual_physical_alias_consistency() {
    std::cout << "[Stage 252.4] Testing Virtual Memory & Physical Alias Consistency...\n";

    XnuVmMap vmap(0x0ULL, 0x800000ULL, "kernel_map");
    vmap.set_strict_wx(true);

    std::string err;
    // Map text at 0x10000: Read + Execute
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "code", &err), "Map code");
    TEST_ASSERT(vmap.register_physical_alias(0x10000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err), "Register alias for PA 0x50000 as RO+X");
    TEST_ASSERT(vmap.alias_count() == 1, "Alias count is 1");

    // Attempt to register a writable alias covering PA 0x50000 -> must fail W^X!
    TEST_ASSERT(!vmap.register_physical_alias(0x20000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Conflicting writable alias rejected");
    TEST_ASSERT(err.find("W^X violation") != std::string::npos, "Error reports W^X alias conflict");

    // Unregister physical alias
    TEST_ASSERT(vmap.unregister_physical_alias(0x10000ULL, 0x4000ULL, &err), "Unregister alias");
    TEST_ASSERT(vmap.alias_count() == 0, "Alias count is 0 after unregister");

    // Once unregister completes, writable alias on that PA can now be registered
    TEST_ASSERT(vmap.register_physical_alias(0x20000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Writable alias now allowed after old alias removed");

    // Test vm_map_remove cleans up aliases
    TEST_ASSERT(vmap.vm_map_enter(0x20000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, kVmProtRead | kVmProtWrite, "data", &err), "Map data");
    TEST_ASSERT(vmap.vm_map_remove(0x20000ULL, 0x4000ULL, &err), "Remove entry");
    TEST_ASSERT(vmap.alias_count() == 0, "Alias automatically pruned on vm_map_remove");

    std::cout << "[Stage 252.4] Virtual memory and physical alias consistency verified.\n";
}

int main() {
    std::cout << "=== Running Stages 252.0 - 252.4 Unit Tests ===\n";
    test_stage252_0_defect_reproduction();
    test_stage252_1_canonical_physical_memory_map();
    test_stage252_2_frame_ownership_model();
    test_stage252_3_allocator_reconciliation();
    test_stage252_4_virtual_physical_alias_consistency();
    std::cout << "[PASS] All Stage 252.0 - 252.4 tests passed successfully.\n";
    return 0;
}
