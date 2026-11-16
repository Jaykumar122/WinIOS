// =============================================================================
// WinIOS — Stage 204.13: File Descriptors & Basic I/O Tests
// =============================================================================

#include "runtime/darwin_bsd_subsystem.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Standard Streams Initialization (0, 1, 2)
// -----------------------------------------------------------------------------
void test_standard_streams() {
    std::cout << "[Stage 204.13 Test 1] Standard Streams Initialization...\n";
    DarwinBsdSubsystem subsystem;
    std::uint32_t pid = subsystem.create_process(1, "/usr/bin/testapp", 501);
    TEST_CHECK(pid != 0, "Process creation succeeded");

    auto proc = subsystem.get_process(pid);
    TEST_CHECK(proc != nullptr, "Process retrieved");
    if (!proc) return;

    TEST_CHECK(proc->fds.find(0) != proc->fds.end(), "FD 0 (stdin) exists");
    TEST_CHECK(proc->fds.find(1) != proc->fds.end(), "FD 1 (stdout) exists");
    TEST_CHECK(proc->fds.find(2) != proc->fds.end(), "FD 2 (stderr) exists");

    // FD 0 is stdin: reading returns EOF (0 bytes) from /dev/null
    char in_buf[32]{};
    auto r0 = subsystem.read(pid, 0, in_buf, sizeof(in_buf));
    TEST_CHECK(r0 == 0, "stdin read returns EOF");

    // FD 1 is stdout: writing succeeds and output is swallowed by /dev/null
    const char msg[] = "Hello Standard Output\n";
    auto w1 = subsystem.write(pid, 1, msg, sizeof(msg));
    TEST_CHECK(w1 == static_cast<std::int64_t>(sizeof(msg)), "stdout write succeeds");

    // FD 2 is stderr: writing succeeds
    const char err_msg[] = "Fatal error: test\n";
    auto w2 = subsystem.write(pid, 2, err_msg, sizeof(err_msg));
    TEST_CHECK(w2 == static_cast<std::int64_t>(sizeof(err_msg)), "stderr write succeeds");
}

// -----------------------------------------------------------------------------
// Test 2: Character Devices Behavior (/dev/null, /dev/zero, /dev/urandom)
// -----------------------------------------------------------------------------
void test_character_devices() {
    std::cout << "[Stage 204.13 Test 2] Character Devices Behavior...\n";
    DarwinBsdSubsystem subsystem;
    std::uint32_t pid = subsystem.create_process(1, "/usr/bin/testapp", 501);

    // 1. /dev/null
    int fd_null = subsystem.open(pid, "/dev/null", 0x0002); // O_RDWR
    TEST_CHECK(fd_null >= 3, "open /dev/null succeeded");

    char buf[64]{};
    TEST_CHECK(subsystem.read(pid, fd_null, buf, sizeof(buf)) == 0, "/dev/null read gives EOF");
    TEST_CHECK(subsystem.write(pid, fd_null, "discard", 7) == 7, "/dev/null write succeeds");
    TEST_CHECK(subsystem.lseek(pid, fd_null, 100, 0) == 0, "lseek on character device returns 0");

    // stat check on character device
    DarwinStat64Raw null_stat{};
    TEST_CHECK(subsystem.fstat64(pid, fd_null, null_stat) == 0, "fstat64 on /dev/null succeeds");
    TEST_CHECK((null_stat.st_mode & 0170000) == 0020000, "st_mode has S_IFCHR");
    TEST_CHECK(subsystem.close(pid, fd_null) == 0, "close /dev/null succeeds");

    // 2. /dev/zero
    int fd_zero = subsystem.open(pid, "/dev/zero", 0x0000); // O_RDONLY
    TEST_CHECK(fd_zero >= 3, "open /dev/zero succeeded");
    std::memset(buf, 0xAA, sizeof(buf));
    TEST_CHECK(subsystem.read(pid, fd_zero, buf, sizeof(buf)) == static_cast<std::int64_t>(sizeof(buf)), "/dev/zero read count");
    bool all_zero = true;
    for (std::size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0) all_zero = false;
    }
    TEST_CHECK(all_zero, "/dev/zero returns all 0x00 bytes");
    TEST_CHECK(subsystem.close(pid, fd_zero) == 0, "close /dev/zero succeeds");

    // 3. /dev/urandom
    int fd_urand = subsystem.open(pid, "/dev/urandom", 0x0000);
    TEST_CHECK(fd_urand >= 3, "open /dev/urandom succeeded");
    std::memset(buf, 0, sizeof(buf));
    TEST_CHECK(subsystem.read(pid, fd_urand, buf, sizeof(buf)) == static_cast<std::int64_t>(sizeof(buf)), "/dev/urandom read count");
    bool non_zero_found = false;
    for (std::size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0) {
            non_zero_found = true;
            break;
        }
    }
    TEST_CHECK(non_zero_found, "/dev/urandom generates non-zero pseudo-random data");
    TEST_CHECK(subsystem.close(pid, fd_urand) == 0, "close /dev/urandom succeeds");
}

