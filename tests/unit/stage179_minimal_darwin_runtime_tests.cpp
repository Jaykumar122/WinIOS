// =============================================================================
// stage179_minimal_darwin_runtime_tests.cpp
//
// Stage 179: Minimal Darwin Runtime Library
// Verifies libSystem.B.dylib ARM64 stub generation, guest memory staging,
// dyld dynamic registration, guest CPU execution of BSD syscall stubs,
// Mach trap stubs, errno access, and custom runtime routines.
// =============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_runtime_library.h"
#include "runtime/darwin_userspace_engine.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

// -----------------------------------------------------------------------------
// Test 1: Runtime Stub Generation
// -----------------------------------------------------------------------------
bool test_runtime_stub_generation() {
    std::cout << "[Test 1] Runtime Stub Generation and Opcode Verification...\n";

    DarwinRuntimeLibrary rt(0x180000000ULL);

    const auto* exit_stub = rt.find_stub("_exit");
    TEST_ASSERT(exit_stub != nullptr, "_exit stub must be generated");
    TEST_ASSERT(exit_stub->code.size() == 3, "_exit must contain 3 instructions");
    // movz x16, #1
    TEST_ASSERT(exit_stub->code[0] == 0xD2800030u, "movz x16, #1 opcode mismatch");
    // svc #0x80
    TEST_ASSERT(exit_stub->code[1] == 0xD4001001u, "svc #0x80 opcode mismatch");
    // ret
    TEST_ASSERT(exit_stub->code[2] == 0xD65F03C0u, "ret opcode mismatch");

    const auto* mach_stub = rt.find_stub("_mach_msg_trap");
    TEST_ASSERT(mach_stub != nullptr, "_mach_msg_trap stub must be generated");
    TEST_ASSERT(mach_stub->code.size() == 3, "_mach_msg_trap must contain 3 instructions");
    // movn x16, #25 (~(-26) = 25)
    TEST_ASSERT(mach_stub->code[0] == 0x92800330u, "movn x16, #25 opcode mismatch");
    // svc #0x81
    TEST_ASSERT(mach_stub->code[1] == 0xD4001021u, "svc #0x81 opcode mismatch");

    std::cout << "  Passed (Verified ARM64 BSD and Mach stub opcodes)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Runtime Stubs Memory Staging
// -----------------------------------------------------------------------------
bool test_runtime_stubs_memory_staging() {
    std::cout << "[Test 2] Runtime Stubs Staging into Guest Memory...\n";

    Memory mem(0x100000);
    DarwinRuntimeLibrary rt(0x180000000ULL);

    std::string err;
    bool ok = rt.stage_into_memory(mem, err);
    TEST_ASSERT(ok, err.c_str());

    const auto* write_stub = rt.find_stub("_write");
    TEST_ASSERT(write_stub != nullptr, "_write stub must exist");

    // Read back first instruction from staged memory (offset translated modulo mem size)
    std::size_t mem_off = static_cast<std::size_t>(write_stub->address % mem.size());
    std::uint32_t op = mem.read_u32(mem_off);
    TEST_ASSERT(op == write_stub->code[0], "Memory staged opcode mismatch for _write");

    std::cout << "  Passed (Staged all libSystem stubs into memory successfully)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Runtime Stubs dyld Registration
// -----------------------------------------------------------------------------
bool test_runtime_stubs_dyld_registration() {
    std::cout << "[Test 3] Runtime Stubs Registration with DyldEngine...\n";

    DyldEngine dyld;
    DarwinRuntimeLibrary rt(0x180000000ULL);

    rt.register_with_dyld(dyld);

    // Verify symbols resolve to their stub addresses
    auto exit_addr = dyld.resolve_symbol("_exit");
    TEST_ASSERT(exit_addr.has_value(), "_exit must resolve via dyld");
    TEST_ASSERT(*exit_addr == rt.find_stub("_exit")->address, "_exit resolved address mismatch");

    auto write_addr = dyld.resolve_symbol("_write");
    TEST_ASSERT(write_addr.has_value(), "_write must resolve via dyld");
    TEST_ASSERT(*write_addr == rt.find_stub("_write")->address, "_write resolved address mismatch");

    std::cout << "  Passed (Resolved libSystem stubs via dynamic linker scope)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Guest Execution of _exit Stub
// -----------------------------------------------------------------------------
bool test_guest_execution_of_exit_stub() {
    std::cout << "[Test 4] Guest CPU Execution of _exit Stub...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinRuntimeLibrary rt(0x00000ULL); // base at 0 for simple flat execution
    std::string err;
    TEST_ASSERT(rt.stage_into_memory(mem, err), err.c_str());

    const auto* exit_stub = rt.find_stub("_exit");
    TEST_ASSERT(exit_stub != nullptr, "_exit stub must exist");

    // Setup CPU state: entry at _exit stub, arg0 (exit_code) = 42
    cpu.reset();
    cpu.set_current_el(0); // Userspace runs in EL0
    cpu.set_program_counter(exit_stub->address);
    cpu.write_register(0, 42); // exit code

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 1: movz x16, #1
    bool cont = engine.step_one(result, cfg);
    TEST_ASSERT(cont, "Step 1 must succeed");
    TEST_ASSERT(cpu.read_register(16) == 1, "X16 must be 1 (sys_exit)");

    // Step 2: svc #0x80 -> causes process exit with code 42
    cont = engine.step_one(result, cfg);
    TEST_ASSERT(!cont, "Process must exit after svc #0x80");
    TEST_ASSERT(result.process_exited, "process_exited must be true");
    TEST_ASSERT(result.exit_code == 42, "exit_code must be 42");

    std::cout << "  Passed (Executed _exit stub natively, verified exit code 42)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Guest Execution of _write Stub
// -----------------------------------------------------------------------------
bool test_guest_execution_of_write_stub() {
    std::cout << "[Test 5] Guest CPU Execution of _write Stub...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinRuntimeLibrary rt(0x00000ULL);
    std::string err;
    TEST_ASSERT(rt.stage_into_memory(mem, err), err.c_str());

    const auto* write_stub = rt.find_stub("_write");
    TEST_ASSERT(write_stub != nullptr, "_write stub must exist");

    // Stage string "Darwin!\n" in memory at 0x5000
    const char* msg = "Darwin!\n";
    std::size_t msg_len = std::strlen(msg);
    for (std::size_t i = 0; i < msg_len; ++i) {
        mem.write_u8(0x5000 + i, static_cast<std::uint8_t>(msg[i]));
    }

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(write_stub->address);
    cpu.write_register(0, 1);       // stdout
    cpu.write_register(1, 0x5000);  // buffer
    cpu.write_register(2, msg_len); // count
    cpu.write_register(30, 0x9000); // LR return address

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 1: movz x16, #4
    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(cpu.read_register(16) == 4, "X16 must be 4");

    // Step 2: svc #0x80
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(result.stdout_output == "Darwin!\n", "Output mismatch");
    TEST_ASSERT(cpu.read_register(0) == msg_len, "X0 must equal bytes written");

    // Step 3: ret -> jumps to LR (0x9000)
    TEST_ASSERT(engine.step_one(result, cfg), "Step 3 (ret) failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x9000, "PC must be LR after ret");

    std::cout << "  Passed (Executed _write stub, verified stdout and ret to LR)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Guest Execution of Mach Trap Stub
// -----------------------------------------------------------------------------
bool test_guest_execution_of_mach_trap_stub() {
    std::cout << "[Test 6] Guest CPU Execution of Mach Trap Stub...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinRuntimeLibrary rt(0x00000ULL);
    std::string err;
    TEST_ASSERT(rt.stage_into_memory(mem, err), err.c_str());

    const auto* task_self_stub = rt.find_stub("_mach_task_self");
    TEST_ASSERT(task_self_stub != nullptr, "_mach_task_self stub must exist");

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(task_self_stub->address);
    cpu.write_register(30, 0x8000); // LR

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 1: movn x16, #27 (~(-28) = 27)
    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(static_cast<std::int64_t>(cpu.read_register(16)) == -28, "X16 must be -28");

    // Step 2: svc #0x81 (Mach trap)
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(cpu.read_register(0) == 101, "mach_task_self must return port 101");

    // Step 3: ret
    TEST_ASSERT(engine.step_one(result, cfg), "Step 3 failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x8000, "Must return to caller");

    std::cout << "  Passed (Executed _mach_task_self stub and received port right)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Custom Stubs (errno and free)
// -----------------------------------------------------------------------------
bool test_custom_stubs_errno_and_free() {
    std::cout << "[Test 7] Custom Stubs (___error and _free)...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinRuntimeLibrary rt(0x00000ULL);
    std::string err;
    TEST_ASSERT(rt.stage_into_memory(mem, err), err.c_str());

    // Test _free stub: movz x0, #0; ret
    const auto* free_stub = rt.find_stub("_free");
    TEST_ASSERT(free_stub != nullptr, "_free stub must exist");

    cpu.reset();
    cpu.set_program_counter(free_stub->address);
    cpu.write_register(0, 0x123456); // dirty ptr
    cpu.write_register(30, 0x7000);  // LR

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(cpu.read_register(0) == 0, "_free must clear x0");
    TEST_ASSERT(cpu.get_program_counter() == 0x7000, "_free must return to caller");

    // Test ___error stub: movz x0, #0x1000; ret
    const auto* err_stub = rt.find_stub("___error");
    TEST_ASSERT(err_stub != nullptr, "___error stub must exist");

    cpu.reset();
    cpu.set_program_counter(err_stub->address);
    cpu.write_register(30, 0x6000); // LR

    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(cpu.read_register(0) == 0x1000, "___error must return errno pointer");
    TEST_ASSERT(cpu.get_program_counter() == 0x6000, "___error must return to caller");

    std::cout << "  Passed (Verified ___error and _free stubs executed cleanly)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 179: MINIMAL DARWIN RUNTIME LIBRARY SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_runtime_stub_generation()) passed++; else failed++;
    if (test_runtime_stubs_memory_staging()) passed++; else failed++;
    if (test_runtime_stubs_dyld_registration()) passed++; else failed++;
    if (test_guest_execution_of_exit_stub()) passed++; else failed++;
    if (test_guest_execution_of_write_stub()) passed++; else failed++;
    if (test_guest_execution_of_mach_trap_stub()) passed++; else failed++;
    if (test_custom_stubs_errno_and_free()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 179 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
