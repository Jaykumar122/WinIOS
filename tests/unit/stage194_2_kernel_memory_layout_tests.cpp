// ============================================================================
// Stage 194.2: Kernel Memory Layout and Mapping Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "memory/kernel_memory_layout.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios::memory;

// -----------------------------------------------------------------------------
// Test 1: Canonical ARM64 Top-Half / Lower-Half Partitioning
// -----------------------------------------------------------------------------
static bool test_canonical_virtual_address_partitioning() {
    CHECK(KernelMemoryLayout::is_kernel_address(kKernelVirtualBase), "Kernel virtual base must be kernel address");
    CHECK(KernelMemoryLayout::is_kernel_address(0xFFFFFF8000000000ULL), "Top-half address must be kernel address");
    CHECK(KernelMemoryLayout::is_kernel_address(kKernelVirtualEnd), "Top-of-memory must be kernel address");
    CHECK(!KernelMemoryLayout::is_kernel_address(0x00007FFFFFFFFFFFULL), "User top must not be kernel address");
    CHECK(!KernelMemoryLayout::is_kernel_address(0x10000ULL), "Low address must not be kernel address");

    CHECK(KernelMemoryLayout::is_user_address(0x0ULL), "0x0 must be user address");
    CHECK(KernelMemoryLayout::is_user_address(0x10000000ULL), "0x10000000 must be user address");
    CHECK(KernelMemoryLayout::is_user_address(kUserVirtualEnd), "kUserVirtualEnd must be user address");
    CHECK(!KernelMemoryLayout::is_user_address(kKernelVirtualBase), "Kernel virtual base must not be user address");

    std::cout << "[PASS] test_canonical_virtual_address_partitioning\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Canonical Region Creation, Sizes, and Strict Permission Policies
// -----------------------------------------------------------------------------
static bool test_canonical_regions_and_permissions() {
    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL); // 256 MB DRAM

    std::string err;
    CHECK(layout.validate_layout(&err), "Canonical layout must pass validation: " + err);

    // Verify Kernel Text Region: RX (No Write, Executable)
    const auto* text = layout.find_region(kKernelTextBase);
    CHECK(text != nullptr, "Kernel text region must exist");
    CHECK(text->kind == MemoryRegionKind::KernelText, "Kind must be KernelText");
    CHECK(text->is_readable, "Kernel text must be readable");
    CHECK(!text->is_writable, "Kernel text must NOT be writable (W^X)");
    CHECK(text->is_executable, "Kernel text must be executable");
    CHECK(!text->is_guard, "Kernel text is not a guard page");

    // Verify Kernel Data Region: RW (Writable, Non-Executable)
    const auto* data = layout.find_region(kKernelDataBase);
    CHECK(data != nullptr, "Kernel data region must exist");
    CHECK(data->kind == MemoryRegionKind::KernelData, "Kind must be KernelData");
    CHECK(data->is_readable, "Kernel data must be readable");
    CHECK(data->is_writable, "Kernel data must be writable");
    CHECK(!data->is_executable, "Kernel data must NOT be executable (W^X)");

    // Verify Kernel Heap Region: RW (Writable, Non-Executable)
    const auto* heap = layout.find_region(kKernelHeapBase);
    CHECK(heap != nullptr, "Kernel heap region must exist");
    CHECK(heap->kind == MemoryRegionKind::KernelHeap, "Kind must be KernelHeap");
    CHECK(heap->is_readable, "Kernel heap must be readable");
    CHECK(heap->is_writable, "Kernel heap must be writable");
    CHECK(!heap->is_executable, "Kernel heap must NOT be executable");
    CHECK(heap->size == kKernelHeapSize, "Heap size must match 256 MB");

    // Verify Kernel Stack Region: RW (Writable, Non-Executable)
    const auto* stack = layout.find_region(kKernelStackBase);
    CHECK(stack != nullptr, "Kernel stack region must exist");
    CHECK(stack->kind == MemoryRegionKind::KernelStack, "Kind must be KernelStack");
    CHECK(stack->is_readable, "Kernel stack must be readable");
    CHECK(stack->is_writable, "Kernel stack must be writable");
    CHECK(!stack->is_executable, "Kernel stack must NOT be executable");

    std::cout << "[PASS] test_canonical_regions_and_permissions\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Guard Page Isolation Around Sensitive Data Structures
// -----------------------------------------------------------------------------
static bool test_guard_page_isolation() {
    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL);

    // Pre-Heap Guard Page
    CHECK(layout.is_guard_page(kKernelHeapBase - kGuardPageSize), "Pre-heap page must be guard page");
    CHECK(layout.is_guard_page(kKernelHeapBase - 1), "Pre-heap byte must be inside guard page");

    // Post-Heap Guard Page
    CHECK(layout.is_guard_page(kKernelHeapBase + kKernelHeapSize), "Post-heap page must be guard page");

    // Pre-Stack Guard Page
    CHECK(layout.is_guard_page(kKernelStackBase - kGuardPageSize), "Pre-stack page must be guard page");

    // Post-Stack Guard Page
    CHECK(layout.is_guard_page(kKernelStackBase + kKernelStackSize), "Post-stack page must be guard page");

    // Ordinary regions must not be guard pages
    CHECK(!layout.is_guard_page(kKernelTextBase), "Kernel text is not a guard page");
    CHECK(!layout.is_guard_page(kKernelDataBase), "Kernel data is not a guard page");
    CHECK(!layout.is_guard_page(kKernelHeapBase), "Kernel heap is not a guard page");
    CHECK(!layout.is_guard_page(kKernelStackBase), "Kernel stack is not a guard page");

    // All guard pages must have 0 access permissions
    for (const auto& r : layout.regions()) {
        if (r.is_guard) {
            CHECK(!r.is_readable && !r.is_writable && !r.is_executable,
                  "Guard pages must have zero access permissions");
        }
    }

    std::cout << "[PASS] test_guard_page_isolation\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Physical to Virtual Direct Mapping Bi-directional Translations
// -----------------------------------------------------------------------------
static bool test_direct_mapping_translations() {
    const std::uint64_t phys_base = 0x40000000ULL;
    const std::uint64_t phys_size = 0x10000000ULL; // 256 MB
    KernelMemoryLayout layout(phys_base, phys_size);

    // Physical to Virtual
    const std::uint64_t virt_start = layout.phys_to_virt(phys_base);
    CHECK(virt_start == kKernelDirectMapBase, "Direct map start must equal kKernelDirectMapBase");

    const std::uint64_t sample_offset = 0x02468000ULL;
    const std::uint64_t virt_sample = layout.phys_to_virt(phys_base + sample_offset);
    CHECK(virt_sample == kKernelDirectMapBase + sample_offset, "Direct map translation must preserve offset");

    // Virtual to Physical
    const std::uint64_t phys_sample = layout.virt_to_phys(virt_sample);
    CHECK(phys_sample == phys_base + sample_offset, "Reverse virtual to physical translation must match");

    // Out-of-bounds translation checks
    CHECK(layout.phys_to_virt(phys_base + phys_size) == 0, "Out of bounds PA must return 0");
    CHECK(layout.phys_to_virt(phys_base - 1) == 0, "Underflow PA must return 0");
    CHECK(layout.virt_to_phys(kKernelDirectMapBase + phys_size) == 0, "Out of bounds VA must return 0");

    std::cout << "[PASS] test_direct_mapping_translations\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Region Lookup and Overlap Validation
// -----------------------------------------------------------------------------
static bool test_region_lookup_and_layout_validation() {
    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL);

    // Query helper functions
    CHECK(KernelMemoryLayout::is_heap_address(kKernelHeapBase + 0x1000), "Heap helper check");
    CHECK(!KernelMemoryLayout::is_heap_address(kKernelDataBase), "Heap helper check negative");

    CHECK(KernelMemoryLayout::is_stack_address(kKernelStackBase + 0x2000), "Stack helper check");
    CHECK(!KernelMemoryLayout::is_stack_address(kKernelHeapBase), "Stack helper check negative");

    CHECK(KernelMemoryLayout::is_mmio_address(kKernelMmioBase + 0x4000), "MMIO helper check");
    CHECK(!KernelMemoryLayout::is_mmio_address(kKernelStackBase), "MMIO helper check negative");

    CHECK(KernelMemoryLayout::is_direct_map_address(kKernelDirectMapBase + 0x8000), "Direct map helper check");
    CHECK(!KernelMemoryLayout::is_direct_map_address(kKernelMmioBase), "Direct map helper check negative");

    // Validate 16KB Granule Alignment of all regions
    for (const auto& r : layout.regions()) {
        CHECK((r.virtual_address % kPageGranule16KB) == 0, "Region VA must be 16KB aligned");
        CHECK((r.size % kPageGranule16KB) == 0, "Region size must be multiple of 16KB");
    }

    std::cout << "[PASS] test_region_lookup_and_layout_validation\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.2 Kernel Memory Layout Tests...\n";

    if (!test_canonical_virtual_address_partitioning()) return 1;
    if (!test_canonical_regions_and_permissions()) return 1;
    if (!test_guard_page_isolation()) return 1;
    if (!test_direct_mapping_translations()) return 1;
    if (!test_region_lookup_and_layout_validation()) return 1;

    std::cout << "\nAll Stage 194.2 Kernel Memory Layout tests PASSED successfully.\n";
    return 0;
}
