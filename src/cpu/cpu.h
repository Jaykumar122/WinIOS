#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "cpu/debug_architecture.h"
#include "cpu/gicv3.h"
#include "cpu/synchronous_exception.h"
#include "cpu/mmu.h"
#include "cpu/pmu.h"
#include "cpu/virtual_time.h"

class Memory;

namespace winios {
class SMPCluster;
class PSCIService;
namespace trace {
class TraceManager;
enum class TraceEventFlags : std::uint32_t;
enum class ContextChangeReason : std::uint32_t {
    Unknown             = 0,
    ExceptionEntry      = 1,
    ExceptionReturn     = 2,
    RegisterBankSelect  = 3,
    ContextSwitch       = 4
};

// Phase 52 Stage 14: Timer Trace Operations and Types
enum class TimerType : std::uint8_t {
    Physical           = 0, // EL1 Physical Timer (CNTP, PPI 30)
    Virtual            = 1, // EL1 Virtual Timer (CNTV, PPI 27)
    HypervisorPhysical = 2  // EL2 Hypervisor Physical Timer (CNTHP, PPI 26)
};

enum class TimerOp : std::uint8_t {
    CompareUpdate      = 1, // CVAL / TVAL modified
    ControlUpdate      = 2, // CTL register modified (ENABLE / IMASK)
    Expired            = 3, // Counter >= CVAL condition met (istatus = 1)
    InterruptAsserted  = 4, // IRQ asserted to GIC PPI
    InterruptCleared   = 5, // IRQ deasserted (masked, disabled, or CVAL updated)
    Reset              = 6  // Timer state reset to initial defaults
};
}
}

// Small, deliberately bounded model of the AArch64 general-purpose register
// file, stack pointer, and program counter.
class CPU {
public:
    // AArch64 exposes X0..X30 as the general-purpose registers. X31 is not a
    // general-purpose register (it is the zero register / stack pointer in
    // instruction encodings), so it is deliberately not addressable here.
    static constexpr std::size_t kRegisterCount = 31;
    // AArch64 Advanced SIMD and Floating-point exposes 32 128-bit vector registers
    // V0..V31. Register 31 is real architectural register V31 (not a zero register).
    static constexpr std::size_t kVectorRegisterCount = 32;

    struct VectorRegister {
        std::uint64_t low{};
        std::uint64_t high{};

        bool operator==(const VectorRegister& other) const noexcept {
            return low == other.low && high == other.high;
        }
        bool operator!=(const VectorRegister& other) const noexcept {
            return !(*this == other);
        }
    };

    struct TraceRecord {
        std::uint64_t instruction_address{};
        std::uint32_t instruction_word{};
        std::string disassembly;
        std::uint64_t program_counter_after{};
        std::uint8_t nzcv_after{};
        std::vector<std::string> changed_registers;
    };

    using TraceSink = std::function<void(const TraceRecord&)>;

    CPU();

    // Clears every general-purpose register, stack pointer, and program
    // counter to zero.
    void reset();
    // Resets CPU state and starts execution at a validated guest entry point.
    void initialize_entry(std::uint64_t entry_address, const Memory& memory);

    // Checked accessors; throw std::out_of_range for index >= kRegisterCount.
    std::uint64_t read_register(std::size_t index) const;
    void write_register(std::size_t index, std::uint64_t value);

    // Vector / FP register accessors; throw std::out_of_range for index >= kVectorRegisterCount.
    VectorRegister read_vector_register(std::size_t index) const;
    void write_vector_register(std::size_t index, const VectorRegister& value);
    void write_vector_register(std::size_t index, std::uint64_t low, std::uint64_t high = 0);

    // Scalar FP/SIMD accessors: reads lower bits; writes STRICTLY zero upper bits of the 128-bit register.
    std::uint32_t read_s_register(std::size_t index) const;
    void write_s_register(std::size_t index, std::uint32_t value);
    std::uint64_t read_d_register(std::size_t index) const;
    void write_d_register(std::size_t index, std::uint64_t value);
    std::uint8_t read_b_register(std::size_t index) const;
    void write_b_register(std::size_t index, std::uint8_t value);
    std::uint16_t read_h_register(std::size_t index) const;
    void write_h_register(std::size_t index, std::uint16_t value);

