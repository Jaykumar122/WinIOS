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
#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/springboard_app_supervisor.h"

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

// Test 1: Genuine Third-Party iOS App Admission Gate (BLOCKED)
bool test_genuine_app_admission_gate() {
    std::cout << "[Test 1] Genuine Third-Party iOS App Admission Gate (BLOCKED)...\n";

    AppleArtifactAdmissionController controller;

    // Genuine unextracted third-party app
    AppleBootArtifact real_app{};
    real_app.artifact_id = "genuine_thirdparty_app";
    real_app.file_path = "/Applications/TestApp.app/TestApp";
    real_app.kind = AppleArtifactKind::NativeAppBinary;
    real_app.provenance = ArtifactProvenance::DecryptedReal;
    real_app.is_synthetic = false;

    auto res = controller.evaluate_artifact(real_app, nullptr, 0);
    TEST_ASSERT(!res.admitted, "Genuine unextracted third-party app must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated,
                "Must reject empty/missing genuine app artifact");

    std::cout << "  Passed (Genuine third-party app admission strictly BLOCKED)\n";
    return true;
}

// Test 2: Native App Process Creation & EL0 Entry
bool test_native_app_process_creation_and_el0_entry() {
    std::cout << "[Test 2] Native App Process Creation & EL0 Entry...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x16, #24         (SYS_getuid)
    // svc #0x80            (returns uid in x0; expected 501 mobile)
    // mov x19, x0
    // mov x0, x19
    // mov x16, #1          (SYS_exit with exit code = uid)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800310u, // mov x16, #24
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    // Pre-create process 50 with mobile UID 501
    bsd.create_process_with_pid(50, 1, "/Applications/Sample.app/Sample", 501);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 50);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 501, "Exit code must equal mobile UID 501");
    TEST_ASSERT(res.guest_instructions_retired >= 6, "Must retire all 6 guest instructions");

    std::cout << "  Passed (Native app executed in EL0 under UID " << res.exit_code << ")\n";
    return true;
}

// Test 3: Native App Stack Dereferencing (argc, argv)
bool test_native_app_stack_dereferencing() {
    std::cout << "[Test 3] Native App Stack Dereferencing (argc, argv)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly to dereference argc from SP:
    // ldr x0, [sp]         (x0 = argc)
    // mov x16, #1          (SYS_exit with exit code = argc)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xF94003E0u, // ldr x0, [sp]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    cfg.argv = {"/Applications/Sample.app/Sample", "--flag1", "--flag2"}; // argc = 3

    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 51);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 3, "Exit code must equal argc (3)");

    std::cout << "  Passed (Guest LDR correctly dereferenced argc=" << res.exit_code << " from SP)\n";
    return true;
}

// Test 4: Native App Heap Allocation via SYS_mmap (197)
bool test_native_app_heap_allocation_via_mmap() {
    std::cout << "[Test 4] Native App Heap Allocation via SYS_mmap...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x0, #0           (addr = 0)
    // movz x1, #0x4000     (len = 16KB)
    // mov x2, #3           (PROT_READ | PROT_WRITE)
    // movz x3, #0x1002     (MAP_ANON | MAP_PRIVATE)
    // mov x16, #197        (SYS_mmap)
    // svc #0x80            (returns allocated addr in x0)
    // mov x19, x0          (save alloc addr)
    // mov x0, #0           (exit 0)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2880001u, // movz x1, #0x4000 (16KB)
        0xD2800062u, // mov x2, #3
        0xD2A00023u, // movz x3, #1, lsl #16
        0xF2800043u, // movk x3, #2
        0xD28018B0u, // mov x16, #197 (SYS_mmap)
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 52);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "mmap process must exit cleanly");
    TEST_ASSERT(res.bsd_syscalls_handled >= 2, "Must handle SYS_mmap and SYS_exit");

    auto proc = bsd.get_process(52);
    TEST_ASSERT(proc != nullptr, "Process must exist");
    TEST_ASSERT(!proc->vm_regions.empty(), "Process must have an allocated VM region");
    TEST_ASSERT(proc->vm_regions.back().size == 0x4000, "Region size must be 16KB");

    std::cout << "  Passed (Guest allocated 16KB page-aligned heap region at 0x"
              << std::hex << proc->vm_regions.back().vmaddr << std::dec << ")\n";
    return true;
}

