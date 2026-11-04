#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace winios {

// Generic timer state for a single timer (CNTP, CNTV, CNTHP).
struct TimerState {
    bool enable{false};
    bool imask{false};
    bool istatus{false};
    std::uint64_t cval{0xFFFFFFFFFFFFFFFFULL};

    constexpr std::int32_t read_tval(std::uint64_t current_counter) const noexcept {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(cval) - static_cast<std::int64_t>(current_counter));
    }

    void write_tval(std::uint64_t current_counter, std::uint32_t tval_raw) noexcept {
        const std::int64_t offset = static_cast<std::int64_t>(static_cast<std::int32_t>(tval_raw));
        cval = static_cast<std::uint64_t>(static_cast<std::int64_t>(current_counter) + offset);
    }

    void evaluate(std::uint64_t current_counter) noexcept {
        istatus = enable && (current_counter >= cval);
    }

    constexpr bool irq_asserted() const noexcept {
        return enable && istatus && !imask;
    }
};

// Per-core Generic Timer registers and state.
struct CoreTimerState {
    TimerState phys_timer{};
    TimerState virt_timer{};
    TimerState cnthp_timer{};
    std::uint64_t cntvoff_el2{0};
    std::uint64_t cntkctl_el1{0};
    std::uint64_t cnthctl_el2{3}; // Bits [1:0] = 0b11 (EL1PCEN=1, EL1PCTEN=1 default)
    bool offline{false};          // Set true when PSCI CPU_OFF
};

// Trapping result descriptor.
struct TimerTrapDecision {
    bool trapped{false};
    unsigned target_el{0}; // 1 or 2
};

// Phase 50: Authoritative Virtual-Time Architecture for AArch64.
//
// Manages the shared monotonic system physical counter, frequency,
// per-core timer states, access/trapping rules, GICv3 PPI routing,
// and idle clock advancement for WFI/WFE.
class VirtualTime {
public:
    static constexpr std::uint64_t kDefaultFrequency = 24000000ULL; // 24 MHz architectural default
    static constexpr std::size_t kDefaultCapacity = 8;

    explicit VirtualTime(std::uint64_t frequency = kDefaultFrequency, std::size_t num_cores = kDefaultCapacity);
    ~VirtualTime() = default;

    VirtualTime(const VirtualTime&) = delete;
    VirtualTime& operator=(const VirtualTime&) = delete;

    // --- Physical Counter (Authoritative SoC Monotonic Counter) ---
    std::uint64_t get_physical_counter() const noexcept {
        return physical_counter_.load(std::memory_order_relaxed);
    }

    void set_physical_counter(std::uint64_t val) noexcept {
        physical_counter_.store(val, std::memory_order_relaxed);
        evaluate_all_timers();
    }

    void increment_physical_counter() noexcept {
        physical_counter_.fetch_add(1ULL, std::memory_order_relaxed);
    }

    void advance_physical_counter(std::uint64_t ticks) noexcept {
        physical_counter_.fetch_add(ticks, std::memory_order_relaxed);
        evaluate_all_timers();
    }

    // --- Clock Frequency (CNTFRQ_EL0) ---
    std::uint64_t get_frequency() const noexcept {
        return frequency_.load(std::memory_order_relaxed);
    }

    void set_frequency(std::uint64_t freq) noexcept {
        frequency_.store(freq, std::memory_order_relaxed);
    }

    // --- Virtual Counter (CNTVCT = CNTPCT - CNTVOFF_EL2) ---
    std::uint64_t get_virtual_counter(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return get_physical_counter() - core_timers_[core_id].cntvoff_el2;
    }

