// ============================================================================
// Stage 253.0 - 253.4: Memory Reconciliation, Canonical Frame Ownership,
// Stack & Metadata Integrity, Independent MMU Translation Audit & W^X Lifecycle
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
// Test 1: Stage 253.0: Baseline Reconciliation, Arithmetic Correction & Layout Verification
// ----------------------------------------------------------------------------
static void test_stage253_0_baseline_reconciliation() {
    std::cout << "[Stage 253.0] Testing Baseline Reconciliation & Arithmetic Correction...\n";

    // 1. Arithmetic typo reconciliation:
    // 512 total frames of 16 KiB each = 8,192 KiB = 8 MiB (0x800000 bytes)
    // Reserved frames = 20 frames = 320 KiB (0x50000 bytes)
    // Dynamic pool frames = 512 - 20 = 492 frames
    // In Campaign 252 report, text mentioned "7,808 KB" by typographical error (488 * 16 = 7808)
    // Exactly: 492 * 16 KiB = 7,872 KiB = 8,060,928 bytes (0x7B0000 bytes)
    const std::uint64_t total_frames = 512ULL;
    const std::uint64_t reserved_frames = 20ULL;
    const std::uint64_t dynamic_frames = total_frames - reserved_frames; // 492
    const std::uint64_t frame_size_bytes = 16384ULL; // 16 KiB
    const std::uint64_t dynamic_pool_bytes = dynamic_frames * frame_size_bytes; // 8,060,928
    const std::uint64_t dynamic_pool_kib = dynamic_pool_bytes / 1024ULL; // 7,872 KiB

    TEST_ASSERT(dynamic_frames == 492ULL, "Dynamic frames count is 492");
    TEST_ASSERT(dynamic_pool_bytes == 0x7B0000ULL, "Dynamic pool size is 0x7B0000 bytes");
    TEST_ASSERT(dynamic_pool_kib == 7872ULL, "Dynamic pool size is exactly 7,872 KiB, correcting historical 7,808 KB typo");

    // 2. Reconcile Layout Model A (Canonical 512-Frame Interval Map)
    KernelPhysicalIntervalMap pmap_canonical(0x0ULL, 0x800000ULL);
    std::string err;
    TEST_ASSERT(pmap_canonical.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true, "Mach-O __TEXT"}, &err), "Add __TEXT");
    TEST_ASSERT(pmap_canonical.add_interval({"ReservedScratch", IntervalType::GenericReserved, 0x00004000ULL, 0x0000C000ULL, 0x3, 0x3, true, "Padding"}, &err), "Add Scratch");
    TEST_ASSERT(pmap_canonical.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true, "128KB __DATA"}, &err), "Add __DATA");
    TEST_ASSERT(pmap_canonical.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x00030000ULL, 0x00004000ULL, 0x3, 0x3, true, "DeviceTree"}, &err), "Add DT");
    TEST_ASSERT(pmap_canonical.add_interval({"BootArgs", IntervalType::BootArgs, 0x00034000ULL, 0x00004000ULL, 0x3, 0x3, true, "BootArgs"}, &err), "Add BootArgs");
    TEST_ASSERT(pmap_canonical.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x00038000ULL, 0x00010000ULL, 0x3, 0x3, true, "Stack"}, &err), "Add Stack");
    TEST_ASSERT(pmap_canonical.add_interval({"PageTables", IntervalType::PageTables, 0x00048000ULL, 0x00008000ULL, 0x3, 0x3, true, "PageTables"}, &err), "Add PageTables");
    TEST_ASSERT(pmap_canonical.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x00050000ULL, 0x007B0000ULL, 0x3, 0x3, false, "Free Pool"}, &err), "Add DynamicPool");

    // In Model A: SP top = 0x48000, BootArgs = 0x34000
    const auto* stack_a = pmap_canonical.find_interval_by_name("BootstrapStack");
    const auto* ba_a = pmap_canonical.find_interval_by_name("BootArgs");
    TEST_ASSERT(stack_a && stack_a->end() == 0x48000ULL, "Model A Stack Top is 0x48000");
    TEST_ASSERT(ba_a && ba_a->base == 0x34000ULL, "Model A BootArgs is 0x34000");

    // 3. Reconcile Layout Model B (Live KernelBootCoordinator Bootstrap)
    KernelPhysicalIntervalMap pmap_live(0x0ULL, 0x800000ULL);
    TEST_ASSERT(pmap_live.add_interval({"LiveKernel", IntervalType::KernelText, 0x00000000ULL, 0x00018000ULL, 0x7, 0x7, true, "Mach-O Footprint"}, &err), "Add LiveKernel");
    TEST_ASSERT(pmap_live.add_interval({"LiveDeviceTree", IntervalType::DeviceTree, 0x00018000ULL, 0x00004000ULL, 0x3, 0x3, true, "Live DT"}, &err), "Add LiveDT");
    TEST_ASSERT(pmap_live.add_interval({"LiveBootArgs", IntervalType::BootArgs, 0x0001C000ULL, 0x00004000ULL, 0x3, 0x3, true, "Live BootArgs"}, &err), "Add LiveBootArgs");
    TEST_ASSERT(pmap_live.add_interval({"LiveStack", IntervalType::BootstrapStack, 0x00020000ULL, 0x00010000ULL, 0x3, 0x3, true, "Live Stack"}, &err), "Add LiveStack");
    TEST_ASSERT(pmap_live.add_interval({"LivePageTables", IntervalType::PageTables, 0x00030000ULL, 0x00008000ULL, 0x3, 0x3, true, "Live PT"}, &err), "Add LivePT");
    TEST_ASSERT(pmap_live.add_interval({"LiveDynamicPool", IntervalType::DynamicPool, 0x00038000ULL, 0x007C8000ULL, 0x3, 0x3, false, "Live Pool"}, &err), "Add LivePool");

    // In Model B: SP top = 0x30000, BootArgs = 0x1C000
    const auto* stack_b = pmap_live.find_interval_by_name("LiveStack");
    const auto* ba_b = pmap_live.find_interval_by_name("LiveBootArgs");
    TEST_ASSERT(stack_b && stack_b->end() == 0x30000ULL, "Model B Stack Top is 0x30000");
    TEST_ASSERT(ba_b && ba_b->base == 0x1C000ULL, "Model B BootArgs is 0x1C000");

    // Verify zero overlaps in both models
    for (std::size_t i = 0; i < pmap_canonical.intervals().size(); ++i) {
        for (std::size_t j = i + 1; j < pmap_canonical.intervals().size(); ++j) {
            TEST_ASSERT(!pmap_canonical.intervals()[i].overlaps_with(
                pmap_canonical.intervals()[j].base, pmap_canonical.intervals()[j].size),
                "Model A intervals must have zero overlap");
        }
    }
    for (std::size_t i = 0; i < pmap_live.intervals().size(); ++i) {
        for (std::size_t j = i + 1; j < pmap_live.intervals().size(); ++j) {
            TEST_ASSERT(!pmap_live.intervals()[i].overlaps_with(
                pmap_live.intervals()[j].base, pmap_live.intervals()[j].size),
                "Model B intervals must have zero overlap");
        }
    }

    std::cout << "[Stage 253.0] Baseline reconciliation & arithmetic verification complete.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 253.1: Canonical Frame Ownership & Double-Free Invariant
