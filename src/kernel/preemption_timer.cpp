#include "kernel/preemption_timer.h"

namespace winios::kernel {

std::vector<std::uint32_t> PreemptionTimer::emit_guest_irq_handler() {
    return {
        0xA9BF07E0u, // [00] stp x0, x1, [sp, #-16]!
        0xA9BF0FE2u, // [04] stp x2, x3, [sp, #-16]!
        0xA9BF7BF0u, // [08] stp x16, x30, [sp, #-16]!
        0xD538CC00u, // [0c] mrs x0, ICC_IAR1_EL1
        0xD518CC20u, // [10] msr ICC_EOIR1_EL1, x0
        0xD284E201u, // [14] mov x1, #10000
        0xD51BE301u, // [18] msr CNTV_TVAL_EL0, x1
        0xD2A00082u, // [1c] movz x2, #0x4, lsl #16 (0x40000)
        0xF9400043u, // [20] ldr x3, [x2]
        0x91000463u, // [24] add x3, x3, #1
        0xF9000043u, // [28] str x3, [x2]
        0xA8C17BF0u, // [2c] ldp x16, x30, [sp], #16
        0xA8C10FE2u, // [30] ldp x2, x3, [sp], #16
        0xA8C107E0u, // [34] ldp x0, x1, [sp], #16
        0xD69F03E0u  // [38] eret
    };
}

bool PreemptionTimer::setup_vector_table(Memory& mem,
                                        std::uint64_t vbar_pa,
                                        std::uint64_t handler_pa) {
    if (vbar_pa + 0x800 > mem.size() || handler_pa >= mem.size()) {
        return false;
    }

    // Offset 0x280: Current EL with SPx IRQ
    const std::int64_t diff1 = static_cast<std::int64_t>(handler_pa) - static_cast<std::int64_t>(vbar_pa + 0x280);
    const std::uint32_t imm26_1 = static_cast<std::uint32_t>((diff1 >> 2) & 0x03FFFFFF);
    const std::uint32_t b_instr1 = 0x14000000u | imm26_1;
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x280), b_instr1);

    // Offset 0x480: Lower EL using AArch64 IRQ
    const std::int64_t diff2 = static_cast<std::int64_t>(handler_pa) - static_cast<std::int64_t>(vbar_pa + 0x480);
    const std::uint32_t imm26_2 = static_cast<std::uint32_t>((diff2 >> 2) & 0x03FFFFFF);
    const std::uint32_t b_instr2 = 0x14000000u | imm26_2;
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x480), b_instr2);

    return true;
}

void PreemptionTimer::configure_virtual_timer(CPU& cpu, std::uint64_t interval) {
    auto& gic = cpu.get_gic();
    const auto core = static_cast<std::uint32_t>(cpu.get_core_id());

    // 1. Enable GIC Distributor
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 3, 4);

    // 2. Configure GIC Redistributor for core: enable PPI 27 in Group 1
    const std::uint64_t gicr_sgi_base = GICv3::kGicrBase + core * GICv3::kGicrCoreStride + 0x10000;
    gic.write_mmio(gicr_sgi_base + 0x0080, (1u << kVirtualTimerPpi), 4); // IGROUPR0: Group 1
    gic.write_mmio(gicr_sgi_base + 0x0100, (1u << kVirtualTimerPpi), 4); // ISENABLER0: Enable PPI 27

    // 3. Configure GIC CPU interface: priority mask and group 1 enable
    gic.write_icc_pmr(0xF0, core);
    gic.write_icc_igrpen1(1, core);

    // 4. Configure virtual timer state
    CPU::TimerState t = cpu.get_virt_timer();
    t.enable = true;
    t.imask = false;
    t.istatus = false;
    t.cval = interval;
    cpu.set_virt_timer(t);
    cpu.set_guest_counter(0);
    cpu.set_cntfrq_el0(1000000); // 1 MHz nominal virtual counter frequency
}

void PreemptionTimer::assert_virtual_timer_tick(CPU& cpu) {
    CPU::TimerState t = cpu.get_virt_timer();
    t.enable = true;
    t.imask = false;
    t.cval = 100;
    cpu.set_virt_timer(t);
    cpu.set_guest_counter(200); // 200 >= 100 ensures timer expiry
    cpu.check_timer_status();
}

void PreemptionTimer::clear_virtual_timer(CPU& cpu) {
    CPU::TimerState t = cpu.get_virt_timer();
    t.cval = cpu.get_guest_counter() + kDefaultTimerInterval;
    t.istatus = false;
    cpu.set_virt_timer(t);

    cpu.get_gic().set_ppi_level(kVirtualTimerPpi, false, static_cast<std::uint32_t>(cpu.get_core_id()));
}

} // namespace winios::kernel
