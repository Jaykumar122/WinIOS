// =============================================================================
// stage180_bsd_syscall_expansion_tests.cpp
//
// Stage 180: BSD Syscall Expansion
// Verifies expansion of Darwin BSD syscalls: lseek, stat64, fstat64, mprotect,
// gettimeofday, clock_gettime, getuid/gid credentials, and ioctl.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
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
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: SYS_lseek File Seeking
// -----------------------------------------------------------------------------
bool test_sys_lseek() {
    std::cout << "[Test 1] SYS_lseek Seeking (SEEK_SET, SEEK_CUR, SEEK_END)...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_lseek", 501);

    // Create file in VFS with 100 bytes
    std::string test_data(100, 'X');
    bsd.vfs().write_file("/tmp/seek_test.bin", test_data.data(), test_data.size());

    int fd = bsd.open(pid, "/tmp/seek_test.bin", 0);
    TEST_ASSERT(fd >= 3, "open must return valid fd");

    // 1. SEEK_SET to offset 25
    std::int64_t off = bsd.lseek(pid, fd, 25, 0);
    TEST_ASSERT(off == 25, "SEEK_SET mismatch");

    // 2. SEEK_CUR by +10 (to 35)
    off = bsd.lseek(pid, fd, 10, 1);
    TEST_ASSERT(off == 35, "SEEK_CUR mismatch");

    // 3. SEEK_END by -15 (to 100 - 15 = 85)
    off = bsd.lseek(pid, fd, -15, 2);
    TEST_ASSERT(off == 85, "SEEK_END mismatch");

    // 4. Invalid negative seek before start of file must fail
    off = bsd.lseek(pid, fd, -200, 0);
    TEST_ASSERT(off == -1, "Negative seek must fail with -1");

    bsd.close(pid, fd);
    std::cout << "  Passed (Validated SEEK_SET, SEEK_CUR, SEEK_END, and error boundary)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: stat64 and fstat64 Conformance
// -----------------------------------------------------------------------------
bool test_sys_stat64_and_fstat64() {
    std::cout << "[Test 2] stat64 and fstat64 Memory Layout and Attributes...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_stat", 501);

    std::string test_content = "Hello, Darwin stat64 testing buffer!";
    bsd.vfs().write_file("/etc/banner.txt", test_content.data(), test_content.size());

    // 1. stat64 by path
    DarwinStat64Raw st{};
    int ret = bsd.stat64(pid, "/etc/banner.txt", st);
    TEST_ASSERT(ret == 0, "stat64 by path must succeed");
    TEST_ASSERT(st.st_size == static_cast<std::int64_t>(test_content.size()), "st_size mismatch");
    TEST_ASSERT((st.st_mode & 0100000) != 0, "Must be regular file");
    TEST_ASSERT(st.st_blksize == 4096, "st_blksize must be 4096");
    TEST_ASSERT(st.st_atimespec.tv_sec > 0, "atime must be populated");

    // 2. fstat64 by descriptor
    int fd = bsd.open(pid, "/etc/banner.txt", 0);
    TEST_ASSERT(fd >= 3, "open must succeed");

    DarwinStat64Raw fst{};
    ret = bsd.fstat64(pid, fd, fst);
    TEST_ASSERT(ret == 0, "fstat64 must succeed");
    TEST_ASSERT(fst.st_size == st.st_size, "fstat64 size must match stat64");
    TEST_ASSERT(fst.st_ino == st.st_ino, "fstat64 inode must match stat64");

    bsd.close(pid, fd);
    std::cout << "  Passed (Verified Darwin 64-bit stat memory layout and timestamps)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: mprotect and W^X Protection
// -----------------------------------------------------------------------------
bool test_sys_mprotect_wx_protection() {
    std::cout << "[Test 3] mprotect and Strict W^X Validation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_mprotect", 501);

    // Map a region with PROT_READ | PROT_WRITE (3)
    std::uint64_t mapped = bsd.mmap(pid, 0x180000000ULL, 0x4000, 3, 0x1002, -1, 0);
    TEST_ASSERT(mapped == 0x180000000ULL, "mmap must succeed");

    // 1. mprotect to PROT_READ | PROT_EXEC (5)
    bool ok = bsd.mprotect(pid, mapped, 0x4000, 5);
    TEST_ASSERT(ok, "mprotect to RX must succeed");

    // 2. mprotect to PROT_READ | PROT_WRITE | PROT_EXEC (7) -> W^X violation!
    ok = bsd.mprotect(pid, mapped, 0x4000, 7);
    TEST_ASSERT(!ok, "mprotect to RWX without MAP_JIT must be rejected by W^X");

    std::cout << "  Passed (Enforced W^X security invariants on mprotect)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Identity Syscalls
