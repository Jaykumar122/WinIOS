#include "pmu.h"

namespace winios {

namespace {

constexpr std::uint32_t kCounterMask = (1u << 31u) | ((1u << kNumEventCounters) - 1u);

} // namespace

PMU::PMU() noexcept {
    reset();
}

void PMU::reset() noexcept {
    pmcr_ = static_cast<std::uint32_t>(kNumEventCounters) << kPMCRNumCountersShift;
    pmcntenset_ = 0u;
    pmovsset_ = 0u;
    pmintenset_ = 0u;
    pmselr_ = 0u;
    pmuserenr_ = 0u;
    pmccntr_ = 0u;
    pmccfiltr_ = 0u;
    cycle_prescaler_ = 0u;

    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        pmevcntr_[i] = 0u;
        pmevtyper_[i] = 0u;
    }
}

std::uint32_t PMU::read_pmcr() const noexcept {
    // Bits [2:1] (C and P) always read as 0.
    // Bits [15:11] return number of event counters implemented (kNumEventCounters).
    return (pmcr_ & ~0x6u) | (static_cast<std::uint32_t>(kNumEventCounters) << kPMCRNumCountersShift);
}

void PMU::write_pmcr(std::uint32_t val) noexcept {
    if ((val & kPMCREventReset) != 0u) {
        for (std::size_t i = 0; i < kNumEventCounters; ++i) {
            pmevcntr_[i] = 0u;
        }
    }
    if ((val & kPMCRCycleReset) != 0u) {
        pmccntr_ = 0u;
        cycle_prescaler_ = 0u;
    }
    // Store control bits (E, D, X, DP, LC, LP) preserving N
    pmcr_ = (val & 0xFD7Du) | (static_cast<std::uint32_t>(kNumEventCounters) << kPMCRNumCountersShift);
}

std::uint32_t PMU::read_pmcntenset() const noexcept {
    return pmcntenset_;
}

void PMU::write_pmcntenset(std::uint32_t val) noexcept {
    pmcntenset_ |= (val & kCounterMask);
}

std::uint32_t PMU::read_pmcntenclr() const noexcept {
    return pmcntenset_;
}

void PMU::write_pmcntenclr(std::uint32_t val) noexcept {
    pmcntenset_ &= ~(val & kCounterMask);
}

std::uint32_t PMU::read_pmovsset() const noexcept {
    return pmovsset_;
}

void PMU::write_pmovsset(std::uint32_t val) noexcept {
    pmovsset_ |= (val & kCounterMask);
}

std::uint32_t PMU::read_pmovsclr() const noexcept {
    return pmovsset_;
}

void PMU::write_pmovsclr(std::uint32_t val) noexcept {
    pmovsset_ &= ~(val & kCounterMask);
}

void PMU::write_pmswinc(std::uint32_t val, unsigned current_el) noexcept {
    if ((pmcr_ & kPMCREnable) == 0u) {
        return;
    }
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        if ((val & (1u << i)) != 0u) {
            if ((pmcntenset_ & (1u << i)) != 0u) {
                if ((pmevtyper_[i] & 0xFFFFu) == ARMV8_EVENT_SW_INCR) {
                    if (is_counting_permitted(pmevtyper_[i], current_el)) {
                        const std::uint32_t old = pmevcntr_[i];
                        pmevcntr_[i] = old + 1u;
                        if (old == 0xFFFFFFFFu) {
                            pmovsset_ |= (1u << i);
                        }
                    }
                }
            }
        }
    }
}

std::uint32_t PMU::read_pmselr() const noexcept {
    return pmselr_ & 0x1Fu;
}

void PMU::write_pmselr(std::uint32_t val) noexcept {
    pmselr_ = val & 0x1Fu;
}

std::uint32_t PMU::read_pmceid0() const noexcept {
    return kPMCEID0Value;
}

std::uint32_t PMU::read_pmceid1() const noexcept {
    return kPMCEID1Value;
}

std::uint64_t PMU::read_pmccntr() const noexcept {
    return pmccntr_;
}

void PMU::write_pmccntr(std::uint64_t val) noexcept {
    pmccntr_ = val;
}

std::uint32_t PMU::read_pmxevtyper() const noexcept {
    if (pmselr_ < kNumEventCounters) {
        return pmevtyper_[pmselr_];
    }
    return 0u;
}

void PMU::write_pmxevtyper(std::uint32_t val) noexcept {
    if (pmselr_ < kNumEventCounters) {
        pmevtyper_[pmselr_] = val;
    }
}

std::uint32_t PMU::read_pmxevcntr() const noexcept {
    if (pmselr_ < kNumEventCounters) {
        return pmevcntr_[pmselr_];
    }
    return 0u;
}

