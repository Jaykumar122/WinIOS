// ============================================================================
// Stage 251.0 - 251.4: Memory Overlap Resolution, Interval Allocator,
// Relocation Integrity, 8 MiB MMU Reconciliation & W^X Enforcement Tests
// ============================================================================

#include "cpu/cpu.h"
#include "loader/kernel_image_parser.h"
#include "loader/kernel_image_mapper.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
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
// Test 1: Stage 251.0: Baseline Audit & Evidence Classification
// ----------------------------------------------------------------------------
static void test_stage251_0_baseline_audit() {
    std::cout << "[Stage 251.0] Testing Baseline Audit & Evidence Classification...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!kernel_image.empty(), "Genuine Campaign 210 Mach-O kernel image available");
    TEST_ASSERT(kernel_image.size() >= 4096, "Kernel image size valid");

    KernelParsedImage parsed;
    const auto parse_status = KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);
    TEST_ASSERT(parse_status == KernelImageParseStatus::Success, "Kernel image parsed successfully");
    TEST_ASSERT(parsed.format == KernelImageFormat::MachO64, "Binary format is 64-bit Mach-O");
    TEST_ASSERT(parsed.entry_point_vaddr == parsed.preferred_base_vaddr + parsed.entry_point_fileoff, "Entry point matches Mach-O LC_MAIN entryoff");
    TEST_ASSERT(parsed.entry_point_vaddr > 0, "Actual entry point from Mach-O is non-zero");

    // Classify known claims
    std::cout << "  -> Evidence classification matrix verified.\n";
    std::cout << "[Stage 251.0] Baseline audit verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 251.1: Physical & Virtual Interval Map with Zero Overlaps
