#include "test_harness.h"
#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kRamSize = 1024 * 1024; // 1 MB RAM

// AArch64 Opcode Constants
constexpr std::uint32_t kNop        = 0xD503201Fu;
constexpr std::uint32_t kRet        = 0xD65F03E0u;
constexpr std::uint32_t kWfi        = 0xD503207Fu; // WFI
constexpr std::uint32_t kDmbIsh     = 0xD5033BBFu;
constexpr std::uint32_t kDmbIshld   = 0xD50339BFu;
constexpr std::uint32_t kDmbIshst   = 0xD5033ABFu;
constexpr std::uint32_t kDsbIsh     = 0xD5033B9Fu;
constexpr std::uint32_t kIsb        = 0xD5033FDFu;
constexpr std::uint32_t kTlbiVmalle1is = 0xD508831Fu;
constexpr std::uint32_t kTlbiVmalle1   = 0xD508871Fu;
constexpr std::uint32_t kIcIallu    = 0xD508751Fu;
constexpr std::uint32_t kIcIalluis  = 0xD508711Fu;

inline std::uint32_t movz_x(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | rd;
}

inline std::uint32_t movk_x(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xF2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | rd;
}

inline std::uint32_t add_x_imm(unsigned rd, unsigned rn, unsigned imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | (rn << 5u) | rd;
}

inline std::uint32_t subs_x_imm(unsigned rd, unsigned rn, unsigned imm12) {
    return 0xF1000000u | ((imm12 & 0xFFFu) << 10u) | (rn << 5u) | rd;
}

inline std::uint32_t subs_x_reg(unsigned rd, unsigned rn, unsigned rm) {
    return 0xEB000000u | (rm << 16u) | (rn << 5u) | rd;
}

inline std::uint32_t cbnz_x(unsigned rt, std::int64_t byte_offset) {
    const std::int64_t imm19 = byte_offset / 4;
    return 0xB5000000u | ((static_cast<std::uint32_t>(imm19) & 0x7FFFFu) << 5u) | rt;
}

inline std::uint32_t cbz_w(unsigned rt, std::int64_t byte_offset) {
    const std::int64_t imm19 = byte_offset / 4;
    return 0x34000000u | ((static_cast<std::uint32_t>(imm19) & 0x7FFFFu) << 5u) | rt;
}

inline std::uint32_t cbnz_w(unsigned rt, std::int64_t byte_offset) {
    const std::int64_t imm19 = byte_offset / 4;
    return 0x35000000u | ((static_cast<std::uint32_t>(imm19) & 0x7FFFFu) << 5u) | rt;
}

inline std::uint32_t b_rel(std::int64_t byte_offset) {
    const std::int64_t imm26 = byte_offset / 4;
    return 0x14000000u | (static_cast<std::uint32_t>(imm26) & 0x03FFFFFFu);
}

inline std::uint32_t ldr_x(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xF9400000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t str_x(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xF9000000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t ldr_w(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xB9400000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t str_w(unsigned rt, unsigned rn, unsigned imm12_scaled = 0) {
    return 0xB9000000u | ((imm12_scaled & 0xFFFu) << 10u) | (rn << 5u) | rt;
}

inline std::uint32_t ldar_x(unsigned rt, unsigned rn) {
    return 0xC8DFFC00u | (rn << 5u) | rt;
}

inline std::uint32_t stlr_x(unsigned rt, unsigned rn) {
    return 0xC89FFC00u | (rn << 5u) | rt;
}

inline std::uint32_t ldxr_x(unsigned rt, unsigned rn) {
    return 0xC85F7C00u | (rn << 5u) | rt;
}

inline std::uint32_t stxr_w(unsigned rs, unsigned rt, unsigned rn) {
    return 0xC8007C00u | (rs << 16u) | (rn << 5u) | rt;
}

inline std::uint32_t ldaxr_x(unsigned rt, unsigned rn) {
    return 0xC85FFC00u | (rn << 5u) | rt;
}

inline std::uint32_t stlxr_w(unsigned rs, unsigned rt, unsigned rn) {
    return 0xC800FC00u | (rs << 16u) | (rn << 5u) | rt;
}

inline std::uint32_t ldxp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xC87F0000u | (rt2 << 10u) | (rn << 5u) | rt1;
}

inline std::uint32_t stxp_w(unsigned rs, unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xC8200000u | (rs << 16u) | (rt2 << 10u) | (rn << 5u) | rt1;
}

inline std::uint32_t ldaxp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xC87F8000u | (rt2 << 10u) | (rn << 5u) | rt1;
}

inline std::uint32_t stlxp_w(unsigned rs, unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xC8208000u | (rs << 16u) | (rt2 << 10u) | (rn << 5u) | rt1;
}

inline std::uint32_t lse_x(unsigned opc, unsigned rs, unsigned rt, unsigned rn, bool a = false, bool r = false) {
    const std::uint32_t a_bit = a ? (1u << 23u) : 0u;
    const std::uint32_t r_bit = r ? (1u << 22u) : 0u;
    return 0xF8200000u | a_bit | r_bit | (rs << 16u) | (opc << 12u) | (rn << 5u) | rt;
}

inline std::uint32_t dc_zva(unsigned rt) {
    return 0xD50B7420u | rt;
}

inline std::uint32_t dc_cvau(unsigned rt) {
    return 0xD50B7B20u | rt;
}

inline std::uint32_t ic_ivau(unsigned rt) {
    return 0xD50B7520u | rt;
}

inline std::uint32_t tlbi_vae1is(unsigned rt) {
    return 0xD5088320u | rt;
}

inline std::uint32_t tlbi_aside1is(unsigned rt) {
    return 0xD5088340u | rt;
}

inline void setup_core(winios::SMPCluster& cluster, std::size_t id, std::uint64_t entry) {
    if (id == 0) {
        cluster.initialize_core(0, entry);
    } else {
        cluster.start_secondary_core(id, entry);
    }
    cluster.get_core(id).set_architectural_exceptions(true);
}

// ===========================================================================
// Stage 3: AArch64 Memory-Ordering Litmus Tests
// ===========================================================================

// Litmus 1: Store Buffering (SB)
// Initial: x = 0, y = 0
// Core 0: STR #1 -> [x]; DMB ISH; LDR r1 <- [y]
// Core 1: STR #1 -> [y]; DMB ISH; LDR r2 <- [x]
// Permitted outcomes: (r1=1, r2=1), (r1=0, r2=1), (r1=1, r2=0)
// Forbidden outcome:  (r1=0, r2=0)
void litmus_store_buffering() {
    constexpr std::uint64_t addr_x = 0x8000;
    constexpr std::uint64_t addr_y = 0x8008;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(2, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_x, 0);
            mem.write_u64(addr_y, 0);

            // Core 0 program at 0x1000
            const std::size_t c0_pc = 0x1000;
            mem.write_u32(c0_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(c0_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(c0_pc + 0x08, movz_x(2, 1));
            mem.write_u32(c0_pc + 0x0C, str_x(2, 0)); // STR X2, [X0] (x = 1)
            mem.write_u32(c0_pc + 0x10, kDmbIsh);
            mem.write_u32(c0_pc + 0x14, ldr_x(3, 1)); // LDR X3, [X1] (r1 = y)
            mem.write_u32(c0_pc + 0x18, kWfi);

            // Core 1 program at 0x2000
            const std::size_t c1_pc = 0x2000;
            mem.write_u32(c1_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(c1_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(c1_pc + 0x08, movz_x(2, 1));
            mem.write_u32(c1_pc + 0x0C, str_x(2, 0)); // STR X2, [X0] (y = 1)
            mem.write_u32(c1_pc + 0x10, kDmbIsh);
            mem.write_u32(c1_pc + 0x14, ldr_x(3, 1)); // LDR X3, [X1] (r2 = x)
            mem.write_u32(c1_pc + 0x18, kWfi);

            setup_core(cluster, 0, c0_pc);
            setup_core(cluster, 1, c1_pc);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(0).read_register(3);
            const std::uint64_t r2 = cluster.get_core(1).read_register(3);

            if (r1 == 0 && r2 == 0) {
                ++forbidden_count;
            }
        }

        CHECK(forbidden_count == 0); // (0, 0) is forbidden by DMB ISH and SC
    }
    std::cout << "  [PASS] Litmus 1: Store Buffering (SB)\n";
}

// Litmus 2: Message Passing (MP)
// Initial: data = 0, flag = 0
// Core 0: STR #42 -> data; DMB ISHST; STR #1 -> flag
// Core 1: LDR r1 <- flag; DMB ISHLD; LDR r2 <- data
// Forbidden outcome: (r1 == 1 && r2 == 0)
void litmus_message_passing() {
    constexpr std::uint64_t addr_data = 0x8100;
    constexpr std::uint64_t addr_flag = 0x8108;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(2, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_data, 0);
            mem.write_u64(addr_flag, 0);

            // Core 0
            const std::size_t c0_pc = 0x1000;
            mem.write_u32(c0_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_data)));
            mem.write_u32(c0_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_flag)));
            mem.write_u32(c0_pc + 0x08, movz_x(2, 42));
            mem.write_u32(c0_pc + 0x0C, str_x(2, 0)); // data = 42
            mem.write_u32(c0_pc + 0x10, kDmbIshst);
            mem.write_u32(c0_pc + 0x14, movz_x(3, 1));
            mem.write_u32(c0_pc + 0x18, str_x(3, 1)); // flag = 1
            mem.write_u32(c0_pc + 0x1C, kWfi);

            // Core 1
            const std::size_t c1_pc = 0x2000;
            mem.write_u32(c1_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_flag)));
            mem.write_u32(c1_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_data)));
            mem.write_u32(c1_pc + 0x08, ldr_x(2, 0)); // r1 = flag
            mem.write_u32(c1_pc + 0x0C, kDmbIshld);
            mem.write_u32(c1_pc + 0x10, ldr_x(3, 1)); // r2 = data
            mem.write_u32(c1_pc + 0x14, kWfi);

            setup_core(cluster, 0, c0_pc);
            setup_core(cluster, 1, c1_pc);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(1).read_register(2);
            const std::uint64_t r2 = cluster.get_core(1).read_register(3);

            if (r1 == 1 && r2 == 0) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 2: Message Passing (MP)\n";
}

