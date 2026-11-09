#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "device/apple_display.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "runtime/darwin_platform_expert.h"
#include "runtime/iokit_registry.h"

#define PERIPH_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Test 1: UART MMIO Registration & VirtualDeviceBus Routing
// -----------------------------------------------------------------------------
bool test_uart_bus_registration_and_routing() {
    std::cout << "[RUN] Test 1: UART MMIO Registration & VirtualDeviceBus Routing ... ";

    VirtualDeviceBus bus;
    auto uart = std::make_shared<VirtualUartDevice>(
        0x55415254u, VirtualUartDevice::kDefaultUartBase, VirtualUartDevice::kDefaultUartSize, 33u);

    PERIPH_TEST_ASSERT(bus.register_device(uart), "UART device must register successfully on bus");
    PERIPH_TEST_ASSERT(bus.is_device_address(VirtualUartDevice::kDefaultUartBase), "UART base must be recognized");
    PERIPH_TEST_ASSERT(bus.find_device_by_address(VirtualUartDevice::kDefaultUartBase) == uart, "Bus must route to UART");

    // Read PrimeCell PL011 ID registers via MMIO
    const std::uint64_t base = VirtualUartDevice::kDefaultUartBase;
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFE0, 4) == 0x11u, "UARTPeriphID0 must be 0x11");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFE4, 4) == 0x10u, "UARTPeriphID1 must be 0x10");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFE8, 4) == 0x34u, "UARTPeriphID2 must be 0x34");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFEC, 4) == 0x00u, "UARTPeriphID3 must be 0x00");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFF0, 4) == 0x0Du, "UARTPCellID0 must be 0x0D");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFF4, 4) == 0xF0u, "UARTPCellID1 must be 0xF0");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFF8, 4) == 0x05u, "UARTPCellID2 must be 0x05");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + 0xFFC, 4) == 0xB1u, "UARTPCellID3 must be 0xB1");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Early Kernel Console Character Stream & Buffer Capture
// -----------------------------------------------------------------------------
bool test_early_kernel_console_stream() {
    std::cout << "[RUN] Test 2: Early Kernel Console Stream & Buffer Capture ... ";

    VirtualDeviceBus bus;
    auto uart = std::make_shared<VirtualUartDevice>();
    bus.register_device(uart);

    const std::string kernel_banner =
        "Darwin Kernel Version 23.4.0: Fri Mar 15 20:09:12 PDT 2024; root:xnu-10063.101.17~1/RELEASE_ARM64_T8120\n";

    for (char c : kernel_banner) {
        bus.write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR,
                       static_cast<std::uint64_t>(static_cast<std::uint8_t>(c)), 4);
    }

    PERIPH_TEST_ASSERT(uart->output_bytes_count() == kernel_banner.size(), "Output byte count must match banner length");
    PERIPH_TEST_ASSERT(uart->get_output_text() == kernel_banner, "Buffered output text must strictly match transmitted banner");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: UART Flag Register (FR) & Line Control Conformance