    // Floating-Point Control and Status Registers
    std::uint32_t get_fpcr() const noexcept { return fpcr_; }
    void set_fpcr(std::uint32_t value) noexcept { fpcr_ = value; }
    std::uint32_t get_fpsr() const noexcept { return fpsr_; }
    void set_fpsr(std::uint32_t value) noexcept { fpsr_ = value; }

    // Phase 35: Exception level, PSTATE, and system register accessors.
    // current_el returns the current Exception Level (0 or 1).
    std::uint64_t get_current_el() const noexcept { return current_el_; }
    void set_current_el(std::uint64_t el) noexcept { current_el_ = el & 0x3u; }

    // SPSel: true = use SP_ELx (EL1h), false = use SP_EL0 (EL1t / EL0).
    bool get_sp_sel() const noexcept { return sp_sel_; }
    void set_sp_sel(bool sel) noexcept { sp_sel_ = sel; }

    // SP_EL0: the EL0 stack pointer (banked from the main stack pointer).
    std::uint64_t get_sp_el0() const noexcept { return sp_el0_; }
    void set_sp_el0(std::uint64_t value) noexcept { sp_el0_ = value; }

    // Phase 40: Banked EL1 and EL2 stack pointers.
    std::uint64_t get_sp_el1() const noexcept { return sp_el1_; }
    void set_sp_el1(std::uint64_t value) noexcept { sp_el1_ = value; }
    std::uint64_t get_sp_el2() const noexcept { return sp_el2_; }
    void set_sp_el2(std::uint64_t value) noexcept { sp_el2_ = value; }

    // Exception Link Register EL1 (return address after exception entry).
    std::uint64_t get_elr_el1() const noexcept { return elr_el1_; }
    void set_elr_el1(std::uint64_t value) noexcept { elr_el1_ = value; }

    // Saved Program Status Register EL1.
    std::uint32_t get_spsr_el1() const noexcept { return spsr_el1_; }
    void set_spsr_el1(std::uint32_t value) noexcept { spsr_el1_ = value; }

    // Exception Syndrome Register EL1.
    std::uint32_t get_esr_el1() const noexcept { return esr_el1_; }
    void set_esr_el1(std::uint32_t value) noexcept { esr_el1_ = value; }

    // Fault Address Register EL1.
    std::uint64_t get_far_el1() const noexcept { return far_el1_; }
    void set_far_el1(std::uint64_t value) noexcept { far_el1_ = value; }

    // Vector Base Address Register EL1.
    std::uint64_t get_vbar_el1() const noexcept { return vbar_el1_; }
    void set_vbar_el1(std::uint64_t value) noexcept { vbar_el1_ = value & ~0x7FFULL; }

    // Phase 36 / 52: Thread pointer system registers
    std::uint64_t get_tpidr_el0() const noexcept { return tpidr_el0_; }
    void set_tpidr_el0(std::uint64_t value) noexcept { tpidr_el0_ = value; }
    std::uint64_t get_tpidr_el1() const noexcept { return tpidr_el1_; }
    void set_tpidr_el1(std::uint64_t value) noexcept { tpidr_el1_ = value; }
    std::uint64_t get_tpidrro_el0() const noexcept { return tpidrro_el0_; }
    void set_tpidrro_el0(std::uint64_t value) noexcept { tpidrro_el0_ = value; }

    // Phase 37 / 52: CPACR_EL1 and FP/SIMD access control
    std::uint64_t get_cpacr_el1() const noexcept { return cpacr_el1_; }
    void set_cpacr_el1(std::uint64_t value) noexcept { cpacr_el1_ = value; }
    bool is_fp_simd_enabled(unsigned current_el) const noexcept {
        const std::uint32_t fpen = static_cast<std::uint32_t>((cpacr_el1_ >> 20u) & 0x3u);
        if (current_el == 0) {
            return fpen == 0b11;
        }
        return (fpen & 0x1u) != 0;
    }

    // Phase 39/50: Timer and interrupt state.
    using TimerState = winios::TimerState;

