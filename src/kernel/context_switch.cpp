#include "kernel/context_switch.h"

namespace winios::kernel {

std::vector<std::uint32_t> ContextSwitch::emit_guest_switch_routine() {
    return {
        0xB4000140u, // [00] cbz x0, .Lload_new (+40 bytes / 10 instructions)
        0x910003E2u, // [04] mov x2, sp
        0xF9001402u, // [08] str x2, [x0, #0x28]
        0xF900181Eu, // [0c] str x30, [x0, #0x30]
        0xA90DD013u, // [10] stp x19, x20, [x0, #0xd8]
        0xA90ED815u, // [14] stp x21, x22, [x0, #0xe8]
        0xA90FE017u, // [18] stp x23, x24, [x0, #0xf8]
        0xA910E819u, // [1c] stp x25, x26, [x0, #0x108]
        0xA911F01Bu, // [20] stp x27, x28, [x0, #0x118]
        0xA912F81Du, // [24] stp x29, x30, [x0, #0x128]
        // .Lload_new:
        0xF9401422u, // [28] ldr x2, [x1, #0x28]
        0x9100005Fu, // [2c] mov sp, x2
        0xA94DD033u, // [30] ldp x19, x20, [x1, #0xd8]
        0xA94ED835u, // [34] ldp x21, x22, [x1, #0xe8]
        0xA94FE037u, // [38] ldp x23, x24, [x1, #0xf8]
        0xA950E839u, // [3c] ldp x25, x26, [x1, #0x108]
        0xA951F03Bu, // [40] ldp x27, x28, [x1, #0x118]
        0xA952F83Du, // [44] ldp x29, x30, [x1, #0x128]
        0xF9401830u, // [48] ldr x16, [x1, #0x30]
        0x52800022u, // [4c] mov w2, #1
        0xB9001022u, // [50] str w2, [x1, #0x10]
        0xD61F0200u  // [54] br x16
    };
}

bool ContextSwitch::switch_threads(KernelThread* old_thread,
                                   KernelThread* new_thread,
                                   CPU& cpu) {
    if (!new_thread) {
        return false;
    }

    if (old_thread) {
        old_thread->save_context_from_cpu(cpu);
        if (old_thread->state() == ThreadState::Running) {
            old_thread->set_state(ThreadState::Ready);
        }
    }

    new_thread->set_state(ThreadState::Running);
    new_thread->load_context_to_cpu(cpu);
    return true;
}

bool ContextSwitch::execute_guest_switch(Memory& mem,
                                        CPU& cpu,
                                        std::uint64_t routine_addr,
                                        std::uint64_t old_tcb_guest_addr,
                                        std::uint64_t new_tcb_guest_addr,
                                        std::uint64_t return_pc_for_old,
                                        std::size_t max_steps) {
    if (new_tcb_guest_addr + sizeof(GuestTcb) > mem.size()) {
        return false;
    }

    // Read expected target PC from incoming thread's TCB at offset 0x30
    const std::uint64_t target_pc = mem.read_u64(static_cast<std::size_t>(new_tcb_guest_addr + 0x30));
    if ((target_pc & 0x3ULL) != 0) {
        return false; // Misaligned target PC
    }

    // Set parameters
    cpu.set_program_counter(routine_addr);
    cpu.write_register(0, old_tcb_guest_addr);
    cpu.write_register(1, new_tcb_guest_addr);
    cpu.write_register(30, return_pc_for_old);

    std::size_t steps = 0;
    while (cpu.get_program_counter() != target_pc && steps < max_steps) {
        cpu.step(mem);
        ++steps;
    }

    return (cpu.get_program_counter() == target_pc);
}

} // namespace winios::kernel
