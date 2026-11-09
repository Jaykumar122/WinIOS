#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/guest_memory_map.h"

#define STAGE78_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Default RAM Geometry and Bounds
// -----------------------------------------------------------------------------
bool test_default_ram_geometry_and_bounds() {
    std::cout << "[1] test_default_ram_geometry_and_bounds...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL); // 128 MB

    STAGE78_ASSERT(map.ram_base() == 0x40000000ULL, "RAM base mismatch");
    STAGE78_ASSERT(map.ram_size() == 0x08000000ULL, "RAM size mismatch");
    STAGE78_ASSERT(map.ram_end() == 0x48000000ULL, "RAM end mismatch");
    STAGE78_ASSERT((map.ram_base() % 0x200000ULL) == 0, "RAM base must be 2MB aligned");

    std::cout << "[PASS] Default RAM Geometry and Bounds\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Valid Explicit Region Registration
// -----------------------------------------------------------------------------
bool test_valid_explicit_region_registration() {
    std::cout << "[2] test_valid_explicit_region_registration...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL); // 128 MB

    // Register standard placements:
    // Kernel: 0x40080000 (16 MB) -> ends 0x41080000
    // Initramfs: 0x42000000 (16 MB) -> ends 0x43000000
    // DTB: 0x47E00000 (1 MB) -> ends 0x47F00000
    STAGE78_ASSERT(map.register_kernel_image(0x40080000ULL, 0x01000000ULL), "Register kernel");
    STAGE78_ASSERT(map.register_initramfs(0x42000000ULL, 0x01000000ULL), "Register initramfs");
    STAGE78_ASSERT(map.register_dtb(0x47E00000ULL, 0x00100000ULL), "Register DTB");

    std::string msg;
    MemoryMapValidationResult res = map.validate(&msg);
    STAGE78_ASSERT(res == MemoryMapValidationResult::Valid, "Validation must succeed");

    STAGE78_ASSERT(map.find_region(GuestMemoryRegionType::KernelImage) != nullptr, "Find kernel");
    STAGE78_ASSERT(map.find_region(GuestMemoryRegionType::Initramfs) != nullptr, "Find initramfs");
    STAGE78_ASSERT(map.find_region(GuestMemoryRegionType::DeviceTree) != nullptr, "Find DTB");

    std::cout << "[PASS] Valid Explicit Region Registration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Region Overlap Collision Detection
// -----------------------------------------------------------------------------
bool test_region_overlap_collision_detection() {
    std::cout << "[3] test_region_overlap_collision_detection...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL);

    // Kernel: 0x40080000..0x42080000 (32 MB)
    map.register_kernel_image(0x40080000ULL, 0x02000000ULL);
    // Initramfs starts at 0x42000000 (collides with tail of kernel!)
    map.register_initramfs(0x42000000ULL, 0x01000000ULL);

    std::string err;
    MemoryMapValidationResult res = map.validate(&err);
    STAGE78_ASSERT(res == MemoryMapValidationResult::RegionOverlap, "Overlap must be detected");
    STAGE78_ASSERT(err.find("Collision") != std::string::npos, "Error must describe collision");
    STAGE78_ASSERT(err.find("KernelImage") != std::string::npos, "Error must mention KernelImage");

    std::cout << "[PASS] Region Overlap Collision Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Region Out-of-Bounds Detection
// -----------------------------------------------------------------------------
bool test_region_out_of_bounds_detection() {
    std::cout << "[4] test_region_out_of_bounds_detection...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL); // ends at 0x48000000

    // Region extending beyond 0x48000000
    map.register_kernel_image(0x47000000ULL, 0x02000000ULL); // ends at 0x49000000

    std::string err;
    MemoryMapValidationResult res = map.validate(&err);
    STAGE78_ASSERT(res == MemoryMapValidationResult::RegionOutOfBounds, "Out-of-bounds must be detected");

    std::cout << "[PASS] Region Out-of-Bounds Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: DTB Alignment Enforcement
// -----------------------------------------------------------------------------
bool test_dtb_alignment_enforcement() {
    std::cout << "[5] test_dtb_alignment_enforcement...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL);

    // Register DTB at misaligned address (odd address)
    map.register_dtb(0x47000003ULL, 0x10000ULL);

    std::string err;
    MemoryMapValidationResult res = map.validate(&err);
    STAGE78_ASSERT(res == MemoryMapValidationResult::DtbMisaligned, "Misaligned DTB must be rejected");

    std::cout << "[PASS] DTB Alignment Enforcement\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Initramfs Page Alignment Enforcement
// -----------------------------------------------------------------------------
bool test_initramfs_page_alignment_enforcement() {
    std::cout << "[6] test_initramfs_page_alignment_enforcement...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL);

    // Register Initramfs not page-aligned (e.g. 0x42000010)
    map.register_initramfs(0x42000010ULL, 0x100000ULL);

    std::string err;
    MemoryMapValidationResult res = map.validate(&err);
    STAGE78_ASSERT(res == MemoryMapValidationResult::InitramfsMisaligned, "Misaligned Initramfs must be rejected");

    std::cout << "[PASS] Initramfs Page Alignment Enforcement\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Zero Size Region Rejection
// -----------------------------------------------------------------------------
bool test_zero_size_region_rejection() {
    std::cout << "[7] test_zero_size_region_rejection...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL);

    bool ok = map.add_region({"ZeroRegion", GuestMemoryRegionType::FirmwareReserved, 0x41000000ULL, 0, true});
    STAGE78_ASSERT(!ok, "Adding 0-size region must fail");

    std::cout << "[PASS] Zero Size Region Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Automated Safe Layout Computation
// -----------------------------------------------------------------------------
bool test_compute_safe_auto_layout() {
    std::cout << "[8] test_compute_safe_auto_layout...\n";

    GuestMemoryMap map;
    std::string err;

    // 128 MB RAM, 14 MB kernel, text_offset 0x80000, 500 KB DTB, 8 MB initramfs
    bool ok = GuestMemoryMap::compute_safe_layout(
        0x40000000ULL, 0x08000000ULL,
        0x80000ULL, 0x00E00000ULL,
        500000ULL, 0x00800000ULL,
        map, err);

    STAGE78_ASSERT(ok, "Auto-layout should succeed");
    STAGE78_ASSERT(err.empty(), "Error message should be empty");

    MemoryMapValidationResult res = map.validate(&err);
    STAGE78_ASSERT(res == MemoryMapValidationResult::Valid, "Generated map must be valid");

    const auto* k = map.find_region(GuestMemoryRegionType::KernelImage);
    const auto* i = map.find_region(GuestMemoryRegionType::Initramfs);
    const auto* d = map.find_region(GuestMemoryRegionType::DeviceTree);

    STAGE78_ASSERT(k != nullptr && i != nullptr && d != nullptr, "All 3 regions must exist");
    STAGE78_ASSERT(k->base_address == 0x40080000ULL, "Kernel load address");
    STAGE78_ASSERT(i->base_address > k->end_address(), "Initramfs placed after kernel");
    STAGE78_ASSERT(d->base_address > i->end_address(), "DTB placed after initramfs");
    STAGE78_ASSERT(d->end_address() <= map.ram_end(), "DTB fits within RAM");

    std::cout << "[PASS] Automated Safe Layout Computation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Available RAM Spans Computation
// -----------------------------------------------------------------------------
bool test_available_ram_spans_computation() {
    std::cout << "[9] test_available_ram_spans_computation...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL); // 128 MB

    // Kernel at 0x40080000 (16 MB) -> 0x40080000..0x41080000
    // DTB at 0x47E00000 (1 MB) -> 0x47E00000..0x47F00000
    map.register_kernel_image(0x40080000ULL, 0x01000000ULL);
    map.register_dtb(0x47E00000ULL, 0x00100000ULL);

    auto spans = map.compute_available_spans();
    STAGE78_ASSERT(spans.size() == 3, "Must have 3 free spans");

    // Span 0: 0x40000000..0x40080000 (512 KB before kernel)
    STAGE78_ASSERT(spans[0].start == 0x40000000ULL, "Span 0 start");
    STAGE78_ASSERT(spans[0].size == 0x80000ULL, "Span 0 size (text_offset headroom)");

    // Span 1: between kernel end (0x41080000) and DTB (0x47E00000)
    STAGE78_ASSERT(spans[1].start == 0x41080000ULL, "Span 1 start");
    STAGE78_ASSERT(spans[1].size == (0x47E00000ULL - 0x41080000ULL), "Span 1 size");

    // Span 2: between DTB end (0x47F00000) and RAM end (0x48000000)
    STAGE78_ASSERT(spans[2].start == 0x47F00000ULL, "Span 2 start");
    STAGE78_ASSERT(spans[2].size == 0x00100000ULL, "Span 2 size");

    std::cout << "[PASS] Available RAM Spans Computation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: RAM Overflow Stress and Diagnostics Dump
// -----------------------------------------------------------------------------
bool test_ram_overflow_stress_and_diagnostics_dump() {
    std::cout << "[10] test_ram_overflow_stress_and_diagnostics_dump...\n";

    // 1. Stress: Attempt auto-layout with 100 MB kernel in 64 MB RAM
    GuestMemoryMap map;
    std::string err;
    bool ok = GuestMemoryMap::compute_safe_layout(
        0x40000000ULL, 0x04000000ULL, // 64 MB RAM
        0x80000ULL, 0x06000000ULL,    // 96 MB kernel!
        100000ULL, 0,
        map, err);
    STAGE78_ASSERT(!ok, "Oversized kernel must be rejected");
    STAGE78_ASSERT(!err.empty(), "Error message must be set");

    // 2. Diagnostics dump test
    GuestMemoryMap valid_map;
    ok = GuestMemoryMap::compute_safe_layout(
        0x40000000ULL, 0x08000000ULL,
        0x80000ULL, 0x01000000ULL,
        0x10000ULL, 0x01000000ULL,
        valid_map, err);
    STAGE78_ASSERT(ok, "Valid layout succeeds");

    std::string dump = valid_map.dump_memory_map();
    STAGE78_ASSERT(!dump.empty(), "Dump must not be empty");
    STAGE78_ASSERT(dump.find("KernelImage") != std::string::npos, "Dump mentions KernelImage");
    STAGE78_ASSERT(dump.find("DeviceTree") != std::string::npos, "Dump mentions DeviceTree");
    STAGE78_ASSERT(dump.find("FREE:") != std::string::npos, "Dump lists free memory spans");

    std::cout << "[PASS] RAM Overflow Stress and Diagnostics Dump\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 78: Guest RAM Layout & Boot Memory Map Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_default_ram_geometry_and_bounds, "test_default_ram_geometry_and_bounds");
    run_test(test_valid_explicit_region_registration, "test_valid_explicit_region_registration");
    run_test(test_region_overlap_collision_detection, "test_region_overlap_collision_detection");
    run_test(test_region_out_of_bounds_detection, "test_region_out_of_bounds_detection");
    run_test(test_dtb_alignment_enforcement, "test_dtb_alignment_enforcement");
    run_test(test_initramfs_page_alignment_enforcement, "test_initramfs_page_alignment_enforcement");
    run_test(test_zero_size_region_rejection, "test_zero_size_region_rejection");
    run_test(test_compute_safe_auto_layout, "test_compute_safe_auto_layout");
    run_test(test_available_ram_spans_computation, "test_available_ram_spans_computation");
    run_test(test_ram_overflow_stress_and_diagnostics_dump, "test_ram_overflow_stress_and_diagnostics_dump");

    std::cout << "\nStage 78 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