// Litmus 3: Message Passing via LDAR / STLR (Release/Acquire)
// Core 0: STR #99 -> data; STLR #1 -> flag
// Core 1: LDAR r1 <- flag; LDR r2 <- data
// Forbidden: (r1 == 1 && r2 == 0)
void litmus_message_passing_release_acquire() {
    constexpr std::uint64_t addr_data = 0x8200;
    constexpr std::uint64_t addr_flag = 0x8208;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(2, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_data, 0);
            mem.write_u64(addr_flag, 0);

            // Core 0
            const std::size_t c0_pc = 0x1000;
            mem.write_u32(c0_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_data)));
            mem.write_u32(c0_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_flag)));
            mem.write_u32(c0_pc + 0x08, movz_x(2, 99));
            mem.write_u32(c0_pc + 0x0C, str_x(2, 0)); // data = 99
            mem.write_u32(c0_pc + 0x10, movz_x(3, 1));
            mem.write_u32(c0_pc + 0x14, stlr_x(3, 1)); // STLR flag, 1
            mem.write_u32(c0_pc + 0x18, kWfi);

            // Core 1
            const std::size_t c1_pc = 0x2000;
            mem.write_u32(c1_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_flag)));
            mem.write_u32(c1_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_data)));
            mem.write_u32(c1_pc + 0x08, ldar_x(2, 0)); // LDAR r1, [flag]
            mem.write_u32(c1_pc + 0x0C, ldr_x(3, 1));  // LDR r2, [data]
            mem.write_u32(c1_pc + 0x10, kWfi);

            setup_core(cluster, 0, c0_pc);
            setup_core(cluster, 1, c1_pc);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(1).read_register(2);
            const std::uint64_t r2 = cluster.get_core(1).read_register(3);

            if (r1 == 1 && r2 == 0) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 3: Release/Acquire Message Passing\n";
}

