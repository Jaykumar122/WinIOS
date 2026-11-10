// =============================================================================
// stage_v1_userspace_memory_model_tests.cpp
//
// Campaign 191-V Stage V1:
//   Verifies ARMv8-A EL0/EL1 Stage-1 16 KB MMU & Userspace Normal-Memory Model:
//     - Unaligned 16/32/64-bit LDR/STR on Normal memory (SCTLR_EL1.A = 0)
//     - Unaligned LDP/STP and 128-bit LDR Q/STR Q on Normal memory
//     - Unaligned reads/writes crossing a 16 KB page boundary between split PAs
//     - Page-crossing write fault atomicity (page 2 RO/unmapped -> zero partial store)
//     - Alignment faults preserved with MMU=ON & SCTLR_EL1.A=0 for:
//         * LDXR/STXR, LDXP/STXP, LDAR/STLR, CAS, LDADD
//         * Device memory (MAIR 0x00) unaligned LDR/STR and DC ZVA on Device
//         * SCTLR_EL1.A=1 on Normal memory
//         * SP alignment faults (SA0 at EL0, SA at EL1 -> EC=0x26)
//     - W^X enforcement via 16 KB pages + SCTLR_EL1.WXN=1
// =============================================================================

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;

namespace {

int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n"; \
            ++g_failed; \
        } \
    } while (0)

// Builds a minimal 16 KB-granule Stage-1 page table (T0SZ=28, 36-bit VA):
//   L2 at l2_pa (16 KB), L3 at l3_pa (16 KB) covering [0, 32 MB).
struct Stage1Table16K {
    std::uint64_t l2_pa{0x100000ULL};
    std::uint64_t l3_pa{0x104000ULL};

    void init(Memory& mem) const {
        for (std::uint64_t off = 0; off < 0x8000ULL; off += 8u) {
            mem.write_u64(static_cast<std::size_t>(l2_pa + off), 0ULL);
        }
        // L2[0] -> table descriptor pointing to L3 table
        mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);
    }

    // AttrIndx=0 (Normal 0xFF) or AttrIndx=1 (Device 0x00)
    // ap: 1=EL0+EL1 RW, 3=EL0+EL1 RO
    void map_page_16k(Memory& mem, std::uint64_t va, std::uint64_t pa,
                      std::uint8_t ap, bool uxn, bool pxn, bool is_device) const {
        const std::uint64_t l3_idx = (va >> 14u) & 0x7FFULL;
        const std::uint8_t attr_idx = is_device ? 1u : 0u;
        const std::uint64_t desc =
            (pa & 0x0000FFFFFFFFC000ULL)
            | 0x3ULL
            | (static_cast<std::uint64_t>(attr_idx) << 2u)
            | (static_cast<std::uint64_t>(ap & 0x3u) << 6u)
            | (1ULL << 10u) // AF=1
            | (uxn ? (1ULL << 54u) : 0ULL)
            | (pxn ? (1ULL << 53u) : 0ULL);
        mem.write_u64(static_cast<std::size_t>(l3_pa + l3_idx * 8u), desc);
    }

    void unmap_page_16k(Memory& mem, std::uint64_t va) const {
        const std::uint64_t l3_idx = (va >> 14u) & 0x7FFULL;
        mem.write_u64(static_cast<std::size_t>(l3_pa + l3_idx * 8u), 0ULL);
    }

    static void setup_cpu_el0(CPU& cpu, Memory& mem, const Stage1Table16K& pt,
                              std::uint64_t sctlr_val = 0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u) | (1ULL << 19u)) {
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_cpacr_el1(0x00300000ULL); // FPEN=11
        cpu.set_vbar_el1(0x00080000ULL);
        mem.write_u32(0x00080000ULL + 0x400u, 0xD503201Fu); // Lower EL sync vector NOP

        // T0SZ=28 (36-bit VA), TG0=0b10 (16KB), SH0=0b11, ORGN0=0b01, IRGN0=0b01
        const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
        cpu.get_mmu().set_tcr_el1(tcr);
        // Attr0 = 0xFF (Normal WB RW-allocate), Attr1 = 0x00 (Device nGnRnE)
        cpu.get_mmu().set_mair_el1(0x00FFULL);
        cpu.get_mmu().set_ttbr0_el1(pt.l2_pa);
        cpu.get_mmu().invalidate_tlb_all();
        cpu.set_sctlr_el1(sctlr_val);
        cpu.set_current_el(0);
    }
};

