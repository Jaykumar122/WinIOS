#include "cpu/smp_core_state.h"
#include "trace/trace_serializer.h"

#include <cstring>

namespace winios {

namespace {

constexpr std::uint64_t kFnv1aOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnv1aPrime = 0x100000001b3ULL;

inline std::uint64_t fnv1a_bytes(std::uint64_t hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnv1aPrime;
    }
    return hash;
}

inline std::uint64_t fnv1a_u64(std::uint64_t hash, std::uint64_t val) noexcept {
    std::uint8_t b[8];
    trace::encode_u64_le(b, val);
    return fnv1a_bytes(hash, b, 8);
}

inline std::uint64_t fnv1a_u32(std::uint64_t hash, std::uint32_t val) noexcept {
    std::uint8_t b[4];
    trace::encode_u32_le(b, val);
    return fnv1a_bytes(hash, b, 4);
}

} // namespace

void SmpCoreState::reset(std::size_t id, CoreLifecycleState initial_state) noexcept {
    core_id = id;
    lifecycle = initial_state;
    instruction_retirement_count = 0;
    current_budget = 0;
    executed_in_quantum = 0;
    pending_interrupt = false;

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        x[i] = 0;
    }
    pc = 0;
    sp_el0 = 0;
    sp_el1 = 0;
    sp_el2 = 0;
    sp_el3 = 0;
    nzcv = 0;
    daif = 0xFu;
    sp_sel = true;
    il = false;
    pstate_ss = false;

    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        v[i] = CPU::VectorRegister{};
    }
    fpcr = 0;
    fpsr = 0;

    current_el = 1;
    elr_el1 = 0;
    spsr_el1 = 0;
    esr_el1 = 0;
    far_el1 = 0;
    vbar_el1 = 0;

    elr_el2 = 0;
    spsr_el2 = 0;
    esr_el2 = 0;
    far_el2 = 0;
    vbar_el2 = 0;
    hcr_el2 = 0;

    elr_el3 = 0;
    spsr_el3 = 0;
    esr_el3 = 0;
    far_el3 = 0;
    vbar_el3 = 0;
    scr_el3 = 0x501ULL;

    sctlr_el1 = 0;
    tcr_el1 = 0;
    ttbr0_el1 = 0;
    ttbr1_el1 = 0;
    mair_el1 = 0;
    cpacr_el1 = 0;
    cntvoff_el2 = 0;
    tpidr_el0 = 0;
    tpidr_el1 = 0;
    tpidrro_el0 = 0;

    cntp_cval_el0 = 0;
    cntp_ctl_el0 = 0;
    cntv_cval_el0 = 0;
    cntv_ctl_el0 = 0;

    debug = SmpDebugState{};
    pmu = SmpPmuState{};
    pmu.pmcr = static_cast<std::uint32_t>(kNumEventCounters) << kPMCRNumCountersShift;
}