// Litmus 4: Load Buffering (LB)
// Initial: x = 0, y = 0
// Core 0: LDR r1 <- x; DMB ISH; STR #1 -> y
// Core 1: LDR r2 <- y; DMB ISH; STR #1 -> x
// Forbidden: (r1 == 1 && r2 == 1)
void litmus_load_buffering() {
    constexpr std::uint64_t addr_x = 0x8300;
    constexpr std::uint64_t addr_y = 0x8308;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(2, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_x, 0);
            mem.write_u64(addr_y, 0);

            // Core 0
            const std::size_t c0_pc = 0x1000;
            mem.write_u32(c0_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(c0_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(c0_pc + 0x08, ldr_x(2, 0)); // r1 = x
            mem.write_u32(c0_pc + 0x0C, kDmbIsh);
            mem.write_u32(c0_pc + 0x10, movz_x(3, 1));
            mem.write_u32(c0_pc + 0x14, str_x(3, 1)); // y = 1
            mem.write_u32(c0_pc + 0x18, kWfi);

            // Core 1
            const std::size_t c1_pc = 0x2000;
            mem.write_u32(c1_pc + 0x00, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(c1_pc + 0x04, movz_x(1, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(c1_pc + 0x08, ldr_x(2, 0)); // r2 = y
            mem.write_u32(c1_pc + 0x0C, kDmbIsh);
            mem.write_u32(c1_pc + 0x10, movz_x(3, 1));
            mem.write_u32(c1_pc + 0x14, str_x(3, 1)); // x = 1
            mem.write_u32(c1_pc + 0x18, kWfi);

            setup_core(cluster, 0, c0_pc);
            setup_core(cluster, 1, c1_pc);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(0).read_register(2);
            const std::uint64_t r2 = cluster.get_core(1).read_register(2);

            if (r1 == 1 && r2 == 1) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 4: Load Buffering (LB)\n";
}

// Litmus 5: Independent Reads of Independent Writes (IRIW) (4 Cores)
// Core 0: STR #1 -> x
// Core 1: STR #1 -> y
// Core 2: LDR r1 <- x; DMB ISH; LDR r2 <- y
// Core 3: LDR r3 <- y; DMB ISH; LDR r4 <- x
// Forbidden: (r1 == 1 && r2 == 0 && r3 == 1 && r4 == 0)
void litmus_iriw_4cores() {
    constexpr std::uint64_t addr_x = 0x8400;
    constexpr std::uint64_t addr_y = 0x8408;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(4, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_x, 0);
            mem.write_u64(addr_y, 0);

            // Core 0: writes x
            mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x1004, movz_x(1, 1));
            mem.write_u32(0x1008, str_x(1, 0));
            mem.write_u32(0x100C, kWfi);

            // Core 1: writes y
            mem.write_u32(0x2000, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x2004, movz_x(1, 1));
            mem.write_u32(0x2008, str_x(1, 0));
            mem.write_u32(0x200C, kWfi);

            // Core 2: reads x then y
            mem.write_u32(0x3000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x3004, movz_x(1, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x3008, ldr_x(2, 0));
            mem.write_u32(0x300C, kDmbIsh);
            mem.write_u32(0x3010, ldr_x(3, 1));
            mem.write_u32(0x3014, kWfi);

            // Core 3: reads y then x
            mem.write_u32(0x4000, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x4004, movz_x(1, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x4008, ldr_x(2, 0));
            mem.write_u32(0x400C, kDmbIsh);
            mem.write_u32(0x4010, ldr_x(3, 1));
            mem.write_u32(0x4014, kWfi);

            setup_core(cluster, 0, 0x1000);
            setup_core(cluster, 1, 0x2000);
            setup_core(cluster, 2, 0x3000);
            setup_core(cluster, 3, 0x4000);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(2).read_register(2);
            const std::uint64_t r2 = cluster.get_core(2).read_register(3);
            const std::uint64_t r3 = cluster.get_core(3).read_register(2);
            const std::uint64_t r4 = cluster.get_core(3).read_register(3);

            if (r1 == 1 && r2 == 0 && r3 == 1 && r4 == 0) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 5: IRIW (4 Cores)\n";
}

// Litmus 6: Write-Read Causality (WRC) (3 Cores)
// Core 0: STR #1 -> x
// Core 1: LDR r1 <- x; DMB ISH; STR #1 -> y
// Core 2: LDR r2 <- y; DMB ISH; LDR r3 <- x
// Forbidden: (r1 == 1 && r2 == 1 && r3 == 0)
void litmus_wrc_causality() {
    constexpr std::uint64_t addr_x = 0x8500;
    constexpr std::uint64_t addr_y = 0x8508;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(3, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_x, 0);
            mem.write_u64(addr_y, 0);

            // Core 0: writes x
            mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x1004, movz_x(1, 1));
            mem.write_u32(0x1008, str_x(1, 0));
            mem.write_u32(0x100C, kWfi);

            // Core 1: reads x, writes y
            mem.write_u32(0x2000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x2004, movz_x(1, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x2008, ldr_x(2, 0));
            mem.write_u32(0x200C, kDmbIsh);
            mem.write_u32(0x2010, movz_x(3, 1));
            mem.write_u32(0x2014, str_x(3, 1));
            mem.write_u32(0x2018, kWfi);

            // Core 2: reads y then x
            mem.write_u32(0x3000, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x3004, movz_x(1, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x3008, ldr_x(2, 0));
            mem.write_u32(0x300C, kDmbIsh);
            mem.write_u32(0x3010, ldr_x(3, 1));
            mem.write_u32(0x3014, kWfi);

            setup_core(cluster, 0, 0x1000);
            setup_core(cluster, 1, 0x2000);
            setup_core(cluster, 2, 0x3000);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(1).read_register(2);
            const std::uint64_t r2 = cluster.get_core(2).read_register(2);
            const std::uint64_t r3 = cluster.get_core(2).read_register(3);

            if (r1 == 1 && r2 == 1 && r3 == 0) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 6: Write-Read Causality (WRC)\n";
}

// Litmus 7: Read-Write Causality (RWC) (3 Cores)
// Core 0: STR #1 -> x
// Core 1: LDR r1 <- x; DMB ISH; LDR r2 <- y
// Core 2: STR #1 -> y; DMB ISH; LDR r3 <- x
// Forbidden: (r1 == 1 && r2 == 0 && r3 == 0)
void litmus_rwc_causality() {
    constexpr std::uint64_t addr_x = 0x8600;
    constexpr std::uint64_t addr_y = 0x8608;

    for (bool parallel : {false, true}) {
        std::size_t forbidden_count = 0;
        const std::size_t kRuns = parallel ? 20 : 30;
        for (std::size_t run = 0; run < kRuns; ++run) {
            winios::SMPCluster cluster(3, kRamSize);
            Memory& mem = cluster.get_memory();
            mem.write_u64(addr_x, 0);
            mem.write_u64(addr_y, 0);

            // Core 0: writes x
            mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x1004, movz_x(1, 1));
            mem.write_u32(0x1008, str_x(1, 0));
            mem.write_u32(0x100C, kWfi);

            // Core 1: reads x, reads y
            mem.write_u32(0x2000, movz_x(0, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x2004, movz_x(1, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x2008, ldr_x(2, 0));
            mem.write_u32(0x200C, kDmbIsh);
            mem.write_u32(0x2010, ldr_x(3, 1));
            mem.write_u32(0x2014, kWfi);

            // Core 2: writes y, reads x
            mem.write_u32(0x3000, movz_x(0, static_cast<std::uint16_t>(addr_y)));
            mem.write_u32(0x3004, movz_x(1, static_cast<std::uint16_t>(addr_x)));
            mem.write_u32(0x3008, movz_x(2, 1));
            mem.write_u32(0x300C, str_x(2, 0));
            mem.write_u32(0x3010, kDmbIsh);
            mem.write_u32(0x3014, ldr_x(3, 1));
            mem.write_u32(0x3018, kWfi);

            setup_core(cluster, 0, 0x1000);
            setup_core(cluster, 1, 0x2000);
            setup_core(cluster, 2, 0x3000);

            if (parallel) {
                cluster.run_parallel(0);
                cluster.stop_parallel();
            } else {
                cluster.run_deterministic(0);
            }

            const std::uint64_t r1 = cluster.get_core(1).read_register(2);
            const std::uint64_t r2 = cluster.get_core(1).read_register(3);
            const std::uint64_t r3 = cluster.get_core(2).read_register(3);

            if (r1 == 1 && r2 == 0 && r3 == 0) {
                ++forbidden_count;
            }
        }
        CHECK(forbidden_count == 0);
    }
    std::cout << "  [PASS] Litmus 7: Read-Write Causality (RWC)\n";
}

// Barrier Semantics (DMB ISH, ISHLD, ISHST, DSB ISH, ISB)
void barrier_semantics_validation() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();

    mem.write_u32(0x1000, kDmbIsh);
    mem.write_u32(0x1004, kDmbIshld);
    mem.write_u32(0x1008, kDmbIshst);
    mem.write_u32(0x100C, kDsbIsh);
    mem.write_u32(0x1010, kIsb);
    mem.write_u32(0x1014, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.halt_core(1);

    // Step each barrier and verify normal retirement
    for (std::size_t i = 0; i < 5; ++i) {
        cluster.get_core(0).step(mem);
    }
    CHECK(cluster.get_core(0).get_program_counter() == 0x1014);
    std::cout << "  [PASS] Barrier Semantics Validation\n";
}

// ===========================================================================
// Stage 4: Exclusive Monitor and LSE Validation
// ===========================================================================

// 2-core concurrent LDXR / STXR contention
void exclusive_monitor_concurrency_2cores() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kCounterAddr = 0x9000;
    constexpr std::uint64_t kPerCore = 1000;
    mem.write_u64(kCounterAddr, 0);

    // Program: loop kPerCore times doing LDXR/ADD/STXR
    auto install_worker = [&](std::size_t pc) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kCounterAddr)));
        mem.write_u32(pc + 0x04, movz_x(4, static_cast<std::uint16_t>(kPerCore)));
        // Loop head (pc + 0x08):
        mem.write_u32(pc + 0x08, ldxr_x(0, 1));
        mem.write_u32(pc + 0x0C, add_x_imm(0, 0, 1));
        mem.write_u32(pc + 0x10, stxr_w(2, 0, 1));
        mem.write_u32(pc + 0x14, cbnz_w(2, -12)); // retry if status != 0 (jump to pc + 0x08)
        mem.write_u32(pc + 0x18, subs_x_imm(4, 4, 1));
        mem.write_u32(pc + 0x1C, cbnz_x(4, -20)); // decrement counter (jump to pc + 0x08)
        mem.write_u32(pc + 0x20, kWfi);
    };

    install_worker(0x1000);
    install_worker(0x2000);

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);

    cluster.run_parallel(0); // run until quiescence
    cluster.stop_parallel();

    const std::uint64_t final_val = mem.read_u64(kCounterAddr);
    CHECK(final_val == kPerCore * 2);
    std::cout << "  [PASS] Exclusive Monitor Concurrency (2 Cores)\n";
}

// 4-core concurrent LDXR / STXR contention
void exclusive_monitor_concurrency_4cores() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kCounterAddr = 0x9020;
    constexpr std::uint64_t kPerCore = 800;
    mem.write_u64(kCounterAddr, 0);

    auto install_worker = [&](std::size_t pc) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kCounterAddr)));
        mem.write_u32(pc + 0x04, movz_x(4, static_cast<std::uint16_t>(kPerCore)));
        mem.write_u32(pc + 0x08, ldxr_x(0, 1));
        mem.write_u32(pc + 0x0C, add_x_imm(0, 0, 1));
        mem.write_u32(pc + 0x10, stxr_w(2, 0, 1));
        mem.write_u32(pc + 0x14, cbnz_w(2, -12));
        mem.write_u32(pc + 0x18, subs_x_imm(4, 4, 1));
        mem.write_u32(pc + 0x1C, cbnz_x(4, -20));
        mem.write_u32(pc + 0x20, kWfi);
    };

    install_worker(0x1000);
    install_worker(0x2000);
    install_worker(0x3000);
    install_worker(0x4000);

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);
    setup_core(cluster, 2, 0x3000);
    setup_core(cluster, 3, 0x4000);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    const std::uint64_t final_val = mem.read_u64(kCounterAddr);
    CHECK(final_val == kPerCore * 4);
    std::cout << "  [PASS] Exclusive Monitor Concurrency (4 Cores)\n";
}

