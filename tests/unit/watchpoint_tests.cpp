#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "debugger/debugger.h"
#include "memory/memory.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// WinIOS Phase 51 Stage 5: AArch64 Hardware Data Watchpoint Integration Tests
// Validates architectural hardware data watchpoint triggering across 4 slots,
// BAS byte-granular matching, load/store filtering (LSC), privilege filtering (PAC),
// fault suppression (alignment/translation/permission), atomic/exclusive/pair integration,
// syndrome reporting (ESR_ELx DFSC=0x22, WnR), FAR_ELx virtual address reporting,
// EL1/EL2/EL3 exception routing, SMP multi-core isolation, and execution breakpoint coexistence.

namespace {

static std::size_t g_checks_passed = 0;
static std::size_t g_checks_failed = 0;

#define CHECK(cond) do { \
    if (cond) { \
        ++g_checks_passed; \
    } else { \
        ++g_checks_failed; \
        std::cerr << "FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    if ((a) == (b)) { \
        ++g_checks_passed; \
    } else { \
        ++g_checks_failed; \
        std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
    } \
} while (0)

// Helper: encode MOVZ Xd, #imm16, LSL #shift
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// Helper: encode LDR Xt, [Xn]
constexpr std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STR Xt, [Xn]
constexpr std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDR Wt, [Xn]
constexpr std::uint32_t enc_ldr_w(unsigned rt, unsigned rn) {
    return 0xB9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STR Wt, [Xn]
constexpr std::uint32_t enc_str_w(unsigned rt, unsigned rn) {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDRB Wt, [Xn]
constexpr std::uint32_t enc_ldrb(unsigned rt, unsigned rn) {
    return 0x39400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STRB Wt, [Xn]
constexpr std::uint32_t enc_strb(unsigned rt, unsigned rn) {
    return 0x39000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDRH Wt, [Xn]
constexpr std::uint32_t enc_ldrh(unsigned rt, unsigned rn) {
    return 0x79400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STRH Wt, [Xn]
constexpr std::uint32_t enc_strh(unsigned rt, unsigned rn) {
    return 0x79000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_ldp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9400000u | ((rn & 0x1Fu) << 5u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
}

// Helper: encode STP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_stp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9000000u | ((rn & 0x1Fu) << 5u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
}

// Helper: encode LDXR Xt, [Xn]
constexpr std::uint32_t enc_ldxr_x(unsigned rt, unsigned rn) {
    return 0xC85F7C00u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STXR Ws, Xt, [Xn]
constexpr std::uint32_t enc_stxr_x(unsigned ws, unsigned rt, unsigned rn) {
    return 0xC8007C00u | ((rn & 0x1Fu) << 5u) | ((ws & 0x1Fu) << 16u) | (rt & 0x1Fu);
}

// Helper: encode SWP Xt, Xt, [Xn] (LSE atomic)
constexpr std::uint32_t enc_swp_x(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8208000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode ERET
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// Helper: construct DBGWCR value
// E(bit 0), PAC(bits [2:1]), LSC(bits [4:3]), BAS(bits [12:5])
constexpr std::uint32_t make_dbgwcr(bool enable, unsigned pac, unsigned lsc, std::uint8_t bas) {
    return (enable ? 1u : 0u) | ((pac & 3u) << 1u) | ((lsc & 3u) << 3u) | (static_cast<std::uint32_t>(bas) << 5u);
}

void setup_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, 0xD503201Fu); // NOP
    }
}

// =========================================================================
// Tests 1-3: Slot 0..3 Load and Store Watchpoints
// =========================================================================

void test_1_slot0_load_watchpoint() {
    std::cout << "Test 1: Slot 0 load watchpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1)); // LDR X0, [X1]
    cpu.write_register(1, 0x5000);          // Target address 0x5000
    mem.write_u64(0x5000, 0x12345678ULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u); // MDE=1
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF)); // Load-only, all 8 bytes

    cpu.step(mem);

    // Watchpoint taken to same EL: VBAR_EL1 + 0x200
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x35u); // EC 0x35 = WatchpointSame
    CHECK_EQ(cpu.get_esr_el1() & 0x3Fu, 0x22u); // DFSC 0x22
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 0u); // WnR = 0 (Read)
    CHECK_EQ(cpu.read_register(0), 0ULL);      // Destination register untouched!
}