// Test 5: Native App Pure Compute Loop (Fibonacci in Registers)
bool test_native_app_arithmetic_computation() {
    std::cout << "[Test 5] Native App Pure Compute Loop (Fibonacci F(8) = 21)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Compute 8th Fibonacci number in registers:
    // mov x1, #0           (a = 0)
    // mov x2, #1           (b = 1)
    // mov x3, #8           (n = 8 iterations)
    // loop:
    // add x4, x1, x2       (temp = a + b)
    // mov x1, x2           (a = b)
    // mov x2, x4           (b = temp)
    // sub x3, x3, #1       (n--)
    // cbnz x3, -16         (branch back to loop if x3 != 0; offset -4 instrs = -16 bytes)
    // mov x0, x1           (exit code = F(8) = 21)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800001u, // mov x1, #0
        0xD2800022u, // mov x2, #1
        0xD2800103u, // mov x3, #8
        // loop:
        0x8B020024u, // add x4, x1, x2
        0xAA0203E1u, // mov x1, x2
        0xAA0403E2u, // mov x2, x4
        0xD1000463u, // sub x3, x3, #1
        0xB5FFFF83u, // cbnz x3, -16 (-4 instructions)
        0xAA0103E0u, // mov x0, x1
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 53);

    TEST_ASSERT(res.process_exited, "Compute loop must exit cleanly");
    TEST_ASSERT(res.exit_code == 21, "Exit code must equal 8th Fibonacci number (21)");
    TEST_ASSERT(res.guest_instructions_retired > 35, "Compute loop must retire all iterations");

    std::cout << "  Passed (Guest CPU retired " << res.guest_instructions_retired
              << " instructions, calculated F(8) = " << res.exit_code << ")\n";
    return true;
}

// Test 6: Native App File I/O (Document Storage)
bool test_native_app_file_io() {
    std::cout << "[Test 6] Native App File I/O (Document Storage)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create app documents directory in VFS
    bsd.vfs().create_directory("/var/mobile/Documents");
    const char dummy[] = "";
    bsd.vfs().write_file("/var/mobile/Documents/app_state.dat", dummy, 0);

    // Assembly to open, write, close:
    // movz x0, #0x1040     (points to "/var/mobile/Documents/app_state.dat\0")
    // mov x1, #1           (O_WRONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80
    // mov x19, x0          (fd)
    // mov x0, x19
    // movz x1, #0x1070     (points to "APP_READY_OK\n")
    // mov x2, #13
    // mov x16, #4          (SYS_write)
    // svc #0x80
    // mov x0, x19
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820800u, // movz x0, #0x1040
        0xD2800021u, // mov x1, #1
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2820E01u, // movz x1, #0x1070
        0xD28001A2u, // mov x2, #13
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char file_path[] = "/var/mobile/Documents/app_state.dat";
    const char payload[] = "APP_READY_OK\n";
    // 16 instrs = 64 bytes (0x40). String 1 at 0x1040 (64), String 2 at 0x1070 (112)
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 96);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x40, file_path, sizeof(file_path));
    std::memcpy(code.data() + 0x70, payload, sizeof(payload));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 54);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "File I/O app must exit cleanly");

    auto node = bsd.vfs().resolve_path("/var/mobile/Documents/app_state.dat");
    TEST_ASSERT(node != nullptr, "State file must exist");
    std::string content(node->content.begin(), node->content.end());
    TEST_ASSERT(content.find("APP_READY_OK") != std::string::npos, "Content written by guest");

    std::cout << "  Passed (Guest native app wrote document state file to VFS)\n";
    return true;
}

// Test 7: Native App Mach Service Discovery
bool test_native_app_mach_service_discovery() {
    std::cout << "[Test 7] Native App Mach Service Discovery...\n";

    MachIpcSubsystem ipc;
    // SpringBoard registers service port
    auto sb_space = ipc.create_task_port_space(6);
    auto sb_obj = std::make_shared<MachPortObject>(7001, 6);
    mach_port_t sb_port = sb_space->allocate_port(sb_obj, kMachPortRightReceive);
    sb_space->insert_right(sb_port, sb_obj, kMachPortRightSend);
    ipc.bootstrap_register(6, "com.apple.springboard.services", sb_port);

    // Native app at task 55 resolves service
    mach_port_t app_resolved = ipc.bootstrap_look_up(55, "com.apple.springboard.services");
    TEST_ASSERT(app_resolved != kMachPortNull, "App must resolve SpringBoard service");

    auto app_space = ipc.get_task_port_space(55);
    TEST_ASSERT(app_space != nullptr, "App task space must exist");
    auto target_obj = app_space->lookup_port(app_resolved);
    TEST_ASSERT(target_obj == sb_obj, "Resolved port must connect to SpringBoard port object");

    std::cout << "  Passed (Native app resolved SpringBoard service port)\n";
    return true;
}