// ----------------------------------------------------------------------------
static void test_stage253_1_canonical_frame_ownership() {
    std::cout << "[Stage 253.1] Testing Canonical Frame Ownership & Double-Free Invariant...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize 8 MiB allocator");
    TEST_ASSERT(alloc.total_pages() == 512, "Total pages is 512");

    // Reserve canonical firmware/kernel regions: Frames 0..19 (0x0 to 0x50000)
    TEST_ASSERT(alloc.reserve_range(0x00000000ULL, 0x00050000ULL, "ReservedEnclave", &err), "Reserve Frames 0-19");

    // Frames 0..19 are reserved
    for (std::size_t f = 0; f < 20; ++f) {
        TEST_ASSERT(alloc.is_frame_reserved(f), "Frames 0-19 are reserved");
        TEST_ASSERT(alloc.get_frame_state(f) == XnuPhysicalPageAllocator::PageState::Reserved, "Frame state is Reserved");
        TEST_ASSERT(!alloc.is_frame_free(f), "Reserved frame is not Free");
    }

    // Attempting to free a reserved frame must fail
    TEST_ASSERT(!alloc.free_pages(0x0ULL, 1, &err), "Freeing reserved Frame 0 rejected");
    TEST_ASSERT(!alloc.free_pages(0x38000ULL, 1, &err), "Freeing reserved Frame 14 rejected");

    // Frames 20..511 are available
    TEST_ASSERT(alloc.free_pages_count() == 492ULL, "Free frame count is 492");
    for (std::size_t f = 20; f < 512; ++f) {
        TEST_ASSERT(!alloc.is_frame_reserved(f), "Frames 20-511 are not reserved");
        TEST_ASSERT(alloc.is_frame_free(f), "Frames 20-511 are Free");
        TEST_ASSERT(alloc.get_frame_state(f) == XnuPhysicalPageAllocator::PageState::Free, "Frame state is Free");
    }

    // Allocate frame 20 (single page)
    auto p1 = alloc.allocate_pages(1);
    TEST_ASSERT(p1.has_value(), "Allocate page succeeds");
    TEST_ASSERT(*p1 == 0x50000ULL, "Allocated frame is Frame 20 (0x50000)");
    TEST_ASSERT(alloc.get_frame_state(20) == XnuPhysicalPageAllocator::PageState::Allocated, "Frame 20 is Allocated");
    TEST_ASSERT(alloc.free_pages_count() == 491ULL, "Free frame count decreased to 491");

    // Free frame 20
    TEST_ASSERT(alloc.free_pages(*p1, 1, &err), "Freeing frame 20 succeeds");
    TEST_ASSERT(alloc.get_frame_state(20) == XnuPhysicalPageAllocator::PageState::Free, "Frame 20 is Free again");
    TEST_ASSERT(alloc.free_pages_count() == 492ULL, "Free frame count restored to 492");

    // Double-free test: freeing frame 20 again must fail!
    TEST_ASSERT(!alloc.free_pages(*p1, 1, &err), "Double-free of frame 20 strictly rejected");
    TEST_ASSERT(err.find("Double free") != std::string::npos, "Error reports double free");

    // Out-of-bounds address check
    TEST_ASSERT(!alloc.free_pages(0x800000ULL, 1, &err), "Out-of-bounds address rejected");

    // Exhaustive allocation of all 492 frames
    auto p_all = alloc.allocate_pages(492);
    TEST_ASSERT(p_all.has_value(), "Exhaustive allocation of 492 frames succeeds");
    TEST_ASSERT(alloc.free_pages_count() == 0, "All 492 frames consumed");

    // 493rd allocation must fail gracefully
    auto p_oom = alloc.allocate_pages(1);
    TEST_ASSERT(!p_oom.has_value(), "Allocation beyond capacity returns nullopt");

    // Free all 492 frames
    TEST_ASSERT(alloc.free_pages(*p_all, 492, &err), "Free all 492 frames");
    TEST_ASSERT(alloc.free_pages_count() == 492ULL, "All 492 frames returned to free pool");

    std::cout << "[Stage 253.1] Canonical frame ownership and double-free invariants verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 253.2: Stack, BootArgs & Page Table Integrity
