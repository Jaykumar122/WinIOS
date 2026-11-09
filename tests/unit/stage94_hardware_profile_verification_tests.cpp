#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "device/apple_hardware_profile.h"

#define STAGE94_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Stage 94 Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::device;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: iPhone 15 Hardware Profile Core Identification
// -----------------------------------------------------------------------------
bool test_iphone15_identification() {
    std::cout << "[1] test_iphone15_identification...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.model == "iPhone15,4", "Model must be iPhone15,4");
    STAGE94_ASSERT(p.target_type == "D37AP", "Target type must be D37AP");
    STAGE94_ASSERT(p.board_id == 0x18u, "Board ID must be 0x18 (24 decimal)");
    STAGE94_ASSERT(p.chip_id == 0x8120u, "Chip ID must be 0x8120 (A16 Bionic / T8120)");

    const auto p14 = AppleHardwareProfile::get_iphone14_pro_profile();
    STAGE94_ASSERT(p14.model == "iPhone15,2", "iPhone 14 Pro model must be iPhone15,2");
    STAGE94_ASSERT(p14.target_type == "D73AP", "iPhone 14 Pro target type must be D73AP");

    std::cout << "[PASS] iPhone 15 Hardware Profile Core Identification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: CPU Topology & Heterogeneous Core Microarchitecture
// -----------------------------------------------------------------------------
bool test_cpu_topology_and_microarch() {
    std::cout << "[2] test_cpu_topology_and_microarch...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.cores.size() == 6, "Must have exactly 6 CPU cores");

    // Cores 0-1: Everest Performance
    STAGE94_ASSERT(p.cores[0].is_performance, "Core 0 is performance core");
    STAGE94_ASSERT(p.cores[0].microarch == "Everest", "Core 0 microarch is Everest");
    STAGE94_ASSERT(p.cores[0].clock_frequency_hz == 3460000000ULL, "Everest clock is 3.46 GHz");

    STAGE94_ASSERT(p.cores[1].is_performance, "Core 1 is performance core");
    STAGE94_ASSERT(p.cores[1].microarch == "Everest", "Core 1 microarch is Everest");

    // Cores 2-5: Sawtooth Efficiency
    for (std::size_t i = 2; i < 6; ++i) {
        STAGE94_ASSERT(!p.cores[i].is_performance, "Core is efficiency core");
        STAGE94_ASSERT(p.cores[i].microarch == "Sawtooth", "Core microarch is Sawtooth");
        STAGE94_ASSERT(p.cores[i].clock_frequency_hz == 2020000000ULL, "Sawtooth clock is 2.02 GHz");
    }

    std::cout << "[PASS] CPU Topology & Heterogeneous Core Microarchitecture\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: 16 KB Page Granule and 36-bit Virtual Memory Translation Constraints
// -----------------------------------------------------------------------------
bool test_page_granule_and_address_space() {
    std::cout << "[3] test_page_granule_and_address_space...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.page_size_bytes == 16384u, "Page size is 16 KB (16384 bytes)");
    STAGE94_ASSERT(p.page_shift == 14u, "Page shift is 14 (1 << 14 == 16384)");
    STAGE94_ASSERT(p.virtual_address_bits == 36u, "Virtual address space is 36 bits");
    STAGE94_ASSERT((1ULL << p.virtual_address_bits) == 64ULL * 1024 * 1024 * 1024, "36 bits spans 64 GB");

    std::cout << "[PASS] 16 KB Page Granule and 36-bit Virtual Memory Translation Constraints\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: System Timebase Frequency (24 MHz)
// -----------------------------------------------------------------------------
bool test_timebase_frequency() {
    std::cout << "[4] test_timebase_frequency...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.timebase_frequency_hz == 24000000u, "Timebase frequency is 24,000,000 Hz");

    const double tick_period_ns = 1.0e9 / static_cast<double>(p.timebase_frequency_hz);
    STAGE94_ASSERT(tick_period_ns > 41.6 && tick_period_ns < 41.7, "Tick period is ~41.67 ns");

    std::cout << "[PASS] System Timebase Frequency (24 MHz)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: MMIO Apertures & Interrupt Vectors
// -----------------------------------------------------------------------------
bool test_mmio_apertures_and_irqs() {
    std::cout << "[5] test_mmio_apertures_and_irqs...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.peripherals.size() >= 4, "Must configure at least 4 critical peripherals");

    bool found_aic = false, found_uart = false, found_pmgr = false, found_disp = false;
    for (const auto& peri : p.peripherals) {
        if (peri.name == "aic") {
            found_aic = true;
            STAGE94_ASSERT(peri.base_address == 0x08000000ULL, "AIC base is 0x08000000");
            STAGE94_ASSERT(peri.size == 0x100000ULL, "AIC size is 1 MB");
        } else if (peri.name == "uart0") {
            found_uart = true;
            STAGE94_ASSERT(peri.base_address == 0x09000000ULL, "UART base is 0x09000000");
            STAGE94_ASSERT(peri.has_irq && peri.irq_line == 33u, "UART IRQ is line 33");
        } else if (peri.name == "pmgr") {
            found_pmgr = true;
            STAGE94_ASSERT(peri.base_address == 0x10000000ULL, "PMGR base is 0x10000000");
        } else if (peri.name == "disp0") {
            found_disp = true;
            STAGE94_ASSERT(peri.base_address == 0x4F000000ULL, "Display base is 0x4F000000");
        }
    }

    STAGE94_ASSERT(found_aic && found_uart && found_pmgr && found_disp, "All 4 peripherals verified");

    std::cout << "[PASS] MMIO Apertures & Interrupt Vectors\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Super Retina XDR Panel Dimensions & Memory Footprint
// -----------------------------------------------------------------------------
bool test_display_geometry() {
    std::cout << "[6] test_display_geometry...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();
    STAGE94_ASSERT(p.display.width == 1179u, "Panel width is 1179 pixels");
    STAGE94_ASSERT(p.display.height == 2556u, "Panel height is 2556 pixels");
    STAGE94_ASSERT(p.display.depth == 32u, "Color depth is 32-bit ARGB");
    STAGE94_ASSERT(p.display.row_bytes == 1179u * 4u, "rowBytes is 4716 bytes");

    const std::uint64_t expected_fb_bytes = 1179ULL * 2556ULL * 4ULL;
    STAGE94_ASSERT(expected_fb_bytes == 12054096ULL, "Framebuffer size is 12,054,096 bytes (~12.05 MB)");

    std::cout << "[PASS] Super Retina XDR Panel Dimensions & Memory Footprint\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: DeviceTree ADTS Structural Validation against Profile
// -----------------------------------------------------------------------------
bool test_devicetree_validation_against_profile() {
    std::cout << "[7] test_devicetree_validation_against_profile...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v serial=3", p.display.default_framebuffer_base);

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dt_bytes.data(), dt_bytes.size(), &err);
    STAGE94_ASSERT(root != nullptr, "DeviceTree must deserialize cleanly");

    STAGE94_ASSERT(p.validate_devicetree(*root, err), "DeviceTree matches profile exactly: " + err);

    // Test rejection of corrupted DeviceTree (e.g. wrong model)
    AppleDeviceTreeNode bad_root("");
    bad_root.add_property_string("model", "iPad13,1");
    STAGE94_ASSERT(!p.validate_devicetree(bad_root, err), "Validation must reject mismatched model");

    std::cout << "[PASS] DeviceTree ADTS Structural Validation against Profile\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Memory Layout Validation & Non-Compliant Layout Rejection
// -----------------------------------------------------------------------------
bool test_memory_layout_validation() {
    std::cout << "[8] test_memory_layout_validation...\n";

    const auto p = AppleHardwareProfile::get_iphone15_profile();

    std::string err;
    // Compliant 256 MB layout aligned to 16 KB
    STAGE94_ASSERT(p.validate_memory_layout(0x40000000ULL, 0x10000000ULL, err), "256 MB layout valid");

    // Compliant 6 GB standard hardware RAM layout
    STAGE94_ASSERT(p.validate_memory_layout(0x40000000ULL, p.standard_ram_size, err), "6 GB layout valid");

    // Rejected: below minimum RAM (e.g. 64 MB)
    STAGE94_ASSERT(!p.validate_memory_layout(0x40000000ULL, 0x04000000ULL, err), "Rejects < 256 MB RAM");

    // Rejected: misaligned RAM base (not 16 KB aligned)
    STAGE94_ASSERT(!p.validate_memory_layout(0x40001000ULL, 0x10000000ULL, err), "Rejects unaligned base");

    std::cout << "[PASS] Memory Layout Validation & Non-Compliant Layout Rejection\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 94: iPhone 15 Hardware Profile Tests\n";
    std::cout << "========================================================\n\n";

    bool success = true;
    success &= test_iphone15_identification();
    success &= test_cpu_topology_and_microarch();
    success &= test_page_granule_and_address_space();
    success &= test_timebase_frequency();
    success &= test_mmio_apertures_and_irqs();
    success &= test_display_geometry();
    success &= test_devicetree_validation_against_profile();
    success &= test_memory_layout_validation();

    if (success) {
        std::cout << "ALL 8 STAGE 94 HARDWARE PROFILE VERIFICATION SUITES PASSED!\n";
        return 0;
    } else {
        std::cerr << "STAGE 94 TEST FAILED!\n";
        return 1;
    }
}