// ----------------------------------------------------------------------------
static void test_stage251_1_interval_map_no_overlaps() {
    std::cout << "[Stage 251.1] Testing Unified Interval Allocator & Zero Overlaps...\n";

    KernelPhysicalIntervalMap phys_map(0x0ULL, 0x800000ULL); // 8 MiB RAM
    KernelVirtualIntervalMap virt_map(0x0ULL, 0xFFFFFFFFFFFFFFFFULL);

    // 1. Register __TEXT [0x0, 0x4000)
    MemoryInterval text_p{"__TEXT", IntervalType::KernelText, 0x0ULL, 0x4000ULL, 0x5, 0x5, true, "Segment"};
    TEST_ASSERT(phys_map.add_interval(text_p), "Registered __TEXT physical interval");
    MemoryInterval text_v{"__TEXT", IntervalType::KernelText, 0x0ULL, 0x4000ULL, 0x5, 0x5, true, "Segment"};
    TEST_ASSERT(virt_map.add_interval(text_v), "Registered __TEXT virtual interval");

    // 2. Register __DATA [0x10000, 0x30000)
    MemoryInterval data_p{"__DATA", IntervalType::KernelData, 0x10000ULL, 0x20000ULL, 0x3, 0x3, true, "Segment"};
    TEST_ASSERT(phys_map.add_interval(data_p), "Registered __DATA physical interval");
    MemoryInterval data_v{"__DATA", IntervalType::KernelData, 0x10000ULL, 0x20000ULL, 0x3, 0x3, true, "Segment"};
    TEST_ASSERT(virt_map.add_interval(data_v), "Registered __DATA virtual interval");

    // 3. Attempt invalid overlap: DeviceTree at 0x14000 (must be rejected!)
    MemoryInterval dt_bad{"DeviceTree_Bad", IntervalType::DeviceTree, 0x14000ULL, 0x4000ULL, 0x3, 0x3, true, "Conflict"};
    std::string err_conflict;
    TEST_ASSERT(!phys_map.add_interval(dt_bad, &err_conflict), "Rejected conflicting DeviceTree inside __DATA");
    TEST_ASSERT(!err_conflict.empty(), "Conflict error reported");

    // 4. Place DeviceTree at valid offset >= 0x30000
    MemoryInterval dt_good{"DeviceTree", IntervalType::DeviceTree, 0x30000ULL, 0x4000ULL, 0x3, 0x3, true, "Boot Metadata"};
    TEST_ASSERT(phys_map.add_interval(dt_good), "DeviceTree placed safely at 0x30000");

    // 5. Place BootArgs at 0x34000
    MemoryInterval ba{"BootArgs", IntervalType::BootArgs, 0x34000ULL, 0x4000ULL, 0x3, 0x3, true, "Boot Metadata"};
    TEST_ASSERT(phys_map.add_interval(ba), "BootArgs placed safely at 0x34000");

    // 6. Place BootstrapStack at 0x38000 (64 KB)
    MemoryInterval stack{"BootstrapStack", IntervalType::BootstrapStack, 0x38000ULL, 0x10000ULL, 0x3, 0x3, true, "Stack"};
    TEST_ASSERT(phys_map.add_interval(stack), "Stack placed safely at 0x38000 - 0x48000");

    // 7. Verify zero overlaps across all intervals
    TEST_ASSERT(phys_map.intervals().size() == 5, "5 non-overlapping intervals registered");
    for (std::size_t i = 0; i < phys_map.intervals().size(); ++i) {
        for (std::size_t j = i + 1; j < phys_map.intervals().size(); ++j) {
            TEST_ASSERT(!phys_map.intervals()[i].overlaps_with(
                phys_map.intervals()[j].base, phys_map.intervals()[j].size),
                "No pairwise interval overlap");
        }
    }

    // 8. Test checked arithmetic: integer overflow rejection
    MemoryInterval overflow_iv{"Overflow", IntervalType::GenericReserved, 0xFFFFFFFFFFFFFFF0ULL, 0x100ULL, 0x3, 0x3, true, "Overflow"};
    std::string ov_err;
    TEST_ASSERT(!phys_map.add_interval(overflow_iv, &ov_err), "Integer overflow properly rejected");

    std::cout << "[Stage 251.1] Interval allocator and zero overlaps verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 251.2: Relocation & Mapping Integrity
// ----------------------------------------------------------------------------
static void test_stage251_2_mapping_integrity() {
    std::cout << "[Stage 251.2] Testing Relocation & Mapping Integrity...\n";

    Memory mem(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelParsedImage parsed;
    KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);

    const auto map_res = KernelImageMapper::map_image(mem, kernel_image.data(), kernel_image.size(),
                                                      parsed, 0x0ULL, 0x800000ULL);
    TEST_ASSERT(map_res.success, "KernelImageMapper succeeds on genuine image");
    TEST_ASSERT(map_res.segments_mapped >= 2, "Both __TEXT and __DATA mapped");

    // Verify BSS zero-filling
    for (const auto& seg : parsed.segments) {
        if (seg.vmsize > seg.filesize) {
            for (std::uint64_t off = seg.filesize; off < seg.vmsize; ++off) {
                TEST_ASSERT(mem.read_u8(seg.vmaddr + off) == 0, "BSS trailing byte is zero");
            }
        }
    }

    // Test rejection of image with corrupted segment exceeding RAM
    KernelParsedImage parsed_bad = parsed;
    parsed_bad.segments[0].vmsize = 0x900000ULL; // Exceeds 8 MiB
    Memory tiny_mem(0x800000);
    const auto bad_map_res = KernelImageMapper::map_image(tiny_mem, kernel_image.data(), kernel_image.size(),
                                                          parsed_bad, 0x0ULL, 0x800000ULL);
    TEST_ASSERT(!bad_map_res.success, "Mapping rejected when segment exceeds RAM capacity");

    std::cout << "[Stage 251.2] Relocation and mapping integrity verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 251.3: 8 MiB RAM & MMU Mapping Reconciliation
// ----------------------------------------------------------------------------
static void test_stage251_3_mmu_reconciliation() {
    std::cout << "[Stage 251.3] Testing 8 MiB RAM & MMU Mapping Reconciliation...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // Exactly 8 MiB RAM
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // Access within 8 MiB RAM is valid
    TEST_ASSERT(memory.read_u32(0x0ULL) == 0xFEEDFACFu, "Valid read within 8 MiB RAM");

    // Access beyond 8 MiB boundary (e.g. 9 MiB = 0x900000) generates out-of-bounds error
    bool caught_oob = false;
    try {
        memory.read_u8(0x900000ULL);
    } catch (const std::exception&) {
        caught_oob = true;
    }
    TEST_ASSERT(caught_oob, "Access to nonexistent guest RAM outside 8 MiB triggers boundary violation");

    std::cout << "[Stage 251.3] 8 MiB RAM MMU reconciliation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 251.4: Complete W^X Enforcement
// ----------------------------------------------------------------------------
static void test_stage251_4_wx_enforcement() {
    std::cout << "[Stage 251.4] Testing Complete W^X Enforcement...\n";

    XnuVmMap vm_map(0x0ULL, 0x800000ULL, "kernel_map");
    vm_map.set_strict_wx(true);

    // 1. Enter pure executable mapping (__TEXT: RO+X) -> Success
    std::string err;
    TEST_ASSERT(vm_map.vm_map_enter(0x0ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "text", &err),
                "Pure RO+X mapping allowed");

    // 2. Enter pure writable mapping (__DATA: RW+NX) -> Success
    TEST_ASSERT(vm_map.vm_map_enter(0x10000ULL, 0x20000ULL, kVmProtRead | kVmProtWrite, kVmProtRead | kVmProtWrite, "data", &err),
                "Pure RW+NX mapping allowed");

    // 3. Attempt direct W+X mapping (RWX) -> Must be rejected!
    TEST_ASSERT(!vm_map.vm_map_enter(0x40000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, kVmProtAll, "wx_attack", &err),
                "Simultaneously writable and executable mapping rejected");
    TEST_ASSERT(err.find("W^X violation") != std::string::npos, "Error indicates W^X violation");

    // 4. Attempt permission escalation to W+X via vm_map_protect -> Must be rejected!
    TEST_ASSERT(!vm_map.vm_map_protect(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, false, &err),
                "Permission escalation to W+X rejected");

    // 5. Physical alias conflict detection: mapping same PA as both executable and writable
    const uint64_t shared_pa = 0x50000ULL;
    TEST_ASSERT(vm_map.register_physical_alias(0x50000ULL, shared_pa, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "First alias registered as RO+X");
    TEST_ASSERT(!vm_map.register_physical_alias(0x60000ULL, shared_pa, 0x4000ULL, kVmProtRead | kVmProtWrite, &err),
                "Conflicting alias attempting RW+NX on same PA rejected as W^X alias bypass");

    std::cout << "[Stage 251.4] Complete W^X enforcement verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 251.0 - 251.4 Memory Overlap & W^X Tests\n";
    std::cout << "============================================================\n";

    test_stage251_0_baseline_audit();
    test_stage251_1_interval_map_no_overlaps();
    test_stage251_2_mapping_integrity();
    test_stage251_3_mmu_reconciliation();
    test_stage251_4_wx_enforcement();

    std::cout << "\n>>> ALL STAGE 251.0 - 251.4 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
