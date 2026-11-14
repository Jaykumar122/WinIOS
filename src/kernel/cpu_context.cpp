#include "kernel/cpu_context.h"
#include <cstring>

namespace winios::kernel {

void ArchitecturalCpuContext::capture(const CPU& cpu) {
    for (std::size_t i = 0; i < 31; ++i) {
        gpr[i] = cpu.read_register(static_cast<std::uint32_t>(i));
    }
    pc = cpu.get_program_counter();
    sp_el0 = cpu.get_sp_el0();
    sp_el1 = cpu.get_sp_el1();
    nzcv = cpu.get_nzcv();
    daif = cpu.get_daif();
    spsr_el1 = (cpu.get_spsr_el1() & ~0xF00003C0ULL) |
               (static_cast<std::uint64_t>(nzcv & 0xFu) << 28) |
               (static_cast<std::uint64_t>(daif & 0xFu) << 6);
    tpidr_el0 = cpu.get_tpidr_el0();
    tpidr_el1 = cpu.get_tpidr_el1();
    ttbr0_el1 = cpu.get_ttbr0_el1();
    asid = (ttbr0_el1 >> 48) & 0xFFFFULL;
    cpacr_el1 = cpu.get_cpacr_el1();
    fpcr = cpu.get_fpcr();
    fpsr = cpu.get_fpsr();

    for (std::size_t i = 0; i < 32; ++i) {
        const auto v = cpu.read_vector_register(i);
        q_regs[i].low = v.low;
        q_regs[i].high = v.high;
    }
}

void ArchitecturalCpuContext::restore(CPU& cpu) const {
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(static_cast<std::uint32_t>(i), gpr[i]);
    }
    cpu.set_program_counter(pc);
    cpu.set_elr_el1(pc);
    cpu.set_sp_el0(sp_el0);
    cpu.set_sp_el1(sp_el1);
    cpu.set_spsr_el1(static_cast<std::uint32_t>(spsr_el1));
    cpu.set_nzcv(static_cast<std::uint8_t>(nzcv & 0xFu));
    cpu.set_daif(static_cast<std::uint8_t>(daif & 0xFu));
    cpu.set_tpidr_el0(tpidr_el0);
    cpu.set_tpidr_el1(tpidr_el1);
    cpu.set_ttbr0_el1(ttbr0_el1);
    cpu.set_cpacr_el1(cpacr_el1 != 0 ? cpacr_el1 : 0x00300000ULL);
    cpu.set_fpcr(fpcr);
    cpu.set_fpsr(fpsr);

    for (std::size_t i = 0; i < 32; ++i) {
        cpu.write_vector_register(i, q_regs[i].low, q_regs[i].high);
    }
}

bool ArchitecturalCpuContext::equals(const ArchitecturalCpuContext& other) const noexcept {
    for (std::size_t i = 0; i < 31; ++i) {
        if (gpr[i] != other.gpr[i]) {
            return false;
        }
    }
    if (pc != other.pc) return false;
    if (sp_el0 != other.sp_el0) return false;
    if (sp_el1 != other.sp_el1) return false;
    if (spsr_el1 != other.spsr_el1) return false;
    if (nzcv != other.nzcv) return false;
    if (daif != other.daif) return false;
    if (tpidr_el0 != other.tpidr_el0) return false;
    if (tpidr_el1 != other.tpidr_el1) return false;
    if (asid != other.asid) return false;
    if (ttbr0_el1 != other.ttbr0_el1) return false;
    if (cpacr_el1 != other.cpacr_el1) return false;
    if (fpcr != other.fpcr) return false;
    if (fpsr != other.fpsr) return false;

    for (std::size_t i = 0; i < 32; ++i) {
        if (q_regs[i] != other.q_regs[i]) return false;
    }
    return true;
}

bool ArchitecturalCpuContext::write_frame_to_memory(Memory& mem, std::uint64_t guest_addr) const {
    if (guest_addr + sizeof(ExceptionFrame) > mem.size()) {
        return false;
    }

    ExceptionFrame frame{};
    frame.magic = kExceptionFrameMagic;
    frame.frame_size = sizeof(ExceptionFrame);
    frame.elr_el1 = pc;
    // Embed NZCV [31:28] and DAIF [9:6] into SPSR_EL1
    frame.spsr_el1 = (spsr_el1 & ~0xF00003C0ULL) |
                     (static_cast<std::uint64_t>(nzcv & 0xFu) << 28) |
                     (static_cast<std::uint64_t>(daif & 0xFu) << 6);
    frame.sp_el0 = sp_el0;
    frame.sp_el1 = sp_el1;
    frame.tpidr_el0 = tpidr_el0;
    frame.tpidr_el1 = tpidr_el1;
    frame.asid = asid;
    frame.ttbr0_el1 = ttbr0_el1;
    frame.fpcr = fpcr;
    frame.fpsr = fpsr;
    frame.simd_saved = 1;

    for (std::size_t i = 0; i < 31; ++i) {
        frame.gpr[i] = gpr[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        frame.q_regs[i] = q_regs[i];
    }

    auto* dest = mem.raw_data() + guest_addr;
    std::memcpy(dest, &frame, sizeof(ExceptionFrame));
    return true;
}

bool ArchitecturalCpuContext::read_frame_from_memory(const Memory& mem, std::uint64_t guest_addr) {
    if (guest_addr + sizeof(ExceptionFrame) > mem.size()) {
        return false;
    }

    ExceptionFrame frame{};
    const auto* src = mem.raw_data() + guest_addr;
    std::memcpy(&frame, src, sizeof(ExceptionFrame));

    if (frame.magic != kExceptionFrameMagic) {
        return false;
    }

    pc = frame.elr_el1;
    spsr_el1 = frame.spsr_el1;
    nzcv = static_cast<std::uint32_t>((frame.spsr_el1 >> 28) & 0xFu);
    daif = static_cast<std::uint32_t>((frame.spsr_el1 >> 6) & 0xFu);
    sp_el0 = frame.sp_el0;
    sp_el1 = frame.sp_el1;
    tpidr_el0 = frame.tpidr_el0;
    tpidr_el1 = frame.tpidr_el1;
    asid = frame.asid;
    ttbr0_el1 = frame.ttbr0_el1;
    cpacr_el1 = 0x00300000ULL;
    fpcr = frame.fpcr;
    fpsr = frame.fpsr;

    for (std::size_t i = 0; i < 31; ++i) {
        gpr[i] = frame.gpr[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        q_regs[i] = frame.q_regs[i];
    }
    return true;
}

} // namespace winios::kernel
