#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cpu/synchronous_exception.h"
#include "cpu/mmu.h"

class Memory;

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
    void set_vbar_el1(std::uint64_t value) noexcept { vbar_el1_ = value; }

    // Phase 39: Timer and interrupt state.
    struct TimerState {
        bool enable{false};
        bool imask{false};
        bool istatus{false};
        std::uint64_t cval{0xFFFFFFFFFFFFFFFFULL};
    };

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
    std::uint64_t get_cntvoff_el2() const noexcept { return cntvoff_el2_; }
    void set_cntvoff_el2(std::uint64_t value) noexcept { cntvoff_el2_ = value; }
    std::uint64_t get_cnthctl_el2() const noexcept { return cnthctl_el2_; }
    void set_cnthctl_el2(std::uint64_t value) noexcept { cnthctl_el2_ = value; }
    const TimerState& get_cnthp_timer() const noexcept { return cnthp_timer_; }
    void set_cnthp_timer(const TimerState& s) noexcept { cnthp_timer_ = s; }

    // Phase 39: Timer register accessors.
    std::uint64_t get_cntfrq_el0() const noexcept { return cntfrq_el0_; }
    void set_cntfrq_el0(std::uint64_t v) noexcept { cntfrq_el0_ = v; }
    std::uint64_t get_guest_counter() const noexcept { return guest_counter_; }
    void set_guest_counter(std::uint64_t v) noexcept { guest_counter_ = v; }
    std::uint64_t get_cntkctl_el1() const noexcept { return cntkctl_el1_; }
    void set_cntkctl_el1(std::uint64_t v) noexcept { cntkctl_el1_ = v; }
    const TimerState& get_phys_timer() const noexcept { return phys_timer_; }
    void set_phys_timer(const TimerState& s) noexcept { phys_timer_ = s; }
    const TimerState& get_virt_timer() const noexcept { return virt_timer_; }
    void set_virt_timer(const TimerState& s) noexcept { virt_timer_ = s; }
    bool get_event_register() const noexcept { return event_register_; }
    void set_event_register(bool v) noexcept { event_register_ = v; }

    // Phase 39: Check timer fire status and pending interrupts.
    void check_timer_status() noexcept;
    void check_pending_interrupts();

    // Phase 39: Deliver an asynchronous exception (IRQ or FIQ) to EL1.
    void raise_asynchronous_exception(bool is_fiq);

    // Phase 40: EL2 exception handling
    void raise_el2_exception(const SynchronousException& exception, bool from_lower_el);
    void raise_el2_asynchronous_exception(bool is_fiq, bool from_lower_el);

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

    std::uint64_t get_program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t address) noexcept { program_counter_ = address; }

    bool uses_sp_el0() const noexcept { return current_el_ == 0 || !sp_sel_; }
    std::uint64_t get_stack_pointer() const noexcept {
        if (uses_sp_el0()) return sp_el0_;
        if (current_el_ == 2) return sp_el2_;
        return sp_el1_;
    }
    void set_stack_pointer(std::uint64_t value) noexcept {
        if (uses_sp_el0()) {
            sp_el0_ = value;
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
    void clear_exclusive_monitor() noexcept { exclusive_monitor_ = ExclusiveMonitor{}; }

    // Informs the monitor that an ordinary (non-exclusive) store wrote `width`
    // bytes at `address`. Any overlapping reservation is invalidated; a
    // disjoint store leaves it intact. Called by every normal store path.
    void note_normal_store(std::uint64_t address, std::size_t width) noexcept;

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
    // daif_: DAIF interrupt mask bits [3:0] = {D, A, I, F}.
    // Starts all-masked (0xF) — typical EL1 reset state.
    std::uint8_t daif_{0xFu};
    // il_: PSTATE.IL (illegal execution state), set by an illegal ERET.
    bool il_{false};
    // Phase 36: see architectural_exceptions().
    bool architectural_exceptions_{false};
    // Phase 37: MMU translation subsystem
    mmu::MMU mmu_{};
    // Phase 39: Timer and interrupt state.
    std::uint64_t cntfrq_el0_{24000000ULL};  // 24 MHz architectural default
    std::uint64_t guest_counter_{0};          // deterministic per-instruction counter
    TimerState phys_timer_{};                 // CNTP_*
    TimerState virt_timer_{};                 // CNTV_*
    std::uint64_t cntkctl_el1_{0};
    bool event_register_{false};              // WFE event register

    // Phase 40: EL2 Hypervisor & Virtualization state
    std::uint64_t sp_el2_{0};
    std::uint32_t spsr_el2_{0};
    std::uint64_t elr_el2_{0};
    std::uint32_t esr_el2_{0};
    std::uint64_t far_el2_{0};
    std::uint64_t hpfar_el2_{0};
    std::uint64_t vbar_el2_{0};
    std::uint64_t hcr_el2_{0};
    std::uint64_t cntvoff_el2_{0};
    std::uint64_t cnthctl_el2_{3};
    TimerState cnthp_timer_{};
};