// Pair Exclusive (LDXP / STXP) Atomicity
void exclusive_pair_atomicity() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kPairAddr = 0x9100;
    mem.write_u64(kPairAddr + 0, 0x1111);
    mem.write_u64(kPairAddr + 8, 0x2222);

    // Core 0: LDXP X0, X1, [X2]
    mem.write_u32(0x1000, movz_x(2, static_cast<std::uint16_t>(kPairAddr)));
    mem.write_u32(0x1004, ldxp_x(0, 1, 2));
    mem.write_u32(0x1008, movz_x(4, 0x3333));
    mem.write_u32(0x100C, movz_x(5, 0x4444));
    mem.write_u32(0x1010, stxp_w(3, 4, 5, 2)); // STXP W3, X4, X5, [X2]
    mem.write_u32(0x1014, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.halt_core(1);

    for (int i = 0; i < 5; ++i) {
        cluster.get_core(0).step(mem);
    }

    CHECK(cluster.get_core(0).read_register(0) == 0x1111);
    CHECK(cluster.get_core(0).read_register(1) == 0x2222);
    CHECK(cluster.get_core(0).read_register(3) == 0); // status == 0
    CHECK(mem.read_u64(kPairAddr + 0) == 0x3333);
    CHECK(mem.read_u64(kPairAddr + 8) == 0x4444);
    std::cout << "  [PASS] Pair Exclusive (LDXP / STXP) Atomicity\n";
}

// Exclusive Monitor Granularity and Overlapping Store Invalidation
void exclusive_monitor_overlap_and_granularity() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kBase = 0x9200;

    // 1. Non-overlapping store does NOT invalidate reservation
    mem.write_u64(kBase, 0xAA);
    mem.write_u64(kBase + 8, 0xBB);

    // Core 0 registers reservation at kBase
    cluster.register_exclusive_reservation(0, kBase, 8);

    // Core 1 writes to kBase + 8 (non-overlapping)
    cluster.invalidate_exclusive_reservations(1, kBase + 8, 8);
    CHECK(cluster.check_and_clear_exclusive_reservation(0, kBase, 8) == true);

    // 2. Overlapping store DOES invalidate reservation
    cluster.register_exclusive_reservation(0, kBase, 8);
    cluster.invalidate_exclusive_reservations(1, kBase, 8);
    CHECK(cluster.check_and_clear_exclusive_reservation(0, kBase, 8) == false);

    // 3. Partial overlap (e.g. 4-byte write at kBase + 4)
    cluster.register_exclusive_reservation(0, kBase, 8);
    cluster.invalidate_exclusive_reservations(1, kBase + 4, 4);
    CHECK(cluster.check_and_clear_exclusive_reservation(0, kBase, 8) == false);

    // 4. CLREX clears local reservation
    cluster.register_exclusive_reservation(0, kBase, 8);
    cluster.clear_exclusive_reservation(0);
    CHECK(cluster.check_and_clear_exclusive_reservation(0, kBase, 8) == false);

    // 5. Failed STXR leaves memory unmodified
    mem.write_u64(kBase, 0x12345678ULL);
    mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(kBase)));
    mem.write_u32(0x1004, movz_x(1, 0x9999));
    mem.write_u32(0x1008, stxr_w(2, 1, 0)); // STXR W2, X1, [X0] without LDXR
    mem.write_u32(0x100C, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.get_core(0).step(mem);
    cluster.get_core(0).step(mem);
    cluster.get_core(0).step(mem);

    CHECK(cluster.get_core(0).read_register(2) == 1); // failed -> status 1
    CHECK(mem.read_u64(kBase) == 0x12345678ULL);      // memory untouched
    std::cout << "  [PASS] Exclusive Monitor Overlap & Granularity\n";
}

