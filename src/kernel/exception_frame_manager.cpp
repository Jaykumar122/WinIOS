#include "kernel/exception_frame_manager.h"
#include <cstring>

namespace winios::kernel {

bool ExceptionFrameManager::save_exception_frame(CPU& cpu,
                                                 Memory& mem,
                                                 KernelThread* thread,
                                                 std::uint64_t syndrome,
                                                 std::uint64_t fault_addr) {
    if (!thread) return false;

    // Use current SP_EL1 and allocate 896-byte frame
    std::uint64_t sp = cpu.get_sp_el1();
    if (sp < sizeof(ExceptionFrame)) return false;

    sp -= sizeof(ExceptionFrame);
    // Enforce 16-byte alignment
    sp &= ~0xFULL;

    if (sp + sizeof(ExceptionFrame) > mem.size()) return false;

    ArchitecturalCpuContext ctx{};
    ctx.capture(cpu);

    ExceptionFrame frame{};
    frame.magic = kExceptionFrameMagic;
    frame.frame_size = sizeof(ExceptionFrame);
    frame.elr_el1 = ctx.pc;
    frame.spsr_el1 = ctx.spsr_el1;
    frame.esr_el1 = syndrome != 0 ? syndrome : cpu.get_esr_el1();
    frame.far_el1 = fault_addr != 0 ? fault_addr : cpu.get_far_el1();
    frame.sp_el0 = ctx.sp_el0;
    frame.sp_el1 = sp + sizeof(ExceptionFrame); // Original SP before frame push
    frame.tpidr_el0 = ctx.tpidr_el0;
    frame.tpidr_el1 = ctx.tpidr_el1;
    frame.asid = ctx.asid;
    frame.ttbr0_el1 = ctx.ttbr0_el1;
    frame.fpcr = ctx.fpcr;
    frame.fpsr = ctx.fpsr;
    frame.exception_origin_el = static_cast<std::uint32_t>(cpu.get_current_el());
    frame.simd_saved = 1;

    for (std::size_t i = 0; i < 31; ++i) {
        frame.gpr[i] = ctx.gpr[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        frame.q_regs[i] = ctx.q_regs[i];
    }

    std::memcpy(mem.raw_data() + sp, &frame, sizeof(ExceptionFrame));

    // Update CPU SP_EL1 to the top of the pushed frame
    cpu.set_sp_el1(sp);

    // Update thread's TCB with saved SP_EL1 and state
    thread->tcb().sp_el1 = sp;
    thread->tcb().pc = ctx.pc;
    thread->tcb().spsr_el1 = ctx.spsr_el1;

    return true;
}

bool ExceptionFrameManager::restore_exception_frame(CPU& cpu,
                                                    const Memory& mem,
                                                    KernelThread* thread) {
    if (!thread) return false;

    const std::uint64_t sp = thread->tcb().sp_el1;
    if (sp + sizeof(ExceptionFrame) > mem.size()) {
        thread->load_context_to_cpu(cpu);
        return true;
    }

    ExceptionFrame frame{};
    std::memcpy(&frame, mem.raw_data() + sp, sizeof(ExceptionFrame));

    if (frame.magic != kExceptionFrameMagic) {
        // Initial thread launch before any exception frame has been pushed
        thread->load_context_to_cpu(cpu);
        return true;
    }

    ArchitecturalCpuContext ctx{};
    ctx.pc = frame.elr_el1;
    ctx.spsr_el1 = frame.spsr_el1;
    ctx.nzcv = static_cast<std::uint32_t>((frame.spsr_el1 >> 28) & 0xFu);
    ctx.daif = static_cast<std::uint32_t>((frame.spsr_el1 >> 6) & 0xFu);
    ctx.sp_el0 = frame.sp_el0;
    ctx.sp_el1 = frame.sp_el1; // Pop frame: restore original SP
    ctx.tpidr_el0 = frame.tpidr_el0;
    ctx.tpidr_el1 = frame.tpidr_el1;
    ctx.asid = frame.asid;
    ctx.ttbr0_el1 = frame.ttbr0_el1;
    ctx.cpacr_el1 = 0x00300000ULL;
    ctx.fpcr = frame.fpcr;
    ctx.fpsr = frame.fpsr;

    for (std::size_t i = 0; i < 31; ++i) {
        ctx.gpr[i] = frame.gpr[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        ctx.q_regs[i] = frame.q_regs[i];
    }

    ctx.restore(cpu);
    return true;
}

bool ExceptionFrameManager::preemptive_switch(CPU& cpu,
                                              Memory& mem,
                                              KernelThread* old_thread,
                                              KernelThread* new_thread) {
    if (!new_thread) return false;

    if (old_thread) {
        if (!save_exception_frame(cpu, mem, old_thread)) {
            return false;
        }
        if (old_thread->state() == ThreadState::Running) {
            old_thread->set_state(ThreadState::Ready);
        }
    }

    new_thread->set_state(ThreadState::Running);
    return restore_exception_frame(cpu, mem, new_thread);
}

std::vector<std::uint32_t> ExceptionFrameManager::emit_full_gpr_irq_stub(std::uint64_t counter_tick_addr) {
    std::vector<std::uint32_t> code = {
        // [00] sub sp, sp, #256
        0xD10403FFu,
        // Save X0..X30 (16 stores)
        0xA90007E0u, // [04] stp x0, x1, [sp, #0]
        0xA9010FE2u, // [08] stp x2, x3, [sp, #16]
        0xA90217E4u, // [0c] stp x4, x5, [sp, #32]
        0xA9031FE6u, // [10] stp x6, x7, [sp, #48]
        0xA90427E8u, // [14] stp x8, x9, [sp, #64]
        0xA9052FEAu, // [18] stp x10, x11, [sp, #80]
        0xA90637ECu, // [1c] stp x12, x13, [sp, #96]
        0xA9073FEEu, // [20] stp x14, x15, [sp, #112]
        0xA90847F0u, // [24] stp x16, x17, [sp, #128]
        0xA9094FF2u, // [28] stp x18, x19, [sp, #144]
        0xA90A57F4u, // [2c] stp x20, x21, [sp, #160]
        0xA90B5FF6u, // [30] stp x22, x23, [sp, #176]
        0xA90C67F8u, // [34] stp x24, x25, [sp, #192]
        0xA90D6FFAu, // [38] stp x26, x27, [sp, #208]
        0xA90E77FCu, // [3c] stp x28, x29, [sp, #224]
        0xF9007BFEu, // [40] str x30, [sp, #240]

        // Acknowledge interrupt in GICv3
        0xD538CC00u, // [44] mrs x0, ICC_IAR1_EL1
        0xD518CC20u  // [48] msr ICC_EOIR1_EL1, x0
    };

    if (counter_tick_addr != 0) {
        // Increment tick counter in RAM
        const auto high16 = static_cast<std::uint32_t>((counter_tick_addr >> 16) & 0xFFFF);
        const auto low16 = static_cast<std::uint32_t>(counter_tick_addr & 0xFFFF);
        code.push_back(0xD2A00002u | (high16 << 5)); // movz x2, #high16, lsl #16
        if (low16 != 0) {
            code.push_back(0xF2800002u | (low16 << 5)); // movk x2, #low16
        }
        code.push_back(0xF9400043u); // ldr x3, [x2]
        code.push_back(0x91000463u); // add x3, x3, #1
        code.push_back(0xF9000043u); // str x3, [x2]
    }

    // Restore X0..X30
    code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    code.push_back(0xA94E77FCu); // ldp x28, x29, [sp, #224]
    code.push_back(0xA94D6FFAu); // ldp x26, x27, [sp, #208]
    code.push_back(0xA94C67F8u); // ldp x24, x25, [sp, #192]
    code.push_back(0xA94B5FF6u); // ldp x22, x23, [sp, #176]
    code.push_back(0xA94A57F4u); // ldp x20, x21, [sp, #160]
    code.push_back(0xA9494FF2u); // ldp x18, x19, [sp, #144]
    code.push_back(0xA94847F0u); // ldp x16, x17, [sp, #128]
    code.push_back(0xA9473FEEu); // ldp x14, x15, [sp, #112]
    code.push_back(0xA94637ECu); // ldp x12, x13, [sp, #96]
    code.push_back(0xA9452FEAu); // ldp x10, x11, [sp, #80]
    code.push_back(0xA94427E8u); // ldp x8, x9, [sp, #64]
    code.push_back(0xA9431FE6u); // ldp x6, x7, [sp, #48]
    code.push_back(0xA94217E4u); // ldp x4, x5, [sp, #32]
    code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]

    // [xx] add sp, sp, #256
    code.push_back(0x910403FFu);

    // [xx] eret
    code.push_back(0xD69F03E0u);

    return code;
}

} // namespace winios::kernel
