// =============================================================================
// stage_192_8_bsd_syscall_tests.cpp
//
// Stage 192.8: Darwin BSD Syscall ABI Conformance Tests
// Verifies:
//   - Syscall invocation & register convention (SVC #0x80, X16 nr, X0..X5 args)
//   - Return convention & error flag (Carry bit: C=0 success, C=1 failure)
//   - File I/O: open, write, read, lseek, close, EBADF error reporting
//   - Stat: stat64, fstat64 size & metadata validation
//   - Identity: getpid, getuid, getgid
//   - Descriptor manipulation: pipe (dual X0/X1 returns), dup, dup2
//   - Virtual Memory: mmap, mprotect, madvise, munmap
//   - Time: gettimeofday
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

using namespace winios;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to invoke handle_bsd_syscall via engine with simulated registers
struct SyscallFixture {
    CPU cpu;
    Memory memory{64 * 1024 * 1024};
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine;
    uint32_t pid{901};

    SyscallFixture() : engine(cpu, memory, bsd, ipc) {
        bsd.create_process_with_pid(pid, 0, "/usr/bin/syscall_tester", 501);
        engine.set_current_pid(pid);
    }

    void invoke(uint64_t nr) {
        cpu.write_register(16, nr);
        DarwinPipelineResult dummy_res;
        engine.handle_bsd_syscall(dummy_res);
    }

    bool is_success() const {
        return (cpu.get_nzcv() & 0x2u) == 0; // Carry clear
    }

    bool is_error() const {
        return (cpu.get_nzcv() & 0x2u) != 0; // Carry set
    }

    uint64_t ret0() const { return cpu.read_register(0); }
    uint64_t ret1() const { return cpu.read_register(1); }
};