// -----------------------------------------------------------------------------
// Test 3: Regular File Lifecycle, Offsets, pread, and pwrite
// -----------------------------------------------------------------------------
void test_regular_file_lifecycle() {
    std::cout << "[Stage 204.13 Test 3] Regular File Lifecycle, Offsets, pread & pwrite...\n";
    DarwinBsdSubsystem subsystem;
    std::uint32_t pid = subsystem.create_process(1, "/usr/bin/testapp", 501);

    constexpr std::uint32_t kOCreat = 0x0200u;
    constexpr std::uint32_t kORdWr  = 0x0002u;

    int fd = subsystem.open(pid, "/tmp/testfile.txt", kOCreat | kORdWr);
    TEST_CHECK(fd >= 3, "open /tmp/testfile.txt succeeded");

    const char* sample = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::size_t len = std::strlen(sample);
    TEST_CHECK(subsystem.write(pid, fd, sample, len) == static_cast<std::int64_t>(len), "write sample content");

    // Current offset should be 26
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 1) == 26, "lseek SEEK_CUR returns 26");

    // Seek back to beginning
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 0) == 0, "lseek SEEK_SET returns 0");

    // Read first 5 bytes
    char read_buf[16]{};
    TEST_CHECK(subsystem.read(pid, fd, read_buf, 5) == 5, "read 5 bytes");
    read_buf[5] = '\0';
    TEST_CHECK(std::strcmp(read_buf, "ABCDE") == 0, "read content matches ABCDE");
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 1) == 5, "offset is 5");

    // Seek to SEEK_END - 4
    TEST_CHECK(subsystem.lseek(pid, fd, -4, 2) == 22, "lseek SEEK_END - 4 returns 22");
    TEST_CHECK(subsystem.read(pid, fd, read_buf, 4) == 4, "read 4 bytes");
    read_buf[4] = '\0';
    TEST_CHECK(std::strcmp(read_buf, "WXYZ") == 0, "read content matches WXYZ");

    // Invalid whence
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 99) == -1, "invalid whence fails");

    // Negative resulting offset
    TEST_CHECK(subsystem.lseek(pid, fd, -100, 0) == -1, "negative offset fails");

    // pread at offset 10 without changing cursor
    char pbuf[6]{};
    TEST_CHECK(subsystem.pread(pid, fd, pbuf, 5, 10) == 5, "pread at offset 10");
    pbuf[5] = '\0';
    TEST_CHECK(std::strcmp(pbuf, "KLMNO") == 0, "pread content matches KLMNO");
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 1) == 26, "offset remains 26 after pread");

    // pwrite at offset 0 without changing cursor
    TEST_CHECK(subsystem.pwrite(pid, fd, "12345", 5, 0) == 5, "pwrite at offset 0");
    TEST_CHECK(subsystem.lseek(pid, fd, 0, 1) == 26, "offset remains 26 after pwrite");

    // Read beginning with pread to confirm modification
    TEST_CHECK(subsystem.pread(pid, fd, pbuf, 5, 0) == 5, "pread verification");
    pbuf[5] = '\0';
    TEST_CHECK(std::strcmp(pbuf, "12345") == 0, "pwrite overwritten first 5 bytes");

    TEST_CHECK(subsystem.close(pid, fd) == 0, "close regular file succeeds");
}

