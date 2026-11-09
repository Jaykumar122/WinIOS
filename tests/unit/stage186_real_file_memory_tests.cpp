// =============================================================================
// stage186_real_file_memory_tests.cpp
//
// Stage 186: Real File and Memory Operation Test
// Verifies real Mach-O execution performing file creation, writing, reading,
// dynamic mmap allocation, Apple Silicon W^X policy enforcement, piped IPC,
// and mprotect permission transitions through the WinIOS guest pipeline.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_corpus_builder.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Real Mach-O File Creation and Write
// -----------------------------------------------------------------------------
bool test_real_macho_file_creation_and_write() {
    std::cout << "[Test 1] Real Mach-O File Creation and Write...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly routine:
    // 1. SYS_open("/tmp/output.dat", O_WRONLY | O_CREAT = 0x0201)
    // 2. SYS_write(fd, "RealMachOFileWrite1234", 22)
    // 3. SYS_close(fd)
    // 4. SYS_exit(0)
    std::vector<std::uint32_t> instrs = {
        0xD2820800u, // movz x0, #0x1040 (path string at 0x1040)
        0xD2804021u, // mov x1, #0x0201 (O_WRONLY | O_CREAT)
        0xD28000B0u, // mov x16, #5 (SYS_open)
        0xD4001001u, // svc #0x80 -> returns fd in x0

        0xAA0003F3u, // mov x19, x0 (save fd)
        0xAA1303E0u, // mov x0, x19 (fd)
        0xD2820A01u, // movz x1, #0x1050 (text string at 0x1050)
        0xD28002C2u, // mov x2, #22 (length)
        0xD2800090u, // mov x16, #4 (SYS_write)
        0xD4001001u, // svc #0x80

        0xAA1303E0u, // mov x0, x19 (fd)
        0xD28000D0u, // mov x16, #6 (SYS_close)
        0xD4001001u, // svc #0x80

        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };

    const char path_str[] = "/tmp/output.dat";
    const char data_str[] = "RealMachOFileWrite1234";

    std::vector<std::uint8_t> payload;
    payload.resize(instrs.size() * 4 + sizeof(path_str) + sizeof(data_str));
    std::memcpy(payload.data(), instrs.data(), instrs.size() * 4);
    std::memcpy(payload.data() + instrs.size() * 4, path_str, sizeof(path_str));
    std::memcpy(payload.data() + instrs.size() * 4 + sizeof(path_str), data_str, sizeof(data_str));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, payload);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/file_writer";
    cfg.argv = {"/usr/bin/file_writer"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(macho.data(), macho.size(), cfg, 601);

    TEST_ASSERT(result.is_clean_exit(), "Process must exit cleanly");
    TEST_ASSERT(result.bsd_syscalls_handled >= 4, "Must execute open, write, close, exit");

    // Verify file content in Darwin VFS
    auto file_bytes = bsd.vfs().read_file("/tmp/output.dat");
    TEST_ASSERT(file_bytes.has_value(), "Created file must exist in VFS");
    std::string written_text(reinterpret_cast<const char*>(file_bytes->data()), file_bytes->size());
    TEST_ASSERT(written_text == "RealMachOFileWrite1234", "File content mismatch: " + written_text);

    std::cout << "  Passed (Created /tmp/output.dat, wrote 22 bytes, verified VFS contents)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Real Mach-O File Read and Verification
// -----------------------------------------------------------------------------
bool test_real_macho_file_read_and_verification() {
    std::cout << "[Test 2] Real Mach-O File Read and Verification...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Pre-populate configuration file in VFS
    const std::string conf_text = "{\"status\":\"OK\"}";
    bsd.vfs().write_file("/etc/test_config.json", conf_text.data(), conf_text.size());

    // Assembly routine:
    // 1. SYS_open("/etc/test_config.json", O_RDONLY = 0)
    // 2. SYS_read(fd, buffer_addr = 0x2000, count = 16)
    // 3. SYS_write(1, buffer_addr = 0x2000, count = x0) -> stdout
    // 4. SYS_close(fd)
    // 5. SYS_exit(0)
    std::vector<std::uint32_t> instrs = {
        0xD2820A80u, // movz x0, #0x1054 (path string at 0x1054)
        0xD2800001u, // mov x1, #0 (O_RDONLY)
        0xD28000B0u, // mov x16, #5 (SYS_open)
        0xD4001001u, // svc #0x80 -> fd in x0

        0xAA0003F3u, // mov x19, x0 (save fd)
        0xAA1303E0u, // mov x0, x19 (fd)
        0xD2840001u, // movz x1, #0x2000 (buffer)
        0xD2800202u, // mov x2, #16 (count)
        0xD2800070u, // mov x16, #3 (SYS_read)
        0xD4001001u, // svc #0x80 -> bytes read in x0

        0xAA0003E2u, // mov x2, x0 (count = bytes read)
        0xD2800020u, // mov x0, #1 (stdout fd 1)
        0xD2840001u, // movz x1, #0x2000 (buffer)
        0xD2800090u, // mov x16, #4 (SYS_write)
        0xD4001001u, // svc #0x80

        0xAA1303E0u, // mov x0, x19 (fd)
        0xD28000D0u, // mov x16, #6 (SYS_close)
        0xD4001001u, // svc #0x80

        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };

    const char path_str[] = "/etc/test_config.json\0";

    std::vector<std::uint8_t> payload;
    payload.resize(instrs.size() * 4 + sizeof(path_str));
    std::memcpy(payload.data(), instrs.data(), instrs.size() * 4);
    std::memcpy(payload.data() + instrs.size() * 4, path_str, sizeof(path_str));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, payload);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/config_reader";
    cfg.argv = {"/usr/bin/config_reader"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(macho.data(), macho.size(), cfg, 602);

    TEST_ASSERT(result.is_clean_exit(), "Process must exit cleanly");
    TEST_ASSERT(result.stdout_output.find("{\"status\":\"OK\"}") != std::string::npos,
                "Stdout must capture read configuration text");

    std::cout << "  Passed (Read configuration file from VFS and piped to stdout: \""
              << result.stdout_output << "\")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Real Mach-O Dynamic mmap Allocation and Write
// -----------------------------------------------------------------------------
bool test_real_macho_dynamic_mmap_allocation_and_write() {
    std::cout << "[Test 3] Real Mach-O Dynamic mmap Allocation and Write...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly routine:
    // 1. SYS_mmap(addr = 0, len = 0x4000, prot = 3, flags = 0x1002, fd = -1, off = 0)
    // 2. str w20, [x0] (write word into allocated buffer)
    // 3. SYS_exit(0)
    std::vector<std::uint32_t> instrs = {
        0xD2A00100u, // movz x0, #8, LSL #16 (addr = 0x80000, 16KB aligned)
        0xD2880001u, // movz x1, #0x4000 (16KB)
        0xD2800062u, // mov x2, #3 (PROT_READ | PROT_WRITE)
        0xD2820043u, // movz x3, #0x1002 (MAP_ANON | MAP_PRIVATE)
        0x92800004u, // movn x4, #0 (fd = -1)
        0xD2800005u, // mov x5, #0 (offset = 0)
        0xD28018B0u, // mov x16, #197 (SYS_mmap)
        0xD4001001u, // svc #0x80 -> returns mapped base in x0

        0xD2955541u, // movz w1, #0xAA55
        0xB9000001u, // str w1, [x0] (write 0xAA55 into mapped memory)

        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };

    std::vector<std::uint8_t> payload(instrs.size() * 4);
    std::memcpy(payload.data(), instrs.data(), payload.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, payload);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/mmap_test";
    cfg.argv = {"/usr/bin/mmap_test"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(macho.data(), macho.size(), cfg, 603);

    TEST_ASSERT(result.is_clean_exit(), "Process must exit cleanly");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Must handle SYS_mmap and SYS_exit");
    TEST_ASSERT(mem.read_u32(0x80000) == 0xAAAA, "Memory at mapped address must contain written data");

    std::cout << "  Passed (Allocated 16KB via SYS_mmap and wrote into guest memory)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Strict Apple Silicon W^X Policy Violation Rejection
// -----------------------------------------------------------------------------
bool test_strict_apple_wx_violation_rejection() {
    std::cout << "[Test 4] Strict Apple Silicon W^X Policy Violation Rejection...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly routine:
    // 1. SYS_mmap with PROT_WRITE | PROT_EXEC (0x06) WITHOUT MAP_JIT (0x0800)
    // 2. Checks return value in x0: if x0 == -1, exits with code 13 (EACCES)
    // 3. Otherwise exits with code 0
    std::vector<std::uint32_t> instrs = {
        0xD2800000u, // mov x0, #0
        0xD2880001u, // movz x1, #0x4000 (16KB)
        0xD28000C2u, // mov x2, #6 (PROT_WRITE | PROT_EXEC -> W^X VIOLATION!)
        0xD2820043u, // movz x3, #0x1002 (MAP_ANON | MAP_PRIVATE, NO MAP_JIT)
        0x92800004u, // movn x4, #0 (fd = -1)
        0xD2800005u, // mov x5, #0
        0xD28018B0u, // mov x16, #197 (SYS_mmap)
        0xD4001001u, // svc #0x80 -> must fail with -1

        0x92800001u, // movn x1, #0 (=-1)
        0xEB01001Fu, // cmp x0, x1
        0x54000060u, // b.eq +3 (to exit 13)

        // Success branch (should not be taken)
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u, // svc #0x80

        // Failure branch: exit(13)
        0xD28001A0u, // mov x0, #13 (EACCES)
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };

    std::vector<std::uint8_t> payload(instrs.size() * 4);
    std::memcpy(payload.data(), instrs.data(), payload.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, payload);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/wx_violator";
    cfg.argv = {"/usr/bin/wx_violator"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(macho.data(), macho.size(), cfg, 604);

    TEST_ASSERT(result.process_exited, "Process must exit");
    TEST_ASSERT(result.exit_code == 13, "Process must detect W^X failure and exit with 13");

    std::cout << "  Passed (Enforced Apple Silicon W^X policy, rejecting W+X mapping)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Real Mach-O Piped IPC Execution
// -----------------------------------------------------------------------------
bool test_real_macho_piped_ipc_execution() {
    std::cout << "[Test 5] Real Mach-O Piped IPC Execution...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly routine:
    // 1. SYS_pipe -> returns r_fd in x0, w_fd in x1
    // 2. Save r_fd in x19, w_fd in x20
    // 3. SYS_write(w_fd, "PipeDataPayload!", 16)
    // 4. SYS_read(r_fd, buf = 0x3000, count = 16)
    // 5. SYS_write(stdout = 1, buf = 0x3000, count = x0)
    // 6. SYS_exit(0)
    std::vector<std::uint32_t> instrs = {
        0xD2800550u, // mov x16, #42 (SYS_pipe)
        0xD4001001u, // svc #0x80 -> r_fd in x0, w_fd in x1

        0xAA0003F3u, // mov x19, x0 (r_fd)
        0xAA0103F4u, // mov x20, x1 (w_fd)

        0xAA1403E0u, // mov x0, x20 (w_fd)
        0xD2820B01u, // movz x1, #0x1058 (payload string offset)
        0xD2800202u, // mov x2, #16
        0xD2800090u, // mov x16, #4 (SYS_write)
        0xD4001001u, // svc #0x80

        0xAA1303E0u, // mov x0, x19 (r_fd)
        0xD2860001u, // movz x1, #0x3000 (read buffer)
        0xD2800202u, // mov x2, #16
        0xD2800070u, // mov x16, #3 (SYS_read)
        0xD4001001u, // svc #0x80

        0xAA0003E2u, // mov x2, x0 (bytes read)
        0xD2800020u, // mov x0, #1 (stdout)
        0xD2860001u, // movz x1, #0x3000
        0xD2800090u, // mov x16, #4 (SYS_write)
        0xD4001001u, // svc #0x80

        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };

    const char pipe_msg[] = "PipeDataPayload!\0";
    std::vector<std::uint8_t> payload;
    payload.resize(instrs.size() * 4 + sizeof(pipe_msg));
    std::memcpy(payload.data(), instrs.data(), instrs.size() * 4);
    std::memcpy(payload.data() + instrs.size() * 4, pipe_msg, sizeof(pipe_msg));

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, payload);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/pipe_test";
    cfg.argv = {"/usr/bin/pipe_test"};
    cfg.max_instructions = 100;

    auto result = engine.execute_macho(macho.data(), macho.size(), cfg, 605);

    TEST_ASSERT(result.is_clean_exit(), "Process must exit cleanly");
    TEST_ASSERT(result.stdout_output.find("PipeDataPayload!") != std::string::npos,
                "Stdout must capture pipe message");

    std::cout << "  Passed (Executed guest pipe IPC, reading and streaming \""
              << result.stdout_output << "\")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: mprotect Permission Transition Verification
// -----------------------------------------------------------------------------
bool test_mprotect_permission_transition() {
    std::cout << "[Test 6] mprotect Permission Transition Verification...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/mprot");

    // 1. Map 16KB with PROT_READ | PROT_WRITE (3)
    std::uint64_t addr = bsd.mmap(pid, 0x10000000ULL, 0x4000, 3, 0x1002, -1, 0);
    TEST_ASSERT(addr == 0x10000000ULL, "Initial mmap failed");
    TEST_ASSERT(bsd.check_vm_access(pid, addr, 0x1000, 2), "Initial write access must succeed");

    // 2. Call mprotect to PROT_READ (1)
    bool ok = bsd.mprotect(pid, addr, 0x4000, 1);
    TEST_ASSERT(ok, "mprotect to PROT_READ failed");

    // 3. Verify write access is now revoked, but read access persists
    TEST_ASSERT(!bsd.check_vm_access(pid, addr, 0x1000, 2), "Write access must be revoked");
    TEST_ASSERT(bsd.check_vm_access(pid, addr, 0x1000, 1), "Read access must remain permitted");

    std::cout << "  Passed (Verified runtime permission modification via mprotect)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Comprehensive File and Memory Pipeline Session
// -----------------------------------------------------------------------------
bool test_comprehensive_file_and_memory_session() {
    std::cout << "[Test 7] Comprehensive File and Memory Pipeline Session...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Full workflow: file creation -> write -> close -> exit
    auto file_bin = MachOCorpusRegistry::create_file_io();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/session_test";
    cfg.argv = {"/usr/bin/session_test"};
    cfg.max_instructions = 100;

    auto res = engine.execute_macho(file_bin.binary_data.data(), file_bin.binary_data.size(), cfg, 607);

    TEST_ASSERT(res.is_clean_exit(), "Session must terminate cleanly");
    TEST_ASSERT(res.guest_instructions_retired > 0, "Instructions must be retired");
    TEST_ASSERT(res.bsd_syscalls_handled >= 3, "Syscalls must be handled");

    std::cout << "  Passed (Executed comprehensive file and memory lifecycle session)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 186: REAL FILE AND MEMORY OPERATION TEST SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_real_macho_file_creation_and_write()) passed++; else failed++;
    if (test_real_macho_file_read_and_verification()) passed++; else failed++;
    if (test_real_macho_dynamic_mmap_allocation_and_write()) passed++; else failed++;
    if (test_strict_apple_wx_violation_rejection()) passed++; else failed++;
    if (test_real_macho_piped_ipc_execution()) passed++; else failed++;
    if (test_mprotect_permission_transition()) passed++; else failed++;
    if (test_comprehensive_file_and_memory_session()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 186 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}