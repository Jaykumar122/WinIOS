#include "cpu/early_scheduler_accounting.h"

#include <cstring>

namespace winios::cpu {

void EarlySchedulerAccounting::reset() noexcept {
    global_ticks_ = 0;
    for (auto& c : cores_) {
        c.total_ticks = 0;
        c.idle_ticks = 0;
        c.active_ticks = 0;
        c.timer_deliveries = 0;
        c.acknowledged_irqs = 0;
        c.missed_or_coalesced_ticks = 0;
    }
}

bool EarlySchedulerAccounting::record_timer_tick(std::size_t core_id, bool is_idle, bool is_masked) noexcept {
    if (core_id >= kMaxCores) return false;

    // Rule: Masked interrupts must NOT increment delivered tick counts or per-core ticks
    if (is_masked) {
        return false;
    }

    ++global_ticks_;
    auto& c = cores_[core_id];
    ++c.total_ticks;
    ++c.timer_deliveries;

    if (is_idle) {
        ++c.idle_ticks;
    } else {
        ++c.active_ticks;
    }
    return true;
}

void EarlySchedulerAccounting::record_ack(std::size_t core_id) noexcept {
    if (core_id >= kMaxCores) return;
    ++cores_[core_id].acknowledged_irqs;
}

void EarlySchedulerAccounting::record_coalesced_tick(std::size_t core_id, std::uint64_t missed_count) noexcept {
    if (core_id >= kMaxCores) return;
    cores_[core_id].missed_or_coalesced_ticks += missed_count;
}

const CoreTickAccounting& EarlySchedulerAccounting::core_accounting(std::size_t core_id) const noexcept {
    static const CoreTickAccounting s_empty{};
    if (core_id >= kMaxCores) return s_empty;
    return cores_[core_id];
}

std::uint64_t EarlySchedulerAccounting::total_deliveries() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& c : cores_) {
        sum += c.timer_deliveries;
    }
    return sum;
}

std::uint64_t EarlySchedulerAccounting::total_acks() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& c : cores_) {
        sum += c.acknowledged_irqs;
    }
    return sum;
}

std::uint64_t EarlySchedulerAccounting::total_coalesced() const noexcept {
    std::uint64_t sum = 0;
    for (const auto& c : cores_) {
        sum += c.missed_or_coalesced_ticks;
    }
    return sum;
}

bool EarlySchedulerAccounting::check_invariants() const noexcept {
    std::uint64_t sum_ticks = 0;
    for (const auto& c : cores_) {
        // Invariant: total_ticks == idle_ticks + active_ticks
        if (c.total_ticks != (c.idle_ticks + c.active_ticks)) return false;
        // Invariant: total_ticks == timer_deliveries
        if (c.total_ticks != c.timer_deliveries) return false;
        // Invariant: acks cannot exceed deliveries
        if (c.acknowledged_irqs > c.timer_deliveries) return false;
        sum_ticks += c.total_ticks;
    }
    // Invariant: global_ticks == sum of all core total_ticks
    return (global_ticks_ == sum_ticks);
}

std::vector<std::uint8_t> EarlySchedulerAccounting::serialize() const {
    std::vector<std::uint8_t> buf(sizeof(std::uint64_t) + sizeof(cores_));
    std::memcpy(buf.data(), &global_ticks_, sizeof(std::uint64_t));
    std::memcpy(buf.data() + sizeof(std::uint64_t), cores_.data(), sizeof(cores_));
    return buf;
}

bool EarlySchedulerAccounting::deserialize(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < sizeof(std::uint64_t) + sizeof(cores_)) return false;
    std::memcpy(&global_ticks_, data, sizeof(std::uint64_t));
    std::memcpy(cores_.data(), data + sizeof(std::uint64_t), sizeof(cores_));
    return true;
}

} // namespace winios::cpu
