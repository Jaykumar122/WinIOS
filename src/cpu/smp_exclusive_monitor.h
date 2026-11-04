#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace winios {

// -----------------------------------------------------------------------------
// ReservationEntry: State of a virtual core's exclusive reservation
// -----------------------------------------------------------------------------
struct ReservationEntry {
    std::size_t core_id{0};
    std::uint64_t granule_address{0}; // 64-byte aligned base address
    std::uint64_t exact_address{0};   // Architectural access address
    std::size_t size{0};              // Access width (1, 2, 4, 8, 16)
    bool valid{false};                // Reservation valid flag
};

// -----------------------------------------------------------------------------
// SmpExclusiveMonitorTelemetry: Non-architectural performance telemetry
// -----------------------------------------------------------------------------
struct SmpExclusiveMonitorTelemetry {
    std::uint64_t load_exclusive_count{0};
    std::uint64_t store_exclusive_success_count{0};
    std::uint64_t store_exclusive_fail_count{0};
    std::uint64_t invalidation_count{0};
    std::uint64_t clrex_count{0};
};

// -----------------------------------------------------------------------------
// SmpExclusiveMonitorManager: Global exclusive monitor across virtual cores
//
// Follows ARMv8-A architecture manual DDI 0487:
// - Granule size: 64 bytes (cache line reservation granule)
// - LDXR / LDAXR registers a reservation for the calling core
// - STXR / STLXR verifies reservation; if valid, commits store and clears reservation
// - Stores from other cores to the same reservation granule invalidate the reservation
// - CLREX explicitly clears reservation for calling core
// -----------------------------------------------------------------------------
class SmpExclusiveMonitorManager {
public:
    static constexpr std::uint64_t kGranuleSize = 64;
    static constexpr std::uint64_t kGranuleMask = ~(kGranuleSize - 1);

    explicit SmpExclusiveMonitorManager(std::size_t max_cores = 16);

    void load_exclusive(std::size_t core_id, std::uint64_t address, std::size_t size) noexcept;
    [[nodiscard]] bool is_reservation_valid(std::size_t core_id, std::uint64_t address, std::size_t size) const noexcept;

    // Executes write_action if valid, invalidates other reservations, clears caller's reservation, returns true (status 0).
    // If invalid, does not execute write_action, returns false (status 1).
    bool store_exclusive(std::size_t core_id, std::uint64_t address, std::size_t size,
                         const std::function<void()>& write_action);

    // Ordinary store from core_id invalidates overlapping reservations of other cores
    void invalidate_reservations(std::size_t writer_core_id, std::uint64_t address, std::size_t size) noexcept;

    // Explicit clearing via CLREX or exception entry/exit
    void clear_reservation(std::size_t core_id) noexcept;

    // Resets all core reservations
    void reset() noexcept;

    // Direct reservation inspection & restoration for cluster snapshots
    [[nodiscard]] std::size_t max_cores() const noexcept { return reservations_.size(); }
    [[nodiscard]] const std::vector<ReservationEntry>& all_reservations() const noexcept { return reservations_; }
    void set_core_reservation(const ReservationEntry& entry) noexcept;

    // Telemetry access
    [[nodiscard]] SmpExclusiveMonitorTelemetry get_telemetry() const noexcept;
    void reset_telemetry() noexcept;

    [[nodiscard]] const ReservationEntry& get_core_reservation(std::size_t core_id) const;

private:
    std::vector<ReservationEntry> reservations_;
    mutable SmpExclusiveMonitorTelemetry telemetry_{};
};

} // namespace winios
