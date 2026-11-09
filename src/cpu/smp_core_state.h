#pragma once

#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace winios {

using ::CPU;

// -----------------------------------------------------------------------------
// SmpDebugState: Stage 39 per-core architectural debug snapshot
//
// Captures the bounded debug subset modeled by DebugArchitecture.  It is part
// of SmpCoreState so canonical hashing, equality, and SMP snapshot/replay all
// observe debug configuration changes.
// -----------------------------------------------------------------------------
struct SmpDebugState {
    std::uint64_t mdscr_el1{0};
    std::uint64_t mdccint_el1{0};
    std::uint64_t breakpoint_value[kDebugBreakpointCount]{};
    std::uint32_t breakpoint_control[kDebugBreakpointCount]{};
    std::uint64_t watchpoint_value[kDebugWatchpointCount]{};
    std::uint32_t watchpoint_control[kDebugWatchpointCount]{};

    bool operator==(const SmpDebugState& other) const noexcept {
        if (mdscr_el1 != other.mdscr_el1 || mdccint_el1 != other.mdccint_el1) {
            return false;
        }
        for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
            if (breakpoint_value[i] != other.breakpoint_value[i] ||
                breakpoint_control[i] != other.breakpoint_control[i]) {
                return false;
            }
        }
        for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
            if (watchpoint_value[i] != other.watchpoint_value[i] ||
                watchpoint_control[i] != other.watchpoint_control[i]) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const SmpDebugState& other) const noexcept { return !(*this == other); }
};

struct SmpPmuState {
    std::uint32_t pmcr{0};
    std::uint32_t counter_enable{0};
    std::uint32_t overflow_status{0};
    std::uint32_t interrupt_enable{0};
    std::uint32_t selector{0};
    std::uint32_t user_enable{0};
    std::uint64_t cycle_counter{0};
    std::uint32_t cycle_filter{0};
    std::uint64_t cycle_prescaler{0};
    std::uint32_t event_counter[kNumEventCounters]{};
    std::uint32_t event_type[kNumEventCounters]{};

    bool operator==(const SmpPmuState& other) const noexcept {
        if (pmcr != other.pmcr || counter_enable != other.counter_enable ||
            overflow_status != other.overflow_status ||
            interrupt_enable != other.interrupt_enable || selector != other.selector ||
            user_enable != other.user_enable || cycle_counter != other.cycle_counter ||
            cycle_filter != other.cycle_filter || cycle_prescaler != other.cycle_prescaler) {
            return false;
        }
        for (std::size_t i = 0; i < kNumEventCounters; ++i) {
            if (event_counter[i] != other.event_counter[i] ||
                event_type[i] != other.event_type[i]) {
                return false;
            }
        }
        return true;
    }
};

// -----------------------------------------------------------------------------
// CoreLifecycleState: Execution run-state of a virtual CPU core
// -----------------------------------------------------------------------------
enum class CoreLifecycleState : std::uint32_t {
    Halted              = 0, // Inactive, not scheduled (reset state for secondary cores)
    Runnable            = 1, // Ready to execute instructions in next scheduler quantum
    WaitingForInterrupt = 2, // Quiescent in WFI; awakens only on pending unmasked interrupt
    WaitingForEvent     = 3, // Quiescent in WFE; awakens on SEV, interrupt, or event register
    Stopped             = 4  // Terminated or powered off via PSCI CPU_OFF
};

[[nodiscard]] constexpr std::string_view core_lifecycle_state_name(CoreLifecycleState state) noexcept {
    switch (state) {
    case CoreLifecycleState::Halted:              return "Halted";
    case CoreLifecycleState::Runnable:            return "Runnable";
    case CoreLifecycleState::WaitingForInterrupt: return "WaitingForInterrupt";
    case CoreLifecycleState::WaitingForEvent:     return "WaitingForEvent";
    case CoreLifecycleState::Stopped:             return "Stopped";
    default:                                      return "UnknownLifecycleState";
    }
}

