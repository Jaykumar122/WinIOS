// =============================================================================
// WinIOS — Stage 204.14: BSD Syscall Dispatcher Expansion Unit Tests
// =============================================================================

#include "runtime/darwin_syscall_dispatcher.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_userspace_engine.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

using namespace winios;
using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// Helper to check carry flag in ARM64 NZCV (bit 1 = C)
static inline bool is_carry_set(const CPU& cpu) {
    return (cpu.get_nzcv() & 0x2u) != 0;
}

// -----------------------------------------------------------------------------
// Test 1: Darwin Syscall Dispatcher with Bound BSD Subsystem (Open, Write, Read, Lseek, Close)
// -----------------------------------------------------------------------------
void test_file_io_lifecycle_and_carry_flag() {
    std::cout << "[Stage 204.14 Test 1] File I/O Lifecycle & Carry Flag Convention...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(1, "/usr/bin/testapp", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    Memory mem(0x100000); // 1 MB
    CPU cpu;

    // 1. SYS_open (/tmp/stage204_14.txt, O_CREAT | O_RDWR = 0x0202)
    const char filename[] = "/tmp/stage204_14.txt";
    const std::uint64_t path_va = 0x1000;
    std::memcpy(mem.raw_data() + path_va, filename, sizeof(filename));

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Open));
    cpu.write_register(0, path_va);
    cpu.write_register(1, 0x0202);
    cpu.write_register(2, 0644);

    std::int64_t r_open = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_open >= 3, "SYS_open returned valid fd >= 3");
    TEST_CHECK(!is_carry_set(cpu), "Carry flag is clear on open success");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(r_open), "X0 holds returned fd");
    int fd = static_cast<int>(r_open);

    // 2. SYS_write (Write 16 bytes)
    const char write_data[] = "0123456789ABCDEF";
    const std::uint64_t buf_va = 0x2000;
    std::memcpy(mem.raw_data() + buf_va, write_data, 16);

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Write));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, buf_va);
    cpu.write_register(2, 16);

    std::int64_t r_write = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_write == 16, "SYS_write wrote 16 bytes");
    TEST_CHECK(!is_carry_set(cpu), "Carry flag is clear on write success");
    TEST_CHECK(cpu.read_register(0) == 16, "X0 holds written count");

    // 3. SYS_lseek (SEEK_SET = 0 back to 0)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Lseek));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, 0); // offset
    cpu.write_register(2, 0); // whence SEEK_SET

    std::int64_t r_lseek = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_lseek == 0, "SYS_lseek returned new offset 0");
    TEST_CHECK(!is_carry_set(cpu), "Carry flag is clear on lseek success");
    TEST_CHECK(cpu.read_register(0) == 0, "X0 holds 0");

    // 4. SYS_read (Read 16 bytes back)
    const std::uint64_t read_va = 0x3000;
    std::memset(mem.raw_data() + read_va, 0, 16);

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Read));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, read_va);
    cpu.write_register(2, 16);

    std::int64_t r_read = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_read == 16, "SYS_read read 16 bytes");
    TEST_CHECK(!is_carry_set(cpu), "Carry flag is clear on read success");
    TEST_CHECK(cpu.read_register(0) == 16, "X0 holds read count");
    TEST_CHECK(std::memcmp(mem.raw_data() + read_va, write_data, 16) == 0, "Read data matches written data");

    // 5. SYS_close
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Close));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));

    std::int64_t r_close = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_close == 0, "SYS_close succeeded");
    TEST_CHECK(!is_carry_set(cpu), "Carry flag is clear on close success");
    TEST_CHECK(cpu.read_register(0) == 0, "X0 holds 0");

    // 6. Error Verification: SYS_read on closed fd must fail with EBADF (9) and Carry flag SET
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Read));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, read_va);
    cpu.write_register(2, 16);

    std::int64_t r_err = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_err == -9, "SYS_read on closed fd returns -9 (-EBADF)");
    TEST_CHECK(is_carry_set(cpu), "Carry flag is set on read error");
    TEST_CHECK(cpu.read_register(0) == 9, "X0 holds positive errno 9 (EBADF)");
}

