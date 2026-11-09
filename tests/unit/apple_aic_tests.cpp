#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_aic.h"
#include "device/apple_pmgr.h"
#include "device/apple_display.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"

#define AIC_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::device;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: AIC 2 Hardware Info, Configuration, and Identification
// -----------------------------------------------------------------------------
bool test_aic_info_and_config() {
    std::cout << "[1] test_aic_info_and_config...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    AIC_ASSERT(aic.core_count() == 6, "AIC core count should be 6 for iPhone 15");
    AIC_ASSERT(aic.mmio_base() == 0x08000000ULL, "AIC MMIO base must be 0x08000000");
    AIC_ASSERT(aic.mmio_size() == 0x00100000ULL, "AIC MMIO size must be 1 MB");

    // Read AIC_INFO (0x0004): [31:24]=6, [23:16]=1, [15:0]=512
    const std::uint64_t info = aic.read_register(AppleAIC::kRegInfo, 4);
    const auto nr_cpus = static_cast<std::uint32_t>((info >> 24) & 0xFFu);
    const auto nr_dies = static_cast<std::uint32_t>((info >> 16) & 0xFFu);
    const auto nr_irqs = static_cast<std::uint32_t>(info & 0xFFFFu);

    AIC_ASSERT(nr_cpus == 6, "AIC_INFO must report 6 CPUs for A16 Bionic");
    AIC_ASSERT(nr_dies == 1, "AIC_INFO must report 1 die");
    AIC_ASSERT(nr_irqs == 512, "AIC_INFO must report 512 IRQ lines");

    // Read/write AIC_CONFIG (0x0010)
    std::uint64_t cfg = aic.read_register(AppleAIC::kRegConfig, 4);
    AIC_ASSERT(cfg == 1, "AIC should be enabled by default");

    aic.write_register(AppleAIC::kRegConfig, 0, 4);
    cfg = aic.read_register(AppleAIC::kRegConfig, 4);
    AIC_ASSERT(cfg == 0, "AIC should be disabled after clearing enable bit");

    aic.write_register(AppleAIC::kRegConfig, 1, 4);
    cfg = aic.read_register(AppleAIC::kRegConfig, 4);
    AIC_ASSERT(cfg == 1, "AIC should re-enable successfully");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Core Identification (AIC_WHOAMI) and Banked Per-Core Offsets
// -----------------------------------------------------------------------------
bool test_aic_whoami_and_banked_offsets() {
    std::cout << "[2] test_aic_whoami_and_banked_offsets...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Test active core configuration
    for (std::size_t c = 0; c < 6; ++c) {
        aic.set_current_core(c);
        AIC_ASSERT(aic.current_core() == c, "current_core() must reflect set core");
    }

    // Test per-core banked offsets: 0x2000 + (core * 0x80)
    for (std::size_t c = 0; c < 6; ++c) {
        const std::uint64_t off = AppleAIC::kRegCoreBase + (c * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI;
        const std::uint64_t who = aic.read_register(off, 4);
        AIC_ASSERT(who == c, "Banked WHOAMI must match core stride index");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Hardware IRQ Mask Set / Clear and Pending Tracking
// -----------------------------------------------------------------------------
bool test_aic_mask_and_hw_pending() {
    std::cout << "[3] test_aic_mask_and_hw_pending...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // By default, all IRQs are masked
    AIC_ASSERT(aic.is_hw_irq_masked(33), "IRQ 33 should start masked");

    // Unmask IRQ 33 (UART) via MMIO Mask Clear (0x8080 + 4 for word 1)
    // IRQ 33 is word 1, bit 1
    aic.write_register(AppleAIC::kRegMaskClrBase + 4, (1u << 1), 4);
    AIC_ASSERT(!aic.is_hw_irq_masked(33), "IRQ 33 should now be unmasked");

    // Assert hardware IRQ 33
    aic.set_hw_irq(33, true);
    AIC_ASSERT(aic.is_hw_irq_asserted(33), "IRQ 33 wire should be asserted");

    // Read HW Pending (0x8100 + 4)
    std::uint64_t pend = aic.read_register(AppleAIC::kRegHwPendingBase + 4, 4);
    AIC_ASSERT((pend & (1u << 1)) != 0, "IRQ 33 must be in HW Pending register");

    // Mask IRQ 33 again via Mask Set (0x8000 + 4)
    aic.write_register(AppleAIC::kRegMaskSetBase + 4, (1u << 1), 4);
    AIC_ASSERT(aic.is_hw_irq_masked(33), "IRQ 33 should be masked again");

    // When masked, HW Pending register hides it
    pend = aic.read_register(AppleAIC::kRegHwPendingBase + 4, 4);
    AIC_ASSERT((pend & (1u << 1)) == 0, "Masked IRQ must not show in pending register");

    // But HW State register (0x8180 + 4) still shows raw asserted wire
    const std::uint64_t raw = aic.read_register(AppleAIC::kRegHwStateBase + 4, 4);
    AIC_ASSERT((raw & (1u << 1)) != 0, "Raw wire state should still show asserted");

    // Deassert wire
    aic.set_hw_irq(33, false);
    AIC_ASSERT(!aic.is_hw_irq_asserted(33), "IRQ 33 wire should now be deasserted");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Hardware IRQ Dispatching, Routing, Event Pop, and EOI
// -----------------------------------------------------------------------------
bool test_aic_irq_dispatch_and_eoi() {
    std::cout << "[4] test_aic_irq_dispatch_and_eoi...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Route IRQ 33 to Core 2 only
    aic.set_irq_target(33, (1u << 2));
    AIC_ASSERT(aic.get_irq_target(33) == (1u << 2), "Target affinity for IRQ 33 should be Core 2");

    // Unmask IRQ 33
    aic.unmask_hw_irq(33);

    // Assert IRQ 33
    aic.set_hw_irq(33, true);

    // Core 0 should NOT have pending interrupt
    AIC_ASSERT(!aic.has_pending_interrupt(0), "Core 0 must not receive IRQ targeted to Core 2");
    AIC_ASSERT(aic.pop_event(0) == AppleAIC::kEventNone, "Core 0 must have no event");

    // Core 2 MUST have pending interrupt
    AIC_ASSERT(aic.has_pending_interrupt(2), "Core 2 must have pending interrupt");

    // Pop event on Core 2 via MMIO (0x2000 + 2 * 0x80 + 0x04)
    const std::uint64_t ev_off = AppleAIC::kRegCoreBase + (2 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent;
    const std::uint64_t ev = aic.read_register(ev_off, 4);

    const auto ev_type = static_cast<std::uint32_t>((ev >> 16) & 0xFFFFu);
    const auto ev_vector = static_cast<std::uint32_t>(ev & 0xFFFFu);

    AIC_ASSERT(ev_type == AppleAIC::kEventTypeHw, "Event type must be HW IRQ");
    AIC_ASSERT(ev_vector == 33, "Event vector must be IRQ 33");

    // Deassert wire and write EOI
    aic.set_hw_irq(33, false);
    aic.write_register(ev_off, ev, 4);

    // Now Core 2 should have no pending interrupts
    AIC_ASSERT(!aic.has_pending_interrupt(2), "Core 2 must have no pending interrupts after EOI");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Inter-Processor Interrupts (IPI) Lifecycle
// -----------------------------------------------------------------------------
bool test_aic_ipi_lifecycle() {
    std::cout << "[5] test_aic_ipi_lifecycle...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Send IPI targeting Cores 1 and 4 via AIC_IPI_SEND (0x0040)
    const std::uint32_t target_mask = (1u << 1) | (1u << 4);
    aic.write_register(AppleAIC::kRegIpiSend, target_mask, 4);

    AIC_ASSERT(!aic.has_pending_interrupt(0), "Core 0 must not receive IPI");
    AIC_ASSERT(aic.has_pending_interrupt(1), "Core 1 must receive IPI");
    AIC_ASSERT(!aic.has_pending_interrupt(2), "Core 2 must not receive IPI");
    AIC_ASSERT(!aic.has_pending_interrupt(3), "Core 3 must not receive IPI");
    AIC_ASSERT(aic.has_pending_interrupt(4), "Core 4 must receive IPI");
    AIC_ASSERT(!aic.has_pending_interrupt(5), "Core 5 must not receive IPI");

    // Check Core 1 event
    const std::uint32_t ev1 = aic.pop_event(1);
    const auto type1 = (ev1 >> 16) & 0xFFFFu;
    AIC_ASSERT(type1 == AppleAIC::kEventTypeIpi, "Core 1 event must be IPI");

    // Clear IPI on Core 1 via AIC_IPI_ACK (0x2008 + 1 * 0x80)
    const std::uint64_t ack_off1 = AppleAIC::kRegCoreBase + (1 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffIpiAck;
    aic.write_register(ack_off1, 0x01u, 4);

    AIC_ASSERT(!aic.has_pending_interrupt(1), "Core 1 must clear IPI after ack");
    AIC_ASSERT(aic.has_pending_interrupt(4), "Core 4 IPI must still be pending");

    // Clear Core 4
    aic.clear_ipi(4, 0x01u);
    AIC_ASSERT(!aic.has_pending_interrupt(4), "Core 4 must clear IPI");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Architectural SoC Timer Interrupt Routing
// -----------------------------------------------------------------------------
bool test_aic_timer_interrupt_routing() {
    std::cout << "[6] test_aic_timer_interrupt_routing...\n";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Assert timer interrupt on Core 0 (boot core)
    aic.set_timer_irq(0, true);
    AIC_ASSERT(aic.is_timer_irq_asserted(0), "Core 0 timer interrupt should be asserted");
    AIC_ASSERT(aic.has_pending_interrupt(0), "Core 0 should have pending interrupt");

    // Pop event
    const std::uint32_t ev = aic.pop_event(0);
    const auto type = (ev >> 16) & 0xFFFFu;
    AIC_ASSERT(type == AppleAIC::kEventTypeTimer, "Event must be Timer interrupt");

    // Acknowledge EOI
    aic.acknowledge_eoi(0, ev);
    AIC_ASSERT(!aic.has_pending_interrupt(0), "Core 0 should be clear after timer EOI");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Apple PMGR Controller and Chip ID (T8120 / A16 Bionic)
// -----------------------------------------------------------------------------
bool test_apple_pmgr_chip_id_and_status() {
    std::cout << "[7] test_apple_pmgr_chip_id_and_status...\n";

    ApplePMGR pmgr;

    AIC_ASSERT(pmgr.get_chip_id() == 0x8120u, "PMGR must identify as T8120 (0x8120)");
    AIC_ASSERT(pmgr.mmio_base() == 0x10000000ULL, "PMGR base must be 0x10000000");

    // Read Chip ID (0x0000)
    const std::uint64_t chip_id = pmgr.read_register(ApplePMGR::kRegChipId, 4);
    AIC_ASSERT(chip_id == 0x8120ULL, "Chip ID register read must return 0x8120");

    // Read Revision (0x0004)
    const std::uint64_t rev = pmgr.read_register(ApplePMGR::kRegRevision, 4);
    AIC_ASSERT(rev == 0x0001ULL, "Revision register must return 1");

    // Read System Status (0x0008)
    const std::uint64_t status = pmgr.read_register(ApplePMGR::kRegSysStatus, 4);
    AIC_ASSERT(status == 1ULL, "System status must indicate Active");

    // Test software reset trigger (0x0010)
    AIC_ASSERT(!pmgr.was_reset_requested(), "Reset must not be requested initially");
    pmgr.write_register(ApplePMGR::kRegResetTrigger, 0xDEADBEEFULL, 4);
    AIC_ASSERT(pmgr.was_reset_requested(), "Reset request flag must be set after writing reset trigger");
    pmgr.clear_reset_request();
    AIC_ASSERT(!pmgr.was_reset_requested(), "Reset request should clear");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Apple PMGR Power Domain Transitions
// -----------------------------------------------------------------------------
bool test_apple_pmgr_power_domains() {
    std::cout << "[8] test_apple_pmgr_power_domains...\n";

    ApplePMGR pmgr;

    // Check default power states
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster0), "CPU0 must be powered");
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster1), "CPU1 must be powered");
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainAic), "AIC must be powered");
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainUart0), "UART0 must be powered");
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainDisplay), "Display must be powered");

    // Read Display domain register (0x0280)
    // Full power (0xF): bits [3:0]=0xF, bits [7:4]=0xF, bit [8]=1 (0x1FF)
    std::uint64_t disp_pwr = pmgr.read_register(ApplePMGR::kDomainDisplay, 4);
    AIC_ASSERT((disp_pwr & 0x0Fu) == ApplePMGR::kPowerStateFull, "Display target should be Full");
    AIC_ASSERT((disp_pwr & 0x100u) != 0, "Clock stable bit should be set");

    // Power off GPU domain (0x0300)
    pmgr.write_register(ApplePMGR::kDomainGpu, ApplePMGR::kPowerStateOff, 4);
    AIC_ASSERT(!pmgr.is_domain_powered(ApplePMGR::kDomainGpu), "GPU should now be powered off");
    const std::uint64_t gpu_pwr = pmgr.read_register(ApplePMGR::kDomainGpu, 4);
    AIC_ASSERT(gpu_pwr == 0, "GPU power register should read 0 when off");

    // Power GPU back to Active
    pmgr.write_register(ApplePMGR::kDomainGpu, ApplePMGR::kPowerStateActive, 4);
    AIC_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainGpu), "GPU should be powered active");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Apple Display Framebuffer Geometry & Visuals
