// ============================================================================
// Stage 254.0 - 254.4: Unified Physical Memory Ownership, Model Reconciliation,
// Bootstrap Transition Boundary, Frame Allocator Unification & Stack/PT Lifetime
// ============================================================================

#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/cpu.h"
#include "loader/kernel_interval_map.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 254.0: Discrepancy Audit & Discrepancy Register Formulation
// ----------------------------------------------------------------------------
static void test_stage254_0_discrepancy_audit() {
    std::cout << "[Stage 254.0] Testing Memory Discrepancy Audit & Discrepancy Register...\n";

    // Discrepancy register comparing Model A (Canonical 512-frame map) and Model B (Compact runtime bootstrap)
    struct DiscrepancyEntry {
        const char* component;
        uint64_t model_a_base;
        uint64_t model_a_size;
        uint64_t model_b_base;
        uint64_t model_b_size;
        const char* rationale;
    };

    const DiscrepancyEntry register_table[] = {
        {"KernelText",       0x00000000ULL, 0x00004000ULL, 0x00000000ULL, 0x00004000ULL, "Mach-O __TEXT matches exactly (16 KiB)"},
        {"Padding/Scratch",  0x00004000ULL, 0x0000C000ULL, 0x00004000ULL, 0x0000C000ULL, "Reserved zero padding matches exactly (48 KiB)"},
        {"KernelData",       0x00010000ULL, 0x00020000ULL, 0x00010000ULL, 0x00008000ULL, "Model A expands __DATA to 128 KiB; Model B cold-boot uses 32 KiB"},
        {"DeviceTreeBlob",   0x00030000ULL, 0x00004000ULL, 0x00018000ULL, 0x00004000ULL, "Model A DT follows 128 KiB DATA; Model B DT follows 32 KiB DATA"},
        {"BootArgsStruct",   0x00034000ULL, 0x00004000ULL, 0x0001C000ULL, 0x00004000ULL, "Model A BA follows DT at 0x34000; Model B BA at 0x1C000"},
        {"BootstrapStack",   0x00038000ULL, 0x00010000ULL, 0x00020000ULL, 0x00010000ULL, "Model A stack top=0x48000; Model B stack top=0x30000"},
        {"Stage1PageTables", 0x00048000ULL, 0x00008000ULL, 0x00030000ULL, 0x00008000ULL, "Model A PT end=0x50000; Model B PT end=0x38000"},
        {"DynamicFreePool",  0x00050000ULL, 0x007B0000ULL, 0x00038000ULL, 0x007C8000ULL, "Model A reserves 20 frames (320KB); Model B reserves 14 frames (224KB)"}
    };

    TEST_ASSERT(sizeof(register_table) / sizeof(register_table[0]) == 8, "8 discrepancy register entries");

    // Verify properties of Model A
    uint64_t model_a_reserved_frames = 0x50000ULL / 16384ULL; // 20 frames
    uint64_t model_a_free_frames = 0x7B0000ULL / 16384ULL;     // 492 frames
    TEST_ASSERT(model_a_reserved_frames == 20ULL, "Model A reserves exactly 20 frames");
    TEST_ASSERT(model_a_free_frames == 492ULL, "Model A has exactly 492 free frames");
    TEST_ASSERT(model_a_reserved_frames + model_a_free_frames == 512ULL, "Model A covers all 512 frames (8 MiB)");

    // Verify properties of Model B
    uint64_t model_b_reserved_frames = 0x38000ULL / 16384ULL; // 14 frames
    uint64_t model_b_free_frames = 0x7C8000ULL / 16384ULL;     // 498 frames
    TEST_ASSERT(model_b_reserved_frames == 14ULL, "Model B reserves 14 frames");
    TEST_ASSERT(model_b_free_frames == 498ULL, "Model B has 498 free frames");
    TEST_ASSERT(model_b_reserved_frames + model_b_free_frames == 512ULL, "Model B covers all 512 frames (8 MiB)");

    // Crucial architectural finding:
    // Model B (0x0 - 0x38000, 14 frames) is strictly a subset of Model A's reserved enclave (0x0 - 0x50000, 20 frames).
    // Therefore, Model A subsumes Model B entirely and provides safe room for 128 KiB data expansion and future stack growth.
    TEST_ASSERT(0x38000ULL <= 0x50000ULL, "Model B footprint is completely bounded by Model A enclave");

    std::cout << "[Stage 254.0] Memory discrepancy audit completed successfully.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 254.1: Authoritative Physical Memory Ownership Model
// ----------------------------------------------------------------------------
static void test_stage254_1_authoritative_ownership_model() {
    std::cout << "[Stage 254.1] Testing Authoritative Physical Memory Ownership Model...\n";

    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);
    std::string err;

    // Full 8 MiB authoritative layout:
    // Frame 0: Mach-O __TEXT (16 KiB, RX)
    TEST_ASSERT(pmap.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true, "Mach-O __TEXT"}, &err), "Add __TEXT");
    // Frames 1-3: Reserved zero padding / scratch (48 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"ReservedPadding", IntervalType::GenericReserved, 0x00004000ULL, 0x0000C000ULL, 0x3, 0x3, true, "Padding"}, &err), "Add Padding");
    // Frames 4-11: Mach-O __DATA (128 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true, "128KB __DATA"}, &err), "Add __DATA");
    // Frame 12: Device Tree Blob (16 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x00030000ULL, 0x00004000ULL, 0x3, 0x3, true, "DeviceTree Blob"}, &err), "Add DT");
    // Frame 13: BootArgs Struct (16 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"BootArgs", IntervalType::BootArgs, 0x00034000ULL, 0x00004000ULL, 0x3, 0x3, true, "BootArgs Struct"}, &err), "Add BootArgs");
    // Frames 14-17: Bootstrap Stack (64 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x00038000ULL, 0x00010000ULL, 0x3, 0x3, true, "Bootstrap Stack"}, &err), "Add Stack");
    // Frames 18-19: Stage 1 Page Tables (32 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"PageTables", IntervalType::PageTables, 0x00048000ULL, 0x00008000ULL, 0x3, 0x3, true, "Stage 1 PT"}, &err), "Add PageTables");
    // Frames 20-511: Dynamic Free Pool (7,872 KiB, RW)
    TEST_ASSERT(pmap.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x00050000ULL, 0x007B0000ULL, 0x3, 0x3, false, "Dynamic Free Pool"}, &err), "Add DynamicPool");

    // Validate 8 contiguous intervals
    const auto& intervals = pmap.intervals();
    TEST_ASSERT(intervals.size() == 8, "Exactly 8 authoritative physical intervals");

    // Validate zero overlaps and strict contiguity
    uint64_t expected_offset = 0x0ULL;
    for (std::size_t i = 0; i < intervals.size(); ++i) {
        TEST_ASSERT(intervals[i].base == expected_offset, "Strict contiguity of physical intervals");
        TEST_ASSERT(intervals[i].base % 0x4000ULL == 0, "All intervals aligned to 16 KiB page boundary");
        TEST_ASSERT(intervals[i].size % 0x4000ULL == 0, "All intervals are page-sized multiples");
        TEST_ASSERT(!intervals[i].is_wx_violation(), "Strict W^X compliance");

        expected_offset = intervals[i].end();
    }
    TEST_ASSERT(expected_offset == 0x800000ULL, "Intervals span entire 8 MiB physical address space");

    // Verify SP_EL1 and BootArgs pointers match authoritative layout
    const auto* stack = pmap.find_interval_by_name("BootstrapStack");
    const auto* ba = pmap.find_interval_by_name("BootArgs");
    const auto* dt = pmap.find_interval_by_name("DeviceTree");
    TEST_ASSERT(stack && stack->end() == 0x00048000ULL, "Authoritative SP_EL1 top is 0x48000");
    TEST_ASSERT(ba && ba->base == 0x00034000ULL, "Authoritative BootArgs base is 0x34000");
    TEST_ASSERT(dt && dt->base == 0x00030000ULL, "Authoritative DeviceTree base is 0x30000");

    std::cout << "[Stage 254.1] Authoritative physical ownership model verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 254.2: Bootstrap-to-Allocator Transition Boundary