// -----------------------------------------------------------------------------
// Test 2: File Metadata (SYS_fstat64 & SYS_stat64)
// -----------------------------------------------------------------------------
void test_stat_syscalls() {
    std::cout << "[Stage 204.14 Test 2] File Metadata Syscalls (fstat64 & stat64)...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(1, "/usr/bin/testapp", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    Memory mem(0x100000);
    CPU cpu;

    // Create a 40-byte test file
    std::string content(40, 'Z');
    bsd->vfs().write_file("/tmp/stat_test.bin", content.data(), content.size());

    // 1. SYS_stat64
    const char path[] = "/tmp/stat_test.bin";
    const std::uint64_t path_va = 0x4000;
    const std::uint64_t stat_va = 0x5000;
    std::memcpy(mem.raw_data() + path_va, path, sizeof(path));
    std::memset(mem.raw_data() + stat_va, 0, sizeof(DarwinStat64Raw));

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Stat64));
    cpu.write_register(0, path_va);
    cpu.write_register(1, stat_va);

    std::int64_t r_stat = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_stat == 0, "SYS_stat64 succeeded");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on stat64");
    DarwinStat64Raw st_out{};
    std::memcpy(&st_out, mem.raw_data() + stat_va, sizeof(DarwinStat64Raw));
    TEST_CHECK(st_out.st_size == 40, "stat64 reports 40 bytes size");
    TEST_CHECK((st_out.st_mode & 0170000) == 0100000, "stat64 reports regular file S_IFREG");

    // 2. Open file and call SYS_fstat64
    int fd = bsd->open(pid, "/tmp/stat_test.bin", 0);
    TEST_CHECK(fd >= 3, "open succeeded");

    std::memset(mem.raw_data() + stat_va, 0, sizeof(DarwinStat64Raw));
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Fstat64));
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, stat_va);

    std::int64_t r_fstat = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_fstat == 0, "SYS_fstat64 succeeded");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on fstat64");
    std::memcpy(&st_out, mem.raw_data() + stat_va, sizeof(DarwinStat64Raw));
    TEST_CHECK(st_out.st_size == 40, "fstat64 reports 40 bytes size");

    // 3. Error case: stat on non-existent path must fail with ENOENT (2)
    const char bad_path[] = "/tmp/does_not_exist.bin";
    std::memcpy(mem.raw_data() + path_va, bad_path, sizeof(bad_path));
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Stat64));
    cpu.write_register(0, path_va);
    cpu.write_register(1, stat_va);

    std::int64_t r_bad = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_bad == -2, "SYS_stat64 on missing file returns -2 (-ENOENT)");
    TEST_CHECK(is_carry_set(cpu), "Carry set on stat error");
    TEST_CHECK(cpu.read_register(0) == 2, "X0 holds positive errno 2 (ENOENT)");

    bsd->close(pid, fd);
}

// -----------------------------------------------------------------------------
// Test 3: Virtual Memory Syscalls (mmap, munmap, mprotect)
// -----------------------------------------------------------------------------
void test_vm_syscalls() {
    std::cout << "[Stage 204.14 Test 3] Virtual Memory Syscalls (mmap, munmap, mprotect)...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(1, "/usr/bin/testapp", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    Memory mem(0x100000);
    CPU cpu;

    // 1. SYS_mmap (Allocate 64 KB anonymous R/W)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap));
    cpu.write_register(0, 0); // addr = 0 (auto-assign)
    cpu.write_register(1, 0x10000); // 64 KB
    cpu.write_register(2, 0x3); // PROT_READ | PROT_WRITE
    cpu.write_register(3, 0x1002); // MAP_ANON | MAP_PRIVATE
    cpu.write_register(4, static_cast<std::uint64_t>(-1)); // fd = -1
    cpu.write_register(5, 0); // offset = 0

    std::int64_t r_mmap = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_mmap > 0, "SYS_mmap returned non-zero address");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on mmap success");
    std::uint64_t alloc_addr = static_cast<std::uint64_t>(r_mmap);
    TEST_CHECK((alloc_addr & 0x3FFFULL) == 0, "Address is 16KB aligned");

    // 2. SYS_mprotect (Change to Read-Only PROT_READ = 1)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mprotect));
    cpu.write_register(0, alloc_addr);
    cpu.write_register(1, 0x10000);
    cpu.write_register(2, 0x1); // PROT_READ

    std::int64_t r_mprot = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_mprot == 0, "SYS_mprotect to PROT_READ succeeded");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on mprotect");

    // 3. SYS_mprotect (Violate W^X: PROT_WRITE | PROT_EXEC without MAP_JIT)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mprotect));
    cpu.write_register(0, alloc_addr);
    cpu.write_register(1, 0x10000);
    cpu.write_register(2, 0x6); // PROT_WRITE | PROT_EXEC

    std::int64_t r_wx = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_wx == -13, "W^X violation rejected with -13 (-EACCES)");
    TEST_CHECK(is_carry_set(cpu), "Carry set on W^X rejection");
    TEST_CHECK(cpu.read_register(0) == 13, "X0 holds positive errno 13 (EACCES)");

    // 4. SYS_munmap
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Munmap));
    cpu.write_register(0, alloc_addr);
    cpu.write_register(1, 0x10000);

    std::int64_t r_munmap = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_munmap == 0, "SYS_munmap succeeded");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on munmap");
}