void SmpCoreState::capture_from_cpu(const CPU& cpu) {
    core_id = cpu.get_core_id();
    instruction_retirement_count = cpu.get_instruction_retirement_count();

    if (cpu.is_stopped()) {
        lifecycle = CoreLifecycleState::Stopped;
    } else if (cpu.is_halted()) {
        lifecycle = CoreLifecycleState::Halted;
    } else if (cpu.is_waiting_for_interrupt()) {
        lifecycle = CoreLifecycleState::WaitingForInterrupt;
    } else if (cpu.is_waiting_for_event()) {
        lifecycle = CoreLifecycleState::WaitingForEvent;
    } else {
        lifecycle = CoreLifecycleState::Runnable;
    }

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        x[i] = cpu.read_register(i);
    }
    pc = cpu.get_program_counter();
    sp_el0 = cpu.get_sp_el0();
    sp_el1 = cpu.get_sp_el1();
    sp_el2 = cpu.get_sp_el2();
    sp_el3 = cpu.get_sp_el3();
    current_el = static_cast<unsigned>(cpu.get_current_el());
    sp_sel = cpu.get_sp_sel();
    nzcv = cpu.get_nzcv();
    daif = cpu.get_daif();
    il = cpu.get_il();
    pstate_ss = cpu.get_pstate_ss();

    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        v[i] = cpu.read_vector_register(i);
    }
    fpcr = cpu.get_fpcr();
    fpsr = cpu.get_fpsr();

    elr_el1 = cpu.get_elr_el1();
    spsr_el1 = cpu.get_spsr_el1();
    esr_el1 = cpu.get_esr_el1();
    far_el1 = cpu.get_far_el1();
    vbar_el1 = cpu.get_vbar_el1();

    elr_el2 = cpu.get_elr_el2();
    spsr_el2 = cpu.get_spsr_el2();
    esr_el2 = cpu.get_esr_el2();
    far_el2 = cpu.get_far_el2();
    vbar_el2 = cpu.get_vbar_el2();
    hcr_el2 = cpu.get_hcr_el2();
    cntvoff_el2 = cpu.get_cntvoff_el2();

    elr_el3 = cpu.get_elr_el3();
    spsr_el3 = cpu.get_spsr_el3();
    esr_el3 = cpu.get_esr_el3();
    far_el3 = cpu.get_far_el3();
    vbar_el3 = cpu.get_vbar_el3();
    scr_el3 = cpu.get_scr_el3();

    sctlr_el1 = cpu.get_sctlr_el1();
    tcr_el1 = cpu.get_tcr_el1();
    ttbr0_el1 = cpu.get_ttbr0_el1();
    ttbr1_el1 = cpu.get_ttbr1_el1();
    mair_el1 = cpu.get_mair_el1();
    cpacr_el1 = cpu.get_cpacr_el1();
    tpidr_el0 = cpu.get_tpidr_el0();
    tpidr_el1 = cpu.get_tpidr_el1();
    tpidrro_el0 = cpu.get_tpidrro_el0();

    const auto& pt = cpu.get_phys_timer();
    cntp_cval_el0 = pt.cval;
    cntp_ctl_el0 = (pt.enable ? 1u : 0u) | (pt.imask ? 2u : 0u) | (pt.istatus ? 4u : 0u);
    const auto& vt = cpu.get_virt_timer();
    cntv_cval_el0 = vt.cval;
    cntv_ctl_el0 = (vt.enable ? 1u : 0u) | (vt.imask ? 2u : 0u) | (vt.istatus ? 4u : 0u);

    // Stage 39: per-core architectural debug configuration.
    const auto& dbg = cpu.get_debug_arch();
    debug.mdscr_el1 = dbg.get_mdscr_el1();
    debug.mdccint_el1 = dbg.get_mdccint_el1();
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        debug.breakpoint_value[i] = dbg.get_dbgbvr(i);
        debug.breakpoint_control[i] = dbg.get_dbgbcr(i);
    }
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        debug.watchpoint_value[i] = dbg.get_dbgwvr(i);
        debug.watchpoint_control[i] = dbg.get_dbgwcr(i);
    }

    const auto& p = cpu.get_pmu();
    pmu.pmcr = p.read_pmcr();
    pmu.counter_enable = p.read_pmcntenset();
    pmu.overflow_status = p.read_pmovsset();
    pmu.interrupt_enable = p.read_pmintenset();
    pmu.selector = p.read_pmselr();
    pmu.user_enable = p.read_pmuserenr();
    pmu.cycle_counter = p.read_pmccntr();
    pmu.cycle_filter = p.read_pmccfiltr();
    pmu.cycle_prescaler = p.cycle_prescaler();
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        pmu.event_counter[i] = p.read_pmevcntr(i);
        pmu.event_type[i] = p.read_pmevtyper(i);
    }
}

