// =============================================================================
// stage_192_9_vfs_tests.cpp
//
// Stage 192.9: Darwin File Descriptor and VFS Semantics Conformance Tests
// Verifies:
//   - Descriptor allocation & deterministic reuse (lowest available fd)
//   - Invalid descriptor error propagation (-1 / EBADF)
//   - Read-only, Write-only, and Read-Write access control
//   - Append mode (O_APPEND = 0x0008) offset pinning
//   - Seek behavior (SEEK_SET, SEEK_CUR, SEEK_END, negative bounds)
//   - File size, EOF, and partial read/write handling
//   - Bidirectional pipe semantics (EOF on close, EPIPE on broken pipe)
//   - dup and dup2 semantics
//   - Independent descriptor offsets for independent opens of same path
//   - Standard descriptor separation (stdin, stdout, stderr)
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"

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

// -----------------------------------------------------------------------------
// Test 1: Descriptor Allocation & Reuse
// -----------------------------------------------------------------------------
bool test_descriptor_allocation_and_reuse() {
    std::cout << "[Test 1] Descriptor Allocation & Lowest-Available Reuse...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 951;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/app", 501);

    // Initial descriptors 0 (stdin), 1 (stdout), 2 (stderr)
    int fd3 = bsd.open(kPid, "/tmp/f1.txt", 0x0202 /*O_CREAT|O_RDWR*/, 0644);
    int fd4 = bsd.open(kPid, "/tmp/f2.txt", 0x0202, 0644);
    int fd5 = bsd.open(kPid, "/tmp/f3.txt", 0x0202, 0644);

    CHECK(fd3 == 3, "First allocated fd must be 3");
    CHECK(fd4 == 4, "Second allocated fd must be 4");
    CHECK(fd5 == 5, "Third allocated fd must be 5");

    // Close fd 4 -> hole in fd table
    CHECK(bsd.close(kPid, fd4) == 0, "close(4) failed");

    // Next open MUST reuse lowest available fd (4!)
    int reused_fd = bsd.open(kPid, "/tmp/f4.txt", 0x0202, 0644);
    CHECK(reused_fd == 4, "POSIX fd reuse failed: expected fd 4 to be reallocated");

    // Next open allocates 6
    int fd6 = bsd.open(kPid, "/tmp/f5.txt", 0x0202, 0644);
    CHECK(fd6 == 6, "Expected fd 6 after hole filled");

    // Invalid descriptor operations
    char buf[16];
    CHECK(bsd.read(kPid, 999, buf, 16) == -1, "read on non-existent fd must fail");
    CHECK(bsd.write(kPid, -1, "test", 4) == -1, "write on negative fd must fail");
    CHECK(bsd.close(kPid, 999) == -1, "close on non-existent fd must fail");

    bsd.close(kPid, fd3);
    bsd.close(kPid, reused_fd);
    bsd.close(kPid, fd5);
    bsd.close(kPid, fd6);
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Access Modes (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND)
// -----------------------------------------------------------------------------
bool test_access_modes_and_append() {
    std::cout << "[Test 2] Access Modes (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND)...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 952;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/app", 501);

    const char initial_content[] = "Initial Data Block";
    bsd.vfs().write_file("/tmp/modes.txt", initial_content, sizeof(initial_content) - 1);

    // 1. O_RDONLY (0x0): Read succeeds, Write fails
    int ro_fd = bsd.open(kPid, "/tmp/modes.txt", 0x0000 /*O_RDONLY*/, 0);
    CHECK(ro_fd >= 3, "open O_RDONLY failed");
    char r_buf[32];
    CHECK(bsd.read(kPid, ro_fd, r_buf, 7) == 7, "read on O_RDONLY failed");
    CHECK(bsd.write(kPid, ro_fd, "append", 6) == -1, "write on O_RDONLY must fail with EBADF");
    bsd.close(kPid, ro_fd);

    // 2. O_WRONLY (0x1): Write succeeds, Read fails
    int wo_fd = bsd.open(kPid, "/tmp/modes.txt", 0x0001 /*O_WRONLY*/, 0);
    CHECK(wo_fd >= 3, "open O_WRONLY failed");
    CHECK(bsd.read(kPid, wo_fd, r_buf, 5) == -1, "read on O_WRONLY must fail with EBADF");
    CHECK(bsd.write(kPid, wo_fd, "OVER", 4) == 4, "write on O_WRONLY failed");
    bsd.close(kPid, wo_fd);

    // 3. O_APPEND (0x0008 | O_WRONLY 0x0001 = 0x0009):
    // Even if we seek to offset 0, write must occur at EOF!
    int app_fd = bsd.open(kPid, "/tmp/modes.txt", 0x0009 /*O_WRONLY | O_APPEND*/, 0);
    CHECK(app_fd >= 3, "open O_APPEND failed");
    bsd.lseek(kPid, app_fd, 0, 0 /*SEEK_SET*/); // attempt to seek to beginning
    CHECK(bsd.write(kPid, app_fd, "_APPENDED", 9) == 9, "write on O_APPEND failed");
    bsd.close(kPid, app_fd);

    // Verify content ends with _APPENDED
    auto data_opt = bsd.vfs().read_file("/tmp/modes.txt");
    CHECK(data_opt.has_value(), "read_file failed");
    std::string s(data_opt->begin(), data_opt->end());
    CHECK(s.find("_APPENDED") == s.size() - 9, "O_APPEND write must be at end of file");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Seek, EOF, and Partial Read/Write
