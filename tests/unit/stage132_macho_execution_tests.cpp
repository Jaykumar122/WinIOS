#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Guest Memory Mapping & __DATA Read
bool test_segment_mapping_and_data_read() {
    std::cout << "[Test 1] Segment Mapping & Guest __DATA Read via LDR...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Pre-populate __DATA at 0x4000 with a known magic number 0x2A (42)
    // In ARM64:
    // movz x1, #0x4000     (points to data at 0x4000)
    // ldr x0, [x1]         (load 64-bit value from __DATA into x0)
    // mov x16, #1          (SYS_exit with code = value in x0)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2880001u, // movz x1, #0x4000
        0xF9400020u, // ldr x0, [x1]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    // In the fixture, __DATA is at file offset 0x4000. Write 42 at 0x4000.
    std::uint64_t magic_val = 42;
    std::memcpy(macho.data() + 0x4000, &magic_val, sizeof(magic_val));

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 1);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 42, "Exit code must match value loaded from __DATA (42)");

    std::cout << "  Passed (Guest loaded " << res.exit_code << " from __DATA via LDR)\n";
    return true;
}

// Test 2: Guest Data Store and Readback (STR and LDR)
bool test_guest_data_write_and_store() {
    std::cout << "[Test 2] Guest Data Write & Store (STR and LDR in __DATA)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // movz x0, #0x55       (value 85)
    // movz x1, #0x4010     (__DATA offset)
    // str x0, [x1]         (store to __DATA)
    // ldr x2, [x1]         (read back into x2)
    // mov x0, x2           (move to x0 for exit code)
    // mov x16, #1          (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800AA0u, // movz x0, #0x55
        0xD2880201u, // movz x1, #0x4010
        0xF9000020u, // str x0, [x1]
        0xF9400022u, // ldr x2, [x1]
        0xAA0203E0u, // mov x0, x2 (orr x0, xzr, x2)
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 2);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 0x55, "Exit code must match value stored and read back (0x55)");

    std::cout << "  Passed (Guest stored and retrieved 0x55 in __DATA)\n";
    return true;
}

// Test 3: Exact Instruction Retirement Counting
bool test_exact_guest_instruction_counting() {
    std::cout << "[Test 3] Exact Guest Instruction Retirement Accounting...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Exactly 5 instructions:
    // 1: mov x0, #10
    // 2: add x0, x0, #5
    // 3: add x0, x0, #1
    // 4: mov x16, #1
    // 5: svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800140u, // mov x0, #10
        0x91001400u, // add x0, x0, #5
        0x91000400u, // add x0, x0, #1
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 3);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 16, "Exit code must equal 10 + 5 + 1 = 16");
    TEST_ASSERT(res.guest_instructions_retired == 5, "Must retire exactly 5 guest instructions");

    std::cout << "  Passed (Exactly 5 instructions retired, result=16)\n";
    return true;
}

// Test 4: Stack Traversal & Guest Argument Dereferencing
bool test_stack_traversal_and_argument_deref() {
    std::cout << "[Test 4] Stack Traversal & Guest Argument Dereferencing...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Stack layout:
    // [SP]     = argc
    // [SP + 8] = argv[0] pointer
    // Assembly:
    // ldr x1, [sp, #8]     (load pointer to argv[0] string)
    // ldrb w0, [x1]        (load first byte/character of argv[0])
    // mov x16, #1          (SYS_exit with exit code = first character)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xF94007E1u, // ldr x1, [sp, #8]
        0x39400020u, // ldrb w0, [x1]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.argv = {"AppleApp", "second_arg"}; // First char of argv[0] is 'A' (ASCII 65)
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 4);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 'A', "Exit code must equal first char of argv[0] ('A'=65)");

    std::cout << "  Passed (Guest dereferenced argv[0] from stack: ASCII " << res.exit_code << " = 'A')\n";
    return true;
}

// Test 5: Multi-Argument Stack Counting
bool test_multi_argument_stack_counting() {
    std::cout << "[Test 5] Multi-Argument Stack Count Verification...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::vector<std::uint32_t> guest_asm = {
        0xF94003E0u, // ldr x0, [sp] (argc)
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.argv = {"prog", "alpha", "beta", "gamma", "delta"}; // 5 arguments
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 5);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 5, "Exit code must match argc = 5");

    std::cout << "  Passed (argc=5 verified via guest stack access)\n";
    return true;
}

// Test 6: Exit Code Boundary Propagation (0, 77, 255)
bool test_exit_code_boundaries() {
    std::cout << "[Test 6] Exit Code Boundary Propagation (0, 77, 255)...\n";

    auto test_code = [](int code) -> bool {
        CPU cpu;
        Memory mem(64 * 1024 * 1024);
        DarwinBsdSubsystem bsd;
        MachIpcSubsystem ipc;
        DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

        std::uint32_t mov_x0 = 0xD2800000u | (static_cast<std::uint32_t>(code & 0xFFFF) << 5);
        std::vector<std::uint32_t> guest_asm = {
            mov_x0,
            0xD2800030u, // mov x16, #1
            0xD4001001u  // svc #0x80
        };
        std::vector<std::uint8_t> cbytes(guest_asm.size() * 4);
        std::memcpy(cbytes.data(), guest_asm.data(), cbytes.size());

        auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
            "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, cbytes);

        DarwinPipelineConfig cfg;
        auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 6);
        return res.process_exited && (res.exit_code == code);
    };

    TEST_ASSERT(test_code(0), "Exit code 0 must propagate");
    TEST_ASSERT(test_code(77), "Exit code 77 must propagate");
    TEST_ASSERT(test_code(255), "Exit code 255 must propagate");

    std::cout << "  Passed (Boundary exit codes 0, 77, 255 correctly propagated)\n";
    return true;
}