// -----------------------------------------------------------------------------
// Test 4: IPC, Pipes, Duplication & Process Identity (pipe, dup, dup2, getpid, exit)
// -----------------------------------------------------------------------------
void test_pipes_and_process_identity() {
    std::cout << "[Stage 204.14 Test 4] Pipes, Duplication & Process Identity...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(1, "/usr/bin/testapp", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    Memory mem(0x100000);
    CPU cpu;

    // 1. SYS_getpid
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid));
    std::int64_t r_pid = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_pid == static_cast<std::int64_t>(pid), "SYS_getpid returned matching PID");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on getpid");
    TEST_CHECK(cpu.read_register(0) == pid, "X0 holds PID");

    // 2. SYS_pipe
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Pipe));
    std::int64_t r_pipe = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_pipe >= 3, "SYS_pipe returned read fd");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on pipe");
    std::uint64_t r_fd0 = cpu.read_register(0);
    std::uint64_t r_fd1 = cpu.read_register(1);
    TEST_CHECK(r_fd0 >= 3 && r_fd1 >= 3 && r_fd0 != r_fd1, "Pipe allocated distinct FDs in X0 and X1");

    // 3. SYS_dup2 (dup r_fd1 to FD 15)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Dup2));
    cpu.write_register(0, r_fd1);
    cpu.write_register(1, 15);
    std::int64_t r_dup2 = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_dup2 == 15, "SYS_dup2 returned target FD 15");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on dup2");

    // 4. SYS_fcntl (F_GETFD = 1 on FD 15)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Fcntl));
    cpu.write_register(0, 15);
    cpu.write_register(1, 1); // F_GETFD
    std::int64_t r_fc = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_fc == 0, "F_GETFD returns 0 initially");
    TEST_CHECK(!is_carry_set(cpu), "Carry clear on fcntl");

    // 5. SYS_exit (exit code 7)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Exit));
    cpu.write_register(0, 7);
    std::int64_t r_exit = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_exit == 0, "SYS_exit returns 0");
    TEST_CHECK(dispatcher.has_exited(), "dispatcher marks process exited");
    TEST_CHECK(dispatcher.exit_code() == 7, "exit code is 7");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Running Stage 204.14: BSD Syscall Dispatcher Expansion Tests\n";
    std::cout << "=================================================================\n";

    test_file_io_lifecycle_and_carry_flag();
    test_stat_syscalls();
    test_vm_syscalls();
    test_pipes_and_process_identity();

    if (g_failed_tests == 0) {
        std::cout << "=================================================================\n";
        std::cout << "Stage 204.14: ALL TESTS PASSED!\n";
        std::cout << "=================================================================\n";
        return 0;
    } else {
        std::cerr << "=================================================================\n";
        std::cerr << "Stage 204.14: " << g_failed_tests << " TESTS FAILED!\n";
        std::cerr << "=================================================================\n";
        return 1;
    }
}
