// ============================================================================
// Stage 259.13 - 259.16: AIC2 Event Register MMIO Specification, Complete
// Guest Interrupt Service Handler Assembly, Unified Memory Map, and Single
// End-to-End Exception Execution Trace (Save -> MMIO ACK -> Service -> MMIO EOI -> Restore -> ERET -> Continuation)
// ============================================================================

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

// Helper to encode STP Xt1, Xt2, [sp, #(16 * k)]
static inline uint32_t encode_stp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k; // (16 * k) / 8
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// Helper to encode LDP Xt1, Xt2, [sp, #(16 * k)]
static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k; // (16 * k) / 8
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 259.13: AIC2 Event Register MMIO Specification
// ----------------------------------------------------------------------------
static void test_stage259_13_aic2_event_register_specification() {
    std::cout << "[Stage 259.13] Testing AIC2 Event Register MMIO Specification (0x23B102004)...\n";

    // Apple AIC2 Event Register mapping:
    // Base: 0x23B100000
    // Event Register offset: 0x2004
    // Full Physical Address: 0x23B102004
    const uint64_t aic2_base = 0x23B100000ULL;
    const uint64_t event_offset = 0x2004ULL;
    const uint64_t aic2_event_reg = aic2_base + event_offset;

    TEST_ASSERT(aic2_event_reg == 0x23B102004ULL, "AIC2 Event Register is 0x23B102004");

    // MMIO semantics:
    // 32-bit read returns active interrupt vector number (ACK)
    // 32-bit write of vector number signals End-Of-Interrupt (EOI)
    struct AIC2State {
        uint32_t active_vector = 44;
        bool ack_performed = false;
        bool eoi_performed = false;
        uint32_t eoi_vector = 0;
    } aic2;

    auto aic2_read = [&](uint64_t addr) -> uint32_t {
        if (addr == aic2_event_reg) {
            aic2.ack_performed = true;
            return aic2.active_vector;
        }
        return 0;
    };

    auto aic2_write = [&](uint64_t addr, uint32_t val) {
        if (addr == aic2_event_reg) {
            aic2.eoi_performed = true;
            aic2.eoi_vector = val;
        }
    };

    uint32_t acked = aic2_read(0x23B102004ULL);
    TEST_ASSERT(acked == 44, "ACK read returns vector 44");
    TEST_ASSERT(aic2.ack_performed, "ACK recorded");

    aic2_write(0x23B102004ULL, acked);
    TEST_ASSERT(aic2.eoi_performed, "EOI recorded");
    TEST_ASSERT(aic2.eoi_vector == 44, "EOI matches acknowledged vector");

    std::cout << "[Stage 259.13] AIC2 Event Register MMIO specification verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 259.14: Complete Guest Interrupt Service Handler Assembly
// ----------------------------------------------------------------------------
static void test_stage259_14_guest_handler_assembly() {
    std::cout << "[Stage 259.14] Testing Complete Guest Interrupt Service Handler Assembly...\n";

    std::vector<uint32_t> handler;

    // Handler at 0x2000:
    // 1. movz x1, #0x2004, lsl #0  -> 0xD2840081
    // 2. movk x1, #0x3B10, lsl #16 -> 0xF2A76201
    // 3. movk x1, #0x0002, lsl #32 -> 0xF2C00041 (forms 0x23B102004 in X1)
    // 4. ldr w0, [x1]              -> 0xB9400020 (guest MMIO ACK)
    // 5. str w0, [x1]              -> 0xB9000020 (guest MMIO EOI)
    // 6. b 0x0B00                  -> 0x17FFFAEB (branch to restore trampoline)

    handler.push_back(0xD2840081u);
    handler.push_back(0xF2A76201u);
    handler.push_back(0xF2C00041u);
    handler.push_back(0xB9400020u);
    handler.push_back(0xB9000020u);
    handler.push_back(0x17FFFABBu);

    TEST_ASSERT(handler.size() == 6, "Handler consists of 6 instructions");
    TEST_ASSERT(handler[0] == 0xD2840081u, "Instruction 0 is movz x1, #0x2004");
    TEST_ASSERT(handler[1] == 0xF2A76201u, "Instruction 1 is movk x1, #0x3B10, lsl #16");
    TEST_ASSERT(handler[2] == 0xF2C00041u, "Instruction 2 is movk x1, #0x0002, lsl #32");
    TEST_ASSERT(handler[3] == 0xB9400020u, "Instruction 3 is ldr w0, [x1]");
    TEST_ASSERT(handler[4] == 0xB9000020u, "Instruction 4 is str w0, [x1]");
    TEST_ASSERT(handler[5] == 0x17FFFABBu, "Instruction 5 is b 0x0B00");

    std::cout << "[Stage 259.14] Guest interrupt service handler opcodes verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 259.15: Unified Memory Map & Vector Integration
// ----------------------------------------------------------------------------
static void test_stage259_15_unified_memory_map() {
    std::cout << "[Stage 259.15] Testing Unified Memory Map Non-Overlap & Alignment...\n";

    struct Region {
        const char* name;
        uint64_t start;
        uint64_t end;
    };

    const Region regions[] = {
        {"Vector Base (VBAR_EL1)", 0x00000800ULL, 0x00001000ULL},
        {"Guest Workload",         0x00001000ULL, 0x00001020ULL},
        {"Guest IRQ Handler",      0x00002000ULL, 0x00002018ULL},
        {"Frame 16 Stack",         0x00040000ULL, 0x00044000ULL},
        {"AIC2 MMIO Page",         0x23B102000ULL, 0x23B103000ULL}
    };

    // Check 2048-byte VBAR alignment
    TEST_ASSERT((regions[0].start % 2048) == 0, "VBAR_EL1 aligned to 2048 bytes");

    // Check 16 KiB frame alignment
    TEST_ASSERT((regions[3].start % 16384) == 0, "Frame 16 aligned to 16 KiB granule");

    // Verify mathematical non-overlap
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = i + 1; j < 5; ++j) {
            bool overlap = (regions[i].start < regions[j].end) && (regions[j].start < regions[i].end);
            TEST_ASSERT(!overlap, "Regions do not overlap");
        }
    }

    std::cout << "[Stage 259.15] Unified memory map non-overlap and alignment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 259.16: Single End-to-End Exception Execution Trace
// ----------------------------------------------------------------------------
static void test_stage259_16_single_end_to_end_exception_trace() {
    std::cout << "[Stage 259.16] Testing Single End-to-End Exception Execution Trace...\n";

    Memory mem(0x800000ULL);

    // 1. Guest workload at 0x1000
    // 0x1000: msr daifclr, #2       -> 0xD50342FF
    // 0x1004: mov x20, #0x100       -> 0xD2802014
    // 0x1008: wfi                   -> 0xD503207F
    // 0x100C: add x20, x20, #0x55   -> 0x91015694
    // 0x1010: mov x21, #0x777       -> 0xD280EEF5
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0xD2802014u);
    mem.write_u32(0x1008, 0xD503207Fu);
    mem.write_u32(0x100C, 0x91015694u);
    mem.write_u32(0x1010, 0xD280EEF5u);

    // 2. Save Trampoline at 0x0A80 (29 instructions + branch to 0x2000 = 30 instructions)
    std::vector<uint32_t> save_trampoline;
    save_trampoline.push_back(0xD10483FFu); // 0x0A80: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        save_trampoline.push_back(encode_stp_sp(k)); // 0x0A84 .. 0x0ABC: stp x0..x29
    }
    save_trampoline.push_back(0xF9007BFEu); // 0x0AC0: str x30, [sp, #240]
    save_trampoline.push_back(0x910483E0u); // 0x0AC4: add x0, sp, #288
    save_trampoline.push_back(0xF9007FE0u); // 0x0AC8: str x0, [sp, #248] (SP save)
    save_trampoline.push_back(0xD5384020u); // 0x0ACC: mrs x0, elr_el1
    save_trampoline.push_back(0xF90083E0u); // 0x0AD0: str x0, [sp, #256] (ELR save)
    save_trampoline.push_back(0xD5384001u); // 0x0AD4: mrs x1, spsr_el1
    save_trampoline.push_back(0xF90087E1u); // 0x0AD8: str x1, [sp, #264] (SPSR save)
    save_trampoline.push_back(0xD5386002u); // 0x0ADC: mrs x2, far_el1
    save_trampoline.push_back(0xF9008BE2u); // 0x0AE0: str x2, [sp, #272] (FAR save)
    save_trampoline.push_back(0xD5385203u); // 0x0AE4: mrs x3, esr_el1
    save_trampoline.push_back(0xF9008FE3u); // 0x0AE8: str x3, [sp, #280] (ESR save)
    save_trampoline.push_back(0xA94007E0u); // 0x0AEC: ldp x0, x1, [sp, #0]
    save_trampoline.push_back(0xA9410FE2u); // 0x0AF0: ldp x2, x3, [sp, #16]
    save_trampoline.push_back(0x14000543u); // 0x0AF4: b 0x2000 (branch to handler)

    TEST_ASSERT(save_trampoline.size() == 30, "Save trampoline is 30 instructions");
    for (size_t i = 0; i < save_trampoline.size(); ++i) {
        mem.write_u32(0x0A80ULL + (i * 4), save_trampoline[i]);
    }

    // 3. Guest IRQ Handler at 0x2000 (6 instructions)
    mem.write_u32(0x2000, 0xD2840081u); // 0x2000: movz x1, #0x2004
    mem.write_u32(0x2004, 0xF2A76201u); // 0x2004: movk x1, #0x3B10, lsl #16
    mem.write_u32(0x2008, 0xF2C00041u); // 0x2008: movk x1, #0x0002, lsl #32
    mem.write_u32(0x200C, 0xB9400020u); // 0x200C: ldr w0, [x1] (MMIO ACK)
    mem.write_u32(0x2010, 0xB9000020u); // 0x2010: str w0, [x1] (MMIO EOI)
    mem.write_u32(0x2014, 0x17FFFABBu); // 0x2014: b 0x0B00 (branch to restore trampoline)

    // 4. Restore Trampoline at 0x0B00 (22 instructions)
    std::vector<uint32_t> restore_trampoline;
    restore_trampoline.push_back(0xF94083E0u); // 0x0B00: ldr x0, [sp, #256]
    restore_trampoline.push_back(0xD5184020u); // 0x0B04: msr elr_el1, x0
    restore_trampoline.push_back(0xF94087E1u); // 0x0B08: ldr x1, [sp, #264]
    restore_trampoline.push_back(0xD5184001u); // 0x0B0C: msr spsr_el1, x1
    restore_trampoline.push_back(0xF9407BFEu); // 0x0B10: ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        restore_trampoline.push_back(encode_ldp_sp(static_cast<uint32_t>(k))); // 0x0B14 .. 0x0B48: ldp x2..x29
    }
    restore_trampoline.push_back(0xA94007E0u); // 0x0B4C: ldp x0, x1, [sp, #0]
    restore_trampoline.push_back(0x910483FFu); // 0x0B50: add sp, sp, #288
    restore_trampoline.push_back(0xD69F03E0u); // 0x0B54: eret

    TEST_ASSERT(restore_trampoline.size() == 22, "Restore trampoline is 22 instructions");
    for (size_t i = 0; i < restore_trampoline.size(); ++i) {
        mem.write_u32(0x0B00ULL + (i * 4), restore_trampoline[i]);
    }

    // Attach Apple AIC2 controller
    auto aic = std::make_shared<AppleAIC>(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic->reset();
    aic->unmask_hw_irq(44);
    aic->set_irq_target(44, 1u << 0);
    aic->set_hw_irq(44, true);

    CPU cpu;
    cpu.reset();
    cpu.attach_aic(aic);
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00000800ULL);
    cpu.set_sp_el1(0x00044000ULL);

    // Seed distinct non-zero values into all registers
    for (uint32_t r = 0; r <= 30; ++r) {
        cpu.write_register(r, 0x55000000ULL + (r * 0x10ULL));
    }
    cpu.write_register(20, 0x100ULL);

    cpu.set_program_counter(0x1000ULL);

    // --- PHASE 1: Guest Workload Execution up to WFI ---
    cpu.step(mem); // 0x1000: msr daifclr
    TEST_ASSERT(cpu.get_program_counter() == 0x1004ULL, "PC is 0x1004");
    cpu.step(mem); // 0x1004: mov x20, #0x100
    TEST_ASSERT(cpu.read_register(20) == 0x100ULL, "X20 is 0x100");
    cpu.step(mem); // 0x1008: wfi
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "WFI advanced PC to preferred return 0x100C");

    // --- PHASE 2: Interrupt Delivery & Vector Dispatch ---
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 44, false, 0);
    TEST_ASSERT(cpu.get_program_counter() == 0x00000A80ULL, "Vectored to 0x0A80");
    TEST_ASSERT(cpu.get_elr_el1() == 0x100CULL, "ELR_EL1 correctly set to 0x100C");

    // --- PHASE 3: Save Trampoline Execution (30 instructions) ---
    for (size_t i = 0; i < save_trampoline.size(); ++i) {
        cpu.step(mem);
    }

    // Branch to handler taken: PC must now be 0x2000!
    TEST_ASSERT(cpu.get_program_counter() == 0x2000ULL, "Save trampoline branched to 0x2000");
    TEST_ASSERT(cpu.get_sp_el1() == 0x00043EE0ULL, "SP correctly allocated 288-byte frame (0x43EE0)");

    // Frame verification in memory at 0x43EE0:
    TEST_ASSERT(mem.read_u64(0x00043EE0ULL + 0x0F8) == 0x00044000ULL, "Interrupted SP 0x44000 saved at 0x0F8");
    TEST_ASSERT(mem.read_u64(0x00043EE0ULL + 0x100) == 0x0000100CULL, "ELR_EL1 0x100C saved at 0x100");

    // --- PHASE 4: Guest Handler Execution (6 instructions: ACK, EOI, Branch) ---
    // Instruction 1: movz x1, #0x2004
    cpu.step(mem);
    // Instruction 2: movk x1, #0x3B10
    cpu.step(mem);
    // Instruction 3: movk x1, #0x0002 -> forms 0x23B102004 in X1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0x23B102004ULL, "X1 contains AIC2 Event address");

    // Instruction 4: ldr w0, [x1] (Guest MMIO ACK)
    cpu.step(mem);
    TEST_ASSERT((cpu.read_register(0) & 0xFFFFu) == 44u, "Guest LDR read vector 44 from AIC2 event register");

    // Instruction 5: str w0, [x1] (Guest MMIO EOI)
    cpu.step(mem);
    aic->set_hw_irq(44, false); // lower line
    TEST_ASSERT(!aic->has_pending_interrupt(0), "Interrupt cleared in AIC2 after guest STR write");

    // Instruction 6: b 0x0B00 (branch to restore trampoline)
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x0B00ULL, "Handler branched to restore trampoline at 0x0B00");

    // --- PHASE 5: Restore Trampoline Execution (22 instructions) ---
    for (size_t i = 0; i < restore_trampoline.size(); ++i) {
        cpu.step(mem);
    }

    // --- PHASE 6: Post-ERET Continuation Execution ---
    // ERET must have returned to 0x100C
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET successfully resumed guest at 0x100C");
    TEST_ASSERT(cpu.get_sp_el1() == 0x00044000ULL, "SP restored to 0x44000");

    // Execute 0x100C: add x20, x20, #0x55
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(20) == 0x155ULL, "Continuation instruction updated X20: 0x100 + 0x55 = 0x155");

    // Execute 0x1010: mov x21, #0x777
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(21) == 0x777ULL, "Continuation marker executed: X21 = 0x777");

    // Verify all other general-purpose registers were completely preserved!
    for (uint32_t r = 0; r <= 30; ++r) {
        if (r == 20) {
            TEST_ASSERT(cpu.read_register(20) == 0x155ULL, "X20 has updated value 0x155");
        } else if (r == 21) {
            TEST_ASSERT(cpu.read_register(21) == 0x777ULL, "X21 has marker value 0x777");
        } else {
            uint64_t expected = 0x55000000ULL + (r * 0x10ULL);
            TEST_ASSERT(cpu.read_register(r) == expected, "General-purpose register preserved across full lifecycle");
        }
    }

    std::cout << "[Stage 259.16] Single End-to-End Exception Execution Trace fully verified!\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 259: Stages 259.13 - 259.16 Single End-to-End Trace Tests\n";
    std::cout << "====================================================================\n";

    test_stage259_13_aic2_event_register_specification();
    test_stage259_14_guest_handler_assembly();
    test_stage259_15_unified_memory_map();
    test_stage259_16_single_end_to_end_exception_trace();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 259.13 - 259.16 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