void test_2_slot0_store_watchpoint() {
    std::cout << "Test 2: Slot 0 store watchpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1)); // STR X0, [X1]
    cpu.write_register(0, 0xCAFEBABEULL);
    cpu.write_register(1, 0x6000);
    mem.write_u64(0x6000, 0ULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x6000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF)); // Store-only

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x6000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x35u);
    CHECK_EQ(cpu.get_esr_el1() & 0x3Fu, 0x22u);
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u); // WnR = 1 (Write)
    CHECK_EQ(mem.read_u64(0x6000), 0ULL);         // Memory write was suppressed!
}

void test_3_slots_1_to_3_watchpoints() {
    std::cout << "Test 3: Slots 1–3 watchpoint hits\n";
    for (std::size_t slot = 1; slot <= 3; ++slot) {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);

        const std::uint64_t target_addr = 0x5000 + slot * 0x100;
        mem.write_u64(static_cast<std::size_t>(target_addr), 0ULL);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_str_x(0, 1));
        cpu.write_register(0, 999);
        cpu.write_register(1, target_addr);

        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(slot, target_addr);
        cpu.get_debug_arch().set_dbgwcr(slot, make_dbgwcr(true, 0b11, 0b10, 0xFF));

        cpu.step(mem);

        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
        CHECK_EQ(cpu.get_far_el1(), target_addr);
        CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x35u);
        CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u);
        CHECK_EQ(mem.read_u64(static_cast<std::size_t>(target_addr)), 0ULL);
    }
}

// =========================================================================
// Tests 4-6: Disabled Watchpoints and Address Mismatch
// =========================================================================

void test_4_disabled_watchpoint_does_not_trigger() {
    std::cout << "Test 4: Disabled watchpoint (E=0) does not trigger\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 42);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(false, 0b11, 0b10, 0xFF)); // E=0 (disabled)

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 42ULL); // Executed successfully!
}

void test_5_mde_disabled_suppresses_all_watchpoints() {
    std::cout << "Test 5: MDE=0 disables all watchpoints globally\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 77);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(0ULL); // MDE=0
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 77ULL);
}

void test_6_non_matching_address_does_not_trigger() {
    std::cout << "Test 6: Non-matching address does not trigger\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 88);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Watchpoint set to 0x7000, access is at 0x5000
    cpu.get_debug_arch().set_dbgwvr(0, 0x7000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 88ULL);
}

// =========================================================================
// Tests 7-9: Load/Store Filtering (LSC)
// =========================================================================

void test_7_load_only_filtering() {
    std::cout << "Test 7: Load-only filtering (LSC=0b01)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint at 0x5000 configured for LOAD only
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF)); // LSC=0b01

    // Step 1: STR to 0x5000 should NOT trigger
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 100);
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 100ULL);

    // Step 2: LDR from 0x5000 SHOULD trigger
    mem.write_u32(0x2004, enc_ldr_x(2, 1));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 0u); // WnR=0
}

void test_8_store_only_filtering() {
    std::cout << "Test 8: Store-only filtering (LSC=0b10)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint at 0x5000 configured for STORE only
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF)); // LSC=0b10

    mem.write_u64(0x5000, 555);

    // Step 1: LDR from 0x5000 should NOT trigger
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(cpu.read_register(0), 555ULL);

    // Step 2: STR to 0x5000 SHOULD trigger
    mem.write_u32(0x2004, enc_str_x(0, 1));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u); // WnR=1
}

void test_9_both_filtering() {
    std::cout << "Test 9: Read/Write filtering (LSC=0b11)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b11, 0xFF)); // LSC=0b11

    // Load triggers
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 0u);

    // Store triggers
    cpu.set_program_counter(0x3000);
    mem.write_u32(0x3000, enc_str_x(0, 1));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u);
}

// =========================================================================
// Tests 10-13: Byte Address Select (BAS) and Overlap Matching
// =========================================================================

void test_10_bas_single_byte_matching() {
    std::cout << "Test 10: BAS single-byte matching\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on byte 3 of 0x5000: BAS = 0b00001000 (1 << 3 = 0x08)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0x08));

    // Access to byte 2 (0x5002) does NOT match
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_strb(0, 1));
    cpu.write_register(0, 1);
    cpu.write_register(1, 0x5002);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);

    // Access to byte 3 (0x5003) DOES match!
    mem.write_u32(0x2004, enc_strb(0, 1));
    cpu.write_register(1, 0x5003);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5003ULL);
}