void run_one(CPU& cpu, Memory& mem, std::uint64_t pc_va, std::uint64_t code_pa, std::uint32_t insn) {
    mem.write_u32(static_cast<std::size_t>(code_pa), insn);
    cpu.set_program_counter(pc_va);
    cpu.step(mem);
}

void test_unaligned_normal_memory_and_split_page_crossing() {
    std::cout << "[Test 1] Unaligned Normal-memory scalar, pair, SIMD & split-PA 16KB crossing...\n";
    Memory mem(0x200000); // 2 MB RAM
    Stage1Table16K pt;
    pt.init(mem);

    // Code page VA 0x0000 -> PA 0x0000 (RO + UXN=0)
    pt.map_page_16k(mem, 0x0000, 0x0000, 3u, false, false, false);
    // Split physical pages for virtual range [0x4000, 0xC000):
    //   VA 0x4000 -> PA 0x50000 (RW, UXN=1, Normal)
    //   VA 0x8000 -> PA 0x20000 (RW, UXN=1, Normal)  <-- non-contiguous PA!
    pt.map_page_16k(mem, 0x4000, 0x50000, 1u, true, true, false);
    pt.map_page_16k(mem, 0x8000, 0x20000, 1u, true, true, false);

    CPU cpu;
    Stage1Table16K::setup_cpu_el0(cpu, mem, pt);

    // 1. Unaligned 16/32/64-bit store & load within VA 0x4000 (odd offset 0x4003)
    cpu.write_register(0, 0x0102030405060708ULL);
    cpu.write_register(1, 0x4003ULL);
    run_one(cpu, mem, 0x0100, 0x0100, 0xF9000020u); // STR X0, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.get_program_counter() == 0x0104);

    cpu.write_register(2, 0ULL);
    run_one(cpu, mem, 0x0104, 0x0104, 0xF9400022u); // LDR X2, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(2) == 0x0102030405060708ULL);

    // Verify exact little-endian bytes in backing PA 0x50003..0x5000A
    CHECK(mem.read_u8(0x50003) == 0x08u);
    CHECK(mem.read_u8(0x5000A) == 0x01u);

    // 2. Unaligned 16-bit (STRH/LDRH) and 32-bit (STR W/LDR W) at odd +1
    cpu.write_register(3, 0xCAFEBABEu);
    cpu.write_register(1, 0x4021ULL);
    run_one(cpu, mem, 0x0108, 0x0108, 0xB9000023u); // STR W3, [X1]
    run_one(cpu, mem, 0x010C, 0x010C, 0xB9400024u); // LDR W4, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(4) == 0xCAFEBABEu);

    cpu.write_register(5, 0xBEEFu);
    run_one(cpu, mem, 0x0110, 0x0110, 0x79000025u); // STRH W5, [X1]
    run_one(cpu, mem, 0x0114, 0x0114, 0x79400026u); // LDRH W6, [X1]
    CHECK(cpu.read_register(6) == 0xBEEFu);

    // 3. Unaligned STP / LDP at 0x4005
    cpu.write_register(7, 0x1122334455667788ULL);
    cpu.write_register(8, 0x99AABBCCDDEEFF00ULL);
    cpu.write_register(1, 0x4005ULL);
    run_one(cpu, mem, 0x0118, 0x0118, 0xA9002027u); // STP X7, X8, [X1]
    cpu.write_register(9, 0ULL);
    cpu.write_register(10, 0ULL);
    run_one(cpu, mem, 0x011C, 0x011C, 0xA9402829u); // LDP X9, X10, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(9) == 0x1122334455667788ULL);
    CHECK(cpu.read_register(10) == 0x99AABBCCDDEEFF00ULL);

    // 4. Unaligned 128-bit STR Q0 / LDR Q1 at 0x4003
    mem.write_u64(0x50100, 0xAAAABBBBCCCCDDDDULL);
    mem.write_u64(0x50108, 0x123456789ABCDEF0ULL);
    cpu.write_register(11, 0x4100ULL);
    run_one(cpu, mem, 0x0120, 0x0120, 0x3DC00160u); // LDR Q0, [X11]
    cpu.write_register(12, 0x4203ULL); // odd 3-byte offset!
    run_one(cpu, mem, 0x0124, 0x0124, 0x3D800180u); // STR Q0, [X12]
    CHECK(cpu.get_current_el() == 0);
    CHECK(mem.read_u8(0x50203) == 0xDDu);
    CHECK(mem.read_u8(0x50212) == 0x12u);
    run_one(cpu, mem, 0x0128, 0x0128, 0x3DC00181u); // LDR Q1, [X12]
    CHECK(cpu.get_current_el() == 0);
    // Store Q1 cleanly aligned at 0x4300 to inspect recovered 128-bit register
    cpu.write_register(13, 0x4300ULL);
    run_one(cpu, mem, 0x012C, 0x012C, 0x3D8001A1u); // STR Q1, [X13]
    CHECK(mem.read_u64(0x50300) == 0xAAAABBBBCCCCDDDDULL);
    CHECK(mem.read_u64(0x50308) == 0x123456789ABCDEF0ULL);

    // 5. Unaligned 64-bit STR/LDR & 128-bit STR Q/LDR Q CROSSING 16KB boundary 0x7FFD..0x800C
    //    between split PAs (PA1 ends at 0x53FFF, PA2 starts at 0x20000)
    cpu.write_register(0, 0xA1B2C3D4E5F60718ULL);
    cpu.write_register(1, 0x7FFDULL); // 3 bytes in PA 0x53FFD..0x53FFF, 5 bytes in PA 0x20000..0x20004
    run_one(cpu, mem, 0x0130, 0x0130, 0xF9000020u); // STR X0, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(mem.read_u8(0x53FFD) == 0x18u);
    CHECK(mem.read_u8(0x53FFF) == 0xF6u);
    CHECK(mem.read_u8(0x20000) == 0xE5u);
    CHECK(mem.read_u8(0x20004) == 0xA1u);

    cpu.write_register(2, 0ULL);
    run_one(cpu, mem, 0x0134, 0x0134, 0xF9400022u); // LDR X2, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(2) == 0xA1B2C3D4E5F60718ULL);

    // Cross-page STP / LDP spanning 0x7FF9..0x8008
    cpu.write_register(7, 0x0123456789ABCDEFULL);
    cpu.write_register(8, 0xFEDCBA9876543210ULL);
    cpu.write_register(1, 0x7FF9ULL);
    run_one(cpu, mem, 0x0138, 0x0138, 0xA9002027u); // STP X7, X8, [X1]
    cpu.write_register(9, 0ULL);
    cpu.write_register(10, 0ULL);
    run_one(cpu, mem, 0x013C, 0x013C, 0xA9402829u); // LDP X9, X10, [X1]
    CHECK(cpu.get_current_el() == 0);
    CHECK(cpu.read_register(9) == 0x0123456789ABCDEFULL);
    CHECK(cpu.read_register(10) == 0xFEDCBA9876543210ULL);
}

