#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "cpu/cpu.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define STAGE65_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

constexpr std::uint32_t kLdxr   = 0xC85F7C20u; // LDXR X0, [X1]
constexpr std::uint32_t kStxr   = 0xC8027C20u; // STXR W2, X0, [X1]
constexpr std::uint32_t kLdar   = 0xC8DFFC20u; // LDAR X0, [X1]
constexpr std::uint32_t kStlr   = 0xC89FFC22u; // STLR X2, [X1]
constexpr std::uint32_t kClrex  = 0xD5033F5Fu; // CLREX
constexpr std::uint32_t kDmbSy  = 0xD5033FBFu; // DMB #15 (SY)
constexpr std::uint32_t kDsbSy  = 0xD5033F9Fu; // DSB #15 (SY)
constexpr std::uint32_t kIsbSy  = 0xD5033FDFu; // ISB #15 (SY)

constexpr std::uint32_t kCasX   = 0xC8A07C41u; // CAS X0, X1, [X2]
constexpr std::uint32_t kCasalW = 0x88E07C41u; // CASAL W0, W1, [X2]
constexpr std::uint32_t kLdaddW = 0xB82300A4u; // LDADD W3, W4, [X5]
constexpr std::uint32_t kLdclrW = 0xB82310A4u; // LDCLR W3, W4, [X5]
constexpr std::uint32_t kLdeorW = 0xB82320A4u; // LDEOR W3, W4, [X5]
constexpr std::uint32_t kLdsetW = 0xB82330A4u; // LDSET W3, W4, [X5]
constexpr std::uint32_t kSwpW   = 0xB82380A4u; // SWP W3, W4, [X5]

void step_inst(CPU& cpu, Memory& memory, std::uint32_t inst) {
    memory.write_u32(cpu.get_program_counter(), inst);
    cpu.step(memory);
}