// -----------------------------------------------------------------------------
bool test_sys_identity_syscalls() {
    std::cout << "[Test 4] Credentials (getuid, geteuid, getgid, getegid)...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_id", 502);

    TEST_ASSERT(bsd.getuid(pid) == 502, "uid mismatch");
    TEST_ASSERT(bsd.geteuid(pid) == 502, "euid mismatch");
    TEST_ASSERT(bsd.getgid(pid) == 502, "gid mismatch");
    TEST_ASSERT(bsd.getegid(pid) == 502, "egid mismatch");

    std::cout << "  Passed (Returned consistent POSIX credentials)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Time Syscalls
// -----------------------------------------------------------------------------
bool test_sys_time_syscalls() {
    std::cout << "[Test 5] Time Syscalls (gettimeofday, clock_gettime)...\n";

    DarwinBsdSubsystem bsd;

    DarwinTimeval64 tv{};
    int ret = bsd.gettimeofday(tv);
    TEST_ASSERT(ret == 0, "gettimeofday must return 0");
    TEST_ASSERT(tv.tv_sec >= 1700000000, "tv_sec must be valid epoch");

    DarwinTimespec64 ts{};
    ret = bsd.clock_gettime(0, ts);
    TEST_ASSERT(ret == 0, "clock_gettime must return 0");
    TEST_ASSERT(ts.tv_sec >= 1700000000, "tv_sec must be valid epoch");
    TEST_ASSERT(ts.tv_nsec > 0, "tv_nsec must be nonzero");

    std::cout << "  Passed (Populated timestamps with epoch time)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: ioctl Stub
// -----------------------------------------------------------------------------
bool test_sys_ioctl_stub() {
    std::cout << "[Test 6] SYS_ioctl Handling...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_ioctl", 501);

    int ret = bsd.ioctl(pid, 1, 0x4004667E, nullptr);
    TEST_ASSERT(ret == 0, "ioctl must return 0");

    std::cout << "  Passed (Handled ioctl without faulting)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Userspace Engine Integration of Expanded Syscalls
// -----------------------------------------------------------------------------
bool test_userspace_engine_syscall_dispatch() {
    std::cout << "[Test 7] Userspace Engine Syscall Dispatch (lseek & fstat64)...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::uint32_t pid = 1;
    bsd.vfs().write_file("/file.txt", "1234567890", 10);
    int fd = bsd.open(pid, "/file.txt", 0);
    TEST_ASSERT(fd >= 3, "open must succeed");

    // Execute SYS_lseek via guest instructions:
    // movz x0, #fd
    // movz x1, #5 (offset 5)
    // movz x2, #0 (SEEK_SET)
    // movz x16, #199 (SYS_lseek)
    // svc #0x80
    // ret
    std::vector<std::uint32_t> code = {
        0xD2800000u | (static_cast<std::uint32_t>(fd) << 5), // movz x0, #fd
        0xD28000A1u,                                         // movz x1, #5
        0xD2800002u,                                         // movz x2, #0
        0xD28018F0u,                                         // movz x16, #199
        0xD4001001u,                                         // svc #0x80
        0xD65F03C0u                                          // ret
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x1000 + (i * 4), code[i]);
    }

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.write_register(30, 0x5000); // LR

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 4 instructions up to SVC
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT(engine.step_one(result, cfg), "Setup step failed");
    }
    TEST_ASSERT(cpu.read_register(16) == 199, "X16 must be 199");

    // Step 5: svc #0x80 -> executes lseek
    TEST_ASSERT(engine.step_one(result, cfg), "SVC step failed");
    TEST_ASSERT(cpu.read_register(0) == 5, "X0 must be 5 (new offset)");

    // Step 6: ret
    TEST_ASSERT(engine.step_one(result, cfg), "RET step failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x5000, "PC must be LR");

    bsd.close(pid, fd);
    std::cout << "  Passed (Executed SYS_lseek through CPU SVC instruction pipeline)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 180: BSD SYSCALL EXPANSION SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_sys_lseek()) passed++; else failed++;
    if (test_sys_stat64_and_fstat64()) passed++; else failed++;
    if (test_sys_mprotect_wx_protection()) passed++; else failed++;
    if (test_sys_identity_syscalls()) passed++; else failed++;
    if (test_sys_time_syscalls()) passed++; else failed++;
    if (test_sys_ioctl_stub()) passed++; else failed++;
    if (test_userspace_engine_syscall_dispatch()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 180 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