// 4-core concurrent LSE atomics (LDADD, SWP, LDSET, LDCLR, LDEOR)
void lse_atomics_concurrency_4cores() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kLseCounter = 0x9300;
    constexpr std::size_t kPerCore = 1000;
    mem.write_u64(kLseCounter, 0);

    // Program: loop kPerCore times doing LDADD
    auto install_lse_worker = [&](std::size_t pc) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kLseCounter)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kPerCore)));
        mem.write_u32(pc + 0x08, movz_x(0, 1));
        // LDADDAL X0, X0, [X1] (a=1, r=1)
        mem.write_u32(pc + 0x0C, lse_x(0, 0, 0, 1, true, true));
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12)); // loop to pc + 0x08
        mem.write_u32(pc + 0x18, kWfi);
    };

    install_lse_worker(0x1000);
    install_lse_worker(0x2000);
    install_lse_worker(0x3000);
    install_lse_worker(0x4000);

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);
    setup_core(cluster, 2, 0x3000);
    setup_core(cluster, 3, 0x4000);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kLseCounter) == kPerCore * 4);

    // Also test SWP, LDSET, LDCLR, LDEOR sequentially
    mem.write_u64(kLseCounter, 0x00FF);
    // SWP X0, X2, [X1]: writes X2 (0x1234) into [X1], returns old value (0x00FF) in X0
    mem.write_u32(0x5000, movz_x(1, static_cast<std::uint16_t>(kLseCounter)));
    mem.write_u32(0x5004, movz_x(2, 0x1234));
    mem.write_u32(0x5008, lse_x(8, 2, 0, 1)); // SWP
    // LDSET X0, X2, [X1]: sets bits 0x8000 into [X1], returns old in X0
    mem.write_u32(0x500C, movz_x(2, 0x8000));
    mem.write_u32(0x5010, lse_x(3, 2, 0, 1)); // LDSET
    // LDCLR X0, X2, [X1]: clears bits 0x0034 from [X1]
    mem.write_u32(0x5014, movz_x(2, 0x0034));
    mem.write_u32(0x5018, lse_x(1, 2, 0, 1)); // LDCLR
    // LDEOR X0, X2, [X1]: toggles bits 0x1000 in [X1]
    mem.write_u32(0x501C, movz_x(2, 0x1000));
    mem.write_u32(0x5020, lse_x(2, 2, 0, 1)); // LDEOR
    mem.write_u32(0x5024, kWfi);

    cluster.initialize_core(0, 0x5000);
    for (int i = 0; i < 9; ++i) {
        cluster.get_core(0).step(mem);
    }
    // Expected: 0x1234 | 0x8000 = 0x9234; 0x9234 & ~0x0034 = 0x9200; 0x9200 ^ 0x1000 = 0x8200
    CHECK(mem.read_u64(kLseCounter) == 0x8200);
    std::cout << "  [PASS] LSE Atomics Concurrency (4 Cores)\n";
}

// ===========================================================================
// Stage 5 & 6: Cache Coherency, DC ZVA & Cache Lines
// ===========================================================================

// DC ZVA block clearing validation
void cache_dc_zva_validation() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kBlockAddr = 0x9400; // 64-byte aligned

    // Fill block with pattern
    for (std::size_t i = 0; i < 64; ++i) {
        mem.write_u8(kBlockAddr + i, static_cast<std::uint8_t>(0xAA + i));
    }

    // DC ZVA X0 at EL1
    mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(kBlockAddr + 16))); // unaligned pointer within block
    mem.write_u32(0x1004, dc_zva(0));
    mem.write_u32(0x1008, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.get_core(0).set_current_el(1);
    cluster.get_core(0).step(mem);
    cluster.get_core(0).step(mem);

    // Verify entire 64-byte aligned block was zeroed
    for (std::size_t i = 0; i < 64; ++i) {
        CHECK(mem.read_u8(kBlockAddr + i) == 0);
    }
    std::cout << "  [PASS] DC ZVA Block Clearing\n";
}

// Cache maintenance operations (DC CVAC, CVAU, CIVAC, IVAC, IC IVAU, IC IALLU)
void cache_maintenance_ops_validation() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kAddr = 0x9500;

    mem.write_u32(0x1000, movz_x(0, static_cast<std::uint16_t>(kAddr)));
    mem.write_u32(0x1004, dc_cvau(0));
    mem.write_u32(0x1008, ic_ivau(0));
    mem.write_u32(0x100C, kIcIallu);
    mem.write_u32(0x1010, kIcIalluis);
    mem.write_u32(0x1014, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.get_core(0).set_current_el(1);

    for (int i = 0; i < 5; ++i) {
        cluster.get_core(0).step(mem);
    }
    CHECK(cluster.get_core(0).get_program_counter() == 0x1014);
    std::cout << "  [PASS] Cache Maintenance Ops Validation\n";
}

// Cache line granularity (64B) and concurrent sharing
void cache_line_concurrent_sharing() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kLineBase = 0x9600; // 64-byte aligned

    constexpr std::size_t kIter = 1000;
    auto install_line_worker = [&](std::size_t pc, unsigned offset_val) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kLineBase + offset_val)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kIter)));
        mem.write_u32(pc + 0x08, movz_x(0, 0));
        mem.write_u32(pc + 0x0C, add_x_imm(0, 0, 1));
        mem.write_u32(pc + 0x10, str_x(0, 1));
        mem.write_u32(pc + 0x14, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x18, cbnz_x(2, -12));
        mem.write_u32(pc + 0x1C, kWfi);
    };

    install_line_worker(0x1000, 0x00);
    install_line_worker(0x2000, 0x08);
    install_line_worker(0x3000, 0x10);
    install_line_worker(0x4000, 0x18);

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);
    setup_core(cluster, 2, 0x3000);
    setup_core(cluster, 3, 0x4000);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kLineBase + 0x00) == kIter);
    CHECK(mem.read_u64(kLineBase + 0x08) == kIter);
    CHECK(mem.read_u64(kLineBase + 0x10) == kIter);
    CHECK(mem.read_u64(kLineBase + 0x18) == kIter);
    std::cout << "  [PASS] Cache Line Concurrent Sharing (64B)\n";
}

// Cache-line false sharing benchmark comparison (same line vs different lines)
void cache_line_false_sharing_benchmark() {
    winios::SMPCluster cluster_same(2, kRamSize);
    Memory& mem_same = cluster_same.get_memory();
    constexpr std::uint64_t kSameLine = 0x9700;

    auto install_worker = [&](Memory& m, std::size_t pc, std::uint64_t target_addr, std::size_t count) {
        m.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(target_addr)));
        m.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(count)));
        m.write_u32(pc + 0x08, movz_x(0, 1));
        m.write_u32(pc + 0x0C, lse_x(0, 0, 0, 1)); // LDADD
        m.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        m.write_u32(pc + 0x14, cbnz_x(2, -12));
        m.write_u32(pc + 0x18, kWfi);
    };

    constexpr std::size_t kOps = 1500;
    // Same 64-byte line (adjacent words)
    install_worker(mem_same, 0x1000, kSameLine + 0, kOps);
    install_worker(mem_same, 0x2000, kSameLine + 8, kOps);
    setup_core(cluster_same, 0, 0x1000);
    setup_core(cluster_same, 1, 0x2000);

    const auto t0 = std::chrono::high_resolution_clock::now();
    cluster_same.run_parallel(0);
    cluster_same.stop_parallel();
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto dur_same = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Different 64-byte lines (offset by 4096 bytes)
    winios::SMPCluster cluster_diff(2, kRamSize);
    Memory& mem_diff = cluster_diff.get_memory();
    install_worker(mem_diff, 0x1000, 0x9700, kOps);
    install_worker(mem_diff, 0x2000, 0xA700, kOps);
    setup_core(cluster_diff, 0, 0x1000);
    setup_core(cluster_diff, 1, 0x2000);

    const auto t2 = std::chrono::high_resolution_clock::now();
    cluster_diff.run_parallel(0);
    cluster_diff.stop_parallel();
    const auto t3 = std::chrono::high_resolution_clock::now();
    const auto dur_diff = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    CHECK(mem_same.read_u64(kSameLine + 0) == kOps);
    CHECK(mem_same.read_u64(kSameLine + 8) == kOps);
    CHECK(mem_diff.read_u64(0x9700) == kOps);
    CHECK(mem_diff.read_u64(0xA700) == kOps);

    std::cout << "  [Benchmark] False-sharing: same-line=" << dur_same << "us, diff-line=" << dur_diff << "us\n";
}

