#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

class ContextSwitch {
public:
    // Emits the authentic 22-instruction ARM64 guest context-switch subroutine
    // Signature: void cpu_switch_to(GuestTcb* old_tcb, GuestTcb* new_tcb)
    // Register convention:
    //   X0: Pointer to outgoing GuestTcb (null if bootstrap/initial)
    //   X1: Pointer to incoming GuestTcb
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_switch_routine();

    // High-level kernel context switch between two threads on virtual CPU
    static bool switch_threads(KernelThread* old_thread,
                               KernelThread* new_thread,
                               CPU& cpu);

    // Executes the guest assembly context-switch routine directly on the virtual CPU
    static bool execute_guest_switch(Memory& mem,
                                     CPU& cpu,
                                     std::uint64_t routine_addr,
                                     std::uint64_t old_tcb_guest_addr,
                                     std::uint64_t new_tcb_guest_addr,
                                     std::uint64_t return_pc_for_old,
                                     std::size_t max_steps = 100);
};

} // namespace winios::kernel