    // Phase 40: EL2 Hypervisor & Virtualization Architectural State.
    std::uint32_t get_spsr_el2() const noexcept { return spsr_el2_; }
    void set_spsr_el2(std::uint32_t value) noexcept { spsr_el2_ = value; }
    std::uint64_t get_elr_el2() const noexcept { return elr_el2_; }
    void set_elr_el2(std::uint64_t value) noexcept { elr_el2_ = value; }
    std::uint32_t get_esr_el2() const noexcept { return esr_el2_; }
    void set_esr_el2(std::uint32_t value) noexcept { esr_el2_ = value; }
    std::uint64_t get_far_el2() const noexcept { return far_el2_; }
    void set_far_el2(std::uint64_t value) noexcept { far_el2_ = value; }
    std::uint64_t get_hpfar_el2() const noexcept { return hpfar_el2_; }
    void set_hpfar_el2(std::uint64_t value) noexcept { hpfar_el2_ = value; }
    std::uint64_t get_vbar_el2() const noexcept { return vbar_el2_; }
    void set_vbar_el2(std::uint64_t value) noexcept { vbar_el2_ = value; }
    std::uint64_t get_hcr_el2() const noexcept { return hcr_el2_; }
    void set_hcr_el2(std::uint64_t value) noexcept {
        hcr_el2_ = value;
        mmu_.set_hcr_el2(value);
    }
    std::uint64_t get_vttbr_el2() const noexcept { return mmu_.get_vttbr_el2(); }
    void set_vttbr_el2(std::uint64_t value) noexcept { mmu_.set_vttbr_el2(value); }
    std::uint64_t get_vtcr_el2() const noexcept { return mmu_.get_vtcr_el2(); }
    void set_vtcr_el2(std::uint64_t value) noexcept { mmu_.set_vtcr_el2(value); }
    std::uint64_t get_cntvoff_el2() const noexcept;
    void set_cntvoff_el2(std::uint64_t value) noexcept;
    std::uint64_t get_cnthctl_el2() const noexcept;
    void set_cnthctl_el2(std::uint64_t value) noexcept;
    const TimerState& get_cnthp_timer() const noexcept;
    TimerState& get_cnthp_timer() noexcept;
    void set_cnthp_timer(const TimerState& s) noexcept;
    std::uint64_t get_mdcr_el2() const noexcept { return mdcr_el2_; }
    void set_mdcr_el2(std::uint64_t value) noexcept { mdcr_el2_ = value; }

    // Phase 49: PMU accessors
    winios::PMU& get_pmu() noexcept { return pmu_; }
    const winios::PMU& get_pmu() const noexcept { return pmu_; }

    // Phase 51: Debug Architecture accessors
    winios::DebugArchitecture& get_debug_arch() noexcept { return debug_arch_; }
    const winios::DebugArchitecture& get_debug_arch() const noexcept { return debug_arch_; }

    // Phase 39/50: Timer register accessors.
    std::uint64_t get_cntfrq_el0() const noexcept;
    void set_cntfrq_el0(std::uint64_t v) noexcept;
    std::uint64_t get_guest_counter() const noexcept;
    void set_guest_counter(std::uint64_t v) noexcept;
    void increment_guest_counter() noexcept;
    std::uint64_t get_cntkctl_el1() const noexcept;
    void set_cntkctl_el1(std::uint64_t v) noexcept;
    const TimerState& get_phys_timer() const noexcept;
    TimerState& get_phys_timer() noexcept;
    void set_phys_timer(const TimerState& s) noexcept;
    const TimerState& get_virt_timer() const noexcept;
    TimerState& get_virt_timer() noexcept;
    void set_virt_timer(const TimerState& s) noexcept;
    bool get_event_register() const noexcept {
        return event_register_.load(std::memory_order_acquire);
    }
    void set_event_register(bool v) noexcept {
        event_register_.store(v, std::memory_order_release);
    }

    // Phase 50: Authoritative Virtual-Time Accessors
    winios::VirtualTime& get_virtual_time() noexcept { return *virtual_time_; }
    const winios::VirtualTime& get_virtual_time() const noexcept { return *virtual_time_; }
    void bind_virtual_time(winios::VirtualTime* vt, std::size_t core_id) noexcept {
        virtual_time_ = vt ? vt : &local_virtual_time_;
        core_id_ = core_id;
    }

    // Phase 43: owner-thread re-evaluation of WFI/WFE wake conditions. Called
    // by the parallel worker while a core is idle so timer expiry can wake the
    // core without any cross-thread access to its timer state.
    void poll_wait_wakeup();

    // Phase 39: Check timer fire status and pending interrupts.
    void check_timer_status() noexcept;
    bool check_pending_interrupts();

    // Phase 52 Stage 38: Centralized Architectural Exception Delivery
    void deliver_architectural_exception(const ArchitecturalException& exc);