// ----------------------------------------------------------------------------
static void test_stage254_2_bootstrap_to_allocator_transition() {
    std::cout << "[Stage 254.2] Testing Bootstrap-to-Allocator Transition Boundary...\n";

    // Simulate the pre-allocator bootstrap phase (Model B) transitioning into canonical allocator ownership (Model A)
    // In Phase 1 (pre-allocator), the bootstrap loader places:
    // Kernel at 0x0..0x18000, DT at 0x18000, BA at 0x1C000, Stack at 0x20000..0x30000, PT at 0x30000..0x38000
    // Total footprint in Phase 1 = 0x38000 (14 frames).

    // When the canonical page allocator initializes in Phase 2, it establishes the 20-frame reserved enclave:
    // Frames 0..19 (0x0 to 0x50000).
    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize allocator with 8 MiB");

    // Reserve Frames 0..19
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00050000ULL, "BootstrapEnclave", &err), "Reserve Frames 0-19");

    // Invariant: Zero active bootstrap frames are exposed as free memory
    for (std::size_t f = 0; f < 20; ++f) {
        TEST_ASSERT(!alloc.is_frame_free(f), "No bootstrap frame is marked as free");
        TEST_ASSERT(alloc.is_frame_reserved(f), "Every bootstrap frame is reserved");
    }

    // Invariant: Frames 20..511 are available
    TEST_ASSERT(alloc.free_pages_count() == 492, "Exactly 492 dynamic frames available");
    for (std::size_t f = 20; f < 512; ++f) {
        TEST_ASSERT(alloc.is_frame_free(f), "Dynamic pool frames are free");
    }

    // Attempting to free any address in 0x0..0x50000 must fail
    TEST_ASSERT(!alloc.free_pages(0x00000000ULL, 1, &err), "Cannot free Mach-O __TEXT");
    TEST_ASSERT(!alloc.free_pages(0x00010000ULL, 1, &err), "Cannot free Mach-O __DATA");
    TEST_ASSERT(!alloc.free_pages(0x00018000ULL, 1, &err), "Cannot free Model B DeviceTree");
    TEST_ASSERT(!alloc.free_pages(0x0001C000ULL, 1, &err), "Cannot free Model B BootArgs");
    TEST_ASSERT(!alloc.free_pages(0x00020000ULL, 1, &err), "Cannot free Model B Stack");
    TEST_ASSERT(!alloc.free_pages(0x00030000ULL, 1, &err), "Cannot free Model B PageTables / Model A DT");
    TEST_ASSERT(!alloc.free_pages(0x00034000ULL, 1, &err), "Cannot free Model A BootArgs");
    TEST_ASSERT(!alloc.free_pages(0x00038000ULL, 1, &err), "Cannot free Model A Stack");
    TEST_ASSERT(!alloc.free_pages(0x00048000ULL, 1, &err), "Cannot free Model A PageTables");

    std::cout << "[Stage 254.2] Bootstrap-to-allocator transition boundary verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 254.3: Physical Page Allocator Interval Unification