// ===========================================================================
// Stage 7: Cross-Core TLB Synchronization
// ===========================================================================

void cross_core_tlb_synchronization() {
    winios::SMPCluster cluster(2, 0x40000);
    Memory& memory = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    core0.set_architectural_exceptions(true);
    core1.set_architectural_exceptions(true);
    core0.set_current_el(1);
    core1.set_current_el(1);

    // Page table setup
    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t page_a  = 0x8000;
    const std::uint64_t page_b  = 0x9000;
    const std::uint64_t page_c  = 0xA000;
    const std::uint64_t page_d  = 0xB000;

    memory.write_u64(l0_base, l1_base | 0x3ULL);
    memory.write_u64(l1_base, l2_base | 0x3ULL);
    memory.write_u64(l2_base, l3_base | 0x3ULL);
    // Identity-map page 0 (code)
    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));
    // Map VA 0x1000 to page_a (0x8000)
    memory.write_u64(l3_base + 8u, page_a | 0x3ULL | (1ULL << 10u));
    // Map VA 0x6000 to page_c (0xA000) (index 6: 0x6000 >> 12 = 6, 6 * 8 = 48)
    memory.write_u64(l3_base + 48u, page_c | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    memory.write_u64(page_a, 0xAAAAAAAAAAAAAAAAULL);
    memory.write_u64(page_b, 0xBBBBBBBBBBBBBBBBULL);
    memory.write_u64(page_c, 0xCCCCCCCCCCCCCCCCULL);
    memory.write_u64(page_d, 0xDDDDDDDDDDDDDDDDULL);

    core0.set_ttbr0_el1(l0_base);
    core0.set_sctlr_el1(0x00C50839ULL); // MMU on
    core1.set_ttbr0_el1(l0_base);
    core1.set_sctlr_el1(0x00C50839ULL); // MMU on

    // 1. Initial translation caching on both cores
    core0.write_register(2, 0x1000);
    core0.set_program_counter(0x100);
    memory.write_u32(0x100, ldr_x(1, 2));
    core0.step(memory);
    CHECK(core0.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);

    core1.write_register(2, 0x1000);
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);

    // 2. Modify page table entry to point to page_b in RAM
    memory.write_u64(l3_base + 8u, page_b | 0x3ULL | (1ULL << 10u));

    // TLB Hit check: without invalidation, both return cached page_a
    core0.set_program_counter(0x100);
    core0.step(memory);
    CHECK(core0.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);

    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);

    // 3. Core 0 executes non-IS TLBI VMALLE1 (0xD508871F)
    core0.set_program_counter(0x104);
    memory.write_u32(0x104, kTlbiVmalle1);
    core0.step(memory);

    // Core 0 refetches from memory -> gets page_b!
    core0.set_program_counter(0x100);
    core0.step(memory);
    CHECK(core0.read_register(1) == 0xBBBBBBBBBBBBBBBBULL);

    // Core 1 was NOT invalidated by local TLBI -> still has cached page_a!
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xAAAAAAAAAAAAAAAAULL);

    // 4. Now Core 0 executes Inner Shareable TLBI VMALLE1IS (0xD508831F)
    core0.set_program_counter(0x108);
    memory.write_u32(0x108, kTlbiVmalle1is);
    core0.step(memory);

    // Core 1 TLB was invalidated by the IS broadcast -> reloads page_b!
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xBBBBBBBBBBBBBBBBULL);

    // 5. Test TLBI VAE1IS (targeted VA broadcast)
    core1.write_register(2, 0x6000);
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xCCCCCCCCCCCCCCCCULL);

    // Modify PTE for 0x6000 to page_d
    memory.write_u64(l3_base + 48u, page_d | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    // Core 1 still gets page_c due to TLB hit
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xCCCCCCCCCCCCCCCCULL);

    // Core 0 executes TLBI VAE1IS with Xt containing VA 0x6000 (ASID=0)
    core0.write_register(3, 0x6ULL);
    core0.set_program_counter(0x10C);
    memory.write_u32(0x10C, tlbi_vae1is(3));
    core0.step(memory);

    // Core 1 now sees page_d after TLBI VAE1IS broadcast!
    core1.set_program_counter(0x100);
    core1.step(memory);
    CHECK(core1.read_register(1) == 0xDDDDDDDDDDDDDDDDULL);
    std::cout << "  [PASS] Cross-Core TLB Synchronization\n";
}

// ===========================================================================
// Stage 8: Instruction and Data Synchronization (Self-Modifying Code)
// ===========================================================================

void instruction_data_synchronization() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kTargetCodeAddr = 0x9800;

    // Initially target has NOP then RET
    mem.write_u32(kTargetCodeAddr + 0, kNop);
    mem.write_u32(kTargetCodeAddr + 4, kRet);

    const std::uint32_t new_insn = add_x_imm(0, 0, 42);
    mem.write_u32(0x1000, movz_x(1, static_cast<std::uint16_t>(kTargetCodeAddr)));
    mem.write_u32(0x1004, movz_x(2, static_cast<std::uint16_t>(new_insn & 0xFFFFu)));
    mem.write_u32(0x1008, movk_x(2, static_cast<std::uint16_t>((new_insn >> 16u) & 0xFFFFu), 16));
    mem.write_u32(0x100C, str_w(2, 1));
    mem.write_u32(0x1010, dc_cvau(1));
    mem.write_u32(0x1014, kDsbIsh);
    mem.write_u32(0x1018, ic_ivau(1));
    mem.write_u32(0x101C, kDsbIsh);
    mem.write_u32(0x1020, kIsb);
    mem.write_u32(0x1024, b_rel(static_cast<std::int64_t>(kTargetCodeAddr) - 0x1024));

    cluster.initialize_core(0, 0x1000);
    cluster.get_core(0).set_current_el(1);
    cluster.halt_core(1);

    for (int i = 0; i < 10; ++i) {
        cluster.get_core(0).step(mem);
    }
    CHECK(cluster.get_core(0).get_program_counter() == kTargetCodeAddr);

    // Execute modified instruction
    cluster.get_core(0).step(mem); // ADD X0, X0, #42
    CHECK(cluster.get_core(0).read_register(0) == 42);
    std::cout << "  [PASS] Instruction & Data Synchronization (Self-Modifying Code)\n";
}

// ===========================================================================
// Stage 10: Multi-Core Stress Testing Suite (10 Distinct Stress Scenarios)
// ===========================================================================

// 1. Two-Core Memory Stress
void stress_two_core_memory() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::size_t kIter = 2000;

    auto install_writer = [&](std::size_t pc, std::uint64_t addr, std::uint64_t val) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(addr)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kIter)));
        mem.write_u32(pc + 0x08, movz_x(0, static_cast<std::uint16_t>(val)));
        mem.write_u32(pc + 0x0C, str_x(0, 1));
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12));
        mem.write_u32(pc + 0x18, kWfi);
    };

    install_writer(0x1000, 0x9900, 0x1111);
    install_writer(0x2000, 0x9908, 0x2222);

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(0x9900) == 0x1111);
    CHECK(mem.read_u64(0x9908) == 0x2222);
    std::cout << "  [PASS] Stress 1: Two-Core Memory Stress\n";
}