    // Phase 39 / Phase 52 Stage 12: Deliver an asynchronous exception (IRQ or FIQ) to EL1.
    void raise_asynchronous_exception(bool is_fiq, std::uint32_t intid = 0, bool is_virtual = false, std::uint8_t priority = 0);

    // Phase 40 / Phase 52 Stage 12: EL2 exception handling
    void raise_el2_exception(const SynchronousException& exception, bool from_lower_el);
    void raise_el2_asynchronous_exception(bool is_fiq, bool from_lower_el, std::uint32_t intid = 0, bool is_virtual = false, std::uint8_t priority = 0);

    // Phase 47: EL3 Secure Monitor & Exception Architecture
    std::uint64_t get_sp_el3() const noexcept { return sp_el3_; }
    void set_sp_el3(std::uint64_t value) noexcept { sp_el3_ = value; }
    std::uint32_t get_spsr_el3() const noexcept { return spsr_el3_; }
    void set_spsr_el3(std::uint32_t value) noexcept { spsr_el3_ = value; }
    std::uint64_t get_elr_el3() const noexcept { return elr_el3_; }
    void set_elr_el3(std::uint64_t value) noexcept { elr_el3_ = value; }
    std::uint32_t get_esr_el3() const noexcept { return esr_el3_; }
    void set_esr_el3(std::uint32_t value) noexcept { esr_el3_ = value; }
    std::uint64_t get_far_el3() const noexcept { return far_el3_; }
    void set_far_el3(std::uint64_t value) noexcept { far_el3_ = value; }
    std::uint64_t get_vbar_el3() const noexcept { return vbar_el3_; }
    void set_vbar_el3(std::uint64_t value) noexcept { vbar_el3_ = value; }
    std::uint64_t get_scr_el3() const noexcept { return scr_el3_; }
    void set_scr_el3(std::uint64_t value) noexcept { scr_el3_ = value; }
    std::uint64_t get_cptr_el3() const noexcept { return cptr_el3_; }
    void set_cptr_el3(std::uint64_t value) noexcept { cptr_el3_ = value; }
    std::uint64_t get_mdcr_el3() const noexcept { return mdcr_el3_; }
    void set_mdcr_el3(std::uint64_t value) noexcept { mdcr_el3_ = value; }
    bool is_secure_state() const noexcept { return current_el_ == 3 || !(scr_el3_ & 1ULL); }
    void raise_el3_exception(const SynchronousException& exception, bool from_lower_el);
    void raise_el3_asynchronous_exception(bool is_fiq, bool from_lower_el, std::uint32_t intid = 0, bool is_virtual = false, std::uint8_t priority = 0);

    // Phase 41/42: GICv3 interrupt controller and virtual interface
    winios::GICv3& get_gic() noexcept;
    const winios::GICv3& get_gic() const noexcept;

    // Phase 42: Multicore SMP state accessors
    std::size_t get_core_id() const noexcept { return core_id_; }
    void set_core_id(std::size_t id) noexcept;
    std::uint64_t get_mpidr_el1() const noexcept {
        return 0x80000000ULL | static_cast<std::uint64_t>(core_id_);
    }
    winios::SMPCluster* get_smp_cluster() noexcept { return smp_cluster_; }
    const winios::SMPCluster* get_smp_cluster() const noexcept { return smp_cluster_; }
    void set_smp_cluster(winios::SMPCluster* cluster) noexcept;

    // Phase 48: PSCI service accessors
    winios::PSCIService* get_psci_service() noexcept { return psci_service_; }
    const winios::PSCIService* get_psci_service() const noexcept { return psci_service_; }
    void set_psci_service(winios::PSCIService* svc) noexcept { psci_service_ = svc; }

    bool is_halted() const noexcept {
        return is_halted_.load(std::memory_order_acquire);
    }
    void set_halted(bool h) noexcept {
        is_halted_.store(h, std::memory_order_release);
    }
    bool is_waiting() const noexcept {
        return waiting_for_interrupt_.load(std::memory_order_acquire)
            || waiting_for_event_.load(std::memory_order_acquire);
    }
    bool is_waiting_for_interrupt() const noexcept {
        return waiting_for_interrupt_.load(std::memory_order_acquire);
    }
    bool is_waiting_for_event() const noexcept {
        return waiting_for_event_.load(std::memory_order_acquire);
    }
    void wake_from_wfe() noexcept {
        waiting_for_event_.store(false, std::memory_order_release);
    }
    void wake_from_wfi() noexcept;
    void set_waiting_for_interrupt(bool w) noexcept {
        waiting_for_interrupt_.store(w, std::memory_order_release);
    }
    void set_waiting_for_event(bool w) noexcept {
        waiting_for_event_.store(w, std::memory_order_release);
    }

