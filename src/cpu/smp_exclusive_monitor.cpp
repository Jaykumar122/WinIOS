#include "cpu/smp_exclusive_monitor.h"

#include <stdexcept>

namespace winios {

SmpExclusiveMonitorManager::SmpExclusiveMonitorManager(std::size_t max_cores) {
    reservations_.resize(max_cores);
    for (std::size_t i = 0; i < max_cores; ++i) {
        reservations_[i].core_id = i;
        reservations_[i].granule_address = 0;
        reservations_[i].exact_address = 0;
        reservations_[i].size = 0;
        reservations_[i].valid = false;
    }
}

void SmpExclusiveMonitorManager::load_exclusive(std::size_t core_id, std::uint64_t address, std::size_t size) noexcept {
    if (core_id >= reservations_.size()) {
        reservations_.resize(core_id + 1);
    }
    auto& entry = reservations_[core_id];
    entry.core_id = core_id;
    entry.granule_address = address & kGranuleMask;
    entry.exact_address = address;
    entry.size = size;
    entry.valid = true;
    telemetry_.load_exclusive_count++;
}

bool SmpExclusiveMonitorManager::is_reservation_valid(std::size_t core_id, std::uint64_t address, [[maybe_unused]] std::size_t size) const noexcept {
    if (core_id >= reservations_.size()) {
        return false;
    }
    const auto& entry = reservations_[core_id];
    if (!entry.valid) {
        return false;
    }
    const std::uint64_t target_granule = address & kGranuleMask;
    return entry.granule_address == target_granule;
}

bool SmpExclusiveMonitorManager::store_exclusive(std::size_t core_id, std::uint64_t address, std::size_t size,
                                                const std::function<void()>& write_action) {
    if (!is_reservation_valid(core_id, address, size)) {
        telemetry_.store_exclusive_fail_count++;
        return false;
    }

    if (write_action) {
        write_action();
    }

    // Reservation is consumed
    reservations_[core_id].valid = false;

    // Invalidate other cores' reservations in the same granule
    invalidate_reservations(core_id, address, size);

    telemetry_.store_exclusive_success_count++;
    return true;
}

void SmpExclusiveMonitorManager::invalidate_reservations(std::size_t writer_core_id, std::uint64_t address, std::size_t size) noexcept {
    if (size == 0) return;
    const std::uint64_t start_granule = address & kGranuleMask;
    const std::uint64_t end_granule = (address + size - 1) & kGranuleMask;

    for (std::size_t i = 0; i < reservations_.size(); ++i) {
        if (i == writer_core_id) continue;
        auto& entry = reservations_[i];
        if (entry.valid) {
            if (entry.granule_address >= start_granule && entry.granule_address <= end_granule) {
                entry.valid = false;
                telemetry_.invalidation_count++;
            }
        }
    }
}

void SmpExclusiveMonitorManager::clear_reservation(std::size_t core_id) noexcept {
    if (core_id < reservations_.size()) {
        if (reservations_[core_id].valid) {
            reservations_[core_id].valid = false;
            telemetry_.clrex_count++;
        }
    }
}

void SmpExclusiveMonitorManager::reset() noexcept {
    for (auto& entry : reservations_) {
        entry.granule_address = 0;
        entry.exact_address = 0;
        entry.size = 0;
        entry.valid = false;
    }
}

void SmpExclusiveMonitorManager::set_core_reservation(const ReservationEntry& entry) noexcept {
    if (entry.core_id >= reservations_.size()) {
        reservations_.resize(entry.core_id + 1);
    }
    reservations_[entry.core_id] = entry;
}

SmpExclusiveMonitorTelemetry SmpExclusiveMonitorManager::get_telemetry() const noexcept {
    return telemetry_;
}

void SmpExclusiveMonitorManager::reset_telemetry() noexcept {
    telemetry_ = SmpExclusiveMonitorTelemetry{};
}

const ReservationEntry& SmpExclusiveMonitorManager::get_core_reservation(std::size_t core_id) const {
    if (core_id >= reservations_.size()) {
        throw std::out_of_range("core_id out of range in SmpExclusiveMonitorManager");
    }
    return reservations_[core_id];
}

} // namespace winios
