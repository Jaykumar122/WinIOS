#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"

namespace winios::cpu {

struct KernelBootTimerConfig {
    std::uint64_t frequency_hz{24000000ULL}; // 24 MHz default AArch64 CNTFRQ
    std::uint32_t timer_ppi{27U};            // PPI 27 = CNTV, PPI 30 = CNTP
    std::uint64_t initial_tick_us{10000ULL}; // 10 ms default tick interval
    bool          enable_timer{true};
    bool          mask_timer{false};
};

struct KernelBootInterruptConfig {
    std::size_t   num_cores{6};              // Default iPhone 15 A16 topology (6 cores)
    std::uint8_t  priority_mask{0xFF};       // Unmasked (all priorities permitted)
    bool          enable_group1{true};
};

class KernelBootTimerInterruptManager {
public:
    KernelBootTimerInterruptManager(GICv3& gic, VirtualTime& vt) noexcept;

    // Initializes both GICv3 and VirtualTime for kernel boot environment
    void initialize_boot_services(
        const KernelBootTimerConfig& timer_cfg = {},
        const KernelBootInterruptConfig& irq_cfg = {});

    // Syncs the timer interrupt lines (PPI 27 and PPI 30) from VirtualTime into GICv3
    void sync_timer_to_gic(std::size_t core_id = 0) noexcept;

    // Advances the authoritative virtual time by specified microseconds and updates GIC
    void advance_time_us(std::uint64_t microseconds, std::size_t core_id = 0) noexcept;

    // Advances the counter by raw ticks and updates GIC
    void advance_ticks(std::uint64_t ticks, std::size_t core_id = 0) noexcept;

    // Checks whether an interrupt is pending for the given core at current EL
    GICv3::PendingSignal poll_interrupt(
        std::size_t core_id = 0,
        std::uint64_t current_el = 1,
        std::uint64_t hcr_el2 = 0) const noexcept;

    // Simulates CPU reading ICC_IAR1_EL1 to acknowledge Group 1 interrupt
    std::uint32_t acknowledge_irq(
        std::size_t core_id = 0,
        std::uint64_t current_el = 1) noexcept;

    // Simulates CPU writing ICC_EOIR1_EL1 to drop priority and deactivate
    void end_of_interrupt(
        std::uint32_t intid,
        std::size_t core_id = 0,
        std::uint64_t current_el = 1) noexcept;

    // Re-arms the virtual timer for the next interval
    void rearm_timer_us(std::uint64_t microseconds, std::size_t core_id = 0) noexcept;

    // Accessors
    GICv3& get_gic() noexcept { return gic_; }
    const GICv3& get_gic() const noexcept { return gic_; }
    VirtualTime& get_virtual_time() noexcept { return vt_; }
    const VirtualTime& get_virtual_time() const noexcept { return vt_; }
    const KernelBootTimerConfig& get_timer_config() const noexcept { return timer_config_; }

private:
    GICv3& gic_;
    VirtualTime& vt_;
    KernelBootTimerConfig timer_config_{};
    KernelBootInterruptConfig irq_config_{};
};

} // namespace winios::cpu