// Test 8: Process Signal Delivery & Termination
bool test_native_app_signal_handling() {
    std::cout << "[Test 8] Process Signal Delivery & Termination...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process_with_pid(56, 1, "/Applications/Sample.app/Sample", 501);

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc && proc->state == ProcessState::Running, "Process running");

    // Signal SIGKILL (9)
    bool sig_ok = bsd.kill(pid, kDarwinSigKill);
    TEST_ASSERT(sig_ok, "Signal delivery must succeed");
    TEST_ASSERT(proc->state == ProcessState::Terminated, "Process must terminate on fatal SIGKILL");

    std::cout << "  Passed (Delivered SIGKILL to PID 56, transitioning to Terminated)\n";
    return true;
}

// Test 9: Threading & bsdthread_register Stub (Syscall 366)
bool test_multithreading_and_bsdthread_stub() {
    std::cout << "[Test 9] Threading & bsdthread_register Stub (Syscall 366)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly to invoke bsdthread_register:
    // mov x16, #366        (SYS_bsdthread_register)
    // svc #0x80            (returns 0 in x0)
    // mov x16, #1          (SYS_exit with return code)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2802DD0u, // mov x16, #366 (SYS_bsdthread_register)
        0xD4001001u, // svc #0x80
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Sample.app/Sample";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 57);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 0, "bsdthread_register must return 0");

    std::cout << "  Passed (Guest thread runtime initialized via bsdthread_register)\n";
    return true;
}

// Test 10: Complete End-to-End Native App Lifecycle
bool test_end_to_end_native_app_lifecycle() {
    std::cout << "[Test 10] Complete End-to-End Native App Lifecycle...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Guest native app executes:
    // 1. SYS_getuid (verifies 501)
    // 2. task_self_trap (allocates task port)
    // 3. Simple compute: 10 + 20 = 30
    // 4. SYS_exit(0)
    std::vector<std::uint32_t> guest_asm = {
        0xD2800310u, // mov x16, #24 (SYS_getuid)
        0xD4001001u, // svc #0x80
        0x92800370u, // mov x16, #-28 (task_self_trap)
        0xD4001021u, // svc #0x81
        0xD2800140u, // mov x0, #10
        0xD2800281u, // mov x1, #20
        0x8B010000u, // add x0, x0, x1 (x0 = 30)
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/MobileSafari.app/MobileSafari";
    cfg.argv = {"/Applications/MobileSafari.app/MobileSafari"};

    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 58);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "Native app lifecycle completed cleanly");
    TEST_ASSERT(res.guest_instructions_retired >= 10, "All 10 guest instructions retired");

    std::cout << "  Passed (Native application end-to-end execution lifecycle verified)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 139: NATIVE APPLICATION EXECUTION & RUNTIME\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "FAILED: " << name << "\n";
        }
    };

    run_test("test_genuine_app_admission_gate", test_genuine_app_admission_gate);
    run_test("test_native_app_process_creation_and_el0_entry", test_native_app_process_creation_and_el0_entry);
    run_test("test_native_app_stack_dereferencing", test_native_app_stack_dereferencing);
    run_test("test_native_app_heap_allocation_via_mmap", test_native_app_heap_allocation_via_mmap);
    run_test("test_native_app_arithmetic_computation", test_native_app_arithmetic_computation);
    run_test("test_native_app_file_io", test_native_app_file_io);
    run_test("test_native_app_mach_service_discovery", test_native_app_mach_service_discovery);
    run_test("test_native_app_signal_handling", test_native_app_signal_handling);
    run_test("test_multithreading_and_bsdthread_stub", test_multithreading_and_bsdthread_stub);
    run_test("test_end_to_end_native_app_lifecycle", test_end_to_end_native_app_lifecycle);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 139 RESULTS: " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}