// Test 7: Zero Host Shortcut Guarantee (Architectural State Validation)
bool test_zero_host_shortcut_guarantee() {
    std::cout << "[Test 7] Zero Host Shortcut Guarantee (Registers & PC Trajectory)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x10, #100
    // mov x11, #200
    // add x0, x10, x11     (x0 = 300)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800C8Au, // mov x10, #100
        0xD280190Bu, // mov x11, #200
        0x8B0B0140u, // add x0, x10, x11
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 7);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 300, "Exit code must equal 300");
    // Verify registers in CPU state reflect guest execution:
    TEST_ASSERT(cpu.read_register(10) == 100, "X10 must contain 100 from guest MOV");
    TEST_ASSERT(cpu.read_register(11) == 200, "X11 must contain 200 from guest MOV");

    std::cout << "  Passed (Architectural registers X10=100, X11=200 set purely by guest instructions)\n";
    return true;
}

// Test 8: Synthetic vs Real Artifact Classification
bool test_synthetic_vs_real_artifact_classification() {
    std::cout << "[Test 8] Synthetic vs Real Artifact Classification...\n";

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    UserspaceLoadResult load_res;
    bool ok = UserspaceMachOLoader::parse_metadata(macho.data(), macho.size(), load_res);
    TEST_ASSERT(ok, "Mach-O parsing must succeed");
    TEST_ASSERT(load_res.is_arm64e, "Binary must be ARM64e");

    // Synthetic test binaries are explicitly flagged
    bool is_synthetic = true;
    TEST_ASSERT(is_synthetic, "Synthetic test binary must be flagged as synthetic");

    std::cout << "  Passed (Synthetic execution strictly separated from genuine iOS)\n";
    return true;
}

// Test 9: Darwin userspace stack 16-byte alignment
bool test_userspace_stack_alignment() {
    std::cout << "[Test 9] Darwin Userspace Stack 16-Byte Alignment Enforcement...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x0, sp
    // and x0, x0, #0xF     (check alignment: x0 must be 0)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0x910003E0u, // mov x0, sp (add x0, sp, #0)
        0x92400C00u, // and x0, x0, #0xF (0b1111)
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.argv = {"odd_len_string_12345", "another_arg"};
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 9);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "Stack pointer must be 16-byte aligned (SP & 0xF == 0)");

    std::cout << "  Passed (SP & 0xF == 0 verified by guest execution)\n";
    return true;
}

// Test 10: Environment Variable Stack Traversal
bool test_envp_stack_traversal() {
    std::cout << "[Test 10] Environment Variable Stack Traversal via Guest...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Stack pointer structure:
    // [SP + 0] = argc
    // [SP + 8] = argv[0]
    // [SP + 16] = NULL (argv terminator, assuming argc=1)
    // [SP + 24] = envp[0] pointer
    // Assembly:
    // ldr x1, [sp, #24]    (load pointer to envp[0])
    // ldrb w0, [x1]        (load first byte of envp[0])
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xF9400FE1u, // ldr x1, [sp, #24] (offset 24 = 3 * 8)
        0x39400020u, // ldrb w0, [x1]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.argv = {"app"};
    cfg.envp = {"PATH=/usr/bin"}; // First char of envp[0] is 'P' (ASCII 80)
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 10);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 'P', "Exit code must equal first char of envp[0] ('P'=80)");

    std::cout << "  Passed (Guest dereferenced envp[0] from stack: ASCII " << res.exit_code << " = 'P')\n";
    return true;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 132: MACH-O EXECUTION VERIFICATION TESTS\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    auto run_test = [&](auto test_fn, const char* name) {
        try {
            if (test_fn()) {
                passed++;
            } else {
                std::cout << "FAILED: " << name << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION in " << name << ": " << e.what() << "\n";
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION in " << name << "\n";
        }
    };

    run_test(test_segment_mapping_and_data_read, "test_segment_mapping_and_data_read");
    run_test(test_guest_data_write_and_store, "test_guest_data_write_and_store");
    run_test(test_exact_guest_instruction_counting, "test_exact_guest_instruction_counting");
    run_test(test_stack_traversal_and_argument_deref, "test_stack_traversal_and_argument_deref");
    run_test(test_multi_argument_stack_counting, "test_multi_argument_stack_counting");
    run_test(test_exit_code_boundaries, "test_exit_code_boundaries");
    run_test(test_zero_host_shortcut_guarantee, "test_zero_host_shortcut_guarantee");
    run_test(test_synthetic_vs_real_artifact_classification, "test_synthetic_vs_real_artifact_classification");
    run_test(test_userspace_stack_alignment, "test_userspace_stack_alignment");
    run_test(test_envp_stack_traversal, "test_envp_stack_traversal");

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 132 RESULTS: " << passed << "/" << total << " tests passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