// -----------------------------------------------------------------------------
// Test 4: Pipes, Buffer Transfer, Broken Pipe, and Pipe fstat
// -----------------------------------------------------------------------------
void test_pipes_and_broken_pipe() {
    std::cout << "[Stage 204.13 Test 4] Pipes, Buffer Transfer & Broken Pipe...\n";
    DarwinBsdSubsystem subsystem;
    std::uint32_t pid = subsystem.create_process(1, "/usr/bin/testapp", 501);

    int pipe_fds[2]{-1, -1};
    TEST_CHECK(subsystem.pipe(pid, pipe_fds) == 0, "pipe creation succeeded");
    TEST_CHECK(pipe_fds[0] >= 3 && pipe_fds[1] >= 3, "valid pipe FDs");
    TEST_CHECK(pipe_fds[0] != pipe_fds[1], "distinct pipe FDs");

    // fstat64 on pipe should succeed and report S_IFIFO
    DarwinStat64Raw pstat{};
    TEST_CHECK(subsystem.fstat64(pid, pipe_fds[0], pstat) == 0, "fstat64 on pipe succeeded");
    TEST_CHECK((pstat.st_mode & 0170000) == 0010000, "st_mode contains S_IFIFO");

    // lseek on pipe must fail (ESPIPE)
    TEST_CHECK(subsystem.lseek(pid, pipe_fds[0], 0, 0) == -1, "lseek on pipe read-end fails");
    TEST_CHECK(subsystem.lseek(pid, pipe_fds[1], 0, 0) == -1, "lseek on pipe write-end fails");

    // Write to read end must fail
    TEST_CHECK(subsystem.write(pid, pipe_fds[0], "fail", 4) == -1, "write to read-end fails");

    // Read from write end must fail
    char buf[32]{};
    TEST_CHECK(subsystem.read(pid, pipe_fds[1], buf, sizeof(buf)) == -1, "read from write-end fails");

    // Write to pipe write-end
    const char payload[] = "Synthetic IPC Packet through Kernel Pipe";
    std::size_t plen = std::strlen(payload);
    TEST_CHECK(subsystem.write(pid, pipe_fds[1], payload, plen) == static_cast<std::int64_t>(plen), "write to pipe succeeds");

    // Read partial 10 bytes
    char part1[11]{};
    TEST_CHECK(subsystem.read(pid, pipe_fds[0], part1, 10) == 10, "read partial 10 bytes");
    part1[10] = '\0';
    TEST_CHECK(std::strcmp(part1, "Synthetic ") == 0, "part 1 matches");

    // Read remaining bytes
    char part2[64]{};
    TEST_CHECK(subsystem.read(pid, pipe_fds[0], part2, sizeof(part2)) == static_cast<std::int64_t>(plen - 10), "read remaining bytes");
    part2[plen - 10] = '\0';
    TEST_CHECK(std::strcmp(part2, "IPC Packet through Kernel Pipe") == 0, "part 2 matches");

    // Close write-end: read-end should see EOF (0)
    TEST_CHECK(subsystem.close(pid, pipe_fds[1]) == 0, "close pipe write-end");
    TEST_CHECK(subsystem.read(pid, pipe_fds[0], buf, sizeof(buf)) == 0, "read after write-end closed gives EOF");
    TEST_CHECK(subsystem.close(pid, pipe_fds[0]) == 0, "close pipe read-end");

    // Broken pipe check:
    int pipe2[2]{-1, -1};
    TEST_CHECK(subsystem.pipe(pid, pipe2) == 0, "second pipe created");
    TEST_CHECK(subsystem.close(pid, pipe2[0]) == 0, "close read-end before write");
    TEST_CHECK(subsystem.write(pid, pipe2[1], "test", 4) == -1, "write to broken pipe returns -1");
    TEST_CHECK(subsystem.close(pid, pipe2[1]) == 0, "close write-end");
}