void test_cross_page_write_atomicity_and_wxn() {
    std::cout << "[Test 2] Cross-page write atomicity & 16KB WXN enforcement...\n";
    Memory mem(0x200000);
    Stage1Table16K pt;
    pt.init(mem);

    pt.map_page_16k(mem, 0x0000, 0x0000, 3u, false, false, false); // Code RO+X
    pt.map_page_16k(mem, 0x4000, 0x50000, 1u, true, true, false); // Page 1: RW+NX
    pt.map_page_16k(mem, 0x8000, 0x20000, 3u, true, true, false); // Page 2: RO+NX
    pt.map_page_16k(mem, 0xC000, 0x30000, 1u, false, false, false); // Page 3: RW with UXN=0 (WXN=1 blocks exec!)

    // Sentinel pattern on Page 1 tail
    for (std::size_t i = 0x53FF0; i < 0x54000; ++i) mem.write_u8(i, 0x5Au);

    CPU cpu;
    Stage1Table16K::setup_cpu_el0(cpu, mem, pt);

    // 1. Cross-page STR X0, [0x7FFC] into RW page 1 + RO page 2 -> Permission fault on page 2, ZERO partial store!
    cpu.write_register(0, 0xDEADBEEFCAFEBABEULL);
    cpu.write_register(1, 0x7FFCUL);
    run_one(cpu, mem, 0x0100, 0x0100, 0xF9000020u); // STR X0, [X1]
    CHECK(cpu.get_current_el() == 1);
    CHECK((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::DataAbortLower));
    CHECK((cpu.get_esr_el1() & 0x3Fu) == static_cast<std::uint32_t>(FaultStatus::PermissionLevel3));
    CHECK(cpu.get_far_el1() == 0x8000ULL);
    for (std::size_t i = 0x53FF0; i < 0x54000; ++i) {
        CHECK(mem.read_u8(i) == 0x5Au); // No partial byte written to page 1!
    }

    // 2. Cross-page STP X0, X1, [0x7FF8] -> PA1 [0x53FF8..0x53FFF] + PA2 [0x20000..0x20007] (RO)
    Stage1Table16K::setup_cpu_el0(cpu, mem, pt);
    cpu.write_register(0, 0x1111111111111111ULL);
    cpu.write_register(1, 0x2222222222222222ULL);
    cpu.write_register(2, 0x7FF8ULL);
    run_one(cpu, mem, 0x0104, 0x0104, 0xA9000440u); // STP X0, X1, [X2]
    CHECK(cpu.get_current_el() == 1);
    CHECK((cpu.get_esr_el1() & 0x3Fu) == static_cast<std::uint32_t>(FaultStatus::PermissionLevel3));
    CHECK(mem.read_u8(0x53FF8) == 0x5Au);

    // 3. Cross-page unmapped second page (0xBFF8..0xC007 with 0xC000 unmapped)
    pt.unmap_page_16k(mem, 0xC000);
    pt.map_page_16k(mem, 0x8000, 0x20000, 1u, true, true, false); // make 0x8000 RW
    for (std::size_t i = 0x23FF0; i < 0x24000; ++i) mem.write_u8(i, 0x3Cu);
    Stage1Table16K::setup_cpu_el0(cpu, mem, pt);
    cpu.write_register(0, 0xAAAAAAAAAAAAAAAAULL);
    cpu.write_register(1, 0xBFF8ULL);
    cpu.write_register(2, 0xBFFEULL);
    run_one(cpu, mem, 0x0108, 0x0108, 0xF9000040u); // STR X0, [X2] spanning 0xBFFE..0xC005
    CHECK(cpu.get_current_el() == 1);
    CHECK((cpu.get_esr_el1() & 0x3Fu) == static_cast<std::uint32_t>(FaultStatus::TranslationLevel3));
    CHECK(mem.read_u8(0x23FFE) == 0x3Cu);

    // 4. WXN enforcement on 16KB page: map VA 0xC000 RW with UXN=0 -> execute MUST fault under WXN=1
    pt.map_page_16k(mem, 0xC000, 0x30000, 1u, false, false, false);
    Stage1Table16K::setup_cpu_el0(cpu, mem, pt);
    run_one(cpu, mem, 0xC000, 0x30000, 0xD503201Fu); // NOP at RW page with UXN=0
    CHECK(cpu.get_current_el() == 1);
    CHECK((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::InstructionAbortLower));
    CHECK((cpu.get_esr_el1() & 0x3Fu) == static_cast<std::uint32_t>(FaultStatus::PermissionLevel3));
    CHECK(cpu.get_far_el1() == 0xC000ULL);
}

