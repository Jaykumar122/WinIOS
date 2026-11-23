#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace winios::cpu {

struct CoreTickAccounting {
    std::uint64_t total_ticks{0};
    std::uint64_t idle_ticks{0};
    std::uint64_t active_ticks{0};
    std::uint64_t timer_deliveries{0};
    std::uint64_t acknowledged_irqs{0};
    std::uint64_t missed_or_coalesced_ticks{0};
};

class EarlySchedulerAccounting {
public:
    static constexpr std::size_t kMaxCores = 6; // iPhone 15 A16 Bionic 6 cores

    EarlySchedulerAccounting() = default;

    void reset() noexcept;

    bool record_timer_tick(std::size_t core_id, bool is_idle, bool is_masked) noexcept;
    void record_ack(std::size_t core_id) noexcept;
    void record_coalesced_tick(std::size_t core_id, std::uint64_t missed_count = 1) noexcept;

    [[nodiscard]] std::uint64_t global_tick_count() const noexcept { return global_ticks_; }
    [[nodiscard]] const CoreTickAccounting& core_accounting(std::size_t core_id) const noexcept;
    [[nodiscard]] std::uint64_t total_deliveries() const noexcept;
    [[nodiscard]] std::uint64_t total_acks() const noexcept;
    [[nodiscard]] std::uint64_t total_coalesced() const noexcept;

    [[nodiscard]] bool check_invariants() const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size) noexcept;

private:
    std::uint64_t global_ticks_{0};
    std::array<CoreTickAccounting, kMaxCores> cores_{};
};

} // namespace winios::cpu
