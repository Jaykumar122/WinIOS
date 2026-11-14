#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/cpu_context.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

class ExceptionFrameManager {
public:
    // Pushes full 896-byte ExceptionFrame onto the active thread's kernel stack (sp_el1)
    // and updates the thread's saved SP_EL1 in its TCB.
    static bool save_exception_frame(CPU& cpu,
                                     Memory& mem,
                                     KernelThread* thread,
                                     std::uint64_t syndrome = 0,
                                     std::uint64_t fault_addr = 0);

    // Pops full 896-byte ExceptionFrame from the thread's kernel stack and applies
    // the restored registers, PC, PSTATE, NZCV, DAIF, SIMD/FP, and TLS to the CPU.
    static bool restore_exception_frame(CPU& cpu,
                                        const Memory& mem,
                                        KernelThread* thread);

    // Switches active execution from old_thread to new_thread using their kernel stack exception frames.
    static bool preemptive_switch(CPU& cpu,
                                  Memory& mem,
                                  KernelThread* old_thread,
                                  KernelThread* new_thread);

    // Emits machine code for complete guest-side general-purpose register preservation stub
    // Saves X0..X30 to stack, reads ICC_IAR1_EL1, writes ICC_EOIR1_EL1, restores X0..X30, ERET
    [[nodiscard]] static std::vector<std::uint32_t> emit_full_gpr_irq_stub(std::uint64_t counter_tick_addr = 0);
};

} // namespace winios::kernel