// -----------------------------------------------------------------------------
// Test 5: Descriptor Duplication & Control (dup, dup2, fcntl)
// -----------------------------------------------------------------------------
void test_duplication_and_fcntl() {
    std::cout << "[Stage 204.13 Test 5] Descriptor Duplication & fcntl...\n";
    DarwinBsdSubsystem subsystem;
    std::uint32_t pid = subsystem.create_process(1, "/usr/bin/testapp", 501);

    int pipe_fds[2]{-1, -1};
    TEST_CHECK(subsystem.pipe(pid, pipe_fds) == 0, "pipe created");

    // Duplicate write-end using dup
    int dup_w = subsystem.dup(pid, pipe_fds[1]);
    TEST_CHECK(dup_w >= 3 && dup_w != pipe_fds[1], "dup succeeded");

    // Duplicate write-end using dup2 to explicit FD 20
    int dup2_w = subsystem.dup2(pid, pipe_fds[1], 20);
    TEST_CHECK(dup2_w == 20, "dup2 to 20 succeeded");

    // Write through dup2_w
    TEST_CHECK(subsystem.write(pid, dup2_w, "Ping", 4) == 4, "write through dup2_w");

    // Read through original read-end
    char buf[16]{};
    TEST_CHECK(subsystem.read(pid, pipe_fds[0], buf, 4) == 4, "read through read-end");
    buf[4] = '\0';
    TEST_CHECK(std::strcmp(buf, "Ping") == 0, "content matches Ping");

    // Close original write-end: dup_w and dup2_w are still open
    TEST_CHECK(subsystem.close(pid, pipe_fds[1]) == 0, "close original write-end");
    // Writing through dup_w works
    TEST_CHECK(subsystem.write(pid, dup_w, "Pong", 4) == 4, "write through dup_w");
    TEST_CHECK(subsystem.read(pid, pipe_fds[0], buf, 4) == 4, "read through read-end");
    buf[4] = '\0';
    TEST_CHECK(std::strcmp(buf, "Pong") == 0, "content matches Pong");

    // Test fcntl on read-end
    // F_GETFD
    TEST_CHECK(subsystem.fcntl(pid, pipe_fds[0], 1) == 0, "fcntl F_GETFD returns 0");
    // F_SETFD to set FD_CLOEXEC
    TEST_CHECK(subsystem.fcntl(pid, pipe_fds[0], 2, 1) == 0, "fcntl F_SETFD succeeds");
    TEST_CHECK(subsystem.fcntl(pid, pipe_fds[0], 1) == 1, "fcntl F_GETFD returns 1 (cloexec)");

    // F_DUPFD with min_fd = 30
    int fdup = subsystem.fcntl(pid, pipe_fds[0], 0, 30);
    TEST_CHECK(fdup >= 30, "fcntl F_DUPFD returned fd >= 30");

    // Clean up
    subsystem.close(pid, dup_w);
    subsystem.close(pid, dup2_w);
    subsystem.close(pid, pipe_fds[0]);
    subsystem.close(pid, fdup);
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Running Stage 204.13: File Descriptors & Basic I/O Tests\n";
    std::cout << "=================================================================\n";

    test_standard_streams();
    test_character_devices();
    test_regular_file_lifecycle();
    test_pipes_and_broken_pipe();
    test_duplication_and_fcntl();

    if (g_failed_tests == 0) {
        std::cout << "=================================================================\n";
        std::cout << "Stage 204.13: ALL TESTS PASSED!\n";
        std::cout << "=================================================================\n";
        return 0;
    } else {
        std::cerr << "=================================================================\n";
        std::cerr << "Stage 204.13: " << g_failed_tests << " TESTS FAILED!\n";
        std::cerr << "=================================================================\n";
        return 1;
    }
}