    // Phase 37: MMU system registers and translation subsystem
    mmu::MMU& get_mmu() noexcept { return mmu_; }
    const mmu::MMU& get_mmu() const noexcept { return mmu_; }

    std::uint64_t get_sctlr_el1() const noexcept { return mmu_.get_sctlr_el1(); }
    void set_sctlr_el1(std::uint64_t val) noexcept { mmu_.set_sctlr_el1(val); }

    std::uint64_t get_tcr_el1() const noexcept { return mmu_.get_tcr_el1(); }
    void set_tcr_el1(std::uint64_t val) noexcept { mmu_.set_tcr_el1(val); }

    std::uint64_t get_ttbr0_el1() const noexcept { return mmu_.get_ttbr0_el1(); }
    void set_ttbr0_el1(std::uint64_t val) noexcept { mmu_.set_ttbr0_el1(val); }

    std::uint64_t get_ttbr1_el1() const noexcept { return mmu_.get_ttbr1_el1(); }
    void set_ttbr1_el1(std::uint64_t val) noexcept { mmu_.set_ttbr1_el1(val); }

    std::uint64_t get_mair_el1() const noexcept { return mmu_.get_mair_el1(); }
    void set_mair_el1(std::uint64_t val) noexcept { mmu_.set_mair_el1(val); }

    std::uint64_t get_id_aa64mmfr0_el1() const noexcept { return mmu_.get_id_aa64mmfr0_el1(); }

    std::uint64_t get_par_el1() const noexcept { return mmu_.get_par_el1(); }
    void set_par_el1(std::uint64_t val) noexcept { mmu_.set_par_el1(val); }

    // DAIF interrupt mask bits (bits 3:0 map to D, A, I, F respectively).
    std::uint8_t get_daif() const noexcept { return daif_; }
    void set_daif(std::uint8_t value) noexcept { daif_ = value & 0xFu; }

    // PSTATE.IL (bit 20). Set by an architecturally illegal ERET; while set, the
    // next instruction fetch takes an Illegal Execution State exception.
    bool get_il() const noexcept { return il_; }
    void set_il(bool value) noexcept { il_ = value; }

    // Phase 51 Stage 8: PSTATE.SS (bit 21). Software Step process state bit.
    bool get_pstate_ss() const noexcept { return pstate_ss_; }
    void set_pstate_ss(bool ss) noexcept { pstate_ss_ = ss; }

    // Evaluates whether Software Step is active in the current execution state.
    bool is_software_step_active() const noexcept;
    void on_instruction_retired_step(bool is_exclusive, std::uint64_t stepped_pc = 0);

    // Phase 36: architectural synchronous-exception mode.
    // Disabled by default so that the historical host-throw semantics (and the
    // historical differential corpus) are unchanged. When enabled, guest
    // faults are reported through the typed model in synchronous_exception.h
    // and address arithmetic follows the architecture (wrap modulo 2^64).
    bool architectural_exceptions() const noexcept { return architectural_exceptions_; }
    void set_architectural_exceptions(bool enabled) noexcept {
        architectural_exceptions_ = enabled;
    }

    // Phase 36: performs architectural entry for one synchronous exception:
    // captures SPSR_EL1 from the current PSTATE, selects the EL1 vector slot,
    // writes ELR_EL1/ESR_EL1 (and FAR_EL1 when the exception has a fault
    // address), then enters EL1h with DAIF masked. Generation of the exception
    // is separate; instruction paths build a SynchronousException and call
    // this exactly once.
    void raise_synchronous_exception(const SynchronousException& exception);

    bool exception_taken_in_step() const noexcept { return exception_taken_in_step_; }
    void set_exception_taken_in_step(bool v) noexcept { exception_taken_in_step_ = v; }