void SmpCoreState::apply_to_cpu(CPU& cpu) const {
    cpu.set_core_id(core_id);
    cpu.set_instruction_retirement_count(instruction_retirement_count);

    switch (lifecycle) {
    case CoreLifecycleState::Halted:
        cpu.set_halted(true);
        cpu.set_waiting_for_interrupt(false);
        cpu.set_waiting_for_event(false);
        break;
    case CoreLifecycleState::Runnable:
        cpu.set_halted(false);
        cpu.set_waiting_for_interrupt(false);
        cpu.set_waiting_for_event(false);
        break;
    case CoreLifecycleState::WaitingForInterrupt:
        cpu.set_halted(false);
        cpu.set_waiting_for_interrupt(true);
        cpu.set_waiting_for_event(false);
        break;
    case CoreLifecycleState::WaitingForEvent:
        cpu.set_halted(false);
        cpu.set_waiting_for_interrupt(false);
        cpu.set_waiting_for_event(true);
        break;
    case CoreLifecycleState::Stopped:
        cpu.set_halted(true);
        cpu.set_stopped(true);
        break;
    }

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        cpu.write_register(i, x[i]);
    }
    cpu.set_program_counter(pc);
    cpu.set_sp_el0(sp_el0);
    cpu.set_sp_el1(sp_el1);
    cpu.set_sp_el2(sp_el2);
    cpu.set_sp_el3(sp_el3);
    cpu.set_current_el(current_el);
    cpu.set_sp_sel(sp_sel);
    cpu.set_nzcv(nzcv);
    cpu.set_daif(daif);
    cpu.set_il(il);
    cpu.set_pstate_ss(pstate_ss);

    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        cpu.write_vector_register(i, v[i]);
    }
    cpu.set_fpcr(fpcr);
    cpu.set_fpsr(fpsr);

    cpu.set_elr_el1(elr_el1);
    cpu.set_spsr_el1(spsr_el1);
    cpu.set_esr_el1(esr_el1);
    cpu.set_far_el1(far_el1);
    cpu.set_vbar_el1(vbar_el1);

    cpu.set_elr_el2(elr_el2);
    cpu.set_spsr_el2(spsr_el2);
    cpu.set_esr_el2(esr_el2);
    cpu.set_far_el2(far_el2);
    cpu.set_vbar_el2(vbar_el2);
    cpu.set_hcr_el2(hcr_el2);
    cpu.set_cntvoff_el2(cntvoff_el2);

    cpu.set_elr_el3(elr_el3);
    cpu.set_spsr_el3(spsr_el3);
    cpu.set_esr_el3(esr_el3);
    cpu.set_far_el3(far_el3);
    cpu.set_vbar_el3(vbar_el3);
    cpu.set_scr_el3(scr_el3);

    cpu.set_sctlr_el1(sctlr_el1);
    cpu.set_tcr_el1(tcr_el1);
    cpu.set_ttbr0_el1(ttbr0_el1);
    cpu.set_ttbr1_el1(ttbr1_el1);
    cpu.set_mair_el1(mair_el1);
    cpu.set_cpacr_el1(cpacr_el1);
    cpu.set_tpidr_el0(tpidr_el0);
    cpu.set_tpidr_el1(tpidr_el1);
    cpu.set_tpidrro_el0(tpidrro_el0);

    auto pt = cpu.get_phys_timer();
    pt.cval = cntp_cval_el0;
    pt.enable = (cntp_ctl_el0 & 1u) != 0;
    pt.imask = (cntp_ctl_el0 & 2u) != 0;
    pt.istatus = (cntp_ctl_el0 & 4u) != 0;
    cpu.set_phys_timer(pt);

    auto vt = cpu.get_virt_timer();
    vt.cval = cntv_cval_el0;
    vt.enable = (cntv_ctl_el0 & 1u) != 0;
    vt.imask = (cntv_ctl_el0 & 2u) != 0;
    vt.istatus = (cntv_ctl_el0 & 4u) != 0;
    cpu.set_virt_timer(vt);

    // Stage 39: restore per-core architectural debug configuration.
    auto& dbg = cpu.get_debug_arch();
    dbg.set_mdscr_el1(debug.mdscr_el1);
    dbg.set_mdccint_el1(debug.mdccint_el1);
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        dbg.set_dbgbvr(i, debug.breakpoint_value[i]);
        dbg.set_dbgbcr(i, debug.breakpoint_control[i]);
    }
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        dbg.set_dbgwvr(i, debug.watchpoint_value[i]);
        dbg.set_dbgwcr(i, debug.watchpoint_control[i]);
    }

    auto& p = cpu.get_pmu();
    p.reset();
    p.write_pmcr(pmu.pmcr);
    p.write_pmcntenset(pmu.counter_enable);
    p.write_pmovsset(pmu.overflow_status);
    p.write_pmintenset(pmu.interrupt_enable);
    p.write_pmselr(pmu.selector);
    p.write_pmuserenr(pmu.user_enable);
    p.write_pmccntr(pmu.cycle_counter);
    p.write_pmccfiltr(pmu.cycle_filter);
    p.set_cycle_prescaler(pmu.cycle_prescaler);
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        p.write_pmevcntr(i, pmu.event_counter[i]);
        p.write_pmevtyper(i, pmu.event_type[i]);
    }
}

