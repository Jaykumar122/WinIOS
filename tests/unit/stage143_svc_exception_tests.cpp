#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to encode SVC #imm16: 1101 0100 000[imm16] 00001
// 0xD4000001 | (imm16 << 5)
std::uint32_t encode_svc(std::uint16_t imm16) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u);
}

void setup_el0_svc_env(CPU& cpu, Memory& mem, std::uint64_t code_addr = 0x2000, std::uint64_t vbar_el1 = 0x10000) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_el1);

    // Vector table offsets in VBAR_EL1:
    // +0x400: Synchronous 64-bit exception from lower EL
    mem.write_u32(vbar_el1 + 0x400, 0xD503201Fu); // NOP at vector handler

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(code_addr);
}

// Test 1: Architectural SVC Syndrome (ESR_EL1), Return PC (ELR_EL1), and Vector Offset
bool test_svc_syndrome_and_vectors() {
    std::cout << "[Test 1] Architectural SVC syndrome (ESR_EL1), ELR_EL1, and vector offset...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_svc_env(cpu, mem, 0x2000, 0x10000);

    // Set initial NZCV = 0x9 (N=1, V=1)
    cpu.set_nzcv(0x9u);

    // Write SVC #0x80 at 0x2000
    mem.write_u32(0x2000, encode_svc(0x80));

    cpu.step(mem);

    // 1. Must transition to EL1
    TEST_ASSERT(cpu.get_current_el() == 1, "CPU must transition to EL1 upon SVC");

    // 2. Vector table PC = VBAR_EL1 + 0x400
    TEST_ASSERT(cpu.get_program_counter() == 0x10400, "PC must be at lower EL synchronous vector (VBAR_EL1 + 0x400)");

    // 3. ELR_EL1 must point to instruction following SVC (0x2004)
    TEST_ASSERT(cpu.get_elr_el1() == 0x2004, "ELR_EL1 must equal 0x2004");

    // 4. ESR_EL1: EC=0x15 (SVC AArch64), IL=1 (bit 25), ISS=0x80
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    const std::uint32_t il = (esr >> 25u) & 0x1u;
    const std::uint32_t iss = esr & 0xFFFFu;

    TEST_ASSERT(ec == 0x15u, "ESR_EL1 EC must be 0x15 (AArch64 SVC)");
    TEST_ASSERT(il == 1u, "ESR_EL1 IL bit must be 1 (32-bit instruction length)");
    TEST_ASSERT(iss == 0x80u, "ESR_EL1 ISS must match SVC immediate (0x80)");

    // 5. SPSR_EL1: Mode=EL0 (0), SPSel=0, DAIF bits masked in EL1, NZCV preserved
    const std::uint32_t spsr = cpu.get_spsr_el1();
    TEST_ASSERT((spsr & 0xFu) == 0x0u, "SPSR_EL1 mode must be EL0t (0b0000)");
    TEST_ASSERT(((spsr >> 28u) & 0xFu) == 0x9u, "SPSR_EL1 NZCV must preserve 0x9");

    // 6. Stack pointer must have switched to SP_EL1
    TEST_ASSERT(cpu.get_sp_sel() == true, "SPSel must be 1 in EL1");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x50000, "Active SP must be SP_EL1 (0x50000)");

    return true;
}

// Test 2: SVC Immediate Decoding Across Full Range (0x0, 0x80, 0x81, 0xFFFF)
bool test_svc_immediate_range_decoding() {
    std::cout << "[Test 2] SVC immediate decoding across range (0x0, 0x80, 0x81, 0xFFFF)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;

    const std::uint16_t immediates[] = {0x0000, 0x0080, 0x0081, 0x1234, 0xFFFF};
    for (std::uint16_t imm : immediates) {
        setup_el0_svc_env(cpu, mem, 0x4000, 0x10000);
        mem.write_u32(0x4000, encode_svc(imm));

        cpu.step(mem);

        TEST_ASSERT(cpu.get_current_el() == 1, "Must transition to EL1");
        const std::uint32_t esr = cpu.get_esr_el1();
        TEST_ASSERT(((esr >> 26u) & 0x3Fu) == 0x15u, "EC must be 0x15 for SVC");
        TEST_ASSERT((esr & 0xFFFFu) == imm, "ISS must accurately capture immediate 0x" + std::to_string(imm));
        TEST_ASSERT(cpu.get_elr_el1() == 0x4004, "ELR_EL1 must point to 0x4004");
    }

    return true;
}

// Test 3: Standard Darwin BSD Syscall Routing via SVC #0x80
bool test_darwin_bsd_syscall_routing() {
    std::cout << "[Test 3] Darwin BSD syscall routing via SVC #0x80...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    setup_el0_svc_env(cpu, mem, 0x1000, 0x10000);

    // Call SYS_getpid (X16 = 20) via SVC #0x80
    // Assembly:
    //   mov x16, #20  (0xD2800290)
    //   svc #0x80     (0xD4001001)
    mem.write_u32(0x1000, 0xD2800290u);
    mem.write_u32(0x1004, 0xD4001001u);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};
    config.max_instructions = 10;

    // Step MOV X16, #20
    bool can_continue = engine.step_one(result, config);
    TEST_ASSERT(can_continue, "Engine step 1 must succeed");
    TEST_ASSERT(cpu.read_register(16) == 20, "X16 must be 20");

    // Step SVC #0x80
    can_continue = engine.step_one(result, config);
    TEST_ASSERT(can_continue, "Engine step 2 must handle syscall and resume EL0");

    TEST_ASSERT(result.bsd_syscalls_handled == 1, "One BSD syscall must be handled");
    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must resume in EL0");
    TEST_ASSERT(cpu.read_register(0) == 1, "SYS_getpid must return PID 1 in X0");
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "PC must advance past SVC to 0x1008");

    return true;
}