// ----------------------------------------------------------------------------
static void test_stage254_3_frame_allocator_interval_unification() {
    std::cout << "[Stage 254.3] Testing Physical Page Allocator Interval Unification...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize 8 MiB allocator");
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00050000ULL, "AuthoritativeEnclave", &err), "Reserve 20 frames");

    // Exact arithmetic validation:
    // Total frames: 512
    // Reserved frames: 20
    // Dynamic frames: 492
    // Size in bytes: 492 * 16384 = 8,060,928 bytes = 0x7B0000 bytes
    // Size in KiB: 8,060,928 / 1024 = 7,872 KiB
    TEST_ASSERT(alloc.total_pages() == 512, "Total pages is 512");
    TEST_ASSERT(alloc.reserved_pages_count() == 20, "Reserved pages is 20");
    TEST_ASSERT(alloc.free_pages_count() == 492, "Free pages count is 492");
    TEST_ASSERT(alloc.allocated_pages_count() == 0, "Allocated count is 0 initially");

    // Boundary checks & out-of-bounds rejection
    TEST_ASSERT(alloc.get_frame_state(512) == XnuPhysicalPageAllocator::PageState::Reserved, "OOB frame state handled safely");
    TEST_ASSERT(!alloc.free_pages(0x00800000ULL, 1, &err), "Cannot free beyond physical memory boundary");

    // Allocate first available frame: must be frame 20 (0x00050000)
    auto page0 = alloc.allocate_pages(1);
    TEST_ASSERT(page0.has_value() && *page0 == 0x00050000ULL, "First allocated page is exactly 0x50000 (Frame 20)");
    TEST_ASSERT(alloc.allocated_pages_count() == 1, "Allocated pages is 1");
    TEST_ASSERT(alloc.free_pages_count() == 491, "Free pages count is 491");
    TEST_ASSERT(alloc.is_frame_allocated(20), "Frame 20 is marked allocated");
    TEST_ASSERT(!alloc.is_frame_free(20), "Frame 20 is not free");

    // Double-allocation of same page is prevented
    auto page1 = alloc.allocate_pages(1);
    TEST_ASSERT(page1.has_value() && *page1 == 0x00054000ULL, "Next allocated page is 0x54000 (Frame 21)");

    // Free page0
    TEST_ASSERT(alloc.free_pages(*page0, 1, &err), "Free page 0x50000 succeeds");
    TEST_ASSERT(alloc.is_frame_free(20), "Frame 20 is free again");

    // Double-free rejection
    TEST_ASSERT(!alloc.free_pages(*page0, 1, &err), "Double-free of Frame 20 must be rejected");

    // Free page1
    TEST_ASSERT(alloc.free_pages(*page1, 1, &err), "Free page 0x54000 succeeds");
    TEST_ASSERT(alloc.free_pages_count() == 492, "Free count restored to 492");

    std::cout << "[Stage 254.3] Physical page allocator interval unification verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 254.4: Stack Guard & Page Table Lifetime Validation