void PMU::write_pmxevcntr(std::uint32_t val) noexcept {
    if (pmselr_ < kNumEventCounters) {
        pmevcntr_[pmselr_] = val;
    }
}

std::uint32_t PMU::read_pmuserenr() const noexcept {
    return pmuserenr_ & 0xFu;
}

void PMU::write_pmuserenr(std::uint32_t val) noexcept {
    pmuserenr_ = val & 0xFu;
}

std::uint32_t PMU::read_pmintenset() const noexcept {
    return pmintenset_;
}

void PMU::write_pmintenset(std::uint32_t val) noexcept {
    pmintenset_ |= (val & kCounterMask);
}

std::uint32_t PMU::read_pmintenclr() const noexcept {
    return pmintenset_;
}

void PMU::write_pmintenclr(std::uint32_t val) noexcept {
    pmintenset_ &= ~(val & kCounterMask);
}

std::uint32_t PMU::read_pmccfiltr() const noexcept {
    return pmccfiltr_;
}

void PMU::write_pmccfiltr(std::uint32_t val) noexcept {
    pmccfiltr_ = val;
}

std::uint32_t PMU::read_pmevcntr(std::size_t index) const noexcept {
    if (index < kNumEventCounters) {
        return pmevcntr_[index];
    }
    return 0u;
}

void PMU::write_pmevcntr(std::size_t index, std::uint32_t val) noexcept {
    if (index < kNumEventCounters) {
        pmevcntr_[index] = val;
    }
}

std::uint32_t PMU::read_pmevtyper(std::size_t index) const noexcept {
    if (index < kNumEventCounters) {
        return pmevtyper_[index];
    }
    return 0u;
}

void PMU::write_pmevtyper(std::size_t index, std::uint32_t val) noexcept {
    if (index < kNumEventCounters) {
        pmevtyper_[index] = val;
    }
}

bool PMU::is_counting_permitted(std::uint32_t filter, unsigned current_el) noexcept {
    switch (current_el) {
    case 0: return (filter & kFilterU) == 0u;
    case 1: return (filter & kFilterP) == 0u;
    case 2: return (filter & kFilterNSH) == 0u;
    case 3: return (filter & kFilterM) == 0u;
    default: return true;
    }
}

void PMU::on_cycle(unsigned current_el, std::uint64_t cycles) noexcept {
    if ((pmcr_ & kPMCREnable) == 0u || cycles == 0u) {
        return;
    }

    if (((pmcntenset_ & (1u << 31u)) != 0u) && is_counting_permitted(pmccfiltr_, current_el)) {
        if ((pmcr_ & kPMCRClockDivider) != 0u) {
            cycle_prescaler_ += cycles;
            const std::uint64_t ticks = cycle_prescaler_ / 64u;
            cycle_prescaler_ %= 64u;
            if (ticks > 0u) {
                const std::uint64_t old = pmccntr_;
                pmccntr_ += ticks;
                if ((pmcr_ & kPMCRLongCycles) != 0u) {
                    if (pmccntr_ < old) {
                        pmovsset_ |= (1u << 31u);
                    }
                } else {
                    if ((old & 0xFFFFFFFFULL) + ticks > 0xFFFFFFFFULL) {
                        pmovsset_ |= (1u << 31u);
                    }
                }
            }
        } else {
            const std::uint64_t old = pmccntr_;
            pmccntr_ += cycles;
            if ((pmcr_ & kPMCRLongCycles) != 0u) {
                if (pmccntr_ < old) {
                    pmovsset_ |= (1u << 31u);
                }
            } else {
                if ((old & 0xFFFFFFFFULL) + cycles > 0xFFFFFFFFULL) {
                    pmovsset_ |= (1u << 31u);
                }
            }
        }
    }

    on_event(ARMV8_EVENT_CPU_CYCLES, current_el, static_cast<std::uint32_t>(cycles));
}

void PMU::on_event(std::uint32_t event_id, unsigned current_el, std::uint32_t count) noexcept {
    if ((pmcr_ & kPMCREnable) == 0u || count == 0u) {
        return;
    }
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        if ((pmcntenset_ & (1u << i)) != 0u) {
            if ((pmevtyper_[i] & 0xFFFFu) == event_id) {
                if (is_counting_permitted(pmevtyper_[i], current_el)) {
                    const std::uint32_t old = pmevcntr_[i];
                    pmevcntr_[i] = old + count;
                    if (pmevcntr_[i] < old) {
                        pmovsset_ |= (1u << i);
                    }
                }
            }
        }
    }
}

bool PMU::has_pending_interrupt() const noexcept {
    return (pmovsset_ & pmintenset_) != 0u;
}

} // namespace winios