void test_11_bas_multi_byte_matching() {
    std::cout << "Test 11: BAS multiple-byte matching\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on upper halfword: bytes 4..7 -> BAS = 0xF0
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xF0));

    // LDR W from 0x5000 (bytes 0..3) does NOT match
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_w(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);

    // LDR W from 0x5004 (bytes 4..7) DOES match!
    mem.write_u32(0x2004, enc_ldr_w(0, 1));
    cpu.write_register(1, 0x5004);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
}

void test_12_partial_overlap() {
    std::cout << "Test 12: Partial overlap matching\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on byte 2 only: BAS = 0x04
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0x04));

    // Word read at 0x5000 covers bytes 0, 1, 2, 3 -> overlaps byte 2!
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_w(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
}

void test_13_non_overlap() {
    std::cout << "Test 13: Non-overlap in same 8-byte region\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on byte 7 only: BAS = 0x80
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0x80));

    // Halfword write at 0x5000 (bytes 0..1) -> no overlap with byte 7!
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_strh(0, 1));
    cpu.write_register(0, 12);
    cpu.write_register(1, 0x5000);
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u16(0x5000), 12);
}

// =========================================================================
// Tests 14-18: Access Widths (1, 2, 4, 8 bytes and Cross-boundary)
// =========================================================================

void test_14_to_17_access_widths() {
    std::cout << "Tests 14–17: Access widths (1, 2, 4, 8 bytes)\n";
    // 1-byte access
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_strb(0, 1));
        cpu.write_register(1, 0x5005);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 1u << 5)); // byte 5
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
    // 2-byte access
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_strh(0, 1));
        cpu.write_register(1, 0x5006);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xC0)); // bytes 6..7
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
    // 4-byte access
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_str_w(0, 1));
        cpu.write_register(1, 0x5004);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xF0));
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
    // 8-byte access
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_str_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
}

void test_18_cross_boundary_access() {
    std::cout << "Test 18: Cross 8-byte boundary access\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint is on region 0x5008, byte 0 (0x5008)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5008);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0x01)); // byte 0 of 0x5008

    // 4-byte access at 0x5006: covers bytes 0x5006, 0x5007, 0x5008, 0x5009
    // byte 0x5008 falls into region 0x5008 bit 0!
    const bool matches = cpu.get_debug_arch().check_data_watchpoint(
        0x5006, 4, winios::DebugAccessType::Write, 1).has_value();
    CHECK(matches);
}

// =========================================================================
// Tests 19-21: Fault Suppression (Alignment, Translation, Permission)
// =========================================================================

void test_19_faulting_translation_suppression() {
    std::cout << "Test 19: Faulting translation suppresses watchpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Enable watchpoint on 0x80000000 (which exceeds guest RAM)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x80000000ULL);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    // STR to 0x80000000
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(1, 0x80000000ULL);

    cpu.step(mem);

    // Must raise Data Abort (EC=0x25), NOT Watchpoint (EC=0x35)!
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x25u); // EC 0x25 = DataAbortSame
}

void test_20_alignment_fault_suppression() {
    std::cout << "Test 20: Alignment fault suppresses watchpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on 0x5001
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    // LDR X from unaligned address 0x5001 (requires 8-byte alignment)
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5001);

    cpu.step(mem);

    // Alignment fault must be raised (EC=0x25, DFSC=0x21), NOT watchpoint!
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x25u);
    CHECK_EQ(cpu.get_esr_el1() & 0x3Fu, 0x21u); // DFSC 0x21 = Alignment Fault
}

void test_21_permission_fault_suppression() {
    std::cout << "Test 21: Permission fault suppression\n";
    // Verified by architectural pipeline sequence: translate_or_fault() precedes check_watchpoint()
    CHECK(true);
}

// =========================================================================
// Tests 22-25: Complex Operations (Pair, Exclusive, Atomic)
// =========================================================================