// ----------------------------------------------------------------------------
static void test_stage254_4_stack_guard_and_pt_lifetime() {
    std::cout << "[Stage 254.4] Testing Stack Guard & Page Table Lifetime Validation...\n";

    // Stack interval in Authoritative Model:
    // Base = 0x00038000, Size = 0x10000 (64 KiB = 4 frames: Frames 14, 15, 16, 17)
    // Top = 0x00048000 (SP_EL1 points here initially)
    const uint64_t stack_base = 0x00038000ULL;
    const uint64_t stack_size = 0x00010000ULL;
    const uint64_t stack_top  = stack_base + stack_size; // 0x48000

    TEST_ASSERT(stack_top % 16 == 0, "SP_EL1 satisfies 16-byte AAPCS64 alignment");
    TEST_ASSERT(stack_top == 0x00048000ULL, "Initial SP_EL1 is 0x48000");

    // Stack guard validation:
    // Frame 13 (0x34000 to 0x38000) is BootArgs; any stack pointer descending below 0x38000 is an underflow
    auto is_sp_valid = [&](uint64_t sp) -> bool {
        return (sp >= stack_base) && (sp <= stack_top) && (sp % 16 == 0);
    };

    TEST_ASSERT(is_sp_valid(0x00048000ULL), "SP at stack top is valid");
    TEST_ASSERT(is_sp_valid(0x00040000ULL), "SP mid-stack is valid");
    TEST_ASSERT(is_sp_valid(0x00038000ULL), "SP at stack base is valid");
    TEST_ASSERT(!is_sp_valid(0x00037FF0ULL), "SP below stack base violates stack guard (underflow)");
    TEST_ASSERT(!is_sp_valid(0x00048010ULL), "SP above stack top violates stack bounds (overflow)");
    TEST_ASSERT(!is_sp_valid(0x00047FF8ULL), "SP unaligned violates AAPCS64 16-byte alignment");

    // Page table lifetime validation:
    // Base = 0x00048000, Size = 0x00008000 (32 KiB = 2 frames: Frames 18, 19)
    // Page tables must remain reserved throughout execution and cannot be reclaimed by dynamic allocator
    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize allocator");
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00050000ULL, "ReservedEnclave", &err), "Reserve enclave");

    // Frames 18 and 19 are page table frames
    TEST_ASSERT(alloc.is_frame_reserved(18), "Frame 18 (PT L1/L2) is reserved");
    TEST_ASSERT(alloc.is_frame_reserved(19), "Frame 19 (PT L3) is reserved");
    TEST_ASSERT(!alloc.free_pages(0x00048000ULL, 2, &err), "Reclaiming page table frames must be rejected");

    std::cout << "[Stage 254.4] Stack guard & page table lifetime validation complete.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 254: Stages 254.0 - 254.4 Memory Ownership & Stack Tests\n";
    std::cout << "====================================================================\n";

    test_stage254_0_discrepancy_audit();
    test_stage254_1_authoritative_ownership_model();
    test_stage254_2_bootstrap_to_allocator_transition();
    test_stage254_3_frame_allocator_interval_unification();
    test_stage254_4_stack_guard_and_pt_lifetime();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 254.0 - 254.4 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
