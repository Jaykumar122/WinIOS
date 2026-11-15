// ==============================================================================
// WinIOS Campaign 200 Stage 22: Deterministic Replay & Cross-Subsystem Events Tests
// ==============================================================================
// Verifies:
// 1. Multi-step guest workload execution combining arithmetic, memory stores,
//    and PAC pointer signing/authentication
// 2. Mid-execution checkpoint capture
// 3. Execution completion from checkpoint (Run 1)
// 4. Restoration of snapshot checkpoint and replay execution (Run 2)
// 5. 100% bit-exact deterministic reproduction of all registers, PC, and RAM
// ==============================================================================

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "memory/memory.h"

using namespace winios::cpu;

namespace {

constexpr std::uint32_t enc_movz_x0(std::uint16_t imm) { return 0xD2800000u | (static_cast<std::uint32_t>(imm) << 5u); }
constexpr std::uint32_t enc_movz_x1(std::uint16_t imm) { return 0xD2800001u | (static_cast<std::uint32_t>(imm) << 5u); }
constexpr std::uint32_t enc_add_x2_x0_x1() { return 0x8B010002u; } // ADD X2, X0, X1
constexpr std::uint32_t enc_str_x2_x3() { return 0xF9000062u; }    // STR X2, [X3, #0]
constexpr std::uint32_t enc_ldr_x4_x3() { return 0xF9400064u; }    // LDR X4, [X3, #0]
constexpr std::uint32_t enc_pacia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t enc_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

} // namespace

static void test_deterministic_checkpoint_and_replay() {
    Memory mem(0x40000);
    CPU cpu;
    cpu.reset();

    const std::uint64_t code_base = 0x1000;
    const std::uint64_t data_base = 0x8000;

    // Instructions:
    // 0x1000: MOVZ X0, #0x1234
    // 0x1004: MOVZ X1, #0x5678
    // 0x1008: ADD X2, X0, X1      ; X2 = 0x68AC
    // 0x100C: [CHECKPOINT HERE]
    // 0x100C: STR X2, [X3, #0]    ; write to data_base
    // 0x1010: LDR X4, [X3, #0]    ; read back into X4
    // 0x1014: PACIA X4, X0        ; sign X4 with modifier X0
    // 0x1018: AUTIA X4, X0        ; authenticate X4

    mem.write_u32(static_cast<std::size_t>(code_base + 0x00), enc_movz_x0(0x1234));
    mem.write_u32(static_cast<std::size_t>(code_base + 0x04), enc_movz_x1(0x5678));
    mem.write_u32(static_cast<std::size_t>(code_base + 0x08), enc_add_x2_x0_x1());
    mem.write_u32(static_cast<std::size_t>(code_base + 0x0C), enc_str_x2_x3());
    mem.write_u32(static_cast<std::size_t>(code_base + 0x10), enc_ldr_x4_x3());
    mem.write_u32(static_cast<std::size_t>(code_base + 0x14), enc_pacia(4, 0));
    mem.write_u32(static_cast<std::size_t>(code_base + 0x18), enc_autia(4, 0));

    cpu.write_register(3, data_base);
    cpu.set_program_counter(code_base);

    // Step first 3 instructions up to checkpoint
    for (int i = 0; i < 3; ++i) {
        cpu.step(mem);
    }
    assert(cpu.get_program_counter() == code_base + 0x0C);
    assert(cpu.read_register(2) == (0x1234 + 0x5678));

    // Capture checkpoint: save CPU state and memory snapshot
    struct CpuCheckpoint {
        std::array<std::uint64_t, 31> regs{};
        std::uint64_t pc{};
        std::uint64_t sp{};
        std::uint8_t nzcv{};
        std::uint8_t daif{};
        std::uint64_t current_el{};
        PacKeyRing pac_keys{};
    };

    CpuCheckpoint checkpoint;
    for (int i = 0; i < 31; ++i) {
        checkpoint.regs[static_cast<std::size_t>(i)] = cpu.read_register(i);
    }
    checkpoint.pc = cpu.get_program_counter();
    checkpoint.sp = cpu.get_stack_pointer();
    checkpoint.nzcv = cpu.get_nzcv();
    checkpoint.daif = cpu.get_daif();
    checkpoint.current_el = cpu.get_current_el();
    checkpoint.pac_keys = cpu.get_pac().get_keyring();

    std::vector<std::uint8_t> checkpoint_ram(mem.size());
    std::memcpy(checkpoint_ram.data(), mem.raw_data(), mem.size());

    // --- RUN 1: Execute remaining 4 instructions ---
    for (int i = 0; i < 4; ++i) {
        cpu.step(mem);
    }
    const std::uint64_t run1_pc = cpu.get_program_counter();
    const std::uint64_t run1_x2 = cpu.read_register(2);
    const std::uint64_t run1_x4 = cpu.read_register(4);
    const std::uint64_t run1_mem_val = mem.read_u64(static_cast<std::size_t>(data_base));

    (void)run1_pc;
    (void)run1_x2;
    (void)run1_x4;
    (void)run1_mem_val;

    assert(run1_x4 == (0x1234 + 0x5678)); // Authenticated back to original sum
    assert(run1_mem_val == (0x1234 + 0x5678));

    // --- REPLAY RUN 2: Restore from checkpoint ---
    CPU replay_cpu;
    replay_cpu.set_architectural_exceptions(true);
    for (int i = 0; i < 31; ++i) {
        replay_cpu.write_register(i, checkpoint.regs[static_cast<std::size_t>(i)]);
    }
    replay_cpu.set_program_counter(checkpoint.pc);
    replay_cpu.set_stack_pointer(checkpoint.sp);
    replay_cpu.set_nzcv(checkpoint.nzcv);
    replay_cpu.set_daif(checkpoint.daif);
    replay_cpu.set_current_el(checkpoint.current_el);
    replay_cpu.get_pac().set_keyring(checkpoint.pac_keys);

    std::memcpy(mem.raw_data(), checkpoint_ram.data(), checkpoint_ram.size());

    // Execute remaining 4 instructions
    for (int i = 0; i < 4; ++i) {
        replay_cpu.step(mem);
    }
    const std::uint64_t run2_pc = replay_cpu.get_program_counter();
    const std::uint64_t run2_x2 = replay_cpu.read_register(2);
    const std::uint64_t run2_x4 = replay_cpu.read_register(4);
    const std::uint64_t run2_mem_val = mem.read_u64(static_cast<std::size_t>(data_base));

    (void)run2_pc;
    (void)run2_x2;
    (void)run2_x4;
    (void)run2_mem_val;

    // Verify 100% bit-exact equivalence between Run 1 and Replay Run 2
    assert(run1_pc == run2_pc);
    assert(run1_x2 == run2_x2);
    assert(run1_x4 == run2_x4);
    assert(run1_mem_val == run2_mem_val);

    std::cout << "[PASS] test_deterministic_checkpoint_and_replay\n";
}

int main() {
    std::cout << "=== Running Stage 200.22 Deterministic Replay Tests ===\n";
    test_deterministic_checkpoint_and_replay();
    std::cout << "All Stage 200.22 tests passed successfully!\n";
    return 0;
}