// 2. Four-Core Memory Stress
void stress_four_core_memory() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::size_t kIter = 1500;

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t pc = 0x1000 + i * 0x1000;
        const std::uint64_t addr = 0x9A00 + i * 8;
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(addr)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kIter)));
        mem.write_u32(pc + 0x08, movz_x(0, static_cast<std::uint16_t>(i + 1)));
        mem.write_u32(pc + 0x0C, str_x(0, 1));
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12));
        mem.write_u32(pc + 0x18, kWfi);

        setup_core(cluster, i, pc);
    }

    cluster.run_parallel(0);
    cluster.stop_parallel();

    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(mem.read_u64(0x9A00 + i * 8) == i + 1);
    }
    std::cout << "  [PASS] Stress 2: Four-Core Memory Stress\n";
}

// 3. Atomic Contention Stress
void stress_atomic_contention() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kAtomicAddr = 0x9B00;
    constexpr std::size_t kPerCore = 1500;
    mem.write_u64(kAtomicAddr, 0);

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t pc = 0x1000 + i * 0x1000;
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kAtomicAddr)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kPerCore)));
        mem.write_u32(pc + 0x08, movz_x(0, 1));
        mem.write_u32(pc + 0x0C, lse_x(0, 0, 0, 1)); // LDADD
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12));
        mem.write_u32(pc + 0x18, kWfi);

        setup_core(cluster, i, pc);
    }

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kAtomicAddr) == kPerCore * 4);
    std::cout << "  [PASS] Stress 3: Atomic Contention Stress\n";
}

// 4. Exclusive-Monitor Contention Stress
void stress_exclusive_contention() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kExclAddr = 0x9C00;
    constexpr std::size_t kPerCore = 1000;
    mem.write_u64(kExclAddr, 0);

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t pc = 0x1000 + i * 0x1000;
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kExclAddr)));
        mem.write_u32(pc + 0x04, movz_x(4, static_cast<std::uint16_t>(kPerCore)));
        mem.write_u32(pc + 0x08, ldxr_x(0, 1));
        mem.write_u32(pc + 0x0C, add_x_imm(0, 0, 1));
        mem.write_u32(pc + 0x10, stxr_w(2, 0, 1));
        mem.write_u32(pc + 0x14, cbnz_w(2, -12));
        mem.write_u32(pc + 0x18, subs_x_imm(4, 4, 1));
        mem.write_u32(pc + 0x1C, cbnz_x(4, -20));
        mem.write_u32(pc + 0x20, kWfi);

        setup_core(cluster, i, pc);
    }

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kExclAddr) == kPerCore * 4);
    std::cout << "  [PASS] Stress 4: Exclusive-Monitor Contention Stress\n";
}

// 5. Cache-Line Sharing Stress
void stress_cache_line_sharing() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kLine = 0x9D00;
    constexpr std::size_t kIter = 1000;

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t pc = 0x1000 + i * 0x1000;
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kLine + i * 8)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kIter)));
        mem.write_u32(pc + 0x08, movz_x(0, 1));
        mem.write_u32(pc + 0x0C, lse_x(0, 0, 0, 1));
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12));
        mem.write_u32(pc + 0x18, kWfi);

        setup_core(cluster, i, pc);
    }

    cluster.run_parallel(0);
    cluster.stop_parallel();

    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(mem.read_u64(kLine + i * 8) == kIter);
    }
    std::cout << "  [PASS] Stress 5: Cache-Line Sharing Stress\n";
}

// 6. Barrier Ping-Pong Synchronization Stress
void stress_barrier_ping_pong() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kTurnAddr = 0x9E00;
    mem.write_u64(kTurnAddr, 0);

    constexpr std::size_t kRounds = 100;

    // Core 0 acts on even turns: 0, 2, 4... up to 2 * kRounds - 2
    // Core 1 acts on odd turns: 1, 3, 5... up to 2 * kRounds - 1
    auto install_ping = [&](std::size_t pc, unsigned start_turn) {
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kTurnAddr)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kRounds)));
        mem.write_u32(pc + 0x08, movz_x(3, static_cast<std::uint16_t>(start_turn)));
        // Loop head (pc + 0x0C):
        // Spin until [X1] == X3
        mem.write_u32(pc + 0x0C, ldr_x(4, 1));
        mem.write_u32(pc + 0x10, subs_x_reg(5, 4, 3));
        mem.write_u32(pc + 0x14, cbnz_x(5, -8)); // if X5 != 0, retry LDR at pc + 0x0C
        mem.write_u32(pc + 0x18, kDmbIsh);
        mem.write_u32(pc + 0x1C, add_x_imm(4, 3, 1));
        mem.write_u32(pc + 0x20, str_x(4, 1)); // write turn + 1
        mem.write_u32(pc + 0x24, kDmbIsh);
        mem.write_u32(pc + 0x28, add_x_imm(3, 3, 2)); // next turn for this core
        mem.write_u32(pc + 0x2C, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x30, cbnz_x(2, -36)); // loop to pc + 0x0C
        mem.write_u32(pc + 0x34, kWfi);
    };

    install_ping(0x1000, 0); // Core 0: even turns
    install_ping(0x2000, 1); // Core 1: odd turns

    setup_core(cluster, 0, 0x1000);
    setup_core(cluster, 1, 0x2000);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kTurnAddr) == kRounds * 2);
    std::cout << "  [PASS] Stress 6: Barrier Ping-Pong Synchronization\n";
}

// 7. Cross-Core TLBI Stress
void stress_cross_core_tlbi() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();

    // Core 0 executes 500 TLBI VMALLE1IS instructions
    mem.write_u32(0x1000, movz_x(1, 500));
    mem.write_u32(0x1004, kTlbiVmalle1is);
    mem.write_u32(0x1008, subs_x_imm(1, 1, 1));
    mem.write_u32(0x100C, cbnz_x(1, -8));
    mem.write_u32(0x1010, kWfi);

    // Core 1 executes memory accesses concurrently
    mem.write_u32(0x2000, movz_x(0, 0x9F00));
    mem.write_u32(0x2004, movz_x(1, 500));
    mem.write_u32(0x2008, str_x(1, 0));
    mem.write_u32(0x200C, subs_x_imm(1, 1, 1));
    mem.write_u32(0x2010, cbnz_x(1, -8));
    mem.write_u32(0x2014, kWfi);

    setup_core(cluster, 0, 0x1000);
    cluster.get_core(0).set_current_el(1);
    setup_core(cluster, 1, 0x2000);
    cluster.get_core(1).set_current_el(1);

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(cluster.get_core(0).read_register(1) == 0);
    CHECK(cluster.get_core(1).read_register(1) == 0);
    std::cout << "  [PASS] Stress 7: Cross-Core TLBI Stress\n";
}

// 8. Parallel Start / Stop Lifecycle Stress
void stress_parallel_start_stop() {
    winios::SMPCluster cluster(2, kRamSize);
    Memory& mem = cluster.get_memory();
    mem.write_u32(0x1000, kWfi);
    setup_core(cluster, 0, 0x1000);
    cluster.halt_core(1);

    for (int i = 0; i < 20; ++i) {
        cluster.start_parallel();
        CHECK(cluster.parallel_started());
        cluster.run_parallel(0);
        cluster.stop_parallel();
        CHECK(!cluster.parallel_started());
        setup_core(cluster, 0, 0x1000);
    }
    std::cout << "  [PASS] Stress 8: Parallel Start / Stop Lifecycle\n";
}

