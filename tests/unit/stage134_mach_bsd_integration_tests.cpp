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

// Test 1: Guest SYS_open and SYS_close via VFS
bool test_guest_open_and_close() {
    std::cout << "[Test 1] Guest SYS_open and SYS_close via VFS...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create a file in VFS first: /tmp/test.txt
    const char initial_content[] = "Initial content";
    bsd.vfs().write_file("/tmp/test.txt", initial_content, sizeof(initial_content) - 1);

    // Assembly:
    // movz x0, #0x1028     (points to string "/tmp/test.txt" in RAM)
    // mov x1, #0           (O_RDONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80            (returns fd in x0)
    // mov x19, x0          (save fd in x19)
    // mov x0, x19          (fd to close)
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // mov x0, x19          (exit code = opened fd, expected >= 3)
    // mov x16, #1          (SYS_exit)
    // svc #0x80
    // String at 0x1028: "/tmp/test.txt\0"
    std::vector<std::uint32_t> guest_asm = {
        0xD2820500u, // movz x0, #0x1028
        0xD2800001u, // mov x1, #0
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0 (orr x19, xzr, x0)
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xAA1303E0u, // mov x0, x19
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char filename[] = "/tmp/test.txt";
    // Instruction count = 11 * 4 = 44 bytes (0x2C). String at 0x102C (movz x0, #0x102C)
    // Let's adjust offset to 0x102C
    guest_asm[0] = 0xD2820580u; // movz x0, #0x102C

    std::vector<std::uint8_t> code(guest_asm.size() * 4 + sizeof(filename));
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + guest_asm.size() * 4, filename, sizeof(filename));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 201);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code >= 3, "Opened file descriptor must be >= 3");
    TEST_ASSERT(res.bsd_syscalls_handled >= 3, "Must handle SYS_open, SYS_close, and SYS_exit");

    std::cout << "  Passed (Guest opened and closed fd " << res.exit_code << ")\n";
    return true;
}

// Test 2: Guest SYS_write to Opened File
bool test_guest_write_to_file() {
    std::cout << "[Test 2] Guest SYS_write to VFS Node...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    const char empty_log[] = "";
    bsd.vfs().write_file("/var/log/system.log", empty_log, 0);

    // Assembly:
    // movz x0, #0x1034     (points to filename "/var/log/system.log\0")
    // mov x1, #1           (O_WRONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80            (fd in x0)
    // mov x19, x0          (save fd)
    // mov x0, x19          (fd)
    // movz x1, #0x1050     (points to payload "iOS-Kernel-Boot\n")
    // mov x2, #16          (len)
    // mov x16, #4          (SYS_write)
    // svc #0x80
    // mov x0, x19          (fd)
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // mov x0, #0
    // mov x16, #1          (SYS_exit)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820680u, // movz x0, #0x1034
        0xD2800021u, // mov x1, #1
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2820A01u, // movz x1, #0x1050
        0xD2800202u, // mov x2, #16
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char filename[] = "/var/log/system.log";
    const char log_entry[] = "iOS-Kernel-Boot\n";
    // 16 instructions = 64 bytes (0x40). String 1 at 0x1040, String 2 at 0x1060
    guest_asm[0] = 0xD2820800u; // movz x0, #0x1040 (filename)
    guest_asm[6] = 0xD2820C01u; // movz x1, #0x1060 (payload)

    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x40, filename, sizeof(filename));
    std::memcpy(code.data() + 0x60, log_entry, sizeof(log_entry));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 202);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 0, "Exit code must be 0");

    auto node = bsd.vfs().resolve_path("/var/log/system.log");
    TEST_ASSERT(node != nullptr, "Log file must exist");
    std::string node_str(node->content.begin(), node->content.end());
    TEST_ASSERT(node_str.find("iOS-Kernel-Boot") != std::string::npos,
                "File content must contain written string from guest");

    std::cout << "  Passed (Guest wrote \"" << node_str << "\" into VFS node)\n";
    return true;
}