void test_architectural_alignment_faults_under_mmu() {
    std::cout << "[Test 3] Architectural alignment faults (Exclusives, Atomics, Device, DC ZVA, SCTLR.A, SP)...\n";
    Memory mem(0x200000);
    Stage1Table16K pt;
    pt.init(mem);

    pt.map_page_16k(mem, 0x0000, 0x0000, 3u, false, false, false);   // Code RO+X
    pt.map_page_16k(mem, 0x4000, 0x50000, 1u, true, true, false);  // Normal RW+NX
    pt.map_page_16k(mem, 0x8000, 0x60000, 1u, true, true, true);   // Device RW+NX (MAIR Attr1 = 0x00)

    // Helper lambda to assert an alignment fault at EL0
    auto expect_data_align = [&](std::uint32_t insn, std::uint64_t x1_addr, bool expect_wnr1,
                                 std::uint64_t sctlr_val = 0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u) | (1ULL << 19u)) {
        CPU cpu;
        Stage1Table16K::setup_cpu_el0(cpu, mem, pt, sctlr_val);
        cpu.write_register(1, x1_addr);
        run_one(cpu, mem, 0x0200, 0x0200, insn);
        CHECK(cpu.get_current_el() == 1);
        const std::uint32_t esr = cpu.get_esr_el1();
        CHECK((esr >> 26u) == static_cast<std::uint32_t>(ExceptionClass::DataAbortLower));
        CHECK((esr & 0x3Fu) == static_cast<std::uint32_t>(FaultStatus::Alignment));
        CHECK(((esr >> 6u) & 1u) == (expect_wnr1 ? 1u : 0u));
    };

    // 1. LDXR X0, [X1] at unaligned Normal 0x4002 (MMU=ON, SCTLR.A=0 -> must still fault!)
    expect_data_align(0xC85F7C20u, 0x4002ULL, true); // Phase 36 exclusive WnR=1 rule
    // 2. LDXP X0, X2, [X1] at unaligned Normal 0x4004
    expect_data_align(0xC87F0820u, 0x4004ULL, false);
    // 3. LDAR X0, [X1] at unaligned Normal 0x4002
    expect_data_align(0xC8DFFC20u, 0x4002ULL, false);
    // 4. CAS W0, W2, [X1] at unaligned Normal 0x4002
    expect_data_align(0x88A27C20u, 0x4002ULL, true);
    // 5. LDADD X0, X2, [X1] at unaligned Normal 0x4002
    expect_data_align(0xF8200022u, 0x4002ULL, true);
    // 6. Device memory unaligned LDR X0, [0x8002] and STR X0, [0x8002]
    expect_data_align(0xF9400020u, 0x8002ULL, false);
    expect_data_align(0xF9000020u, 0x8002ULL, true);
    // 7. DC ZVA X1 on Device memory (0x8040, even 64-byte aligned!) must Alignment fault
    expect_data_align(0xD50B7421u, 0x8040ULL, true);

    // 7b. DC ZVA X1 on Normal memory (0x4040) must succeed!
    {
        for (std::size_t i = 0; i < 64u; ++i) mem.write_u8(0x50040 + i, 0xAAu);
        CPU cpu;
        Stage1Table16K::setup_cpu_el0(cpu, mem, pt);
        cpu.write_register(1, 0x4040ULL);
        run_one(cpu, mem, 0x0210, 0x0210, 0xD50B7421u);
        CHECK(cpu.get_current_el() == 0);
        for (std::size_t i = 0; i < 64u; ++i) CHECK(mem.read_u8(0x50040 + i) == 0u);
    }

    // 8. SCTLR_EL1.A = 1 forces Alignment fault on Normal memory unaligned LDR X0, [0x4002]
    expect_data_align(0xF9400020u, 0x4002ULL, false,
                      0x00C50838ULL | (1ULL << 0u) | (1ULL << 1u) | (1ULL << 14u) | (1ULL << 19u));

    // 9. SP alignment check: EL0 SA0=1 (bit 4 in 0x00C50838 is 1!) with unaligned SP=0x4008 -> EC=0x26
    {
        CPU cpu;
        Stage1Table16K::setup_cpu_el0(cpu, mem, pt);
        cpu.set_stack_pointer(0x4008ULL);
        run_one(cpu, mem, 0x0220, 0x0220, 0xF94003E0u); // LDR X0, [SP]
        CHECK(cpu.get_current_el() == 1);
        CHECK((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::SpAlignment));
        CHECK(cpu.get_elr_el1() == 0x0220ULL);
    }
    // EL0 with SA0=0 (bit 4 cleared) allows LDR X0, [SP] at SP=0x4008
    {
        CPU cpu;
        const std::uint64_t sctlr_no_sa0 = (0x00C50838ULL & ~(1ULL << 4u)) | (1ULL << 0u) | (1ULL << 19u);
        Stage1Table16K::setup_cpu_el0(cpu, mem, pt, sctlr_no_sa0);
        mem.write_u64(0x50008, 0x5566778899AABBCCULL);
        cpu.set_stack_pointer(0x4008ULL);
        run_one(cpu, mem, 0x0224, 0x0224, 0xF94003E0u); // LDR X0, [SP]
        CHECK(cpu.get_current_el() == 0);
        CHECK(cpu.read_register(0) == 0x5566778899AABBCCULL);
    }
}