// -----------------------------------------------------------------------------
bool test_apple_display_framebuffer() {
    std::cout << "[9] test_apple_display_framebuffer...\n";

    AppleDisplay disp(0x44495350u, AppleDisplay::kDefaultDisplayBase, AppleDisplay::kDefaultDisplaySize, 0x7C000000ULL);

    AIC_ASSERT(disp.width() == 1179, "iPhone 15 width must be 1179");
    AIC_ASSERT(disp.height() == 2556, "iPhone 15 height must be 2556");
    AIC_ASSERT(disp.depth() == 32, "Color depth must be 32 bpp");
    AIC_ASSERT(disp.row_bytes() == 1179 * 4, "RowBytes must be 4716");
    AIC_ASSERT(disp.fb_phys_base() == 0x7C000000ULL, "Physical FB base match");

    // Test pixel manipulation
    disp.set_pixel(100, 200, 0x00FF8800u);
    AIC_ASSERT(disp.get_pixel(100, 200) == 0x00FF8800u, "Pixel readback must match written ARGB");

    // Test boot splash
    disp.render_boot_splash();
    // Center pixel should be white (boot glyph)
    const std::uint32_t cx = disp.width() / 2;
    const std::uint32_t cy = disp.height() / 2;
    AIC_ASSERT(disp.get_pixel(cx, cy) == 0x00FFFFFFu, "Center pixel of boot splash should be white glyph");

    // Corner pixel should be black
    AIC_ASSERT(disp.get_pixel(0, 0) == 0x00000000u, "Corner pixel of boot splash should be black");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: State Serialization and Deserialization (.wsnap v2 snapshot)
// -----------------------------------------------------------------------------
bool test_aic_and_pmgr_snapshot_serialization() {
    std::cout << "[10] test_aic_and_pmgr_snapshot_serialization...\n";

    // 1. AppleAIC snapshot
    AppleAIC aic1(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);
    aic1.unmask_hw_irq(42);
    aic1.set_hw_irq(42, true);
    aic1.set_irq_target(42, (1u << 3));
    aic1.send_ipi((1u << 5));
    aic1.set_timer_irq(1, true);

    const auto aic_bytes = aic1.serialize_state();
    AIC_ASSERT(!aic_bytes.empty(), "AIC serialized buffer must not be empty");

    AppleAIC aic2;
    AIC_ASSERT(aic2.deserialize_state(aic_bytes.data(), aic_bytes.size()), "AIC deserialization must succeed");
    AIC_ASSERT(!aic2.is_hw_irq_masked(42), "Restored IRQ 42 should be unmasked");
    AIC_ASSERT(aic2.is_hw_irq_asserted(42), "Restored IRQ 42 wire should be asserted");
    AIC_ASSERT(aic2.get_irq_target(42) == (1u << 3), "Restored IRQ 42 target should be Core 3");
    AIC_ASSERT(aic2.get_pending_ipi(5) == 0x01u, "Restored Core 5 IPI should be pending");
    AIC_ASSERT(aic2.is_timer_irq_asserted(1), "Restored Core 1 timer should be asserted");

    // 2. ApplePMGR snapshot
    ApplePMGR pmgr1;
    pmgr1.set_domain_power_state(ApplePMGR::kDomainGpu, ApplePMGR::kPowerStateOff);
    const auto pmgr_bytes = pmgr1.serialize_state();
    AIC_ASSERT(!pmgr_bytes.empty(), "PMGR serialized buffer must not be empty");

    ApplePMGR pmgr2;
    AIC_ASSERT(pmgr2.deserialize_state(pmgr_bytes.data(), pmgr_bytes.size()), "PMGR deserialization must succeed");
    AIC_ASSERT(!pmgr2.is_domain_powered(ApplePMGR::kDomainGpu), "Restored GPU should be powered off");
    AIC_ASSERT(pmgr2.is_domain_powered(ApplePMGR::kDomainDisplay), "Restored Display should be powered");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 11: VirtualDeviceBus Interconnect Integration (iPhone 15 Peripherals)
// -----------------------------------------------------------------------------
bool test_iphone15_bus_interconnect() {
    std::cout << "[11] test_iphone15_bus_interconnect...\n";

    VirtualDeviceBus bus;

    auto aic = std::make_shared<AppleAIC>(0x41494332u, 0x08000000ULL, 0x00100000ULL, 6);
    auto uart = std::make_shared<VirtualUartDevice>(0x55415254u, 0x09000000ULL, 0x00001000ULL, 33);
    auto pmgr = std::make_shared<ApplePMGR>(0x504D4752u, 0x10000000ULL, 0x00100000ULL, 0x8120u);
    auto disp = std::make_shared<AppleDisplay>(0x44495350u, 0x0B000000ULL, 0x00010000ULL, 0x7C000000ULL);

    AIC_ASSERT(bus.register_device(aic), "AIC registration on bus must succeed");
    AIC_ASSERT(bus.register_device(uart), "UART registration on bus must succeed");
    AIC_ASSERT(bus.register_device(pmgr), "PMGR registration on bus must succeed");
    AIC_ASSERT(bus.register_device(disp), "Display registration on bus must succeed");

    AIC_ASSERT(bus.device_count() == 4, "Bus should contain 4 devices");

    // Perform bus read to AIC
    const std::uint64_t aic_info = bus.read_mmio(0x08000004ULL, 4);
    AIC_ASSERT((aic_info & 0xFFFFu) == 512, "Bus MMIO to AIC must read info register");

    // Perform bus read to PMGR
    const std::uint64_t chip_id = bus.read_mmio(0x10000000ULL, 4);
    AIC_ASSERT(chip_id == 0x8120ULL, "Bus MMIO to PMGR must read chip id");

    // Perform bus write to UART
    bus.write_mmio(0x09000000ULL, 'A', 4);
    AIC_ASSERT(uart->get_output_text() == "A", "Bus MMIO write to UART must send character");

    // Perform bus read to Display
    const std::uint64_t width = bus.read_mmio(0x0B000000ULL, 4);
    AIC_ASSERT(width == 1179, "Bus MMIO to Display must read width");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== Running WinIOS Apple AIC 2, PMGR & iPhone 15 Hardware Tests ===\n";

    if (!test_aic_info_and_config()) return 1;
    if (!test_aic_whoami_and_banked_offsets()) return 2;
    if (!test_aic_mask_and_hw_pending()) return 3;
    if (!test_aic_irq_dispatch_and_eoi()) return 4;
    if (!test_aic_ipi_lifecycle()) return 5;
    if (!test_aic_timer_interrupt_routing()) return 6;
    if (!test_apple_pmgr_chip_id_and_status()) return 7;
    if (!test_apple_pmgr_power_domains()) return 8;
    if (!test_apple_display_framebuffer()) return 9;
    if (!test_aic_and_pmgr_snapshot_serialization()) return 10;
    if (!test_iphone15_bus_interconnect()) return 11;

    std::cout << ">>> All 11 Apple AIC 2, PMGR & iPhone 15 Hardware Test Suites PASSED! <<<\n";
    return 0;
}