// -----------------------------------------------------------------------------
// Test 1: File I/O Lifecycle (open, write, lseek, read, close, error)
// -----------------------------------------------------------------------------
bool test_file_io_syscalls() {
    std::cout << "[Test 1] File I/O Lifecycle Syscalls (open, write, lseek, read, close)...\n";
    SyscallFixture f;

    // 1. SYS_open (5): write filename into guest RAM
    const char path[] = "/tmp/io_test.dat";
    uint64_t path_va = 0x10000;
    for (size_t i = 0; i <= sizeof(path); ++i) f.memory.write_u8(path_va + i, static_cast<uint8_t>(path[i]));

    f.cpu.write_register(0, path_va);
    f.cpu.write_register(1, 0x0202); // O_CREAT | O_RDWR
    f.cpu.write_register(2, 0644);
    f.invoke(5);
    CHECK(f.is_success(), "SYS_open failed");
    int fd = static_cast<int>(f.ret0());
    CHECK(fd >= 3, "SYS_open returned invalid fd");

    // 2. SYS_write (4): write 12 bytes
    const char write_data[] = "Hello Darwin";
    uint64_t buf_va = 0x11000;
    for (size_t i = 0; i < 12; ++i) f.memory.write_u8(buf_va + i, static_cast<uint8_t>(write_data[i]));

    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.cpu.write_register(1, buf_va);
    f.cpu.write_register(2, 12);
    f.invoke(4);
    CHECK(f.is_success(), "SYS_write failed");
    CHECK(f.ret0() == 12, "SYS_write byte count mismatch");

    // 3. SYS_lseek (199): seek back to 0 (SEEK_SET = 0)
    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.cpu.write_register(1, 0);
    f.cpu.write_register(2, 0);
    f.invoke(199);
    CHECK(f.is_success(), "SYS_lseek failed");
    CHECK(f.ret0() == 0, "SYS_lseek offset mismatch");

    // 4. SYS_read (3): read 12 bytes back
    uint64_t read_buf_va = 0x12000;
    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.cpu.write_register(1, read_buf_va);
    f.cpu.write_register(2, 12);
    f.invoke(3);
    CHECK(f.is_success(), "SYS_read failed");
    CHECK(f.ret0() == 12, "SYS_read byte count mismatch");

    for (size_t i = 0; i < 12; ++i) {
        CHECK(f.memory.read_u8(read_buf_va + i) == static_cast<uint8_t>(write_data[i]),
              "Read data does not match written data");
    }

    // 5. SYS_close (6): close descriptor
    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.invoke(6);
    CHECK(f.is_success(), "SYS_close failed");

    // 6. Error convention: read on closed fd must fail with C=1
    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.cpu.write_register(1, read_buf_va);
    f.cpu.write_register(2, 12);
    f.invoke(3);
    CHECK(f.is_error(), "SYS_read on closed fd must set Carry flag");
    CHECK(static_cast<int64_t>(f.ret0()) == -1, "Error return must be -1");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: File Metadata (stat64 and fstat64)
// -----------------------------------------------------------------------------
bool test_stat_syscalls() {
    std::cout << "[Test 2] File Metadata Syscalls (stat64 and fstat64)...\n";
    SyscallFixture f;

    const char path[] = "/tmp/stat_test.txt";
    const char data[] = "12345678901234567890"; // 20 bytes
    f.bsd.vfs().write_file(path, reinterpret_cast<const uint8_t*>(data), 20);

    // 1. SYS_stat64 (338)
    uint64_t path_va = 0x13000;
    for (size_t i = 0; i <= sizeof(path); ++i) f.memory.write_u8(path_va + i, static_cast<uint8_t>(path[i]));
    uint64_t stat_buf_va = 0x14000;

    f.cpu.write_register(0, path_va);
    f.cpu.write_register(1, stat_buf_va);
    f.invoke(338);
    CHECK(f.is_success(), "SYS_stat64 failed");
    CHECK(f.ret0() == 0, "SYS_stat64 return code must be 0");

    // Read st_size from DarwinStat64Raw layout
    DarwinStat64Raw st{};
    for (size_t i = 0; i < sizeof(st); ++i) {
        reinterpret_cast<uint8_t*>(&st)[i] = f.memory.read_u8(stat_buf_va + i);
    }
    CHECK(st.st_size == 20, "SYS_stat64 size mismatch");

    // 2. SYS_fstat64 (339) on opened fd
    int fd = f.bsd.open(f.pid, path, 0x0 /*O_RDONLY*/, 0);
    CHECK(fd >= 3, "open failed");

    std::memset(&st, 0, sizeof(st));
    for (size_t i = 0; i < sizeof(st); ++i) f.memory.write_u8(stat_buf_va + i, 0);

    f.cpu.write_register(0, static_cast<uint64_t>(fd));
    f.cpu.write_register(1, stat_buf_va);
    f.invoke(339);
    CHECK(f.is_success(), "SYS_fstat64 failed");
    for (size_t i = 0; i < sizeof(st); ++i) {
        reinterpret_cast<uint8_t*>(&st)[i] = f.memory.read_u8(stat_buf_va + i);
    }
    CHECK(st.st_size == 20, "SYS_fstat64 size mismatch");

    f.bsd.close(f.pid, fd);
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Process Identity Syscalls (getpid, getuid, getgid)
// -----------------------------------------------------------------------------
bool test_identity_syscalls() {
    std::cout << "[Test 3] Process Identity Syscalls (getpid, getuid, getgid)...\n";
    SyscallFixture f;

    f.invoke(20); // SYS_getpid
    CHECK(f.is_success() && f.ret0() == f.pid, "SYS_getpid mismatch");

    f.invoke(24); // SYS_getuid
    CHECK(f.is_success() && f.ret0() == 501, "SYS_getuid mismatch");

    f.invoke(47); // SYS_getgid
    CHECK(f.is_success() && f.ret0() == 501, "SYS_getgid mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: IPC & Descriptor Duplication (pipe, dup, dup2)
// -----------------------------------------------------------------------------
bool test_pipe_dup_syscalls() {
    std::cout << "[Test 4] IPC & Descriptor Duplication (pipe, dup, dup2)...\n";
    SyscallFixture f;

    // 1. SYS_pipe (42): Darwin returns fds[0] in X0 and fds[1] in X1
    f.invoke(42);
    CHECK(f.is_success(), "SYS_pipe failed");
    int read_fd = static_cast<int>(f.ret0());
    int write_fd = static_cast<int>(f.ret1());
    CHECK(read_fd >= 3 && write_fd >= 3 && read_fd != write_fd, "SYS_pipe dual descriptor invalid");

    // Write through pipe write-end
    const char pipe_msg[] = "Darwin Pipe Test";
    uint64_t w_buf = 0x15000;
    for (size_t i = 0; i < 16; ++i) f.memory.write_u8(w_buf + i, static_cast<uint8_t>(pipe_msg[i]));

    f.cpu.write_register(0, static_cast<uint64_t>(write_fd));
    f.cpu.write_register(1, w_buf);
    f.cpu.write_register(2, 16);
    f.invoke(4);
    CHECK(f.is_success() && f.ret0() == 16, "Write to pipe write-end failed");

    // Read through pipe read-end
    uint64_t r_buf = 0x16000;
    f.cpu.write_register(0, static_cast<uint64_t>(read_fd));
    f.cpu.write_register(1, r_buf);
    f.cpu.write_register(2, 16);
    f.invoke(3);
    CHECK(f.is_success() && f.ret0() == 16, "Read from pipe read-end failed");

    for (size_t i = 0; i < 16; ++i) {
        CHECK(f.memory.read_u8(r_buf + i) == static_cast<uint8_t>(pipe_msg[i]),
              "Pipe data transfer corrupted");
    }

    // 2. SYS_dup (41): duplicate read_fd
    f.cpu.write_register(0, static_cast<uint64_t>(read_fd));
    f.invoke(41);
    CHECK(f.is_success(), "SYS_dup failed");
    int dup_fd = static_cast<int>(f.ret0());
    CHECK(dup_fd > write_fd, "SYS_dup should allocate new fd");

    // 3. SYS_dup2 (90): duplicate dup_fd to exact target fd 10
    f.cpu.write_register(0, static_cast<uint64_t>(dup_fd));
    f.cpu.write_register(1, 10);
    f.invoke(90);
    CHECK(f.is_success() && f.ret0() == 10, "SYS_dup2 failed to bind fd 10");

    // Cleanup
    f.bsd.close(f.pid, read_fd);
    f.bsd.close(f.pid, write_fd);
    f.bsd.close(f.pid, dup_fd);
    f.bsd.close(f.pid, 10);

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Virtual Memory & Timing Syscalls (mmap, mprotect, madvise, munmap, gettimeofday)
// -----------------------------------------------------------------------------
bool test_vm_and_time_syscalls() {
    std::cout << "[Test 5] VM and Time Syscalls (mmap, mprotect, madvise, munmap, gettimeofday)...\n";
    SyscallFixture f;

    // 1. SYS_mmap (197)
    f.cpu.write_register(0, 0); // addr = 0
    f.cpu.write_register(1, 0x4000); // 16 KB
    f.cpu.write_register(2, 0x3); // PROT_READ | PROT_WRITE
    f.cpu.write_register(3, 0x1002); // MAP_PRIVATE | MAP_ANON
    f.cpu.write_register(4, static_cast<uint64_t>(-1));
    f.cpu.write_register(5, 0);
    f.invoke(197);
    CHECK(f.is_success(), "SYS_mmap failed");
    uint64_t mapped_va = f.ret0();
    CHECK((mapped_va & 0x3FFFULL) == 0, "SYS_mmap address must be 16 KB aligned");

    // 2. SYS_madvise (75)
    f.cpu.write_register(0, mapped_va);
    f.cpu.write_register(1, 0x4000);
    f.cpu.write_register(2, 0); // MADV_NORMAL
    f.invoke(75);
    CHECK(f.is_success() && f.ret0() == 0, "SYS_madvise failed");

    // 3. SYS_mprotect (74)
    f.cpu.write_register(0, mapped_va);
    f.cpu.write_register(1, 0x4000);
    f.cpu.write_register(2, 0x1); // PROT_READ
    f.invoke(74);
    CHECK(f.is_success() && f.ret0() == 0, "SYS_mprotect failed");

    // 4. SYS_munmap (73)
    f.cpu.write_register(0, mapped_va);
    f.cpu.write_register(1, 0x4000);
    f.invoke(73);
    CHECK(f.is_success() && f.ret0() == 0, "SYS_munmap failed");

    // 5. SYS_gettimeofday (116)
    uint64_t tv_va = 0x17000;
    f.cpu.write_register(0, tv_va);
    f.invoke(116);
    CHECK(f.is_success() && f.ret0() == 0, "SYS_gettimeofday failed");
    DarwinTimeval64 tv{};
    for (size_t i = 0; i < sizeof(tv); ++i) {
        reinterpret_cast<uint8_t*>(&tv)[i] = f.memory.read_u8(tv_va + i);
    }
    CHECK(tv.tv_sec > 1600000000, "SYS_gettimeofday returned invalid timestamp");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.8: Darwin BSD Syscall ABI Conformance Tests\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_file_io_syscalls, "File I/O Lifecycle Syscalls (open, write, lseek, read, close)");
    run_test(test_stat_syscalls, "File Metadata Syscalls (stat64 and fstat64)");
    run_test(test_identity_syscalls, "Process Identity Syscalls (getpid, getuid, getgid)");
    run_test(test_pipe_dup_syscalls, "IPC & Descriptor Duplication (pipe, dup, dup2)");
    run_test(test_vm_and_time_syscalls, "VM and Time Syscalls (mmap, mprotect, madvise, munmap, gettimeofday)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