// Test 3: Guest SYS_read from File into Guest Memory
bool test_guest_read_from_file() {
    std::cout << "[Test 3] Guest SYS_read from VFS Node into RAM...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    const char motd[] = "Darwin OS";
    bsd.vfs().write_file("/etc/motd", motd, sizeof(motd) - 1);

    // Assembly:
    // movz x0, #0x1040     (points to "/etc/motd\0")
    // mov x1, #0           (O_RDONLY)
    // mov x16, #5          (SYS_open)
    // svc #0x80            (fd in x0)
    // mov x19, x0
    // mov x0, x19          (fd)
    // movz x1, #0x2000     (destination buffer in RAM)
    // mov x2, #9           (read 9 bytes)
    // mov x16, #3          (SYS_read)
    // svc #0x80            (returns bytes read in x0)
    // mov x0, x19          (fd)
    // mov x16, #6          (SYS_close)
    // svc #0x80
    // movz x1, #0x2000
    // ldrb w0, [x1]        (load first byte of read data: 'D'=68)
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820A00u, // movz x0, #0x1050
        0xD2800001u, // mov x1, #0
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xAA0003F3u, // mov x19, x0
        0xAA1303E0u, // mov x0, x19
        0xD2840001u, // movz x1, #0x2000
        0xD2800122u, // mov x2, #9
        0xD2800070u, // mov x16, #3
        0xD4001001u, // svc #0x80
        0xAA1303E0u, // mov x0, x19
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2840001u, // movz x1, #0x2000
        0x39400020u, // ldrb w0, [x1]
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char path[] = "/etc/motd";
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x50, path, sizeof(path));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 203);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 'D', "Exit code must equal first byte 'D' (68) read from file");

    std::cout << "  Passed (Guest read from /etc/motd into RAM at 0x2000, verified 'D'=68)\n";
    return true;
}

// Test 4: Guest Mach Port Allocation & Right Invariants
bool test_guest_mach_port_allocation() {
    std::cout << "[Test 4] Guest Mach Port Allocation & Right Invariants...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // mov x16, #-28        (task_self_trap)
    // svc #0x81            (returns task self port in x0)
    // mov x19, x0
    // mov x16, #-26        (mach_reply_port_trap)
    // svc #0x81            (returns reply port in x0)
    // mov x20, x0
    // sub x0, x19, x20     (x0 = x19 - x20)
    // mov x16, #1          (SYS_exit with return code)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0x92800370u, // mov x16, #-28
        0xD4001021u, // svc #0x81
        0xAA0003F3u, // mov x19, x0
        0x92800330u, // mov x16, #-26 (mach_reply_port)
        0xD4001021u, // svc #0x81
        0xAA0003F4u, // mov x20, x0
        0xCB140260u, // sub x0, x19, x20
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 204);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code != 0, "task_self and reply_port must be distinct valid port names");

    std::cout << "  Passed (task_self and reply_port successfully generated)\n";
    return true;
}

