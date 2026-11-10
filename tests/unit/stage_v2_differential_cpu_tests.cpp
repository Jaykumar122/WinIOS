// =============================================================================
// stage_v2_differential_cpu_tests.cpp
//
// Campaign 191-V Stage V2:
//   Unit tests locking down every real CPU semantic bug uncovered by
//   differential fuzzing against Unicorn Engine 2.1.4 (QEMU TCG AArch64):
//     1. Logical immediate (AND/ORR/EOR imm) Rd=31 writes SP/WSP (opc != 3),
//        while ANDS imm (opc == 3) writes XZR/WZR and updates NZCV.
//     2. Vector CNT Vd.8B/16B, Vn.8B/16B (0x0E205800 / 0x4E205800) executes
//        per-byte population count and disassembles cleanly.
//     3. Cross-check 64-bit / 32-bit edge cases against expected architectural values.
// =============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "memory/memory.h"

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

void step_one(CPU& cpu, Memory& mem, std::uint64_t pc, std::uint32_t insn) {
    mem.write_u32(static_cast<std::size_t>(pc), insn);
    cpu.set_program_counter(pc);
    cpu.step(mem);
}

void test_logical_immediate_sp_destination() {
    std::cout << "[Test 1] Logical immediate Rd=31 writes SP/WSP (opc!=3) vs XZR/WZR (opc==3)...\n";
    CPU cpu;
    Memory mem(0x10000);

    // 1. AND XSP, X0, #0xFFFFFFFFFFFFFFF0 (N=1, immr=4, imms=59 -> 0xFFFFFFFFFFFFFFF0; enc: 0x927CEC1F)
    // Note: Wait - N=1, immr=0, imms=3 (0xF): 0x92400C1F -> AND SP, X0, #0xF
    cpu.reset();
    cpu.write_register(0, 0x123456789ABCDEF5ULL);
    cpu.set_stack_pointer(0x0040000ULL);
    step_one(cpu, mem, 0x100, 0x92400C1Fu); // AND SP, X0, #0xF
    CHECK(cpu.get_stack_pointer() == 0x5ULL);

    // 2. AND WSP, W0, #0xF (sf=0, opc=0, N=0, immr=0, imms=3: 0x12000C1F) -> zero-extends to SP!
    cpu.set_stack_pointer(0xFFFFFFFF0040000ULL);
    cpu.write_register(0, 0xAABBCCDD1122334BULL);
    step_one(cpu, mem, 0x104, 0x12000C1Fu); // AND WSP, W0, #0xF
    CHECK(cpu.get_stack_pointer() == 0xBULL);

    // 3. ORR SP, X0, #0xF (sf=1, opc=1, N=1, immr=0, imms=3: 0xB2400C1F)
    cpu.set_stack_pointer(0x1111ULL);
    cpu.write_register(0, 0x123400ULL);
    step_one(cpu, mem, 0x108, 0xB2400C1Fu); // ORR SP, X0, #0xF
    CHECK(cpu.get_stack_pointer() == 0x12340FULL);

    // 4. EOR SP, X0, #0xF (sf=1, opc=2, N=1, immr=0, imms=3: 0xD2400C1F)
    cpu.write_register(0, 0x12340FULL);
    step_one(cpu, mem, 0x10C, 0xD2400C1Fu); // EOR SP, X0, #0xF
    CHECK(cpu.get_stack_pointer() == 0x123400ULL);

    // 5. ANDS XZR, X0, #0xF (sf=1, opc=3, N=1, immr=0, imms=3: 0xF2400C1F) -> MUST NOT touch SP!
    cpu.set_stack_pointer(0x00077770ULL);
    cpu.write_register(0, 0x10ULL);
    step_one(cpu, mem, 0x110, 0xF2400C1Fu); // ANDS XZR, X0, #0xF -> result 0, Z=1
    CHECK(cpu.get_stack_pointer() == 0x00077770ULL);
    CHECK((cpu.get_nzcv() & 0x4u) != 0); // Z flag set
}

void test_vector_cnt_8b_16b() {
    std::cout << "[Test 2] Vector CNT Vd.8B/16B, Vn.8B/16B & disassembly...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.reset();
    cpu.set_cpacr_el1(0x00300000ULL);

    // Test V1.16B = {0x00, 0xFF, 0x0F, 0xF0, 0x55, 0xAA, 0x01, 0x80} low, {0x13, 0x37, ...} high
    cpu.write_vector_register(1, 0x8001AA55F00FFF00ULL, 0x7E81000F11223713ULL);
    // CNT V2.16B, V1.16B: 0x4E205822
    std::string d16;
    cpu.set_trace_sink([&](const CPU::TraceRecord& rec) { d16 = rec.disassembly; });
    step_one(cpu, mem, 0x200, 0x4E205822u);
    auto v2 = cpu.read_vector_register(2);
    // Low bytes popcount:  00->0, FF->8, 0F->4, F0->4, 55->4, AA->4, 01->1, 80->1 => 0x0101040404040800
    // High bytes popcount: 13->3, 37->5, 22->2, 11->2, 0F->4, 00->0, 81->2, 7E->6 => 0x0602000402020503
    CHECK(v2.low == 0x0101040404040800ULL);
    CHECK(v2.high == 0x0602000402020503ULL);

    // CNT V3.8B, V1.8B: 0x0E205823 -> upper 64 bits cleared!
    std::string d8;
    cpu.set_trace_sink([&](const CPU::TraceRecord& rec) { d8 = rec.disassembly; });
    cpu.write_vector_register(3, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
    step_one(cpu, mem, 0x204, 0x0E205823u);
    auto v3 = cpu.read_vector_register(3);
    CHECK(v3.low == 0x0101040404040800ULL);
    CHECK(v3.high == 0ULL);

    CHECK(d16.find("CNT") != std::string::npos);
    CHECK(d16.find(".16B") != std::string::npos);
    CHECK(d8.find("CNT") != std::string::npos);
    CHECK(d8.find(".8B") != std::string::npos);
}

} // namespace

int main() {
    try {
        test_logical_immediate_sp_destination();
        test_vector_cnt_8b_16b();
    } catch (const std::exception& ex) {
        std::cerr << "[EXCEPTION] " << ex.what() << "\n";
        return 1;
    }

    if (g_failed != 0) {
        std::cerr << "stage_v2_differential_cpu_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE V2 DIFFERENTIAL CPU FIX CHECKS PASSED\n";
    return 0;
}