std::uint64_t SmpCoreState::compute_canonical_hash() const noexcept {
    std::uint64_t h = kFnv1aOffsetBasis;

    // Core ID & Lifecycle
    h = fnv1a_u64(h, static_cast<std::uint64_t>(core_id));
    h = fnv1a_u32(h, static_cast<std::uint32_t>(lifecycle));

    // Integer registers
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        h = fnv1a_u64(h, x[i]);
    }
    h = fnv1a_u64(h, pc);
    h = fnv1a_u64(h, sp_el0);
    h = fnv1a_u64(h, sp_el1);
    h = fnv1a_u64(h, sp_el2);
    h = fnv1a_u64(h, sp_el3);

    const std::uint64_t pstate = (static_cast<std::uint64_t>(nzcv) & 0xFu) |
                                 ((static_cast<std::uint64_t>(daif) & 0xFu) << 4u) |
                                 ((static_cast<std::uint64_t>(current_el) & 0x3u) << 8u) |
                                 ((static_cast<std::uint64_t>(sp_sel ? 1 : 0)) << 10u) |
                                 ((static_cast<std::uint64_t>(il ? 1 : 0)) << 11u) |
                                 ((static_cast<std::uint64_t>(pstate_ss ? 1 : 0)) << 12u);
    h = fnv1a_u64(h, pstate);

    // SIMD / FP registers
    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        h = fnv1a_u64(h, v[i].low);
        h = fnv1a_u64(h, v[i].high);
    }
    h = fnv1a_u32(h, fpcr);
    h = fnv1a_u32(h, fpsr);

    // Exception registers
    h = fnv1a_u64(h, elr_el1);
    h = fnv1a_u32(h, spsr_el1);
    h = fnv1a_u32(h, esr_el1);
    h = fnv1a_u64(h, far_el1);
    h = fnv1a_u64(h, vbar_el1);

    h = fnv1a_u64(h, elr_el2);
    h = fnv1a_u32(h, spsr_el2);
    h = fnv1a_u32(h, esr_el2);
    h = fnv1a_u64(h, far_el2);
    h = fnv1a_u64(h, vbar_el2);
    h = fnv1a_u64(h, hcr_el2);
    h = fnv1a_u64(h, cntvoff_el2);

    h = fnv1a_u64(h, elr_el3);
    h = fnv1a_u32(h, spsr_el3);
    h = fnv1a_u32(h, esr_el3);
    h = fnv1a_u64(h, far_el3);
    h = fnv1a_u64(h, vbar_el3);
    h = fnv1a_u64(h, scr_el3);

    // System registers
    h = fnv1a_u64(h, sctlr_el1);
    h = fnv1a_u64(h, tcr_el1);
    h = fnv1a_u64(h, ttbr0_el1);
    h = fnv1a_u64(h, ttbr1_el1);
    h = fnv1a_u64(h, mair_el1);
    h = fnv1a_u64(h, cpacr_el1);
    h = fnv1a_u64(h, tpidr_el0);
    h = fnv1a_u64(h, tpidr_el1);
    h = fnv1a_u64(h, tpidrro_el0);

    // Timers
    h = fnv1a_u64(h, cntp_cval_el0);
    h = fnv1a_u32(h, cntp_ctl_el0);
    h = fnv1a_u64(h, cntv_cval_el0);
    h = fnv1a_u32(h, cntv_ctl_el0);

    // Stage 39: architectural debug configuration
    h = fnv1a_u64(h, debug.mdscr_el1);
    h = fnv1a_u64(h, debug.mdccint_el1);
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        h = fnv1a_u64(h, debug.breakpoint_value[i]);
        h = fnv1a_u32(h, debug.breakpoint_control[i]);
    }
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        h = fnv1a_u64(h, debug.watchpoint_value[i]);
        h = fnv1a_u32(h, debug.watchpoint_control[i]);
    }

    h = fnv1a_u32(h, pmu.pmcr);
    h = fnv1a_u32(h, pmu.counter_enable);
    h = fnv1a_u32(h, pmu.overflow_status);
    h = fnv1a_u32(h, pmu.interrupt_enable);
    h = fnv1a_u32(h, pmu.selector);
    h = fnv1a_u32(h, pmu.user_enable);
    h = fnv1a_u64(h, pmu.cycle_counter);
    h = fnv1a_u32(h, pmu.cycle_filter);
    h = fnv1a_u64(h, pmu.cycle_prescaler);
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        h = fnv1a_u32(h, pmu.event_counter[i]);
        h = fnv1a_u32(h, pmu.event_type[i]);
    }

    return h;
}

