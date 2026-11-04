#include "cpu/virtual_time.h"

#include <algorithm>

namespace winios {

namespace {

constexpr std::uint16_t kSysregCNTFRQ_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|0u; // 0x5700
constexpr std::uint16_t kSysregCNTPCT_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|1u; // 0x5701
constexpr std::uint16_t kSysregCNTVCT_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|2u; // 0x5702
constexpr std::uint16_t kSysregCNTKCTL_EL1 = (1u<<14)|(0u<<11)|(14u<<7)|(1u<<3)|0u; // 0x4708
constexpr std::uint16_t kSysregCNTP_TVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|0u; // 0x5710
constexpr std::uint16_t kSysregCNTP_CTL    = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|1u; // 0x5711
constexpr std::uint16_t kSysregCNTP_CVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|2u; // 0x5712
constexpr std::uint16_t kSysregCNTV_TVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|0u; // 0x5718
constexpr std::uint16_t kSysregCNTV_CTL    = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|1u; // 0x5719
constexpr std::uint16_t kSysregCNTV_CVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|2u; // 0x571A
constexpr std::uint16_t kSysregCNTVOFF_EL2  = (1u<<14)|(4u<<11)|(14u<<7)|(0u<<3)|3u; // 0x6703
constexpr std::uint16_t kSysregCNTHCTL_EL2  = (1u<<14)|(4u<<11)|(14u<<7)|(1u<<3)|0u; // 0x6708
constexpr std::uint16_t kSysregCNTHP_TVAL   = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|0u; // 0x6710
constexpr std::uint16_t kSysregCNTHP_CTL    = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|1u; // 0x6711
constexpr std::uint16_t kSysregCNTHP_CVAL   = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|2u; // 0x6712

} // namespace

VirtualTime::VirtualTime(std::uint64_t frequency, std::size_t num_cores)
    : physical_counter_(0),
      frequency_(frequency),
      core_timers_(num_cores > 0 ? num_cores : kDefaultCapacity) {}

void VirtualTime::resize_cores(std::size_t new_size) {
    std::lock_guard<std::mutex> lock(resize_mutex_);
    if (new_size > core_timers_.size()) {
        core_timers_.resize(new_size);
    }
}

void VirtualTime::evaluate_core_timers(std::size_t core_id) noexcept {
    ensure_core(core_id);
    const std::uint64_t phys_now = get_physical_counter();
    auto& c = core_timers_[core_id];
    c.phys_timer.evaluate(phys_now);
    const std::uint64_t virt_now = phys_now - c.cntvoff_el2;
    c.virt_timer.evaluate(virt_now);
    c.cnthp_timer.evaluate(phys_now);
}

void VirtualTime::evaluate_all_timers() noexcept {
    const std::uint64_t phys_now = get_physical_counter();
    for (std::size_t i = 0; i < core_timers_.size(); ++i) {
        auto& c = core_timers_[i];
        c.phys_timer.evaluate(phys_now);
        const std::uint64_t virt_now = phys_now - c.cntvoff_el2;
        c.virt_timer.evaluate(virt_now);
        c.cnthp_timer.evaluate(phys_now);
    }
}

std::uint32_t VirtualTime::read_phys_tval(std::size_t core_id) const noexcept {
    ensure_core(core_id);
    return static_cast<std::uint32_t>(core_timers_[core_id].phys_timer.read_tval(get_physical_counter()));
}

void VirtualTime::write_phys_tval(std::size_t core_id, std::uint32_t val) noexcept {
    ensure_core(core_id);
    core_timers_[core_id].phys_timer.write_tval(get_physical_counter(), val);
    evaluate_core_timers(core_id);
}

std::uint32_t VirtualTime::read_virt_tval(std::size_t core_id) const noexcept {
    ensure_core(core_id);
    return static_cast<std::uint32_t>(core_timers_[core_id].virt_timer.read_tval(get_virtual_counter(core_id)));
}

void VirtualTime::write_virt_tval(std::size_t core_id, std::uint32_t val) noexcept {
    ensure_core(core_id);
    core_timers_[core_id].virt_timer.write_tval(get_virtual_counter(core_id), val);
    evaluate_core_timers(core_id);
}

std::uint32_t VirtualTime::read_cnthp_tval(std::size_t core_id) const noexcept {
    ensure_core(core_id);
    return static_cast<std::uint32_t>(core_timers_[core_id].cnthp_timer.read_tval(get_physical_counter()));
}

void VirtualTime::write_cnthp_tval(std::size_t core_id, std::uint32_t val) noexcept {
    ensure_core(core_id);
    core_timers_[core_id].cnthp_timer.write_tval(get_physical_counter(), val);
    evaluate_core_timers(core_id);
}

TimerTrapDecision VirtualTime::check_trap(std::size_t core_id, unsigned current_el, std::uint16_t reg_key, bool /*is_read*/) const noexcept {
    ensure_core(core_id);
    const auto& c = core_timers_[core_id];

    if (current_el == 0) {
        // EL0 traps to EL1 via CNTKCTL_EL1
        if (reg_key == kSysregCNTFRQ_EL0) {
            if ((c.cntkctl_el1 & 0x3u) == 0u) {
                return TimerTrapDecision{true, 1};
            }
        } else if (reg_key == kSysregCNTPCT_EL0) {
            if ((c.cntkctl_el1 & (1u << 0)) == 0u) {
                return TimerTrapDecision{true, 1};
            }
        } else if (reg_key == kSysregCNTVCT_EL0) {
            if ((c.cntkctl_el1 & (1u << 1)) == 0u) {
                return TimerTrapDecision{true, 1};
            }
        } else if (reg_key == kSysregCNTP_CTL || reg_key == kSysregCNTP_CVAL || reg_key == kSysregCNTP_TVAL) {
            if ((c.cntkctl_el1 & (1u << 8)) == 0u) {
                return TimerTrapDecision{true, 1};
            }
        } else if (reg_key == kSysregCNTV_CTL || reg_key == kSysregCNTV_CVAL || reg_key == kSysregCNTV_TVAL) {
            if ((c.cntkctl_el1 & (1u << 9)) == 0u) {
                return TimerTrapDecision{true, 1};
            }
        }

        // If not trapped to EL1, check EL0 traps to EL2 via CNTHCTL_EL2
        if (reg_key == kSysregCNTPCT_EL0) {
            if ((c.cnthctl_el2 & (1u << 0)) == 0u) {
                return TimerTrapDecision{true, 2};
            }
        } else if (reg_key == kSysregCNTP_CTL || reg_key == kSysregCNTP_CVAL || reg_key == kSysregCNTP_TVAL) {
            if ((c.cnthctl_el2 & (1u << 1)) == 0u) {
                return TimerTrapDecision{true, 2};
            }
        }
    } else if (current_el == 1) {
        // EL1 traps to EL2 via CNTHCTL_EL2 (Applies to both MRS and MSR!)
        if (reg_key == kSysregCNTPCT_EL0) {
            if ((c.cnthctl_el2 & (1u << 0)) == 0u) {
                return TimerTrapDecision{true, 2};
            }
        } else if (reg_key == kSysregCNTP_CTL || reg_key == kSysregCNTP_CVAL || reg_key == kSysregCNTP_TVAL) {
            if ((c.cnthctl_el2 & (1u << 1)) == 0u) {
                return TimerTrapDecision{true, 2};
            }
        }
    }

    return TimerTrapDecision{false, 0};
}

std::uint64_t VirtualTime::get_earliest_future_deadline(const std::vector<bool>& active_mask) const noexcept {
    const std::uint64_t now = get_physical_counter();
    std::uint64_t earliest = 0;

    auto consider = [&](std::uint64_t cand) {
        if (cand > now) {
            if (earliest == 0 || cand < earliest) {
                earliest = cand;
            }
        }
    };

    const std::size_t limit = std::min(active_mask.size(), core_timers_.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (!active_mask[i]) continue;
        const auto& c = core_timers_[i];
        if (c.offline) continue;

        if (c.phys_timer.enable && !c.phys_timer.imask) {
            consider(c.phys_timer.cval);
        }
        if (c.virt_timer.enable && !c.virt_timer.imask) {
            // Virtual timer target in physical ticks: cval + cntvoff_el2
            consider(c.virt_timer.cval + c.cntvoff_el2);
        }
        if (c.cnthp_timer.enable && !c.cnthp_timer.imask) {
            consider(c.cnthp_timer.cval);
        }
    }

    return earliest;
}

bool VirtualTime::advance_to_earliest_deadline(const std::vector<bool>& active_mask) noexcept {
    const std::uint64_t deadline = get_earliest_future_deadline(active_mask);
    if (deadline == 0) {
        return false;
    }
    set_physical_counter(deadline);
    return true;
}

void VirtualTime::reset_core(std::size_t core_id) noexcept {
    ensure_core(core_id);
    auto& c = core_timers_[core_id];
    c.phys_timer = TimerState{};
    c.virt_timer = TimerState{};
    c.cnthp_timer = TimerState{};
    c.cntvoff_el2 = 0;
    c.cntkctl_el1 = 0;
    c.cnthctl_el2 = 3;
    c.offline = false;
}

void VirtualTime::reset_all() noexcept {
    physical_counter_.store(0, std::memory_order_relaxed);
    frequency_.store(kDefaultFrequency, std::memory_order_relaxed);
    for (std::size_t i = 0; i < core_timers_.size(); ++i) {
        reset_core(i);
    }
}

} // namespace winios
