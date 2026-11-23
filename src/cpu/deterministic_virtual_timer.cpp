#include "cpu/deterministic_virtual_timer.h"

#include <cstring>

namespace winios::cpu {

DeterministicVirtualTimer::DeterministicVirtualTimer(std::uint64_t freq_hz)
    : frequency_hz_(freq_hz) {
    reset();
}

void DeterministicVirtualTimer::reset() noexcept {
    for (auto& cs : cores_) {
        cs.counter = 0;
        cs.comparator = UINT64_MAX;
        cs.control = 0;
        cs.interrupt_asserted = false;
        cs.total_timer_firings = 0;
        cs.total_acks = 0;
        cs.total_rearms = 0;
    }
}

void DeterministicVirtualTimer::set_control(std::size_t core_id, std::uint32_t ctl) noexcept {
    if (core_id >= kMaxCores) return;
    // Retain read-only ISTATUS bit from current state unless re-evaluated
    uint32_t istatus = cores_[core_id].control & kCtlIStatus;
    cores_[core_id].control = (ctl & (kCtlEnable | kCtlIMask)) | istatus;
    evaluate_core_timer(core_id);
}

std::uint32_t DeterministicVirtualTimer::get_control(std::size_t core_id) const noexcept {
    if (core_id >= kMaxCores) return 0;
    return cores_[core_id].control;
}

void DeterministicVirtualTimer::set_comparator(std::size_t core_id, std::uint64_t cval) noexcept {
    if (core_id >= kMaxCores) return;
    cores_[core_id].comparator = cval;
    evaluate_core_timer(core_id);
}

std::uint64_t DeterministicVirtualTimer::get_comparator(std::size_t core_id) const noexcept {
    if (core_id >= kMaxCores) return UINT64_MAX;
    return cores_[core_id].comparator;
}

void DeterministicVirtualTimer::set_tval(std::size_t core_id, std::int32_t tval) noexcept {
    if (core_id >= kMaxCores) return;
    // TVAL sets CVAL = Counter + TVAL (sign-extended)
    int64_t delta = static_cast<int64_t>(tval);
    uint64_t cur = cores_[core_id].counter;
    uint64_t target = (delta >= 0) ? (cur + static_cast<uint64_t>(delta))
                                   : (cur - static_cast<uint64_t>(-delta));
    set_comparator(core_id, target);
}

std::int32_t DeterministicVirtualTimer::get_tval(std::size_t core_id) const noexcept {
    if (core_id >= kMaxCores) return 0;
    int64_t diff = static_cast<int64_t>(cores_[core_id].comparator - cores_[core_id].counter);
    if (diff > INT32_MAX) return INT32_MAX;
    if (diff < INT32_MIN) return INT32_MIN;
    return static_cast<std::int32_t>(diff);
}

void DeterministicVirtualTimer::advance_counter(std::uint64_t ticks) noexcept {
    for (std::size_t c = 0; c < kMaxCores; ++c) {
        cores_[c].counter += ticks;
        evaluate_core_timer(c);
    }
}

void DeterministicVirtualTimer::advance_us(std::uint64_t microseconds) noexcept {
    if (frequency_hz_ == 0) return;
    // Ticks = (us * frequency_hz) / 1,000,000
    uint64_t ticks = (microseconds * frequency_hz_) / 1000000ULL;
    advance_counter(ticks);
}

std::uint64_t DeterministicVirtualTimer::get_counter(std::size_t core_id) const noexcept {
    if (core_id >= kMaxCores) return 0;
    return cores_[core_id].counter;
}

bool DeterministicVirtualTimer::is_interrupt_asserted(std::size_t core_id) const noexcept {
    if (core_id >= kMaxCores) return false;
    return cores_[core_id].interrupt_asserted;
}

void DeterministicVirtualTimer::acknowledge(std::size_t core_id) noexcept {
    if (core_id >= kMaxCores) return;
    ++cores_[core_id].total_acks;
    // Acknowledge de-asserts line until rearmed or comparator moved
    cores_[core_id].interrupt_asserted = false;
}

void DeterministicVirtualTimer::rearm_periodic(std::size_t core_id, std::uint64_t interval_ticks) noexcept {
    if (core_id >= kMaxCores || interval_ticks == 0) return;
    ++cores_[core_id].total_rearms;
    cores_[core_id].comparator = cores_[core_id].counter + interval_ticks;
    cores_[core_id].interrupt_asserted = false;
    evaluate_core_timer(core_id);
}

const PerCoreTimerState& DeterministicVirtualTimer::core_state(std::size_t core_id) const noexcept {
    static const PerCoreTimerState s_empty{};
    if (core_id >= kMaxCores) return s_empty;
    return cores_[core_id];
}

void DeterministicVirtualTimer::evaluate_core_timer(std::size_t core_id) noexcept {
    auto& cs = cores_[core_id];
    bool enabled = (cs.control & kCtlEnable) != 0;
    bool masked  = (cs.control & kCtlIMask) != 0;

    if (enabled && cs.counter >= cs.comparator) {
        cs.control |= kCtlIStatus;
        if (!masked) {
            if (!cs.interrupt_asserted) {
                cs.interrupt_asserted = true;
                ++cs.total_timer_firings;
            }
        } else {
            cs.interrupt_asserted = false;
        }
    } else {
        cs.control &= ~kCtlIStatus;
        cs.interrupt_asserted = false;
    }
}

std::vector<std::uint8_t> DeterministicVirtualTimer::serialize() const {
    std::vector<std::uint8_t> buf(sizeof(std::uint64_t) + sizeof(cores_));
    std::memcpy(buf.data(), &frequency_hz_, sizeof(std::uint64_t));
    std::memcpy(buf.data() + sizeof(std::uint64_t), cores_.data(), sizeof(cores_));
    return buf;
}

bool DeterministicVirtualTimer::deserialize(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < sizeof(std::uint64_t) + sizeof(cores_)) return false;
    std::memcpy(&frequency_hz_, data, sizeof(std::uint64_t));
    std::memcpy(cores_.data(), data + sizeof(std::uint64_t), sizeof(cores_));
    return true;
}

} // namespace winios::cpu