bool SmpCoreState::operator==(const SmpCoreState& o) const noexcept {
    if (core_id != o.core_id || lifecycle != o.lifecycle) {
        return false;
    }
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        if (x[i] != o.x[i]) return false;
    }
    if (pc != o.pc || sp_el0 != o.sp_el0 || sp_el1 != o.sp_el1 ||
        sp_el2 != o.sp_el2 || sp_el3 != o.sp_el3 ||
        nzcv != o.nzcv || daif != o.daif || current_el != o.current_el ||
        sp_sel != o.sp_sel || il != o.il || pstate_ss != o.pstate_ss) {
        return false;
    }
    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        if (v[i].low != o.v[i].low || v[i].high != o.v[i].high) return false;
    }
    if (fpcr != o.fpcr || fpsr != o.fpsr) return false;

    if (elr_el1 != o.elr_el1 || spsr_el1 != o.spsr_el1 || esr_el1 != o.esr_el1 ||
        far_el1 != o.far_el1 || vbar_el1 != o.vbar_el1) return false;
    if (elr_el2 != o.elr_el2 || spsr_el2 != o.spsr_el2 || esr_el2 != o.esr_el2 ||
        far_el2 != o.far_el2 || vbar_el2 != o.vbar_el2 || hcr_el2 != o.hcr_el2 ||
        cntvoff_el2 != o.cntvoff_el2) return false;
    if (elr_el3 != o.elr_el3 || spsr_el3 != o.spsr_el3 || esr_el3 != o.esr_el3 ||
        far_el3 != o.far_el3 || vbar_el3 != o.vbar_el3 || scr_el3 != o.scr_el3) return false;

    if (sctlr_el1 != o.sctlr_el1 || tcr_el1 != o.tcr_el1 || ttbr0_el1 != o.ttbr0_el1 ||
        ttbr1_el1 != o.ttbr1_el1 || mair_el1 != o.mair_el1 || cpacr_el1 != o.cpacr_el1 ||
        tpidr_el0 != o.tpidr_el0 || tpidr_el1 != o.tpidr_el1 || tpidrro_el0 != o.tpidrro_el0) return false;

    if (cntp_cval_el0 != o.cntp_cval_el0 || cntp_ctl_el0 != o.cntp_ctl_el0 ||
        cntv_cval_el0 != o.cntv_cval_el0 || cntv_ctl_el0 != o.cntv_ctl_el0) return false;

    if (debug != o.debug || pmu != o.pmu) return false;

    return true;
}

} // namespace winios