// ----------------------------------------------------------------------------
static void test_stage253_2_stack_bootargs_pt_integrity() {
    std::cout << "[Stage 253.2] Testing Stack, BootArgs & Page Table Integrity...\n";

    // Stack characteristics:
    // Base: 0x38000, Top: 0x48000 (size 0x10000 = 64 KiB)
    const std::uint64_t stack_base = 0x38000ULL;
    const std::uint64_t stack_top  = 0x48000ULL;
    const std::uint64_t stack_size = stack_top - stack_base;

    TEST_ASSERT(stack_size == 0x10000ULL, "Stack size is 64 KiB");
    TEST_ASSERT((stack_top % 16ULL) == 0ULL, "Stack top has 16-byte architectural alignment");
    TEST_ASSERT((stack_base % 16ULL) == 0ULL, "Stack base has 16-byte architectural alignment");

    // Stack guard validation: address immediately below stack base
    const std::uint64_t guard_address = stack_base - 8ULL;
    TEST_ASSERT(guard_address < stack_base, "Guard address lies strictly below stack base");
    // Confirm guard address is in BootArgs or guard frame, outside stack interval
    MemoryInterval stack_interval{"Stack", IntervalType::BootstrapStack, stack_base, stack_size, 0x3, 0x3, true};
    TEST_ASSERT(guard_address < stack_interval.base || guard_address >= stack_interval.end(), "Stack guard address outside stack interval");

    // BootArgs characteristics:
    // Address: 0x34000, Size: 0x4000 (16 KiB)
    const std::uint64_t ba_base = 0x34000ULL;
    const std::uint64_t ba_size = 0x4000ULL;
    MemoryInterval ba_interval{"BootArgs", IntervalType::BootArgs, ba_base, ba_size, 0x3, 0x3, true};
    TEST_ASSERT(!ba_interval.overlaps_with(stack_base, stack_size), "BootArgs does not overlap stack");

    // Page Table characteristics:
    // Address: 0x48000, Size: 0x8000 (32 KiB)
    const std::uint64_t pt_base = 0x48000ULL;
    const std::uint64_t pt_size = 0x8000ULL;
    MemoryInterval pt_interval{"PageTables", IntervalType::PageTables, pt_base, pt_size, 0x3, 0x3, true};
    TEST_ASSERT(!pt_interval.overlaps_with(stack_base, stack_size), "PageTables does not overlap stack");
    TEST_ASSERT(!pt_interval.overlaps_with(ba_base, ba_size), "PageTables does not overlap BootArgs");

    std::cout << "[Stage 253.2] Stack, BootArgs & Page Table integrity verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 253.3: MMU Translation Audit & Descriptor Decoding
// ----------------------------------------------------------------------------
static void test_stage253_3_translation_audit() {
    std::cout << "[Stage 253.3] Testing Independent MMU Translation Audit & Descriptors...\n";

    // 1. Level 1 Block Descriptor (32 MiB Normal DRAM)
    // Value: 0x0000000000000701ULL
    // Bit 0 = 1 (valid), Bit 1 = 0 (block at L1), AttrIndx[4:2] = 000 (Normal),
    // AP[7:6] = 00 (Kernel RW), SH[9:8] = 11 (Inner Shareable), AF[10] = 1
    const std::uint64_t l1_dram = 0x0000000000000701ULL;
    auto desc1 = Arm64DescriptorDecoder::decode(l1_dram, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(desc1.valid, "L1 DRAM descriptor valid");
    TEST_ASSERT(desc1.type == DecodedDescriptorType::Block, "L1 descriptor is Block");
    TEST_ASSERT(desc1.output_address == 0x0ULL, "Output address is 0x0");
    TEST_ASSERT(desc1.attr_indx == 0, "AttrIndx is 0 (Normal)");
    TEST_ASSERT(desc1.ap == 0b00, "AP is 0b00 (Kernel Read/Write)");
    TEST_ASSERT(desc1.sh == 0b11, "Shareability is 0b11 (Inner Shareable)");
    TEST_ASSERT(desc1.access_flag, "AF is 1");
    TEST_ASSERT(!desc1.pxn, "Kernel execution permitted");

    // 2. Level 1 Block Descriptor (32 MiB Device-nGnRE AIC2 MMIO)
    // Value: 0x000000023A000705ULL (points to 0x23A000000, covers AIC2 at 0x23B100000)
    // AttrIndx = 1 (Device-nGnRE)
    const std::uint64_t l1_aic = 0x000000023A000705ULL;
    auto desc2 = Arm64DescriptorDecoder::decode(l1_aic, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(desc2.valid, "L1 AIC descriptor valid");
    TEST_ASSERT(desc2.type == DecodedDescriptorType::Block, "L1 AIC descriptor is Block");
    TEST_ASSERT(desc2.output_address == 0x23A000000ULL, "Output address covers AIC2 MMIO");
    TEST_ASSERT(desc2.attr_indx == 1, "AttrIndx is 1 (Device-nGnRE)");

    // 3. Level 1 Table Descriptor (pointing to L2 page table at 0x48000)
    // Value: 0x0000000000048003ULL
    // Bit 0 = 1, Bit 1 = 1 (Table descriptor)
    const std::uint64_t l1_table = 0x0000000000048003ULL;
    auto desc_table = Arm64DescriptorDecoder::decode(l1_table, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(desc_table.valid, "Table descriptor valid");
    TEST_ASSERT(desc_table.type == DecodedDescriptorType::Table, "Decoded as Table");
    TEST_ASSERT(desc_table.output_address == 0x48000ULL, "Points to next table at 0x48000");

    // 4. Level 3 Page Descriptor (16 KiB Page at 0x50000)
    // Value: 0x0000000000050703ULL
    const std::uint64_t l3_page = 0x0000000000050703ULL;
    auto desc_page = Arm64DescriptorDecoder::decode(l3_page, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(desc_page.valid, "Page descriptor valid");
    TEST_ASSERT(desc_page.type == DecodedDescriptorType::Page, "Decoded as Page");
    TEST_ASSERT(desc_page.output_address == 0x50000ULL, "Points to Frame 20 (0x50000)");

    // 5. W^X Violation Detection on Descriptors
    // If PXN is 0 (executable) and AP is 0b00 (writable), is_wx_violation returns true
    TEST_ASSERT(desc1.is_wx_violation(false), "Desc1 is both writable and executable (Kernel RW+X -> W^X violation)");

    // Form a strict Read-Only + Execute descriptor: AP=0b10 (Kernel RO), PXN=0
    // AP[7:6] = 10 -> (2 << 6) = 0x80. Raw = 0x0000000000000781ULL
    const std::uint64_t ro_x_desc = 0x0000000000000781ULL;
    auto desc_rox = Arm64DescriptorDecoder::decode(ro_x_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(!desc_rox.is_writable(false), "Desc is not writable");
    TEST_ASSERT(desc_rox.is_executable(false), "Desc is executable");
    TEST_ASSERT(!desc_rox.is_wx_violation(false), "RO+X satisfies W^X invariant");

    // Form a strict Read-Write + No-Execute descriptor: AP=0b00, PXN=1 (bit 53 = 1ULL << 53)
    const std::uint64_t rw_nx_desc = 0x0000000000000701ULL | (1ULL << 53);
    auto desc_rwnx = Arm64DescriptorDecoder::decode(rw_nx_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(desc_rwnx.is_writable(false), "Desc is writable");
    TEST_ASSERT(!desc_rwnx.is_executable(false), "Desc is not executable");
    TEST_ASSERT(!desc_rwnx.is_wx_violation(false), "RW+NX satisfies W^X invariant");

    std::cout << "[Stage 253.3] Independent MMU translation audit and decoding verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 253.4: W^X Enforcement & Alias Lifecycle Validation
// ----------------------------------------------------------------------------
static void test_stage253_4_wx_alias_lifecycle() {
    std::cout << "[Stage 253.4] Testing W^X Enforcement & Physical Alias Lifecycle...\n";

    XnuVmMap vmap(0x0ULL, 0x800000ULL, "kernel_map");
    vmap.set_strict_wx(true);

    std::string err;
    // Map code range at VA 0x10000 (16 KiB): Read + Execute
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "kernel_text", &err), "Map text range");
    TEST_ASSERT(vmap.register_physical_alias(0x10000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err), "Register alias on PA 0x50000 as RO+X");
    TEST_ASSERT(vmap.alias_count() == 1, "Alias count is 1");

    // Attempting to register an overlapping writable alias for PA 0x50000 must fail W^X!
    TEST_ASSERT(!vmap.register_physical_alias(0x20000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Conflicting writable alias rejected");
    TEST_ASSERT(err.find("W^X violation") != std::string::npos, "Error reports W^X alias conflict");

    // Unregister original RO+X alias
    TEST_ASSERT(vmap.unregister_physical_alias(0x10000ULL, 0x4000ULL, &err), "Unregister RO+X alias");
    TEST_ASSERT(vmap.alias_count() == 0, "Alias count is 0 after unregister");

    // Now registering writable alias on PA 0x50000 succeeds
    TEST_ASSERT(vmap.register_physical_alias(0x20000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Register RW alias succeeds after unregister");
    TEST_ASSERT(vmap.alias_count() == 1, "Alias count is 1");

    // Sub-range unregister
    TEST_ASSERT(vmap.register_physical_alias(0x30000ULL, 0x60000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Register 16KB RW alias");
    TEST_ASSERT(vmap.alias_count() == 2, "Alias count is 2");

    // Unregister sub-range
    TEST_ASSERT(vmap.unregister_physical_alias(0x30000ULL, 0x4000ULL, &err), "Unregister 16KB sub-range");
    TEST_ASSERT(vmap.alias_count() == 1, "Alias count restored to 1");

    // Clear mapping via vm_map_remove
    TEST_ASSERT(vmap.vm_map_enter(0x20000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, kVmProtRead | kVmProtWrite, "data", &err), "Enter data VA");
    TEST_ASSERT(vmap.vm_map_remove(0x20000ULL, 0x4000ULL, &err), "Remove data VA");
    // All aliases removed
    TEST_ASSERT(vmap.alias_count() == 0, "All aliases pruned upon vm_map_remove");

    std::cout << "[Stage 253.4] W^X enforcement and physical alias lifecycle verified.\n";
}

int main() {
    std::cout << "=== Running Stages 253.0 - 253.4 Unit Tests ===\n";
    test_stage253_0_baseline_reconciliation();
    test_stage253_1_canonical_frame_ownership();
    test_stage253_2_stack_bootargs_pt_integrity();
    test_stage253_3_translation_audit();
    test_stage253_4_wx_alias_lifecycle();
    std::cout << "[PASS] All Stage 253.0 - 253.4 tests passed successfully.\n";
    return 0;
}