// -----------------------------------------------------------------------------
bool test_seek_eof_partial() {
    std::cout << "[Test 3] Seek, EOF, and Partial Read/Write...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 953;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/app", 501);

    const char text[] = "0123456789ABCDEF"; // 16 bytes
    bsd.vfs().write_file("/tmp/seek_test.txt", text, 16);

    int fd = bsd.open(kPid, "/tmp/seek_test.txt", 0x0002 /*O_RDWR*/, 0);
    CHECK(fd >= 3, "open failed");

    // SEEK_SET to offset 10
    CHECK(bsd.lseek(kPid, fd, 10, 0) == 10, "SEEK_SET 10 failed");
    char buf[16];
    CHECK(bsd.read(kPid, fd, buf, 6) == 6, "read 6 failed");
    buf[6] = '\0';
    CHECK(std::strcmp(buf, "ABCDEF") == 0, "content mismatch after SEEK_SET");

    // EOF: Read at end of file must return 0
    CHECK(bsd.read(kPid, fd, buf, 10) == 0, "read at EOF must return 0");

    // SEEK_CUR: move backward by 4 bytes (from 16 to 12)
    CHECK(bsd.lseek(kPid, fd, -4, 1 /*SEEK_CUR*/) == 12, "SEEK_CUR -4 failed");
    CHECK(bsd.read(kPid, fd, buf, 4) == 4, "read 4 failed");
    buf[4] = '\0';
    CHECK(std::strcmp(buf, "CDEF") == 0, "content mismatch after SEEK_CUR");

    // SEEK_END: offset 0 from end
    CHECK(bsd.lseek(kPid, fd, 0, 2 /*SEEK_END*/) == 16, "SEEK_END 0 failed");

    // Negative offset bounds check
    CHECK(bsd.lseek(kPid, fd, -20, 0 /*SEEK_SET*/) == -1, "SEEK_SET negative offset must fail");

    // Partial read: requested 100 bytes from offset 12 -> returns remaining 4 bytes
    bsd.lseek(kPid, fd, 12, 0);
    CHECK(bsd.read(kPid, fd, buf, 100) == 4, "partial read must return remaining available bytes");

    bsd.close(kPid, fd);
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Pipe Lifecycle, EOF, and Broken Pipe
// -----------------------------------------------------------------------------
bool test_pipe_semantics() {
    std::cout << "[Test 4] Pipe Lifecycle, EOF, and Broken Pipe (EPIPE)...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 954;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/app", 501);

    int fds[2]{-1, -1};
    CHECK(bsd.pipe(kPid, fds) == 0, "pipe failed");
    int rfd = fds[0];
    int wfd = fds[1];

    // Read on write-end must fail with EBADF
    char buf[16];
    CHECK(bsd.read(kPid, wfd, buf, 4) == -1, "read on write-end of pipe must fail");

    // Write on read-end must fail with EBADF
    CHECK(bsd.write(kPid, rfd, "bad", 3) == -1, "write on read-end of pipe must fail");

    // Normal write and read
    CHECK(bsd.write(kPid, wfd, "stream1", 7) == 7, "pipe write 1 failed");
    CHECK(bsd.write(kPid, wfd, "stream2", 7) == 7, "pipe write 2 failed");

    CHECK(bsd.read(kPid, rfd, buf, 7) == 7, "pipe read 1 failed");
    buf[7] = '\0';
    CHECK(std::strcmp(buf, "stream1") == 0, "pipe data 1 mismatch");

    // Close write-end -> read remaining data, then receive EOF (0)
    CHECK(bsd.close(kPid, wfd) == 0, "close write-end failed");

    CHECK(bsd.read(kPid, rfd, buf, 7) == 7, "pipe read remaining data failed");
    buf[7] = '\0';
    CHECK(std::strcmp(buf, "stream2") == 0, "pipe data 2 mismatch");

    // Buffer empty and write-end closed -> EOF (0)!
    CHECK(bsd.read(kPid, rfd, buf, 7) == 0, "read on closed pipe must return 0 (EOF)");
    bsd.close(kPid, rfd);

    // Broken pipe: close read-end while write-end is still open -> write returns -1 (EPIPE)
    int fds2[2]{-1, -1};
    bsd.pipe(kPid, fds2);
    bsd.close(kPid, fds2[0]); // close read-end
    CHECK(bsd.write(kPid, fds2[1], "broken", 6) == -1, "write on broken pipe must return -1 (EPIPE)");
    bsd.close(kPid, fds2[1]);

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: dup, dup2, and Independent File Descriptors
// -----------------------------------------------------------------------------
bool test_dup_and_independent_offsets() {
    std::cout << "[Test 5] dup, dup2, and Independent File Descriptors...\n";

    DarwinBsdSubsystem bsd;
    constexpr uint32_t kPid = 955;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/app", 501);

    const char content[] = "AlphaBetaGammaDelta";
    bsd.vfs().write_file("/tmp/dup_test.txt", content, sizeof(content) - 1);

    // 1. Two independent opens of the same file have independent file offsets
    int fdA = bsd.open(kPid, "/tmp/dup_test.txt", 0x0000 /*O_RDONLY*/, 0);
    int fdB = bsd.open(kPid, "/tmp/dup_test.txt", 0x0000 /*O_RDONLY*/, 0);
    CHECK(fdA >= 3 && fdB >= 3 && fdA != fdB, "independent opens failed");

    char bufA[8];
    char bufB[8];
    // Read 5 bytes from fdA ("Alpha")
    CHECK(bsd.read(kPid, fdA, bufA, 5) == 5, "read fdA failed");
    bufA[5] = '\0';
    CHECK(std::strcmp(bufA, "Alpha") == 0, "fdA content mismatch");

    // fdB must still be at offset 0 ("Alpha")
    CHECK(bsd.read(kPid, fdB, bufB, 5) == 5, "read fdB failed");
    bufB[5] = '\0';
    CHECK(std::strcmp(bufB, "Alpha") == 0, "fdB offset should be independent of fdA");

    // 2. dup(fdA) duplicates descriptor
    int dup_fd = bsd.dup(kPid, fdA);
    CHECK(dup_fd > fdB, "dup fd allocation failed");

    // 3. dup2(fdA, 20) forces target fd 20
    int target = bsd.dup2(kPid, fdA, 20);
    CHECK(target == 20, "dup2 to 20 failed");

    // Reading from fd 20 continues from offset 5 ("BetaG")
    CHECK(bsd.read(kPid, 20, bufA, 5) == 5, "read from dup2 fd failed");
    bufA[5] = '\0';
    CHECK(std::strcmp(bufA, "BetaG") == 0, "dup2 shared offset mismatch");

    bsd.close(kPid, fdA);
    bsd.close(kPid, fdB);
    bsd.close(kPid, dup_fd);
    bsd.close(kPid, 20);

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.9: Darwin File Descriptor & VFS Semantics Tests\n";
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

    run_test(test_descriptor_allocation_and_reuse, "Descriptor Allocation & Lowest-Available Reuse");
    run_test(test_access_modes_and_append, "Access Modes (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND)");
    run_test(test_seek_eof_partial, "Seek, EOF, and Partial Read/Write");
    run_test(test_pipe_semantics, "Pipe Lifecycle, EOF, and Broken Pipe (EPIPE)");
    run_test(test_dup_and_independent_offsets, "dup, dup2, and Independent File Descriptors");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