// 9. Long-Running Shared Memory Stress
void stress_long_running_shared_memory() {
    winios::SMPCluster cluster(4, kRamSize);
    Memory& mem = cluster.get_memory();
    constexpr std::uint64_t kSharedArray = 0x9000;
    constexpr std::size_t kElements = 16;
    for (std::size_t i = 0; i < kElements; ++i) {
        mem.write_u64(kSharedArray + i * 8, 0);
    }

    constexpr std::size_t kIter = 800;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t pc = 0x1000 + i * 0x1000;
        mem.write_u32(pc + 0x00, movz_x(1, static_cast<std::uint16_t>(kSharedArray)));
        mem.write_u32(pc + 0x04, movz_x(2, static_cast<std::uint16_t>(kIter)));
        mem.write_u32(pc + 0x08, movz_x(0, 1));
        mem.write_u32(pc + 0x0C, lse_x(0, 0, 0, 1)); // LDADD
        mem.write_u32(pc + 0x10, subs_x_imm(2, 2, 1));
        mem.write_u32(pc + 0x14, cbnz_x(2, -12));
        mem.write_u32(pc + 0x18, kWfi);

        setup_core(cluster, i, pc);
    }

    cluster.run_parallel(0);
    cluster.stop_parallel();

    CHECK(mem.read_u64(kSharedArray) == kIter * 4);
    std::cout << "  [PASS] Stress 9: Long-Running Shared Memory Stress\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cout << "=== WinIOS Phase 44 Memory Consistency & SMP Litmus Test Suite ===\n";

    // Stage 3: Litmus Tests
    std::cout << ">>> Running litmus_store_buffering..." << std::endl;
    litmus_store_buffering();
    std::cout << "<<< Completed litmus_store_buffering." << std::endl;
    std::cout << ">>> Running litmus_message_passing..." << std::endl;
    litmus_message_passing();
    std::cout << "<<< Completed litmus_message_passing." << std::endl;
    std::cout << ">>> Running litmus_message_passing_release_acquire..." << std::endl;
    litmus_message_passing_release_acquire();
    std::cout << "<<< Completed litmus_message_passing_release_acquire." << std::endl;
    std::cout << ">>> Running litmus_load_buffering..." << std::endl;
    litmus_load_buffering();
    std::cout << "<<< Completed litmus_load_buffering." << std::endl;
    std::cout << ">>> Running litmus_iriw_4cores..." << std::endl;
    litmus_iriw_4cores();
    std::cout << "<<< Completed litmus_iriw_4cores." << std::endl;
    std::cout << ">>> Running litmus_wrc_causality..." << std::endl;
    litmus_wrc_causality();
    std::cout << "<<< Completed litmus_wrc_causality." << std::endl;
    std::cout << ">>> Running litmus_rwc_causality..." << std::endl;
    litmus_rwc_causality();
    std::cout << "<<< Completed litmus_rwc_causality." << std::endl;
    std::cout << ">>> Running barrier_semantics_validation..." << std::endl;
    barrier_semantics_validation();
    std::cout << "<<< Completed barrier_semantics_validation." << std::endl;

    // Stage 4: Exclusive Monitor & LSE
    std::cout << ">>> Running exclusive_monitor_concurrency_2cores..." << std::endl;
    exclusive_monitor_concurrency_2cores();
    std::cout << "<<< Completed exclusive_monitor_concurrency_2cores." << std::endl;
    std::cout << ">>> Running exclusive_monitor_concurrency_4cores..." << std::endl;
    exclusive_monitor_concurrency_4cores();
    std::cout << "<<< Completed exclusive_monitor_concurrency_4cores." << std::endl;
    std::cout << ">>> Running exclusive_pair_atomicity..." << std::endl;
    exclusive_pair_atomicity();
    std::cout << "<<< Completed exclusive_pair_atomicity." << std::endl;
    std::cout << ">>> Running exclusive_monitor_overlap_and_granularity..." << std::endl;
    exclusive_monitor_overlap_and_granularity();
    std::cout << "<<< Completed exclusive_monitor_overlap_and_granularity." << std::endl;
    std::cout << ">>> Running lse_atomics_concurrency_4cores..." << std::endl;
    lse_atomics_concurrency_4cores();
    std::cout << "<<< Completed lse_atomics_concurrency_4cores." << std::endl;

    // Stage 5 & 6: Cache Coherency & Cache Lines
    std::cout << ">>> Running cache_dc_zva_validation..." << std::endl;
    cache_dc_zva_validation();
    std::cout << "<<< Completed cache_dc_zva_validation." << std::endl;
    std::cout << ">>> Running cache_maintenance_ops_validation..." << std::endl;
    cache_maintenance_ops_validation();
    std::cout << "<<< Completed cache_maintenance_ops_validation." << std::endl;
    std::cout << ">>> Running cache_line_concurrent_sharing..." << std::endl;
    cache_line_concurrent_sharing();
    std::cout << "<<< Completed cache_line_concurrent_sharing." << std::endl;
    std::cout << ">>> Running cache_line_false_sharing_benchmark..." << std::endl;
    cache_line_false_sharing_benchmark();
    std::cout << "<<< Completed cache_line_false_sharing_benchmark." << std::endl;

    // Stage 7: TLB Synchronization
    std::cout << ">>> Running cross_core_tlb_synchronization..." << std::endl;
    cross_core_tlb_synchronization();
    std::cout << "<<< Completed cross_core_tlb_synchronization." << std::endl;

    // Stage 8: Instruction / Data Synchronization
    std::cout << ">>> Running instruction_data_synchronization..." << std::endl;
    instruction_data_synchronization();
    std::cout << "<<< Completed instruction_data_synchronization." << std::endl;

    // Stage 10: Stress Testing Suite
    std::cout << ">>> Running stress_two_core_memory..." << std::endl;
    stress_two_core_memory();
    std::cout << "<<< Completed stress_two_core_memory." << std::endl;
    std::cout << ">>> Running stress_four_core_memory..." << std::endl;
    stress_four_core_memory();
    std::cout << "<<< Completed stress_four_core_memory." << std::endl;
    std::cout << ">>> Running stress_atomic_contention..." << std::endl;
    stress_atomic_contention();
    std::cout << "<<< Completed stress_atomic_contention." << std::endl;
    std::cout << ">>> Running stress_exclusive_contention..." << std::endl;
    stress_exclusive_contention();
    std::cout << "<<< Completed stress_exclusive_contention." << std::endl;
    std::cout << ">>> Running stress_cache_line_sharing..." << std::endl;
    stress_cache_line_sharing();
    std::cout << "<<< Completed stress_cache_line_sharing." << std::endl;
    std::cout << ">>> Running stress_barrier_ping_pong..." << std::endl;
    stress_barrier_ping_pong();
    std::cout << "<<< Completed stress_barrier_ping_pong." << std::endl;
    std::cout << ">>> Running stress_cross_core_tlbi..." << std::endl;
    stress_cross_core_tlbi();
    std::cout << "<<< Completed stress_cross_core_tlbi." << std::endl;
    std::cout << ">>> Running stress_parallel_start_stop..." << std::endl;
    stress_parallel_start_stop();
    std::cout << "<<< Completed stress_parallel_start_stop." << std::endl;
    std::cout << ">>> Running stress_long_running_shared_memory..." << std::endl;
    stress_long_running_shared_memory();
    std::cout << "<<< Completed stress_long_running_shared_memory." << std::endl;

    return test::summarize("memory_ordering_tests");
}