std::string resolve_artifact_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.good()) return c;
    }
    return rel;
}

std::vector<std::uint8_t> load_binary(const std::string& rel) {
    const std::string p = resolve_artifact_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

void test_real_unaligned_stress_artifact_in_darwin_engine() {
    std::cout << "[Test 4] Real Compiled unaligned_stress (no -mstrict-align) under 16KB Stage-1 MMU...\n";
    auto dyld_bytes = load_binary("tests/artifacts/bin/minidyld");
    auto libsys_bytes = load_binary("tests/artifacts/bin/libSystem.B.dylib");
    auto app_bytes = load_binary("tests/artifacts/bin/unaligned_stress");
    CHECK(!dyld_bytes.empty());
    CHECK(!libsys_bytes.empty());
    CHECK(!app_bytes.empty());

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    runtime::DarwinBsdSubsystem bsd;
    runtime::MachIpcSubsystem ipc;
    runtime::DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libsys_bytes.data(), libsys_bytes.size());

    runtime::DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/unaligned_stress";
    cfg.argv = {"/usr/bin/unaligned_stress"};
    cfg.max_instructions = 300000;

    auto res = engine.execute_macho_with_guest_dyld(
        dyld_bytes.data(), dyld_bytes.size(),
        app_bytes.data(), app_bytes.size(),
        cfg, 701);

    std::cout << "  Retired: " << res.guest_instructions_retired
              << ", Exit: " << (res.process_exited ? "true" : "false")
              << " (" << res.exit_code << "), stdout=\"" << res.stdout_output << "\"\n";
    CHECK(res.is_clean_exit());
    // Must match independent Windows x64 host compilation of unaligned_stress.c
    CHECK(res.stdout_output.find("UNALIGNED_STRESS_HASH=0x05ea4b3929a540a2") != std::string::npos);
    CHECK(res.stdout_output.find("UNALIGNED_STRESS_PASS") != std::string::npos);

    // Verify 16KB Stage-1 Normal-Memory MMU state left configured by DarwinUserspaceEngine
    const auto& mmu = cpu.get_mmu();
    CHECK(mmu.is_stage1_enabled());
    CHECK(!mmu.is_alignment_check_enabled());
    CHECK(mmu.is_sp_alignment_check_enabled(0));
    CHECK(mmu.is_wxn_enabled());
    CHECK(mmu.get_tg0_granule() == mmu::GranuleType::Granule16KB);
    CHECK(mmu.get_mair_el1() == 0x00FFULL);

    // Verify mapped W^X and unmapped page-table guard behavior via MMU::translate
    mmu::TranslationFault tf{};
    bool is_dev = false;
    // __TEXT of main binary at 0x0 and __TEXT of libSystem at 0x20000: RX only (EL0 exec OK, EL0 write faults!)
    CHECK(cpu.get_mmu().translate(memory, 0x0000ULL, mmu::AccessKind::Execute, 0, &tf, &is_dev).has_value());
    CHECK(!is_dev);
    CHECK(!cpu.get_mmu().translate(memory, 0x0000ULL, mmu::AccessKind::Write, 0, &tf, &is_dev).has_value());
    CHECK(cpu.get_mmu().translate(memory, 0x20000ULL, mmu::AccessKind::Execute, 0, &tf, &is_dev).has_value());
    CHECK(!cpu.get_mmu().translate(memory, 0x20000ULL, mmu::AccessKind::Write, 0, &tf, &is_dev).has_value());
    // __DATA at 0x8000 and minidyld __DATA at 0x200C000: RW+NX (EL0 write OK, EL0 exec faults!)
    CHECK(cpu.get_mmu().translate(memory, 0x8000ULL, mmu::AccessKind::Write, 0, &tf, &is_dev).has_value());
    CHECK(!cpu.get_mmu().translate(memory, 0x8000ULL, mmu::AccessKind::Execute, 0, &tf, &is_dev).has_value());
    // Page tables at top of 64MB RAM (0x03FF4000): UNMAPPED for EL0!
    CHECK(!cpu.get_mmu().translate(memory, 0x03FF4000ULL, mmu::AccessKind::Read, 0, &tf, &is_dev).has_value());
    // VBAR page (0x00010000): EL1-only, blocked from EL0!
    CHECK(!cpu.get_mmu().translate(memory, 0x00010000ULL, mmu::AccessKind::Read, 0, &tf, &is_dev).has_value());
}

} // namespace

int main() {
    test_unaligned_normal_memory_and_split_page_crossing();
    test_cross_page_write_atomicity_and_wxn();
    test_architectural_alignment_faults_under_mmu();
    test_real_unaligned_stress_artifact_in_darwin_engine();

    if (g_failed != 0) {
        std::cerr << "stage_v1_userspace_memory_model_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE V1 USERSPACE MEMORY MODEL CHECKS PASSED\n";
    return 0;
}