// -----------------------------------------------------------------------------
bool test_uart_flags_and_control() {
    std::cout << "[RUN] Test 3: UART Flags & Line Control Conformance ... ";

    VirtualDeviceBus bus;
    auto uart = std::make_shared<VirtualUartDevice>();
    bus.register_device(uart);

    const std::uint64_t base = VirtualUartDevice::kDefaultUartBase;
    const std::uint64_t fr = bus.read_mmio(base + VirtualUartDevice::kRegFR, 4);
    PERIPH_TEST_ASSERT((fr & VirtualUartDevice::kFR_TXFE) != 0, "Transmit FIFO must be empty on reset");
    PERIPH_TEST_ASSERT((fr & VirtualUartDevice::kFR_RXFE) != 0, "Receive FIFO must be empty on reset");

    const std::uint64_t lcrh = bus.read_mmio(base + VirtualUartDevice::kRegLCRH, 4);
    PERIPH_TEST_ASSERT(lcrh == 0x70u, "Default LCRH must be 0x70 (8-bit word length, FIFO enabled)");

    const std::uint64_t cr = bus.read_mmio(base + VirtualUartDevice::kRegCR, 4);
    PERIPH_TEST_ASSERT((cr & VirtualUartDevice::kCR_UARTEN) != 0, "UART must be enabled by default");
    PERIPH_TEST_ASSERT((cr & VirtualUartDevice::kCR_TXE) != 0, "TX must be enabled by default");
    PERIPH_TEST_ASSERT((cr & VirtualUartDevice::kCR_RXE) != 0, "RX must be enabled by default");

    uart->clear_output();
    PERIPH_TEST_ASSERT(uart->output_bytes_count() == 0, "Output buffer must be empty after clear");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: UART RX Input Injection & Interrupt Signaling
// -----------------------------------------------------------------------------
bool test_uart_rx_injection_and_interrupt() {
    std::cout << "[RUN] Test 4: UART RX Injection & Interrupt Signaling ... ";

    VirtualDeviceBus bus;
    auto uart = std::make_shared<VirtualUartDevice>();
    bus.register_device(uart);

    const std::uint64_t base = VirtualUartDevice::kDefaultUartBase;

    // Enable RX interrupt mask (bit 4: 0x10)
    bus.write_mmio(base + VirtualUartDevice::kRegIMSC, 0x10u, 4);

    // Inject command into RX FIFO
    const std::string cmd = "boot -v\n";
    uart->inject_input(cmd);

    // Verify RXFE flag is cleared
    const std::uint64_t fr = bus.read_mmio(base + VirtualUartDevice::kRegFR, 4);
    PERIPH_TEST_ASSERT((fr & VirtualUartDevice::kFR_RXFE) == 0, "RXFE must be cleared when RX FIFO contains data");

    // Verify raw interrupt status (RIS) and masked interrupt status (MIS)
    const std::uint64_t ris = bus.read_mmio(base + VirtualUartDevice::kRegRIS, 4);
    PERIPH_TEST_ASSERT((ris & (1u << 4)) != 0, "RX raw interrupt bit 4 must be set");

    const std::uint64_t mis = bus.read_mmio(base + VirtualUartDevice::kRegMIS, 4);
    PERIPH_TEST_ASSERT((mis & (1u << 4)) != 0, "RX masked interrupt bit 4 must be asserted");
    PERIPH_TEST_ASSERT(uart->is_interrupt_asserted(), "Device interrupt line must be asserted");

    // Read back characters via MMIO Data Register
    std::string received;
    for (std::size_t i = 0; i < cmd.size(); ++i) {
        std::uint64_t ch = bus.read_mmio(base + VirtualUartDevice::kRegDR, 4);
        received.push_back(static_cast<char>(ch & 0xFFu));
    }
    PERIPH_TEST_ASSERT(received == cmd, "Received characters must match injected command");

    // Verify FIFO empty flag is restored
    const std::uint64_t fr_after = bus.read_mmio(base + VirtualUartDevice::kRegFR, 4);
    PERIPH_TEST_ASSERT((fr_after & VirtualUartDevice::kFR_RXFE) != 0, "RXFE must be set when FIFO is empty");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: AppleDisplay MMIO Registration & VirtualDeviceBus Routing
// -----------------------------------------------------------------------------
bool test_display_bus_registration_and_geometry() {
    std::cout << "[RUN] Test 5: AppleDisplay MMIO Registration & Bus Routing ... ";

    VirtualDeviceBus bus;
    auto display = std::make_shared<AppleDisplay>();
    PERIPH_TEST_ASSERT(bus.register_device(display), "Display must register successfully on bus");
    PERIPH_TEST_ASSERT(bus.is_device_address(AppleDisplay::kDefaultDisplayBase), "Display MMIO base must be recognized");
    PERIPH_TEST_ASSERT(bus.find_device_by_address(AppleDisplay::kDefaultDisplayBase) == display, "Bus must route to Display");

    const std::uint64_t base = AppleDisplay::kDefaultDisplayBase;
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegWidth, 4) == 1179u, "Display width must be 1179 pixels");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegHeight, 4) == 2556u, "Display height must be 2556 pixels");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegDepth, 4) == 32u, "Display color depth must be 32-bit");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegRowBytes, 4) == 4716u, "RowBytes must be 4716 (1179 * 4)");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegStatus, 4) == 1u, "Display controller status must be active (1)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Framebuffer Physical Address Mapping & Reconfiguration
// -----------------------------------------------------------------------------
bool test_display_fb_phys_base_reconfiguration() {
    std::cout << "[RUN] Test 6: Framebuffer Physical Address Reconfiguration ... ";

    VirtualDeviceBus bus;
    auto display = std::make_shared<AppleDisplay>();
    bus.register_device(display);

    const std::uint64_t base = AppleDisplay::kDefaultDisplayBase;
    const std::uint64_t low = bus.read_mmio(base + AppleDisplay::kRegFbBaseLow, 4);
    const std::uint64_t high = bus.read_mmio(base + AppleDisplay::kRegFbBaseHigh, 4);
    const std::uint64_t initial_fb = (high << 32) | low;
    PERIPH_TEST_ASSERT(initial_fb == 0x7C000000ULL, "Initial FB base must match 0x7C000000ULL");

    // Reconfigure FB base to 0x8C000000ULL
    bus.write_mmio(base + AppleDisplay::kRegFbBaseLow, 0x8C000000u, 4);
    bus.write_mmio(base + AppleDisplay::kRegFbBaseHigh, 0x00000000u, 4);

    PERIPH_TEST_ASSERT(display->fb_phys_base() == 0x8C000000ULL, "Updated physical FB base must reflect in controller");
    PERIPH_TEST_ASSERT(bus.read_mmio(base + AppleDisplay::kRegFbBaseLow, 4) == 0x8C000000u, "Readback FB low must match");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Display Framebuffer Pixel Operations & Bounds Checking
// -----------------------------------------------------------------------------
bool test_display_pixel_manipulation_and_bounds() {
    std::cout << "[RUN] Test 7: Display Framebuffer Pixel Operations & Bounds ... ";

    auto display = std::make_shared<AppleDisplay>();
    display->clear(0x00000000u);

    // Set corner and center pixels
    display->set_pixel(0, 0, 0xFFFF0000u);             // Top-Left: Red
    display->set_pixel(1178, 0, 0xFF00FF00u);          // Top-Right: Green
    display->set_pixel(0, 2555, 0xFF0000FFu);          // Bottom-Left: Blue
    display->set_pixel(1178, 2555, 0xFFFFFFFFu);       // Bottom-Right: White
    display->set_pixel(589, 1278, 0xFF123456u);        // Center: Custom ARGB

    PERIPH_TEST_ASSERT(display->get_pixel(0, 0) == 0xFFFF0000u, "Top-left pixel must be Red");
    PERIPH_TEST_ASSERT(display->get_pixel(1178, 0) == 0xFF00FF00u, "Top-right pixel must be Green");
    PERIPH_TEST_ASSERT(display->get_pixel(0, 2555) == 0xFF0000FFu, "Bottom-left pixel must be Blue");
    PERIPH_TEST_ASSERT(display->get_pixel(1178, 2555) == 0xFFFFFFFFu, "Bottom-right pixel must be White");
    PERIPH_TEST_ASSERT(display->get_pixel(589, 1278) == 0xFF123456u, "Center pixel must match");

    // Out of bounds write must be safely ignored without memory corruption
    display->set_pixel(1179, 2556, 0xDEADBEEFu);
    PERIPH_TEST_ASSERT(display->get_pixel(1179, 2556) == 0u, "Out of bounds pixel read must return 0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Apple Boot Splash Glyph Rendering & PPM Export
// -----------------------------------------------------------------------------
bool test_display_boot_splash_and_ppm_export() {
    std::cout << "[RUN] Test 8: Apple Boot Splash Glyph & PPM Export ... ";

    auto display = std::make_shared<AppleDisplay>();
    display->render_boot_splash();

    // Center pixel must be part of the glyph (white: 0x00FFFFFFu)
    const std::uint32_t cx = display->width() / 2;
    const std::uint32_t cy = display->height() / 2;
    PERIPH_TEST_ASSERT(display->get_pixel(cx, cy) == 0x00FFFFFFu, "Center pixel must be glyph white");

    // Distant corner pixel must be black (0x00000000u)
    PERIPH_TEST_ASSERT(display->get_pixel(10, 10) == 0x00000000u, "Corner pixel must remain black");

    // Export PPM
    auto ppm = display->export_ppm();
    PERIPH_TEST_ASSERT(!ppm.empty(), "PPM export must produce valid data");

    const std::string expected_hdr = "P6\n1179 2556\n255\n";
    std::string actual_hdr(reinterpret_cast<const char*>(ppm.data()), expected_hdr.size());
    PERIPH_TEST_ASSERT(actual_hdr == expected_hdr, "PPM header must match P6 format and geometry");

    const std::size_t expected_total_size = expected_hdr.size() + (1179ULL * 2556ULL * 3ULL);
    PERIPH_TEST_ASSERT(ppm.size() == expected_total_size, "PPM total payload size must match header + RGB data");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: DeviceTree Binding & Darwin Driver Matching for UART & DCP
// -----------------------------------------------------------------------------
bool test_devicetree_binding_and_driver_matching() {
    std::cout << "[RUN] Test 9: DeviceTree Binding & Driver Matching (UART & DCP) ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    std::string err;
    PERIPH_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "DeviceTree probing must succeed");

    // 1. Verify UART matching
    const auto* uart_driver = pe.find_driver_by_path("/arm-io/uart0");
    PERIPH_TEST_ASSERT(uart_driver != nullptr, "UART driver must be matched");
    PERIPH_TEST_ASSERT(uart_driver->personality.io_class == "AppleSerialUART", "UART driver class must be AppleSerialUART");
    PERIPH_TEST_ASSERT(uart_driver->final_score >= 40000, "UART driver probe score must be >= 40000");

    // 2. Verify DCP Display matching
    const auto* disp_driver = pe.find_driver_by_path("/arm-io/disp0");
    PERIPH_TEST_ASSERT(disp_driver != nullptr, "Display driver must be matched");
    PERIPH_TEST_ASSERT(disp_driver->personality.io_class == "AppleDCP", "Display driver class must be AppleDCP");
    PERIPH_TEST_ASSERT(disp_driver->final_score >= 50000, "Display driver probe score must be >= 50000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Multi-Peripheral State Serialization & Snapshot Restoration
// -----------------------------------------------------------------------------
bool test_peripherals_serialization_and_restore() {
    std::cout << "[RUN] Test 10: Multi-Peripheral Serialization & Snapshot Restore ... ";

    // 1. UART State Setup & Serialization
    auto uart_orig = std::make_shared<VirtualUartDevice>();
    uart_orig->write_register(VirtualUartDevice::kRegDR, 'X', 4);
    uart_orig->inject_input("INPUT_TOKEN");
    auto uart_snapshot = uart_orig->serialize_state();

    // Restore to fresh UART
    auto uart_restored = std::make_shared<VirtualUartDevice>();
    PERIPH_TEST_ASSERT(uart_restored->deserialize_state(uart_snapshot.data(), uart_snapshot.size()),
                       "UART state deserialization must succeed");
    PERIPH_TEST_ASSERT(uart_restored->get_output_text() == "X", "Restored UART output buffer must match");
    PERIPH_TEST_ASSERT(uart_restored->read_register(VirtualUartDevice::kRegDR, 4) == static_cast<std::uint64_t>('I'),
                       "Restored UART RX FIFO must yield first injected character 'I'");

    // 2. Display State Setup & Serialization
    auto disp_orig = std::make_shared<AppleDisplay>();
    disp_orig->set_fb_phys_base(0x90000000ULL);
    auto disp_snapshot = disp_orig->serialize_state();

    // Restore to fresh Display
    auto disp_restored = std::make_shared<AppleDisplay>();
    PERIPH_TEST_ASSERT(disp_restored->deserialize_state(disp_snapshot.data(), disp_snapshot.size()),
                       "Display state deserialization must succeed");
    PERIPH_TEST_ASSERT(disp_restored->fb_phys_base() == 0x90000000ULL, "Restored FB physical base must match");
    PERIPH_TEST_ASSERT(disp_restored->width() == 1179u, "Restored width must be 1179");
    PERIPH_TEST_ASSERT(disp_restored->height() == 2556u, "Restored height must be 2556");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 115: Peripheral Readiness & Console Output Suite         \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_uart_bus_registration_and_routing()) passed++;
    if (test_early_kernel_console_stream()) passed++;
    if (test_uart_flags_and_control()) passed++;
    if (test_uart_rx_injection_and_interrupt()) passed++;
    if (test_display_bus_registration_and_geometry()) passed++;
    if (test_display_fb_phys_base_reconfiguration()) passed++;
    if (test_display_pixel_manipulation_and_bounds()) passed++;
    if (test_display_boot_splash_and_ppm_export()) passed++;
    if (test_devicetree_binding_and_driver_matching()) passed++;
    if (test_peripherals_serialization_and_restore()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