    // --- Core Timer State Accessors ---
    TimerState& get_phys_timer(std::size_t core_id) noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].phys_timer;
    }

    const TimerState& get_phys_timer(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].phys_timer;
    }

    void set_phys_timer(std::size_t core_id, const TimerState& state) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].phys_timer = state;
        evaluate_core_timers(core_id);
    }

    TimerState& get_virt_timer(std::size_t core_id) noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].virt_timer;
    }

    const TimerState& get_virt_timer(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].virt_timer;
    }

    void set_virt_timer(std::size_t core_id, const TimerState& state) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].virt_timer = state;
        evaluate_core_timers(core_id);
    }

    TimerState& get_cnthp_timer(std::size_t core_id) noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].cnthp_timer;
    }

    const TimerState& get_cnthp_timer(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].cnthp_timer;
    }

    void set_cnthp_timer(std::size_t core_id, const TimerState& state) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].cnthp_timer = state;
        evaluate_core_timers(core_id);
    }

    std::uint64_t get_cntvoff_el2(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].cntvoff_el2;
    }

    void set_cntvoff_el2(std::size_t core_id, std::uint64_t val) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].cntvoff_el2 = val;
        evaluate_core_timers(core_id);
    }

    std::uint64_t get_cntkctl_el1(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].cntkctl_el1;
    }

    void set_cntkctl_el1(std::size_t core_id, std::uint64_t val) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].cntkctl_el1 = val;
    }

    std::uint64_t get_cnthctl_el2(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].cnthctl_el2;
    }

    void set_cnthctl_el2(std::size_t core_id, std::uint64_t val) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].cnthctl_el2 = val;
    }

    bool is_core_offline(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        return core_timers_[core_id].offline;
    }

    void set_core_offline(std::size_t core_id, bool offline) noexcept {
        ensure_core(core_id);
        core_timers_[core_id].offline = offline;
    }

    // --- Timer Evaluation & Status ---
    void evaluate_core_timers(std::size_t core_id) noexcept;
    void evaluate_all_timers() noexcept;

    // --- GICv3 PPI Query Methods ---
    // PPI 30: Physical Timer
    bool get_phys_timer_irq(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        const auto& c = core_timers_[core_id];
        return !c.offline && c.phys_timer.irq_asserted();
    }

    // PPI 27: Virtual Timer
    bool get_virt_timer_irq(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        const auto& c = core_timers_[core_id];
        return !c.offline && c.virt_timer.irq_asserted();
    }

    // PPI 26: Hypervisor Physical Timer
    bool get_cnthp_timer_irq(std::size_t core_id) const noexcept {
        ensure_core(core_id);
        const auto& c = core_timers_[core_id];
        return !c.offline && c.cnthp_timer.irq_asserted();
    }

    // --- Architectural TVAL Helpers ---
    std::uint32_t read_phys_tval(std::size_t core_id) const noexcept;
    void write_phys_tval(std::size_t core_id, std::uint32_t val) noexcept;

    std::uint32_t read_virt_tval(std::size_t core_id) const noexcept;
    void write_virt_tval(std::size_t core_id, std::uint32_t val) noexcept;

    std::uint32_t read_cnthp_tval(std::size_t core_id) const noexcept;
    void write_cnthp_tval(std::size_t core_id, std::uint32_t val) noexcept;

    // --- Trapping & Access Control ---
    // Checks CNTKCTL_EL1 and CNTHCTL_EL2 traps for a given sysreg key.
    // Handles BOTH read and write accesses.
    TimerTrapDecision check_trap(std::size_t core_id, unsigned current_el, std::uint16_t reg_key, bool is_read) const noexcept;

    // --- Deterministic Idle Advancement (WFI / WFE) ---
    // Computes the earliest physical counter value > current at which an enabled timer expires.
    // Returns 0 if no enabled timer has a future deadline.
    std::uint64_t get_earliest_future_deadline(const std::vector<bool>& active_mask) const noexcept;
    bool advance_to_earliest_deadline(const std::vector<bool>& active_mask) noexcept;

    // --- Lifecycle Resets ---
    void reset_core(std::size_t core_id) noexcept;
    void reset_all() noexcept;

private:
    void ensure_core(std::size_t core_id) const noexcept {
        if (core_id >= core_timers_.size()) {
            const_cast<VirtualTime*>(this)->resize_cores(core_id + 1);
        }
    }
    void resize_cores(std::size_t new_size);

    std::atomic<std::uint64_t> physical_counter_{0};
    std::atomic<std::uint64_t> frequency_{kDefaultFrequency};
    mutable std::mutex resize_mutex_;
    mutable std::vector<CoreTimerState> core_timers_;
};

} // namespace winios