    std::uint64_t get_program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t address) noexcept { program_counter_ = address; }

    bool uses_sp_el0() const noexcept { return current_el_ == 0 || !sp_sel_; }
    std::uint64_t get_stack_pointer() const noexcept {
        if (uses_sp_el0()) return sp_el0_;
        if (current_el_ == 3) return sp_el3_;
        if (current_el_ == 2) return sp_el2_;
        return sp_el1_;
    }
    void set_stack_pointer(std::uint64_t value) noexcept {
        if (uses_sp_el0()) {
            sp_el0_ = value;
        } else if (current_el_ == 3) {
            sp_el3_ = value;
        } else if (current_el_ == 2) {
            sp_el2_ = value;
        } else {
            sp_el1_ = value;
        }
    }
    std::uint8_t get_nzcv() const noexcept { return nzcv_; }
    void set_nzcv(std::uint8_t flags) noexcept { nzcv_ = static_cast<std::uint8_t>(flags & 0xFu); }

    // A null sink disables tracing. Tracing is disabled by default.
    void set_trace_sink(TraceSink sink) { trace_sink_ = std::move(sink); }
    void clear_trace_sink() { trace_sink_ = {}; }

    // Phase 52 Stage 6: Trace Manager integration
    void bind_trace_manager(winios::trace::TraceManager* tm) noexcept { trace_manager_ = tm; }
    void set_trace_manager(winios::trace::TraceManager* tm) noexcept { bind_trace_manager(tm); }
    winios::trace::TraceManager* get_trace_manager() noexcept { return trace_manager_; }
    const winios::trace::TraceManager* get_trace_manager() const noexcept { return trace_manager_; }
    std::uint64_t get_instruction_trace_sequence() const noexcept { return instruction_trace_sequence_; }
    void reset_instruction_trace_sequence() noexcept { instruction_trace_sequence_ = 0; }
    std::uint64_t get_instruction_retirement_count() const noexcept { return instruction_retirement_count_; }
    void set_instruction_retirement_count(std::uint64_t cnt) noexcept { instruction_retirement_count_ = cnt; }

    // Local exclusive monitor. This interpreter models exactly one CPU and one
    // flat RAM, so the monitor is private per-CPU state and only ever observes
    // stores performed through this CPU (there is no other writer). The
    // granularity is the exact byte range [address, address + size).
    bool exclusive_monitor_valid() const noexcept { return exclusive_monitor_.valid; }
    std::uint64_t exclusive_monitor_address() const noexcept {
        return exclusive_monitor_.address;
    }
    std::size_t exclusive_monitor_size() const noexcept {
        return exclusive_monitor_.size;
    }
    std::uint64_t exclusive_monitor_low_value() const noexcept {
        return exclusive_monitor_.low;
    }
    std::uint64_t exclusive_monitor_high_value() const noexcept {
        return exclusive_monitor_.high;
    }
    void set_exclusive_monitor(bool valid, std::uint64_t address, std::size_t size,
                               std::uint64_t low, std::uint64_t high) noexcept {
        exclusive_monitor_ = ExclusiveMonitor{valid, address, size, low, high};
    }
    void clear_exclusive_monitor() noexcept { exclusive_monitor_ = ExclusiveMonitor{}; }

    // Informs the monitor that an ordinary (non-exclusive) store wrote `width`
    // bytes at `address`. Any overlapping reservation is invalidated; a
    // disjoint store leaves it intact. Called by every normal store path.
    void note_normal_store(std::uint64_t address, std::size_t width) noexcept;

    // Phase 52 Stage 6: Instruction-retirement trace emission
    void emit_instruction_retired_trace(std::uint64_t instruction_address, std::uint32_t instruction);

    // Phase 52 Stage 7: Branch-taken trace emission
    void emit_branch_taken_trace(std::uint64_t instruction_address, std::uint64_t target_address, std::uint32_t instruction);

    // Phase 52 Stage 8: Exception-entered trace emission
    void emit_exception_entered_trace(std::uint64_t source_pc, std::uint64_t vector_address,
                                      std::uint32_t syndrome, std::uint64_t far,
                                      bool has_far, bool is_sync, unsigned target_el);

    // Phase 52 Stage 8: Exception-returned trace emission
    void emit_exception_returned_trace(std::uint64_t eret_pc, std::uint64_t target_pc,
                                       std::uint32_t spsr, unsigned target_el);

    // Phase 52 Stage 9: System-register access trace emission
    void emit_system_register_access_trace(std::uint64_t instruction_address,
                                          std::uint16_t reg_key,
                                          std::uint64_t reg_value,
                                          bool is_write,
                                          std::uint32_t instruction);

    // Phase 52 Stage 10: Memory read trace emission
    void emit_memory_read_trace(std::uint64_t instruction_address,
                                std::uint64_t virtual_address,
                                std::uint64_t physical_address,
                                std::uint64_t value,
                                std::uint16_t access_size,
                                bool is_atomic,
                                std::uint32_t instruction);

    // Phase 52 Stage 10: Memory write trace emission
    void emit_memory_write_trace(std::uint64_t instruction_address,
                                 std::uint64_t virtual_address,
                                 std::uint64_t physical_address,
                                 std::uint64_t value,
                                 std::uint16_t access_size,
                                 bool is_atomic,
                                 std::uint32_t instruction);

    // Phase 52 Stage 11: Debug event trace emission
    void emit_debug_event(std::uint64_t instruction_address,
                          std::uint64_t debug_address,
                          std::uint32_t syndrome,
                          winios::trace::TraceEventFlags debug_flags,
                          std::uint16_t access_size = 0,
                          std::uint64_t data_payload = 0);

    // Phase 52 Stage 12: Interrupt delivery trace emission
    void emit_interrupt_delivered_trace(std::uint64_t source_pc,
                                        std::uint64_t vector_address,
                                        std::uint32_t intid,
                                        bool is_fiq,
                                        bool is_virtual,
                                        unsigned target_el,
                                        std::uint8_t priority = 0);

    // Phase 52 Stage 13: CPU Lifecycle and Context Change Tracing
    bool is_started() const noexcept { return is_started_; }
    bool is_stopped() const noexcept { return is_stopped_; }
    void set_started(bool s) noexcept { is_started_ = s; }
    void set_stopped(bool s) noexcept { is_stopped_ = s; }
    void set_instruction_trace_sequence(std::uint64_t seq) noexcept { instruction_trace_sequence_ = seq; }
    void start();
    void stop(std::uint64_t stop_reason = 0);

    void emit_cpu_started_trace();
    void emit_cpu_stopped_trace(std::uint64_t stop_reason = 0);
    void emit_context_changed_trace(std::uint64_t prev_pc,
                                    std::uint64_t new_pc,
                                    unsigned prev_el,
                                    unsigned new_el,
                                    bool prev_spsel,
                                    bool new_spsel,
                                    winios::trace::ContextChangeReason reason,
                                    std::uint8_t prev_daif = 0,
                                    std::uint8_t new_daif = 0,
                                    bool is_interrupt = false);

    // Phase 52 Stage 14: Timer event trace emission
    void emit_timer_event_trace(winios::trace::TimerOp op,
                                winios::trace::TimerType type,
                                std::uint64_t cval,
                                std::uint64_t counter,
                                bool enable,
                                bool imask,
                                bool istatus,
                                std::uint64_t trigger_pc = 0);

    // Fetches and executes one 32-bit instruction at the current PC.
    // Unsupported encodings throw std::runtime_error and memory errors from
    // the fetch are propagated.
    void step(Memory& memory);

    // Executes at most max_instructions instructions and returns the count
    // actually executed. A zero limit performs no fetch.
    std::size_t run(Memory& memory, std::size_t max_instructions);

    // Demonstration only: performs host arithmetic on the register file.
    void run_demo();

