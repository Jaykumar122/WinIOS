#pragma once

#include <cstddef>
#include <cstdint>

namespace winios {

// Supported ARMv8 architected event numbers (subset for virtual PMUv3)
constexpr std::uint32_t ARMV8_EVENT_SW_INCR          = 0x0000u;
constexpr std::uint32_t ARMV8_EVENT_INST_RETIRED     = 0x0008u;
constexpr std::uint32_t ARMV8_EVENT_EXC_TAKEN        = 0x0009u;
constexpr std::uint32_t ARMV8_EVENT_EXC_RETURN       = 0x000Au;
constexpr std::uint32_t ARMV8_EVENT_BR_IMMED_RETIRED = 0x0010u;
constexpr std::uint32_t ARMV8_EVENT_CPU_CYCLES       = 0x0011u;
constexpr std::uint32_t ARMV8_EVENT_MEM_ACCESS       = 0x0013u;
constexpr std::uint32_t ARMV8_EVENT_L1D_CACHE_ACCESS = 0x0016u;
constexpr std::uint32_t ARMV8_EVENT_L1D_CACHE_WRITE  = 0x0017u;

// Number of architected event counters implemented (Cortex-A standard: 6)
constexpr std::size_t kNumEventCounters = 6;

// Event filtering bits in PMCCFILTR_EL0 and PMEVTYPER<n>_EL0
constexpr std::uint32_t kFilterP   = 1u << 31u; // Inhibit EL1 (privileged)
constexpr std::uint32_t kFilterU   = 1u << 30u; // Inhibit EL0 (user)
constexpr std::uint32_t kFilterNSK = 1u << 29u; // Inhibit Non-secure EL1
constexpr std::uint32_t kFilterNSU = 1u << 28u; // Inhibit Non-secure EL0
constexpr std::uint32_t kFilterNSH = 1u << 27u; // Inhibit Non-secure EL2 (hypervisor)
constexpr std::uint32_t kFilterM   = 1u << 26u; // Inhibit EL3 (Secure Monitor)

// PMCR_EL0 bit definitions
constexpr std::uint32_t kPMCREnable           = 1u << 0u;  // E: Enable all counters
constexpr std::uint32_t kPMCREventReset       = 1u << 1u;  // P: Event counter reset
constexpr std::uint32_t kPMCRCycleReset       = 1u << 2u;  // C: Cycle counter reset
constexpr std::uint32_t kPMCRClockDivider     = 1u << 3u;  // D: Clock divider (count every 64 cycles)
constexpr std::uint32_t kPMCRExportEnable     = 1u << 4u;  // X: Export enable
constexpr std::uint32_t kPMCRDisableCycles    = 1u << 5u;  // DP: Disable cycle counter when prohibited
constexpr std::uint32_t kPMCRLongCycles       = 1u << 6u;  // LC: Long cycle counter overflow (64-bit)
constexpr std::uint32_t kPMCRLongEvents       = 1u << 7u;  // LP: Long event counter overflow
constexpr std::uint32_t kPMCRNumCountersShift = 11u;
constexpr std::uint32_t kPMCRNumCountersMask  = 0x1Fu << 11u;

// PMUSERENR_EL0 bit definitions
constexpr std::uint32_t kPMUserEnable         = 1u << 0u;  // EN: EL0 access enable
constexpr std::uint32_t kPMUserSoftwareIncr   = 1u << 1u;  // SW: Software increment enable
constexpr std::uint32_t kPMUserCycleRead      = 1u << 2u;  // CR: Cycle counter read enable
constexpr std::uint32_t kPMUserEventRead      = 1u << 3u;  // ER: Event counter read enable

// Common Event Identification bitmaps (PMCEID0_EL0 / PMCEID1_EL0)
constexpr std::uint32_t kPMCEID0Value =
    (1u << 0u)  | // 0x0000 SW_INCR
    (1u << 8u)  | // 0x0008 INST_RETIRED
    (1u << 9u)  | // 0x0009 EXC_TAKEN
    (1u << 10u) | // 0x000A EXC_RETURN
    (1u << 16u) | // 0x0010 BR_IMMED_RETIRED
    (1u << 17u) | // 0x0011 CPU_CYCLES
    (1u << 19u) | // 0x0013 MEM_ACCESS
    (1u << 22u) | // 0x0016 L1D_CACHE_ACCESS
    (1u << 23u);  // 0x0017 L1D_CACHE_WRITE
constexpr std::uint32_t kPMCEID1Value = 0x00000000u;

class PMU {
public:
    PMU() noexcept;

    // Reset all PMU registers and internal state to architectural power-on reset defaults
    void reset() noexcept;

    // System register accessors
    [[nodiscard]] std::uint32_t read_pmcr() const noexcept;
    void write_pmcr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmcntenset() const noexcept;
    void write_pmcntenset(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmcntenclr() const noexcept;
    void write_pmcntenclr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmovsset() const noexcept;
    void write_pmovsset(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmovsclr() const noexcept;
    void write_pmovsclr(std::uint32_t val) noexcept;

    void write_pmswinc(std::uint32_t val, unsigned current_el) noexcept;

    [[nodiscard]] std::uint32_t read_pmselr() const noexcept;
    void write_pmselr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmceid0() const noexcept;
    [[nodiscard]] std::uint32_t read_pmceid1() const noexcept;

    [[nodiscard]] std::uint64_t read_pmccntr() const noexcept;
    void write_pmccntr(std::uint64_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmxevtyper() const noexcept;
    void write_pmxevtyper(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmxevcntr() const noexcept;
    void write_pmxevcntr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmuserenr() const noexcept;
    void write_pmuserenr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmintenset() const noexcept;
    void write_pmintenset(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmintenclr() const noexcept;
    void write_pmintenclr(std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmccfiltr() const noexcept;
    void write_pmccfiltr(std::uint32_t val) noexcept;

    // Direct event counter and typer accessors
    [[nodiscard]] std::uint32_t read_pmevcntr(std::size_t index) const noexcept;
    void write_pmevcntr(std::size_t index, std::uint32_t val) noexcept;

    [[nodiscard]] std::uint32_t read_pmevtyper(std::size_t index) const noexcept;
    void write_pmevtyper(std::size_t index, std::uint32_t val) noexcept;

    // Architectural event triggers
    void on_cycle(unsigned current_el, std::uint64_t cycles = 1) noexcept;
    void on_event(std::uint32_t event_id, unsigned current_el, std::uint32_t count = 1) noexcept;

    // Interrupt status
    [[nodiscard]] bool has_pending_interrupt() const noexcept;

    [[nodiscard]] std::uint64_t cycle_prescaler() const noexcept { return cycle_prescaler_; }
    void set_cycle_prescaler(std::uint64_t value) noexcept { cycle_prescaler_ = value % 64u; }

    // Filter verification helper
    [[nodiscard]] static bool is_counting_permitted(std::uint32_t filter, unsigned current_el) noexcept;

private:
    std::uint32_t pmcr_{static_cast<std::uint32_t>(kNumEventCounters) << kPMCRNumCountersShift};
    std::uint32_t pmcntenset_{0};
    std::uint32_t pmovsset_{0};
    std::uint32_t pmintenset_{0};
    std::uint32_t pmselr_{0};
    std::uint32_t pmuserenr_{0};
    std::uint64_t pmccntr_{0};
    std::uint32_t pmccfiltr_{0};

    std::uint32_t pmevcntr_[kNumEventCounters]{};
    std::uint32_t pmevtyper_[kNumEventCounters]{};

    std::uint64_t cycle_prescaler_{0};
};

} // namespace winios