// Test 5: Guest Mach Message Send (mach_msg_trap)
bool test_guest_mach_msg_send() {
    std::cout << "[Test 5] Guest Mach Message Dispatch & Queueing...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Setup destination port in task port space:
    auto space = ipc.create_task_port_space(205);
    auto dest_obj = std::make_shared<MachPortObject>(9999, 205);
    mach_port_t dest_port = space->allocate_port(dest_obj, kMachPortRightReceive);

    // Write a MachMsgHeader at RAM offset 0x20000 (outside Mach-O segments):
    // msgh_bits = 0, msgh_size = 24, msgh_remote_port = dest_port, msgh_id = 1234
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    hdr.msgh_remote_port = dest_port;
    hdr.msgh_local_port = 0;
    hdr.msgh_id = 1234;
    std::memcpy(mem.raw_data() + 0x20000, &hdr, sizeof(hdr));

    // Assembly:
    // movz x0, #2, lsl #16 (0x20000 - msg address)
    // mov x1, #1           (MACH_SEND_MSG)
    // mov x2, #24          (send_size = 24 bytes)
    // mov x3, #0           (rcv_size)
    // mov x4, #0           (rcv_name)
    // mov x5, #0           (timeout)
    // mov x6, #0           (notify)
    // mov x16, #-31        (mach_msg_trap)
    // svc #0x81            (returns 0 in x0)
    // mov x16, #1          (SYS_exit with return code of mach_msg_trap)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2A00040u, // movz x0, #2, lsl #16 (0x20000)
        0xD2800021u, // mov x1, #1
        0xD2800302u, // mov x2, #24
        0xD2800003u, // mov x3, #0
        0xD2800004u, // mov x4, #0
        0xD2800005u, // mov x5, #0
        0xD2800006u, // mov x6, #0
        0x928003D0u, // mov x16, #-31 (mach_msg_trap)
        0xD4001021u, // svc #0x81
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 205);

    TEST_ASSERT(res.process_exited, "Process must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "mach_msg_trap must return MACH_MSG_SUCCESS (0)");

    TEST_ASSERT(dest_obj->queue_size() == 1, "Destination port object must receive 1 queued message");
    auto queued = dest_obj->dequeue_message();
    TEST_ASSERT(queued.has_value(), "Queued message must be present");
    TEST_ASSERT(queued->header.msgh_id == 1234, "Message ID must match 1234");

    std::cout << "  Passed (Guest dispatched mach_msg_trap with msgh_id=1234 into MachPortObject)\n";
    return true;
}

// Test 6: Host API vs Guest Syscall Boundary Separation
bool test_host_api_vs_guest_syscall_separation() {
    std::cout << "[Test 6] Host API vs Guest Syscall Boundary Separation...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Host API: directly calls C++ method on host
    const char hosts_data[] = "127.0.0.1 localhost\n";
    bsd.vfs().write_file("/etc/hosts", hosts_data, sizeof(hosts_data) - 1);
    int host_fd = bsd.open(1, "/etc/hosts", 0);
    TEST_ASSERT(host_fd >= 3, "Host open must allocate fd >= 3");
    bsd.close(1, host_fd);

    // Guest Syscall: executed via ARM64 instructions in guest CPU
    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 206);

    TEST_ASSERT(res.guest_instructions_retired >= 3, "Guest instructions must execute");
    TEST_ASSERT(res.host_events_count > 0, "Host events tracked separately");
    TEST_ASSERT(res.guest_events_count > 0, "Guest events tracked separately");

    std::cout << "  Passed (Verified clean isolation between Host C++ API and Guest SVC traps)\n";
    return true;
}

// Test 7: Pathname Resolution Errors (ENOENT)
bool test_enoent_error_propagation() {
    std::cout << "[Test 7] Pathname Resolution Failure Propagation (ENOENT)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly:
    // movz x0, #0x1030     (points to "/nonexistent/path/file.txt\0")
    // mov x1, #0
    // mov x16, #5          (SYS_open)
    // svc #0x80            (returns -1 on failure)
    // mov x16, #1          (SYS_exit with return of open)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2820600u, // movz x0, #0x1030
        0xD2800001u, // mov x1, #0
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char bad_path[] = "/nonexistent/file.txt";
    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x30, bad_path, sizeof(bad_path));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 207);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == -1, "SYS_open on nonexistent path must return -1");

    std::cout << "  Passed (ENOENT error correctly returned as -1 to guest)\n";
    return true;
}

// Test 8: Process UID / EUID Syscall Verification
bool test_process_identity_syscalls() {
    std::cout << "[Test 8] Process UID / Identity Syscalls (SYS_getuid, SYS_geteuid)...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Pre-create process 208 under mobile UID (501)
    bsd.create_process_with_pid(208, 1, "/Applications/Test.app/Test", 501);

    // Assembly:
    // mov x16, #24         (SYS_getuid)
    // svc #0x80            (returns uid in x0)
    // mov x16, #1          (SYS_exit with exit code = uid)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800310u, // mov x16, #24
        0xD4001001u, // svc #0x80
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 208);

    TEST_ASSERT(res.process_exited, "Process must exit");
    TEST_ASSERT(res.exit_code == 501, "SYS_getuid must return mobile user UID 501");

    std::cout << "  Passed (Guest SYS_getuid returned UID " << res.exit_code << ")\n";
    return true;
}

