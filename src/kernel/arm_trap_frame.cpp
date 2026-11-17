#include "kernel/arm_trap_frame.h"

#include <cstring>

namespace winios::kernel {

void TrapFrameManager::capture_from_cpu(const CPU& cpu, ArmTrapFrame& frame) {
    frame.magic = kTrapFrameMagic;
    for (std::size_t i = 0; i < 31; ++i) {
        frame.gpr[i] = cpu.read_register(static_cast<int>(i));
    }
    frame.sp_el0 = cpu.get_sp_el0();
    frame.elr_el1 = cpu.get_elr_el1();
    frame.spsr_el1 = cpu.get_spsr_el1();
    frame.esr_el1 = cpu.get_esr_el1();
    frame.far_el1 = cpu.get_far_el1();
    frame.padding = 0;
}

void TrapFrameManager::restore_to_cpu(CPU& cpu, const ArmTrapFrame& frame) {
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(static_cast<int>(i), frame.gpr[i]);
    }
    cpu.set_sp_el0(frame.sp_el0);
    cpu.set_elr_el1(frame.elr_el1);
    cpu.set_spsr_el1(static_cast<std::uint32_t>(frame.spsr_el1));
    cpu.set_esr_el1(static_cast<std::uint32_t>(frame.esr_el1));
    cpu.set_far_el1(frame.far_el1);
}

bool TrapFrameManager::write_to_memory(Memory& mem, std::uint64_t addr, const ArmTrapFrame& frame) {
    if (addr + sizeof(ArmTrapFrame) > mem.size()) {
        return false;
    }
    const auto* src = reinterpret_cast<const std::uint8_t*>(&frame);
    for (std::size_t i = 0; i < sizeof(ArmTrapFrame); ++i) {
        mem.write_u8(static_cast<std::size_t>(addr + i), src[i]);
    }
    return true;
}

bool TrapFrameManager::read_from_memory(const Memory& mem, std::uint64_t addr, ArmTrapFrame& frame) {
    if (addr + sizeof(ArmTrapFrame) > mem.size()) {
        return false;
    }
    auto* dst = reinterpret_cast<std::uint8_t*>(&frame);
    for (std::size_t i = 0; i < sizeof(ArmTrapFrame); ++i) {
        dst[i] = mem.read_u8(static_cast<std::size_t>(addr + i));
    }
    return (frame.magic == kTrapFrameMagic);
}

std::vector<std::uint32_t> TrapFrameManager::emit_el0_trap_entry() {
    return {
        // Assume X0 contains pointer to target ArmTrapFrame
        0xA9010801u, // [00] stp x1, x2, [x0, #0x10]
        0xA9021003u, // [04] stp x3, x4, [x0, #0x20]
        0xA9031805u, // [08] stp x5, x6, [x0, #0x30]
        0xA9042007u, // [0c] stp x7, x8, [x0, #0x40]
        0xA9052809u, // [10] stp x9, x10, [x0, #0x50]
        0xA906300Bu, // [14] stp x11, x12, [x0, #0x60]
        0xA907380Du, // [18] stp x13, x14, [x0, #0x70]
        0xA908400Fu, // [1c] stp x15, x16, [x0, #0x80]
        0xA9094811u, // [20] stp x17, x18, [x0, #0x90]
        0xA90A5013u, // [24] stp x19, x20, [x0, #0xa0]
        0xA90B5815u, // [28] stp x21, x22, [x0, #0xb0]
        0xA90C6017u, // [2c] stp x23, x24, [x0, #0xc0]
        0xA90D6819u, // [30] stp x25, x26, [x0, #0xd0]
        0xA90E701Bu, // [34] stp x27, x28, [x0, #0xe0]
        0xA90F781Du, // [38] stp x29, x30, [x0, #0xf0]
        // Save special registers
        0xD5384101u, // [3c] mrs x1, sp_el0
        0xF9008001u, // [40] str x1, [x0, #0x100]
        0xD5384021u, // [44] mrs x1, elr_el1
        0xF9008401u, // [48] str x1, [x0, #0x108]
        0xD5384001u, // [4c] mrs x1, spsr_el1
        0xF9008801u, // [50] str x1, [x0, #0x110]
        0xD5385201u, // [54] mrs x1, esr_el1
        0xF9008C01u, // [58] str x1, [x0, #0x118]
        0xD5386001u, // [5c] mrs x1, far_el1
        0xF9009001u, // [60] str x1, [x0, #0x120]
        // Restore original X1
        0xF9400801u, // [64] ldr x1, [x0, #0x10]
        0xD65F03C0u  // [68] ret
    };
}

std::vector<std::uint32_t> TrapFrameManager::emit_el0_trap_exit() {
    return {
        // Assume X0 contains pointer to target ArmTrapFrame
        // Restore special registers
        0xF9408001u, // [00] ldr x1, [x0, #0x100] (sp_el0)
        0xD5184101u, // [04] msr sp_el0, x1
        0xF9408401u, // [08] ldr x1, [x0, #0x108] (elr_el1)
        0xD5184021u, // [0c] msr elr_el1, x1
        0xF9408801u, // [10] ldr x1, [x0, #0x110] (spsr_el1)
        0xD5184001u, // [14] msr spsr_el1, x1
        // Restore GPRs X2..X30
        0xA9411002u, // [18] ldp x2, x3, [x0, #0x18]
        0xA9422004u, // [1c] ldp x4, x5, [x0, #0x28]
        0xA9433006u, // [20] ldp x6, x7, [x0, #0x38]
        0xA9444008u, // [24] ldp x8, x9, [x0, #0x48]
        0xA945500Au, // [28] ldp x10, x11, [x0, #0x58]
        0xA946600Cu, // [2c] ldp x12, x13, [x0, #0x68]
        0xA947700Eu, // [30] ldp x14, x15, [x0, #0x78]
        0xA9480010u, // [34] ldp x16, x17, [x0, #0x88]
        0xA9491012u, // [38] ldp x18, x19, [x0, #0x98]
        0xA94A2014u, // [3c] ldp x20, x21, [x0, #0xa8]
        0xA94B3016u, // [40] ldp x22, x23, [x0, #0xb8]
        0xA94C4018u, // [44] ldp x24, x25, [x0, #0xc8]
        0xA94D501Au, // [48] ldp x26, x27, [x0, #0xd8]
        0xA94E601Cu, // [4c] ldp x28, x29, [x0, #0xe8]
        0xF9407C1Eu, // [50] ldr x30, [x0, #0xf8]
        // Restore X1, then X0
        0xF9400801u, // [54] ldr x1, [x0, #0x10]
        0xF9400400u, // [58] ldr x0, [x0, #0x08]
        0xD69F03E0u  // [5c] eret
    };
}

} // namespace winios::kernel