// Test 4: Darwin Mach Trap Routing via SVC #0x81
bool test_darwin_mach_trap_routing_svc81() {
    std::cout << "[Test 4] Darwin Mach trap routing via SVC #0x81...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    setup_el0_svc_env(cpu, mem, 0x1000, 0x10000);

    // Call task_self_trap (X16 = -28) via SVC #0x81
    // Assembly:
    //   mov x16, #-28  (0x92800370: movn x16, #27 -> x16 = -28)
    //   svc #0x81      (0xD4001021)
    mem.write_u32(0x1000, 0x92800370u); // movn x16, #27 (0xFFFFFFFFFFFFFFE4 = -28)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};
    config.max_instructions = 10;

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(result.mach_traps_handled == 1, "Mach trap must be handled via SVC #0x81");
    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must resume in EL0");
    TEST_ASSERT(cpu.read_register(0) == 101, "task_self_trap must return valid task port (101)");
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "PC must advance past SVC #0x81");

    return true;
}

// Test 5: Authentic Darwin XNU locore.s Convention: Mach Traps via SVC #0x80 with Negative X16
bool test_darwin_xnu_negative_x16_mach_routing() {
    std::cout << "[Test 5] Authentic Darwin XNU locore.s: Mach traps via SVC #0x80 with negative X16...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    setup_el0_svc_env(cpu, mem, 0x1000, 0x10000);

    // Call mach_reply_port_trap (X16 = -26) via standard SVC #0x80
    // Assembly:
    //   mov x16, #-26  (0x92800330: movn x16, #25 -> -26)
    //   svc #0x80      (0xD4001001)
    mem.write_u32(0x1000, 0x92800330u); // movn x16, #25 (-26)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};
    config.max_instructions = 10;

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(result.mach_traps_handled == 1, "Negative X16 under SVC #0x80 must route to Mach trap handler");
    TEST_ASSERT(cpu.read_register(0) == 102, "mach_reply_port_trap must return reply port (102)");
    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must resume in EL0");

    return true;
}

// Test 6: Unknown / Unimplemented Syscall Handling (ENOSYS / 78)
bool test_unimplemented_syscall_enosys() {
    std::cout << "[Test 6] Unknown / unimplemented syscall returns ENOSYS (78)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    setup_el0_svc_env(cpu, mem, 0x1000, 0x10000);

    // Call unsupported syscall X16 = 999 via SVC #0x80
    mem.write_u32(0x1000, 0xD2807CE0u); // mov x16, #999
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};
    config.max_instructions = 10;

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(result.bsd_syscalls_handled == 1, "Syscall must be handled");
    TEST_ASSERT(cpu.read_register(0) == 78u, "Unsupported syscall must return ENOSYS (78)");
    TEST_ASSERT(cpu.get_current_el() == 0, "CPU must resume in EL0");

    return true;
}

// Test 7: Exception Return (ERET) from EL1 to EL0
bool test_eret_el1_to_el0() {
    std::cout << "[Test 7] Exception Return (ERET) from EL1 to EL0...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    setup_el0_svc_env(cpu, mem, 0x1000, 0x10000);

    // In EL1: set up SPSR_EL1 with target EL0 (M=0x00), NZCV=0x4 (Z=1), DAIF=0
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_elr_el1(0x6000);
    // SPSR: NZCV=0x4 in bits[31:28] = 0x40000000, M=0x00
    cpu.set_spsr_el1(0x40000000u);

    // Write ERET at 0x10400 (vector handler)
    mem.write_u32(0x10400, 0xD69F03E0u);
    cpu.set_program_counter(0x10400);

    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 0, "ERET must restore EL0");
    TEST_ASSERT(cpu.get_sp_sel() == false, "ERET to EL0 must set SPSel=0 (SP_EL0)");
    TEST_ASSERT(cpu.get_program_counter() == 0x6000, "PC must be restored from ELR_EL1 (0x6000)");
    TEST_ASSERT(cpu.get_nzcv() == 0x4u, "NZCV flags must be restored from SPSR_EL1 (Z=1)");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 1, "ERET in EL1 must retire");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 143: AARCH64 SVC & EXCEPTION ENTRY TEST SUITE     \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_svc_syndrome_and_vectors, "Architectural SVC Syndrome, ELR, and Vectors");
    run_test(test_svc_immediate_range_decoding, "SVC Immediate Decoding Range");
    run_test(test_darwin_bsd_syscall_routing, "Darwin BSD Syscall Routing via SVC #0x80");
    run_test(test_darwin_mach_trap_routing_svc81, "Darwin Mach Trap Routing via SVC #0x81");
    run_test(test_darwin_xnu_negative_x16_mach_routing, "Darwin XNU Negative X16 Mach Routing");
    run_test(test_unimplemented_syscall_enosys, "Unimplemented Syscall ENOSYS (78)");
    run_test(test_eret_el1_to_el0, "Exception Return (ERET) from EL1 to EL0");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 143 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