// Test 9: Mach Bootstrap Port Association
bool test_mach_bootstrap_port_association() {
    std::cout << "[Test 9] Mach Bootstrap Port Association...\n";

    MachIpcSubsystem ipc;
    auto space = ipc.create_task_port_space(209);
    TEST_ASSERT(space != nullptr, "Task port space must be created");

    auto server_obj = std::make_shared<MachPortObject>(5001, 209);
    mach_port_t s_port = space->allocate_port(server_obj, kMachPortRightReceive);
    space->insert_right(s_port, server_obj, kMachPortRightSend);

    ipc.bootstrap_register(209, "com.apple.test.service", s_port);
    mach_port_t resolved = ipc.bootstrap_look_up(209, "com.apple.test.service");
    TEST_ASSERT(resolved != kMachPortNull, "Resolved port must be valid");
    TEST_ASSERT(space->lookup_port(resolved) == server_obj, "Resolved port must resolve to server_obj");

    std::cout << "  Passed (Bootstrap namespace verified)\n";
    return true;
}

// Test 10: Complete Integrated VFS & Mach IPC Workflow
bool test_complete_integrated_workflow() {
    std::cout << "[Test 10] Complete Integrated VFS & Mach IPC Workflow...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create file
    const char secret[] = "SECRET_KEY_1234";
    bsd.vfs().write_file("/var/mobile/token.dat", secret, sizeof(secret) - 1);

    // Assembly workflow:
    // 1. SYS_getpid
    // 2. task_self_trap
    // 3. SYS_open ("/var/mobile/token.dat")
    // 4. SYS_close
    // 5. SYS_exit (0)
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20 (SYS_getpid)
        0xD4001001u, // svc #0x80
        0x92800370u, // mov x16, #-28 (task_self_trap)
        0xD4001021u, // svc #0x81
        0xD2820500u, // movz x0, #0x1028
        0xD2800001u, // mov x1, #0
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xD28000D0u, // mov x16, #6 (SYS_close)
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char path[] = "/var/mobile/token.dat";
    guest_asm[4] = 0xD2820800u; // movz x0, #0x1040 (offset 13 instrs * 4 = 52 -> 0x1040)

    std::vector<std::uint8_t> code(guest_asm.size() * 4 + 64);
    std::memcpy(code.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code.data() + 0x40, path, sizeof(path));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 210);

    TEST_ASSERT(res.process_exited, "Workflow must exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(res.bsd_syscalls_handled >= 3, "Must handle getpid, open, close, exit");
    TEST_ASSERT(res.mach_traps_handled >= 1, "Must handle task_self_trap");

    std::cout << "  Passed (Integrated VFS + Mach IPC workflow concluded with exit code 0)\n";
    return true;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 134: MACH IPC & BSD SUBSYSTEM INTEGRATION TESTS\n";
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

    run_test(test_guest_open_and_close, "test_guest_open_and_close");
    run_test(test_guest_write_to_file, "test_guest_write_to_file");
    run_test(test_guest_read_from_file, "test_guest_read_from_file");
    run_test(test_guest_mach_port_allocation, "test_guest_mach_port_allocation");
    run_test(test_guest_mach_msg_send, "test_guest_mach_msg_send");
    run_test(test_host_api_vs_guest_syscall_separation, "test_host_api_vs_guest_syscall_separation");
    run_test(test_enoent_error_propagation, "test_enoent_error_propagation");
    run_test(test_process_identity_syscalls, "test_process_identity_syscalls");
    run_test(test_mach_bootstrap_port_association, "test_mach_bootstrap_port_association");
    run_test(test_complete_integrated_workflow, "test_complete_integrated_workflow");

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 134 RESULTS: " << passed << "/" << total << " tests passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
