#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "memory/apple_physical_memory_map.h"
#include "device/apple_hardware_profile.h"

using namespace winios::memory;
using namespace winios::device;

#define STAGE99_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 99 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Default iPhone 15 Memory Layout
// -----------------------------------------------------------------------------
static bool test_default_iphone15_memory_layout() {
    std::cout << "\n[1] test_default_iphone15_memory_layout...\n";

    ApplePhysicalMemoryMap pmap(0x40000000ULL, 6ULL * 1024 * 1024 * 1024);

    STAGE99_ASSERT(pmap.ram_base() == 0x40000000ULL, "RAM base must be 0x40000000");
    STAGE99_ASSERT(pmap.ram_size() == 6ULL * 1024 * 1024 * 1024, "RAM size must be 6 GB");
    STAGE99_ASSERT(pmap.ram_end() == 0x1C0000000ULL, "RAM end must be 0x1C0000000 (7 GB physical mark)");

    // Verify MMIO peripherals
    const auto* aic = pmap.find_region(0x08000000ULL);
    STAGE99_ASSERT(aic != nullptr && aic->is_mmio, "AIC 2 MMIO must be present at 0x08000000");

    const auto* uart = pmap.find_region(0x09000000ULL);
    STAGE99_ASSERT(uart != nullptr && uart->is_mmio, "UART MMIO must be present at 0x09000000");

    const auto* pmgr = pmap.find_region(0x10000000ULL);
    STAGE99_ASSERT(pmgr != nullptr && pmgr->is_mmio, "PMGR MMIO must be present at 0x10000000");

    // Verify DRAM staging regions
    const auto* kernel = pmap.find_region(0x40000000ULL);
    STAGE99_ASSERT(kernel != nullptr && !kernel->is_mmio, "Kernel staging must be at 0x40000000");

    const auto* dt = pmap.find_region(0x48000000ULL);
    STAGE99_ASSERT(dt != nullptr && !dt->is_mmio, "DeviceTree staging must be at 0x48000000");

    const auto* rd = pmap.find_region(0x4A000000ULL);
    STAGE99_ASSERT(rd != nullptr && !rd->is_mmio, "Ramdisk staging must be at 0x4A000000");

    const auto* fb = pmap.find_region(0x4F000000ULL);
    STAGE99_ASSERT(fb != nullptr && !fb->is_mmio, "Framebuffer staging must be at 0x4F000000");

    std::cout << "[PASS] Default iPhone 15 Memory Layout Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: RAM vs MMIO Classification
// -----------------------------------------------------------------------------
static bool test_ram_and_mmio_classification() {
    std::cout << "\n[2] test_ram_and_mmio_classification...\n";

    ApplePhysicalMemoryMap pmap;

    STAGE99_ASSERT(pmap.is_ram(0x40000000ULL, 4), "0x40000000 must be classified as RAM");
    STAGE99_ASSERT(pmap.is_ram(0x50000000ULL, 1024), "0x50000000 must be classified as RAM");
    STAGE99_ASSERT(!pmap.is_ram(0x08000000ULL, 4), "0x08000000 is MMIO, not RAM");
    STAGE99_ASSERT(!pmap.is_ram(0x1C0000000ULL, 4), "0x1C0000000 is out of bounds, not RAM");

    STAGE99_ASSERT(pmap.is_mmio(0x08000000ULL, 4), "AIC 2 must be MMIO");
    STAGE99_ASSERT(pmap.is_mmio(0x09000000ULL, 4), "UART must be MMIO");
    STAGE99_ASSERT(pmap.is_mmio(0x10000000ULL, 4), "PMGR must be MMIO");
    STAGE99_ASSERT(!pmap.is_mmio(0x40000000ULL, 4), "RAM is not MMIO");

    std::cout << "[PASS] RAM vs MMIO Classification\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Physical to Backing Offset Translation
// -----------------------------------------------------------------------------
static bool test_physical_to_backing_translation() {
    std::cout << "\n[3] test_physical_to_backing_translation...\n";

    ApplePhysicalMemoryMap pmap(0x40000000ULL, 0x10000000ULL); // 256 MB RAM

    std::size_t offset = 0;
    STAGE99_ASSERT(pmap.translate_ram_address(0x40000000ULL, 8, offset), "Translate base");
    STAGE99_ASSERT(offset == 0, "Base translation offset must be 0");

    STAGE99_ASSERT(pmap.translate_ram_address(0x48000000ULL, 4, offset), "Translate DeviceTree base");
    STAGE99_ASSERT(offset == 0x08000000ULL, "Offset must be exactly 0x08000000 (128 MB into RAM)");

    STAGE99_ASSERT(!pmap.translate_ram_address(0x08000000ULL, 4, offset), "Sub-1GB MMIO address must fail translation");
    STAGE99_ASSERT(!pmap.translate_ram_address(0x50000000ULL, 4, offset), "Out-of-bounds address must fail translation");

    std::cout << "[PASS] Physical to Backing Offset Translation\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: 16 KB Page Granule Alignment
// -----------------------------------------------------------------------------
static bool test_16kb_page_granule_alignment() {
    std::cout << "\n[4] test_16kb_page_granule_alignment...\n";

    ApplePhysicalMemoryMap pmap;
    std::string err;
    STAGE99_ASSERT(pmap.validate_layout(err), "Default layout must have valid alignment");

    for (const auto& reg : pmap.all_regions()) {
        if (!reg.is_mmio) {
            STAGE99_ASSERT(reg.phys_base % 16384 == 0,
                           ("DRAM region " + reg.name + " base must be 16 KB aligned").c_str());
        }
    }

    std::cout << "[PASS] 16 KB Page Granule Base Alignment Verified across all DRAM Regions\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: MMIO Device Registration and Dispatch
// -----------------------------------------------------------------------------
static bool test_mmio_device_registration_and_dispatch() {
    std::cout << "\n[5] test_mmio_device_registration_and_dispatch...\n";

    ApplePhysicalMemoryMap pmap;

    std::uint32_t mock_reg = 0x12345678;
    bool registered = pmap.register_mmio_device(
        "MockPeripheral",
        0x15000000ULL,
        0x1000ULL,
        [&mock_reg](std::uint64_t offset) -> std::uint32_t {
            if (offset == 0x0) return mock_reg;
            return 0;
        },
        [&mock_reg](std::uint64_t offset, std::uint32_t val) {
            if (offset == 0x0) mock_reg = val;
        });

    STAGE99_ASSERT(registered, "Custom MMIO device registration must succeed");
    STAGE99_ASSERT(pmap.is_mmio(0x15000000ULL, 4), "MockPeripheral must be recognized as MMIO");

    std::uint32_t val = 0;
    STAGE99_ASSERT(pmap.dispatch_read_u32(0x15000000ULL, val), "Dispatch read must succeed");
    STAGE99_ASSERT(val == 0x12345678, "Read value must match initial register state");

    STAGE99_ASSERT(pmap.dispatch_write_u32(0x15000000ULL, 0x87654321), "Dispatch write must succeed");
    STAGE99_ASSERT(pmap.dispatch_read_u32(0x15000000ULL, val), "Read back after write");
    STAGE99_ASSERT(val == 0x87654321, "Read value must reflect written value");

    std::cout << "[PASS] MMIO Device Registration and Dispatch\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Region Overlap Prevention
// -----------------------------------------------------------------------------
static bool test_region_overlap_prevention() {
    std::cout << "\n[6] test_region_overlap_prevention...\n";

    ApplePhysicalMemoryMap pmap;

    // Attempting to register an overlapping DRAM region
    AppleMemoryRegion overlap_reg;
    overlap_reg.name = "ConflictingRegion";
    overlap_reg.phys_base = 0x40001000ULL; // Overlaps with Kernel Mach-O Staging (0x40000000 - 0x44000000)
    overlap_reg.size = 0x01000000ULL;
    overlap_reg.is_mmio = false;

    std::string err;
    bool ok = pmap.add_region(overlap_reg, &err);
    STAGE99_ASSERT(!ok, "Overlapping DRAM region must be rejected");
    STAGE99_ASSERT(err.find("overlaps") != std::string::npos, "Error must describe overlap");

    // Attempting to register an MMIO device in DRAM space
    bool mmio_ok = pmap.register_mmio_device("BadMMIO", 0x40000000ULL, 0x1000, nullptr, nullptr, &err);
    STAGE99_ASSERT(!mmio_ok, "MMIO inside DRAM must be rejected");

    std::cout << "[PASS] Region Overlap Prevention\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Hardware Profile Conformance
// -----------------------------------------------------------------------------
static bool test_hardware_profile_conformance() {
    std::cout << "\n[7] test_hardware_profile_conformance...\n";

    ApplePhysicalMemoryMap pmap;
    auto hw_profile = AppleHardwareProfile::get_iphone15_profile();

    std::string err;
    bool ok = pmap.validate_against_profile(hw_profile, err);
    if (!ok) {
        std::cerr << "[TEST FAILED] validate_against_profile returned false: " << err << "\n";
    }
    STAGE99_ASSERT(ok, "Physical memory map must conform to iPhone 15 hardware profile");

    std::cout << "[PASS] Hardware Profile Conformance Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Memory Map Report Generation
// -----------------------------------------------------------------------------
static bool test_memory_map_report_generation() {
    std::cout << "\n[8] test_memory_map_report_generation...\n";

    ApplePhysicalMemoryMap pmap;
    std::string report = pmap.generate_physical_memory_report();

    STAGE99_ASSERT(!report.empty(), "Report must not be empty");
    STAGE99_ASSERT(report.find("# iPhone 15 Unified Physical Address Space Map") != std::string::npos,
                   "Header must be present");
    STAGE99_ASSERT(report.find("AppleAIC2_InterruptController") != std::string::npos, "AIC2 must be in report");
    STAGE99_ASSERT(report.find("KernelCache_MachO_Staging") != std::string::npos, "Kernel staging must be in report");
    STAGE99_ASSERT(report.find("SuperRetinaXDR_Framebuffer") != std::string::npos, "Framebuffer must be in report");

    std::cout << "[PASS] Memory Map Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 99: Unified Physical Memory Map Tests\n";
    std::cout << "========================================================\n";

    if (!test_default_iphone15_memory_layout()) return 1;
    if (!test_ram_and_mmio_classification()) return 1;
    if (!test_physical_to_backing_translation()) return 1;
    if (!test_16kb_page_granule_alignment()) return 1;
    if (!test_mmio_device_registration_and_dispatch()) return 1;
    if (!test_region_overlap_prevention()) return 1;
    if (!test_hardware_profile_conformance()) return 1;
    if (!test_memory_map_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 99 PHYSICAL MEMORY MAP SUITES PASSED!\n";
    return 0;
}