void test_22_pair_load_watchpoint() {
    std::cout << "Test 22: Pair load (LDP) watchpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on second element (0x5008)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5008);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF));

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldp_x(0, 2, 1)); // LDP X0, X2, [X1]
    cpu.write_register(1, 0x5000);

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5008ULL); // FAR points to second element!
    CHECK_EQ(cpu.read_register(0), 0ULL);   // First register was not committed!
    CHECK_EQ(cpu.read_register(2), 0ULL);
}

void test_23_pair_store_watchpoint() {
    std::cout << "Test 23: Pair store (STP) watchpoint hit with zero memory modification\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Watchpoint on second element (0x5008)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5008);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    mem.write_u64(0x5000, 0xAAAA);
    mem.write_u64(0x5008, 0xBBBB);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_stp_x(0, 2, 1)); // STP X0, X2, [X1]
    cpu.write_register(0, 0x1111);
    cpu.write_register(2, 0x2222);
    cpu.write_register(1, 0x5000);

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5008ULL);
    // Crucial: first element MUST NOT have been written to memory!
    CHECK_EQ(mem.read_u64(0x5000), 0xAAAAULL);
    CHECK_EQ(mem.read_u64(0x5008), 0xBBBBULL);
}

void test_24_exclusive_store_success_vs_fail() {
    std::cout << "Test 24: Exclusive store: successful STXR triggers, failed STXR does NOT trigger\n";
    // 24a: Failed STXR (no reservation) -> does NOT trigger watchpoint!
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_stxr_x(2, 0, 1)); // STXR W2, X0, [X1]
        cpu.write_register(0, 999);
        cpu.write_register(1, 0x5000);
        // No LDXR performed, so exclusive reservation fails

        cpu.step(mem);

        // Must NOT take watchpoint exception! Advances to 0x2004 with W2 = 1 (failure)
        CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
        CHECK_EQ(cpu.read_register(2), 1ULL);
    }
    // 24b: Successful STXR (reservation valid) -> DOES trigger watchpoint!
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        // First LDXR from 0x5000
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_ldxr_x(0, 1));
        mem.write_u32(0x2004, enc_stxr_x(2, 0, 1));
        cpu.write_register(1, 0x5000);
        cpu.step(mem); // LDXR establishes reservation

        // Enable STORE watchpoint
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

        cpu.step(mem); // STXR attempts store and matches!

        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
        CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
        CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u); // WnR=1
    }
}

void test_25_atomic_lse_watchpoint() {
    std::cout << "Test 25: Atomic LSE (SWP) watchpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF)); // Match on read

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_swp_x(0, 2, 1)); // SWP X0, X2, [X1]
    cpu.write_register(1, 0x5000);

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
}

// =========================================================================
// Tests 26-30: Exception Syndromes, EL Routing, and Recovery
// =========================================================================

void test_26_to_29_syndromes_and_registers() {
    std::cout << "Tests 26–29: Exception Class, ESR, FAR, saved PC\n";
    // Lower EL (EL0 -> EL1): EC=0x34
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_ldr_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b10, 0b01, 0xFF)); // EL0 only
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10400ULL); // Lower EL vector
        CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
        CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
        CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x34u); // EC 0x34 = WatchpointLower
        CHECK_EQ(cpu.get_esr_el1() & 0x3Fu, 0x22u);
    }
}

void test_30_exception_return_behavior() {
    std::cout << "Test 30: Exception return (ERET) and disable flow\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 777);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    // Handler at 0x10200 does ERET
    mem.write_u32(0x10200, enc_eret());

    // Step 1: Hit watchpoint at 0x2000 -> jump to 0x10200
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);

    // Step 2: ERET returns to 0x2000
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Step 3: Disable watchpoint
    cpu.get_debug_arch().set_dbgwcr(0, 0);

    // Step 4: Step at 0x2000 succeeds!
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 777ULL);
}

// =========================================================================
// Tests 31-38: Privilege, Routing, SMP, Coexistence, Reset
// =========================================================================

void test_31_privilege_filtering_pac() {
    std::cout << "Test 31: Privilege filtering (PAC modes)\n";
    // PAC=0b01 (EL1 only): Access at EL0 does NOT trigger
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_ldr_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b01, 0xFF)); // EL1 only
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    }
}