// -----------------------------------------------------------------------------
// Suite 1: LDAR and STLR Ordered Memory Access
// -----------------------------------------------------------------------------
bool test_ldar_stlr_ordered_access() {
    std::cout << "[1] test_ldar_stlr_ordered_access...\n";
    CPU cpu;
    Memory memory(0x4000);

    // X1 holds base address 0x200, X2 holds 0xDEADBEEFCAFE0001
    cpu.write_register(1, 0x200);
    cpu.write_register(2, 0xDEADBEEFCAFE0001ULL);

    // STLR X2, [X1]
    step_inst(cpu, memory, kStlr);
    STAGE65_ASSERT(memory.read_u64(0x200) == 0xDEADBEEFCAFE0001ULL, "STLR commits to memory");

    // LDAR X0, [X1]
    step_inst(cpu, memory, kLdar);
    STAGE65_ASSERT(cpu.read_register(0) == 0xDEADBEEFCAFE0001ULL, "LDAR reads matching value into X0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: LDXR / STXR Exclusive Access Success
// -----------------------------------------------------------------------------
bool test_ldxr_stxr_exclusive_success() {
    std::cout << "[2] test_ldxr_stxr_exclusive_success...\n";
    CPU cpu;
    Memory memory(0x4000);

    cpu.write_register(1, 0x240);
    memory.write_u64(0x240, 0x1111222233334444ULL);

    // LDXR X0, [X1]
    step_inst(cpu, memory, kLdxr);
    STAGE65_ASSERT(cpu.read_register(0) == 0x1111222233334444ULL, "LDXR loaded initial value");
    STAGE65_ASSERT(cpu.exclusive_monitor_valid(), "Exclusive monitor must be active after LDXR");

    // Update X0 and attempt STXR W2, X0, [X1]
    cpu.write_register(0, 0x9999888877776666ULL);
    step_inst(cpu, memory, kStxr);

    STAGE65_ASSERT(cpu.read_register(2) == 0, "STXR status must be 0 on success");
    STAGE65_ASSERT(memory.read_u64(0x240) == 0x9999888877776666ULL, "Memory must be updated on STXR success");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: STXR Failure Without Active Monitor
// -----------------------------------------------------------------------------
bool test_stxr_failure_without_ldxr() {
    std::cout << "[3] test_stxr_failure_without_ldxr...\n";
    CPU cpu;
    Memory memory(0x4000);

    cpu.write_register(1, 0x280);
    cpu.write_register(0, 0x5555555555555555ULL);
    memory.write_u64(0x280, 0x1010101010101010ULL);

    STAGE65_ASSERT(!cpu.exclusive_monitor_valid(), "Exclusive monitor must be initially inactive");

    // Execute STXR W2, X0, [X1] without prior LDXR
    step_inst(cpu, memory, kStxr);

    STAGE65_ASSERT(cpu.read_register(2) == 1, "STXR status must be 1 on failure without monitor");
    STAGE65_ASSERT(memory.read_u64(0x280) == 0x1010101010101010ULL, "Memory must remain unchanged on STXR failure");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: CLREX Invalidation of Exclusive Monitor
// -----------------------------------------------------------------------------
bool test_clrex_invalidation() {
    std::cout << "[4] test_clrex_invalidation...\n";
    CPU cpu;
    Memory memory(0x4000);

    cpu.write_register(1, 0x300);
    memory.write_u64(0x300, 0x7777ULL);

    // Set monitor via LDXR
    step_inst(cpu, memory, kLdxr);
    STAGE65_ASSERT(cpu.exclusive_monitor_valid(), "Monitor active after LDXR");

    // Issue CLREX
    step_inst(cpu, memory, kClrex);
    STAGE65_ASSERT(!cpu.exclusive_monitor_valid(), "CLREX must clear exclusive monitor");

    // Attempt STXR -> must fail
    cpu.write_register(0, 0x8888ULL);
    step_inst(cpu, memory, kStxr);

    STAGE65_ASSERT(cpu.read_register(2) == 1, "STXR must report failure after CLREX");
    STAGE65_ASSERT(memory.read_u64(0x300) == 0x7777ULL, "Memory untouched after CLREX-invalidated STXR");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Normal Store Clears Exclusive Reservation
// -----------------------------------------------------------------------------
bool test_normal_store_clears_reservation() {
    std::cout << "[5] test_normal_store_clears_reservation...\n";
    CPU cpu;
    Memory memory(0x4000);

    cpu.write_register(1, 0x340);
    memory.write_u64(0x340, 0xAAAAULL);

    // Set monitor
    step_inst(cpu, memory, kLdxr);
    STAGE65_ASSERT(cpu.exclusive_monitor_valid(), "Monitor active after LDXR");

    // Normal store to the reservation granule
    // STR X3, [X1] (0xF9000023u)
    cpu.write_register(3, 0xBBBBULL);
    step_inst(cpu, memory, 0xF9000023u);

    // Monitor must now be invalidated by normal store
    STAGE65_ASSERT(!cpu.exclusive_monitor_valid(), "Normal store must invalidate local exclusive monitor");

    // Subsequent STXR must fail
    cpu.write_register(0, 0xCCCCULL);
    step_inst(cpu, memory, kStxr);
    STAGE65_ASSERT(cpu.read_register(2) == 1, "STXR must fail after intervening store");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: LSE Compare-and-Swap (CAS 64-bit Success & Mismatch)
// -----------------------------------------------------------------------------
bool test_lse_cas_64bit() {
    std::cout << "[6] test_lse_cas_64bit...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Address in X2, Expected in X0, Desired in X1
    cpu.write_register(2, 0x400);
    cpu.write_register(0, 0x11223344ULL);
    cpu.write_register(1, 0xAABBCCDDULL);
    memory.write_u64(0x400, 0x11223344ULL);

    // CAS match: memory matches X0
    step_inst(cpu, memory, kCasX);
    STAGE65_ASSERT(cpu.read_register(0) == 0x11223344ULL, "CAS returns old value in X0 on match");
    STAGE65_ASSERT(memory.read_u64(0x400) == 0xAABBCCDDULL, "CAS updates memory with desired value");

    // CAS mismatch: memory now contains AABBCCDD, try with X0=0x99999999, X1=0x55555555
    cpu.write_register(0, 0x99999999ULL);
    cpu.write_register(1, 0x55555555ULL);
    step_inst(cpu, memory, kCasX);

    STAGE65_ASSERT(cpu.read_register(0) == 0xAABBCCDDULL, "CAS returns actual memory value in X0 on mismatch");
    STAGE65_ASSERT(memory.read_u64(0x400) == 0xAABBCCDDULL, "CAS does not modify memory on mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: LSE CASAL 32-bit Ordered Variant
// -----------------------------------------------------------------------------
bool test_lse_casal_32bit() {
    std::cout << "[7] test_lse_casal_32bit...\n";
    CPU cpu;
    Memory memory(0x4000);

    cpu.write_register(2, 0x440);
    cpu.write_register(0, 0x55667788u);
    cpu.write_register(1, 0x99AABBCCu);
    memory.write_u32(0x440, 0x55667788u);

    // CASAL W0, W1, [X2]
    step_inst(cpu, memory, kCasalW);
    STAGE65_ASSERT(cpu.read_register(0) == 0x55667788u, "CASAL returns old 32-bit value in W0");
    STAGE65_ASSERT(memory.read_u32(0x440) == 0x99AABBCCu, "CASAL updates 32-bit memory location");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: LSE Read-Modify-Write Atomics (LDADD, LDCLR, LDEOR, LDSET, SWP)
// -----------------------------------------------------------------------------
bool test_lse_rmw_primitives() {
    std::cout << "[8] test_lse_rmw_primitives...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Base in X5 = 0x500, Source in W3 = 0x0F
    cpu.write_register(5, 0x500);

    // 1. LDADD: memory = 0xF0, W3 = 0x05 -> new memory = 0xF5, old returned in W4
    memory.write_u32(0x500, 0xF0);
    cpu.write_register(3, 0x05);
    step_inst(cpu, memory, kLdaddW);
    STAGE65_ASSERT(cpu.read_register(4) == 0xF0, "LDADD returns old value 0xF0");
    STAGE65_ASSERT(memory.read_u32(0x500) == 0xF5, "LDADD computes 0xF0 + 0x05 = 0xF5");

    // 2. LDCLR: memory = 0xFF, W3 = 0x0F -> new memory = 0xF0, old returned in W4
    memory.write_u32(0x500, 0xFF);
    cpu.write_register(3, 0x0F);
    step_inst(cpu, memory, kLdclrW);
    STAGE65_ASSERT(cpu.read_register(4) == 0xFF, "LDCLR returns old value 0xFF");
    STAGE65_ASSERT(memory.read_u32(0x500) == 0xF0, "LDCLR computes 0xFF & ~0x0F = 0xF0");

    // 3. LDEOR: memory = 0xAA, W3 = 0x55 -> new memory = 0xFF, old returned in W4
    memory.write_u32(0x500, 0xAA);
    cpu.write_register(3, 0x55);
    step_inst(cpu, memory, kLdeorW);
    STAGE65_ASSERT(cpu.read_register(4) == 0xAA, "LDEOR returns old value 0xAA");
    STAGE65_ASSERT(memory.read_u32(0x500) == 0xFF, "LDEOR computes 0xAA ^ 0x55 = 0xFF");

    // 4. LDSET: memory = 0x50, W3 = 0x05 -> new memory = 0x55, old returned in W4
    memory.write_u32(0x500, 0x50);
    cpu.write_register(3, 0x05);
    step_inst(cpu, memory, kLdsetW);
    STAGE65_ASSERT(cpu.read_register(4) == 0x50, "LDSET returns old value 0x50");
    STAGE65_ASSERT(memory.read_u32(0x500) == 0x55, "LDSET computes 0x50 | 0x05 = 0x55");

    // 5. SWP: memory = 0x1234, W3 = 0x5678 -> new memory = 0x5678, old returned in W4
    memory.write_u32(0x500, 0x1234);
    cpu.write_register(3, 0x5678);
    step_inst(cpu, memory, kSwpW);
    STAGE65_ASSERT(cpu.read_register(4) == 0x1234, "SWP returns old value 0x1234");
    STAGE65_ASSERT(memory.read_u32(0x500) == 0x5678, "SWP updates memory with 0x5678");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Architectural Barriers (DMB, DSB, ISB)
// -----------------------------------------------------------------------------
bool test_architectural_barriers() {
    std::cout << "[9] test_architectural_barriers...\n";
    CPU cpu;
    Memory memory(0x4000);

    const std::uint64_t start_pc = cpu.get_program_counter();

    // DMB #15
    step_inst(cpu, memory, kDmbSy);
    STAGE65_ASSERT(cpu.get_program_counter() == start_pc + 4, "DMB advances PC by 4");

    // DSB #15
    step_inst(cpu, memory, kDsbSy);
    STAGE65_ASSERT(cpu.get_program_counter() == start_pc + 8, "DSB advances PC by 4");

    // ISB #15
    step_inst(cpu, memory, kIsbSy);
    STAGE65_ASSERT(cpu.get_program_counter() == start_pc + 12, "ISB advances PC by 4");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Atomic and Ordered Alignment Fault Enforcement
// -----------------------------------------------------------------------------
bool test_atomic_alignment_faults() {
    std::cout << "[10] test_atomic_alignment_faults...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_architectural_exceptions(true);

    // Unaligned STLR: address 0x201 (not 8-byte aligned)
    cpu.write_register(1, 0x201);
    cpu.write_register(2, 0x1234ULL);

    step_inst(cpu, memory, kStlr);

    // Must trigger Data Abort with Alignment Fault syndrome
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    const std::uint32_t fsc = esr & 0x3Fu;

    STAGE65_ASSERT(ec == static_cast<std::uint32_t>(ExceptionClass::DataAbortSame) ||
                   ec == static_cast<std::uint32_t>(ExceptionClass::DataAbortLower),
                   "EC must be DataAbort on unaligned ordered access");
    STAGE65_ASSERT(fsc == 0x21u, "FSC must be 0x21 (AlignmentFault)");
    STAGE65_ASSERT(cpu.get_far_el1() == 0x201, "FAR_EL1 must hold unaligned fault address");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 65 Memory Ordering, Atomics & Barrier Architecture Tests ===" << std::endl;

    if (!test_ldar_stlr_ordered_access()) return 1;
    if (!test_ldxr_stxr_exclusive_success()) return 2;
    if (!test_stxr_failure_without_ldxr()) return 3;
    if (!test_clrex_invalidation()) return 4;
    if (!test_normal_store_clears_reservation()) return 5;
    if (!test_lse_cas_64bit()) return 6;
    if (!test_lse_casal_32bit()) return 7;
    if (!test_lse_rmw_primitives()) return 8;
    if (!test_architectural_barriers()) return 9;
    if (!test_atomic_alignment_faults()) return 10;

    std::cout << "stage65_atomic_ordering_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
