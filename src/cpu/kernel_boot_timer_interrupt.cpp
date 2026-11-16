#include "kernel_boot_timer_interrupt.h"

namespace winios::cpu {

KernelBootTimerInterruptManager::KernelBootTimerInterruptManager(GICv3& gic, VirtualTime& vt) noexcept
    : gic_(gic), vt_(vt) {}

void KernelBootTimerInterruptManager::initialize_boot_services(
    const KernelBootTimerConfig& timer_cfg,
    const KernelBootInterruptConfig& irq_cfg) {

    timer_config_ = timer_cfg;
    irq_config_ = irq_cfg;

    // Reset and configure GIC topology
    gic_.reset();
    gic_.set_core_count(irq_cfg.num_cores);

    // 1. Enable GICv3 Distributor for Group 0 and Group 1 (GICD_CTLR: EnableGrp0=1, EnableGrp1NS=1)
    gic_.write_mmio(GICv3::kGicdBase + 0x0000, 0x3U, 4U);

    // 2. Configure per-core Redistributors and CPU Interfaces (ICC)
    for (std::size_t c = 0; c < irq_cfg.num_cores; ++c) {
        const std::uint64_t sgi_base = GICv3::kGicrBase + (c * GICv3::kGicrCoreStride) + 0x10000ULL;

        // Configure all SGIs and PPIs to Group 1 Non-Secure (GICR_IGROUPR0)
        gic_.write_mmio(sgi_base + 0x0080, 0xFFFFFFFFU, 4U);

        // Enable PPI 27 (CNTV) and PPI 30 (CNTP) in GICR_ISENABLER0
        const std::uint32_t enable_mask = (1U << 27U) | (1U << 30U);
        gic_.write_mmio(sgi_base + 0x0100, enable_mask, 4U);

        // Configure ICC System Registers
        gic_.write_icc_pmr(irq_cfg.priority_mask, c);
        gic_.write_icc_igrpen1(irq_cfg.enable_group1 ? 1U : 0U, c);
        gic_.write_icc_bpr1(0U, c);
    }

    // 3. Configure VirtualTime frequency and reset monotonic counter
    vt_.set_frequency(timer_cfg.frequency_hz);
    vt_.set_physical_counter(0ULL);

    // 4. Configure boot core timer
    if (timer_cfg.enable_timer) {
        const std::uint64_t ticks = (timer_cfg.initial_tick_us * timer_cfg.frequency_hz) / 1000000ULL;
        if (timer_cfg.timer_ppi == 27U) {
            auto& vt_state = vt_.get_virt_timer(0);
            vt_state.enable = true;
            vt_state.imask = timer_cfg.mask_timer;
            vt_.write_virt_tval(0, static_cast<std::uint32_t>(ticks));
        } else {
            auto& pt_state = vt_.get_phys_timer(0);
            pt_state.enable = true;
            pt_state.imask = timer_cfg.mask_timer;
            vt_.write_phys_tval(0, static_cast<std::uint32_t>(ticks));
        }
        sync_timer_to_gic(0);
    }
}

void KernelBootTimerInterruptManager::sync_timer_to_gic(std::size_t core_id) noexcept {
    const bool virt_irq = vt_.get_virt_timer_irq(core_id);
    gic_.set_ppi_level(27U, virt_irq, core_id);

    const bool phys_irq = vt_.get_phys_timer_irq(core_id);
    gic_.set_ppi_level(30U, phys_irq, core_id);
}

void KernelBootTimerInterruptManager::advance_time_us(std::uint64_t microseconds, std::size_t core_id) noexcept {
    const std::uint64_t ticks = (microseconds * timer_config_.frequency_hz) / 1000000ULL;
    advance_ticks(ticks, core_id);
}

void KernelBootTimerInterruptManager::advance_ticks(std::uint64_t ticks, std::size_t core_id) noexcept {
    vt_.advance_physical_counter(ticks);
    sync_timer_to_gic(core_id);
}

GICv3::PendingSignal KernelBootTimerInterruptManager::poll_interrupt(
    std::size_t core_id,
    std::uint64_t current_el,
    std::uint64_t hcr_el2) const noexcept {

    return gic_.evaluate(current_el, hcr_el2, core_id);
}

std::uint32_t KernelBootTimerInterruptManager::acknowledge_irq(
    std::size_t core_id,
    std::uint64_t current_el) noexcept {

    return gic_.acknowledge_group1(current_el, core_id);
}

void KernelBootTimerInterruptManager::end_of_interrupt(
    std::uint32_t intid,
    std::size_t core_id,
    std::uint64_t current_el) noexcept {

    gic_.end_of_interrupt_group1(intid, current_el, core_id);
}

void KernelBootTimerInterruptManager::rearm_timer_us(std::uint64_t microseconds, std::size_t core_id) noexcept {
    const std::uint64_t ticks = (microseconds * timer_config_.frequency_hz) / 1000000ULL;
    if (timer_config_.timer_ppi == 27U) {
        vt_.write_virt_tval(core_id, static_cast<std::uint32_t>(ticks));
    } else {
        vt_.write_phys_tval(core_id, static_cast<std::uint32_t>(ticks));
    }
    sync_timer_to_gic(core_id);
}

} // namespace winios::cpu