private:
    struct ExclusiveMonitor {
        bool valid{};
        std::uint64_t address{};
        std::size_t size{};
        // For a single-register exclusive load, `low` holds the loaded value
        // and `high` is unused. For a pair, they hold the two elements from
        // the lower and higher address respectively.
        std::uint64_t low{};
        std::uint64_t high{};
    };

    // Phase 43: acquires the cluster store-serialization mutex when attached to
    // a cluster. A default-constructed (unlocked) lock is returned otherwise.
    // Recursive, because exclusive/LSE paths call note_normal_store and the
    // Memory store observer while the guard is still held.
    std::unique_lock<std::recursive_mutex> lock_store_serialization();

    std::uint64_t registers_[kRegisterCount]{};
    VectorRegister v_registers_[kVectorRegisterCount]{};
    std::uint64_t sp_el1_{};
    std::uint64_t program_counter_{};
    std::uint8_t nzcv_{};
    std::uint32_t fpcr_{};
    std::uint32_t fpsr_{};
    ExclusiveMonitor exclusive_monitor_{};
    TraceSink trace_sink_{};

    // Phase 35: Exception architecture state.
    // current_el_: current Exception Level (0 = EL0, 1 = EL1).
    // Starts at EL1 (bare-metal boot state).
    std::uint64_t current_el_{1};
    // sp_sel_: SPSel bit. true = use SP_ELx (stack_pointer_) in EL1 (EL1h),
    // false = use SP_EL0 in EL1t; at EL0 SP_EL0 is always used.
    bool sp_sel_{true};
    std::uint64_t sp_el0_{};      // Banked EL0 stack pointer
    std::uint64_t elr_el1_{};     // Exception Link Register EL1
    std::uint32_t spsr_el1_{};    // Saved Program Status Register EL1
    std::uint32_t esr_el1_{};     // Exception Syndrome Register EL1
    std::uint64_t far_el1_{};     // Fault Address Register EL1
    std::uint64_t vbar_el1_{};    // Vector Base Address Register EL1
    std::uint64_t tpidr_el0_{};   // EL0 Read/Write Software Thread ID
    std::uint64_t tpidr_el1_{};   // EL1 Read/Write Software Thread ID
    std::uint64_t tpidrro_el0_{}; // EL0 Read-Only Software Thread ID
    std::uint64_t cpacr_el1_{};   // Architectural Coprocessor Access Control Register EL1
    // daif_: DAIF interrupt mask bits [3:0] = {D, A, I, F}.
    // Starts all-masked (0xF) — typical EL1 reset state.
    std::uint8_t daif_{0xFu};
    // il_: PSTATE.IL (illegal execution state), set by an illegal ERET.
    bool il_{false};
    // pstate_ss_: PSTATE.SS (Software Step process state bit, bit 21).
    bool pstate_ss_{false};
    // Phase 36: see architectural_exceptions().
    bool architectural_exceptions_{false};
    // Phase 37: MMU translation subsystem
    mmu::MMU mmu_{};
    // Phase 39/50: Timer and interrupt state.
    std::atomic<bool> event_register_{false}; // WFE event register

    // Phase 40: EL2 Hypervisor & Virtualization state
    std::uint64_t sp_el2_{0};
    std::uint32_t spsr_el2_{0};
    std::uint64_t elr_el2_{0};
    std::uint32_t esr_el2_{0};
    std::uint64_t far_el2_{0};
    std::uint64_t hpfar_el2_{0};
    std::uint64_t vbar_el2_{0};
    std::uint64_t hcr_el2_{0};
    std::uint64_t mdcr_el2_{0};
    winios::GICv3 gic_{};

    // Phase 47: EL3 Secure Monitor state
    std::uint64_t sp_el3_{0};
    std::uint32_t spsr_el3_{0};
    std::uint64_t elr_el3_{0};
    std::uint32_t esr_el3_{0};
    std::uint64_t far_el3_{0};
    std::uint64_t vbar_el3_{0};
    std::uint64_t scr_el3_{0x501ULL}; // Default RW=1, HCE=1, NS=1 (matches non-secure OS boot default)
    std::uint64_t cptr_el3_{0};
    std::uint64_t mdcr_el3_{0};

    // Phase 49: PMU state
    winios::PMU pmu_{};

    // Phase 51: Debug Architecture state
    winios::DebugArchitecture debug_arch_{};

    // Phase 50: Authoritative Virtual-Time Architecture
    winios::VirtualTime local_virtual_time_{};
    winios::VirtualTime* virtual_time_{&local_virtual_time_};

    // Phase 42: Multicore SMP state
    std::size_t core_id_{0};
    winios::SMPCluster* smp_cluster_{nullptr};
    winios::PSCIService* psci_service_{nullptr}; // Phase 48: PSCI service
    std::atomic<bool> is_halted_{false};
    std::atomic<bool> waiting_for_interrupt_{false};
    std::atomic<bool> waiting_for_event_{false};
    bool exception_taken_in_step_{false};

    // Phase 52 Stage 6: Trace Manager
    winios::trace::TraceManager* trace_manager_{nullptr};
    std::uint64_t instruction_trace_sequence_{0};
    std::uint64_t instruction_retirement_count_{0};
    bool is_started_{false};
    bool is_stopped_{false};

    // Phase 52 Stage 14: Timer Trace State Tracking
    struct CoreTimerTraceState {
        bool phys_prev_istatus{false};
        bool phys_prev_irq{false};
        bool virt_prev_istatus{false};
        bool virt_prev_irq{false};
        bool cnthp_prev_istatus{false};
        bool cnthp_prev_irq{false};
    };
    CoreTimerTraceState timer_trace_state_{};
};