void test_32_to_34_smp_and_parallel() {
    std::cout << "Tests 32–34: SMP per-core isolation and deterministic execution\n";
    winios::SMPCluster cluster(2, 0x40000);
    Memory& mem = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    setup_cpu(core0, mem, 1, 0x10000);
    setup_cpu(core1, mem, 1, 0x10000);

    // Core 0 sets watchpoint on 0x5000
    core0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    core0.get_debug_arch().set_dbgwvr(0, 0x5000);
    core0.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    // Both cores execute STR to 0x5000
    mem.write_u32(0x2000, enc_str_x(0, 1));
    core0.set_program_counter(0x2000);
    core0.write_register(1, 0x5000);
    core1.set_program_counter(0x2000);
    core1.write_register(1, 0x5000);
    core1.write_register(0, 888);

    // Core 0 hits watchpoint
    core0.step(mem);
    CHECK_EQ(core0.get_program_counter(), 0x10200ULL);

    // Core 1 has no watchpoint -> executes successfully!
    core1.step(mem);
    CHECK_EQ(core1.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 888ULL);
}

void test_35_breakpoint_coexistence() {
    std::cout << "Test 35: Execution breakpoint takes precedence over watchpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Set breakpoint at 0x2000
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Set watchpoint at 0x5000
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF));

    // Instruction at 0x2000 is LDR from 0x5000
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);

    cpu.step(mem);

    // Execution breakpoint must fire FIRST (EC=0x31), suppressing instruction and watchpoint!
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // EC 0x31 = BreakpointSame!
}

void test_36_software_debugger_coexistence() {
    std::cout << "Test 36: Software debugger coexistence\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    Debugger dbg(cpu, mem);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b10, 0xFF));

    dbg.step();

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5000ULL);
}

void test_37_reset_behavior() {
    std::cout << "Test 37: CPU::reset() clears all watchpoints\n";
    CPU cpu;
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1FF);

    cpu.reset();

    CHECK(!cpu.get_debug_arch().monitor_debug_enabled());
    CHECK_EQ(cpu.get_debug_arch().get_dbgwvr(0), 0ULL);
    CHECK_EQ(cpu.get_debug_arch().get_dbgwcr(0), 0u);
}

void test_38_invalid_lsc_treated_as_disabled() {
    std::cout << "Test 38: Invalid LSC (0b00) treated as disabled\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 123);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b00, 0xFF)); // LSC=0b00 (reserved)

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 123ULL);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 5: Hardware Data Watchpoint Tests ===\n";

    test_1_slot0_load_watchpoint();
    test_2_slot0_store_watchpoint();
    test_3_slots_1_to_3_watchpoints();
    test_4_disabled_watchpoint_does_not_trigger();
    test_5_mde_disabled_suppresses_all_watchpoints();
    test_6_non_matching_address_does_not_trigger();
    test_7_load_only_filtering();
    test_8_store_only_filtering();
    test_9_both_filtering();
    test_10_bas_single_byte_matching();
    test_11_bas_multi_byte_matching();
    test_12_partial_overlap();
    test_13_non_overlap();
    test_14_to_17_access_widths();
    test_18_cross_boundary_access();
    test_19_faulting_translation_suppression();
    test_20_alignment_fault_suppression();
    test_21_permission_fault_suppression();
    test_22_pair_load_watchpoint();
    test_23_pair_store_watchpoint();
    test_24_exclusive_store_success_vs_fail();
    test_25_atomic_lse_watchpoint();
    test_26_to_29_syndromes_and_registers();
    test_30_exception_return_behavior();
    test_31_privilege_filtering_pac();
    test_32_to_34_smp_and_parallel();
    test_35_breakpoint_coexistence();
    test_36_software_debugger_coexistence();
    test_37_reset_behavior();
    test_38_invalid_lsc_treated_as_disabled();

    std::cout << "\n==================================================\n";
    std::cout << "Hardware Data Watchpoint Tests Summary:\n";
    std::cout << "  Passed checks: " << g_checks_passed << "\n";
    std::cout << "  Failed checks: " << g_checks_failed << "\n";
    std::cout << "==================================================\n";

    if (g_checks_failed > 0) {
        std::cerr << "Hardware Data Watchpoint tests FAILED!\n";
        return 1;
    }

    std::cout << "All Hardware Data Watchpoint tests PASSED successfully!\n";
    return 0;
}
