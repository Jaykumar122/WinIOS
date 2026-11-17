#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::uint64_t kTrapFrameMagic = 0x5452415046524D45ULL; // "TRAPFRME"

#pragma pack(push, 1)
// 304-byte packed ARM64 architectural trap frame for EL0 <-> EL1 transitions
struct ArmTrapFrame {
    std::uint64_t magic{kTrapFrameMagic}; // 0x000: Magic
    std::uint64_t gpr[31]{0};            // 0x008 - 0x0FF: X0..X30 (248 bytes)
    std::uint64_t sp_el0{0};             // 0x100: User stack pointer
    std::uint64_t elr_el1{0};            // 0x108: Exception Link Register (PC to return)
    std::uint64_t spsr_el1{0};           // 0x110: Saved Process Status Register
    std::uint64_t esr_el1{0};            // 0x118: Exception Syndrome Register
    std::uint64_t far_el1{0};            // 0x120: Fault Address Register
    std::uint64_t padding{0};            // 0x128: 16-byte alignment padding (total 304 bytes)
};
#pragma pack(pop)

static_assert(sizeof(ArmTrapFrame) == 304, "ArmTrapFrame must be exactly 304 bytes");

class TrapFrameManager {
public:
    // Captures full architectural state from CPU into trap frame
    static void capture_from_cpu(const CPU& cpu, ArmTrapFrame& frame);

    // Restores full architectural state from trap frame to CPU
    static void restore_to_cpu(CPU& cpu, const ArmTrapFrame& frame);

    // Write trap frame to guest physical memory
    static bool write_to_memory(Memory& mem, std::uint64_t addr, const ArmTrapFrame& frame);

    // Read trap frame from guest physical memory
    static bool read_from_memory(const Memory& mem, std::uint64_t addr, ArmTrapFrame& frame);

    // Emits authentic ARM64 machine code routine for EL0 trap entry (saving full state)
    [[nodiscard]] static std::vector<std::uint32_t> emit_el0_trap_entry();

    // Emits authentic ARM64 machine code routine for EL0 trap exit (restoring full state & ERET)
    [[nodiscard]] static std::vector<std::uint32_t> emit_el0_trap_exit();
};

} // namespace winios::kernel
