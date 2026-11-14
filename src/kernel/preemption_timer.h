#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::uint32_t kVirtualTimerPpi = 27;
inline constexpr std::uint32_t kPhysicalTimerPpi = 30;
inline constexpr std::uint64_t kDefaultTimerInterval = 10000;

class PreemptionTimer {
public:
    // Emits the authentic 15-instruction ARM64 guest IRQ handler
    // Acknowledges via ICC_IAR1_EL1, resets timer, increments tick counter in RAM,
    // completes interrupt via ICC_EOIR1_EL1, and executes ERET.
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_irq_handler();

    // Writes vector table entries at VBAR_EL1:
    //   Offset 0x280: Current EL with SPx IRQ
    //   Offset 0x480: Lower EL using AArch64 IRQ
    static bool setup_vector_table(Memory& mem,
                                   std::uint64_t vbar_pa,
                                   std::uint64_t handler_pa);

    // Configures CPU virtual timer (PPI 27) and GICv3 CPU interface
    static void configure_virtual_timer(CPU& cpu,
                                        std::uint64_t interval = kDefaultTimerInterval);

    // Asserts a virtual timer tick interrupt into GICv3 and CPU
    static void assert_virtual_timer_tick(CPU& cpu);

    // Clears the virtual timer interrupt state
    static void clear_virtual_timer(CPU& cpu);
};

} // namespace winios::kernel