// -----------------------------------------------------------------------------
// SmpCoreState: Per-core architectural and execution state container
//
// Encapsulates the independent state of a single virtual CPU core:
// - A1: General-purpose registers (X0–X30, SP_ELx, PC, PSTATE/NZCV/DAIF)
// - A2: SIMD & FP registers (V0–V31, FPCR, FPSR)
// - A3: Exception state (CurrentEL, ELRx, SPSRx, ESRx, FARx, VBARx)
// - A4: Per-core system registers (SCTLR, TCR, TTBR, MAIR, CPACR, CNTVOFF)
// - A5: Execution metadata (core_id, lifecycle, retirement_count, budget)
// -----------------------------------------------------------------------------
struct SmpCoreState {
    // --- Identification & Lifecycle Metadata ---
    std::size_t core_id{0};
    CoreLifecycleState lifecycle{CoreLifecycleState::Halted};
    std::uint64_t instruction_retirement_count{0};
    std::uint64_t current_budget{0};
    std::uint64_t executed_in_quantum{0};
    bool pending_interrupt{false};

    // --- A1: General-Purpose Registers & Program State ---
    std::uint64_t x[CPU::kRegisterCount]{}; // X0–X30
    std::uint64_t pc{0};
    std::uint64_t sp_el0{0};
    std::uint64_t sp_el1{0};
    std::uint64_t sp_el2{0};
    std::uint64_t sp_el3{0};
    std::uint8_t nzcv{0};
    std::uint8_t daif{0xFu}; // All masked (0xF) at reset
    bool sp_sel{true};       // true = SP_ELx (EL1h), false = SP_EL0 (EL1t)
    bool il{false};          // PSTATE.IL (Illegal execution state)
    bool pstate_ss{false};   // PSTATE.SS (Software step)

    // --- A2: Advanced SIMD & Floating-Point State ---
    CPU::VectorRegister v[CPU::kVectorRegisterCount]{}; // V0–V31 (128-bit)
    std::uint32_t fpcr{0};
    std::uint32_t fpsr{0};

    // --- A3: Exception Architecture State ---
    unsigned current_el{1}; // Reset bare-metal state = EL1
    std::uint64_t elr_el1{0};
    std::uint32_t spsr_el1{0};
    std::uint32_t esr_el1{0};
    std::uint64_t far_el1{0};
    std::uint64_t vbar_el1{0};

    std::uint64_t elr_el2{0};
    std::uint32_t spsr_el2{0};
    std::uint32_t esr_el2{0};
    std::uint64_t far_el2{0};
    std::uint64_t vbar_el2{0};
    std::uint64_t hcr_el2{0};

    std::uint64_t elr_el3{0};
    std::uint32_t spsr_el3{0};
    std::uint32_t esr_el3{0};
    std::uint64_t far_el3{0};
    std::uint64_t vbar_el3{0};
    std::uint64_t scr_el3{0x501ULL};

    // --- A4: Per-Core System Control Registers ---
    std::uint64_t sctlr_el1{0};    std::uint64_t tcr_el1{0};
    std::uint64_t ttbr0_el1{0};
    std::uint64_t ttbr1_el1{0};
    std::uint64_t mair_el1{0};
    std::uint64_t cpacr_el1{0};
    std::uint64_t cntvoff_el2{0};
    std::uint64_t tpidr_el0{0};
    std::uint64_t tpidr_el1{0};
    std::uint64_t tpidrro_el0{0};

    // --- A6: Per-Core Architectural Debug State (Stage 39) ---
    SmpDebugState debug{};
    SmpPmuState pmu{};

    // --- Per-Core Timer Control State ---
    std::uint64_t cntp_cval_el0{0};
    std::uint32_t cntp_ctl_el0{0};
    std::uint64_t cntv_cval_el0{0};
    std::uint32_t cntv_ctl_el0{0};

    // Resets state to power-on defaults
    void reset(std::size_t id = 0, CoreLifecycleState initial_state = CoreLifecycleState::Halted) noexcept;

    // Bi-directional synchronization with CPU instance
    void capture_from_cpu(const CPU& cpu);
    void apply_to_cpu(CPU& cpu) const;

    // Computes canonical 64-bit FNV-1a hash of architectural state
    // (excludes host pointers, debug sinks, and scheduler transient metrics)
    [[nodiscard]] std::uint64_t compute_canonical_hash() const noexcept;

    // Equality operator comparing purely architectural state
    bool operator==(const SmpCoreState& other) const noexcept;
    bool operator!=(const SmpCoreState& other) const noexcept { return !(*this == other); }
};

} // namespace winios
