// =============================================================================
// stage182_vfs_runtime_integration_tests.cpp
//
// Stage 182: File Descriptor and VFS Runtime Integration
// Verifies path canonicalization, symlink traversal, cycle detection,
// multi-process file descriptor isolation, directory iteration via
// getdirentries64, unidirectional pipes, and fd duplication (dup/dup2).
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
// Test 1: Path Canonicalization & Normalization
// -----------------------------------------------------------------------------
bool test_path_canonicalization_and_normalization() {
    std::cout << "[Test 1] Path Canonicalization & Normalization...\n";

    DarwinVfs vfs;

    TEST_ASSERT(vfs.canonicalize_path("//var///log/../run//") == "/var/run", "Canonical path mismatch");
    TEST_ASSERT(vfs.canonicalize_path("/a/b/c/../../d") == "/a/d", "Parent backtrack mismatch");
    TEST_ASSERT(vfs.canonicalize_path("/./usr/./bin/./dyld") == "/usr/bin/dyld", "Current dot removal mismatch");
    TEST_ASSERT(vfs.canonicalize_path("") == "/", "Empty path must normalize to root");
    TEST_ASSERT(vfs.canonicalize_path("/") == "/", "Root must remain root");

    std::cout << "  Passed (Validated POSIX/Darwin path canonicalization)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Symlink Creation and Traversal
// -----------------------------------------------------------------------------
bool test_symlink_creation_and_traversal() {
    std::cout << "[Test 2] Symlink Creation and Traversal...\n";

    DarwinVfs vfs;
    vfs.populate_default_rootfs();

    const std::string payload = "ActiveSystemLogData2026";
    TEST_ASSERT(vfs.write_file("/var/log/system.log", payload.data(), payload.size()), "write_file failed");

    // 1. Absolute symlink
    TEST_ASSERT(vfs.create_symlink("/var/log/system.log", "/tmp/syslog"), "create_symlink failed");
    auto resolved = vfs.resolve_path("/tmp/syslog");
    TEST_ASSERT(resolved != nullptr, "resolve_path on symlink must succeed");
    TEST_ASSERT(resolved->type == VfsNodeType::RegularFile, "Resolved symlink must yield target file node");
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(resolved->content.data()), resolved->content.size()) == payload,
                "Content mismatch through symlink");

    // 2. Symlink chain: /link1 -> /tmp/syslog -> /var/log/system.log
    TEST_ASSERT(vfs.create_symlink("/tmp/syslog", "/link1"), "create link1 failed");
    auto resolved_chain = vfs.resolve_path("/link1");
    TEST_ASSERT(resolved_chain != nullptr && resolved_chain->inode == resolved->inode,
                "Chained symlink resolution failed");

    std::cout << "  Passed (Traversed single and chained Darwin symlinks)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Symlink Cycle Detection & Loop Defense
// -----------------------------------------------------------------------------
bool test_symlink_cycle_detection() {
    std::cout << "[Test 3] Symlink Cycle Detection & Loop Defense...\n";

    DarwinVfs vfs;
    vfs.create_directory("/loop");

    // Self-loop: /loop/self -> /loop/self
    TEST_ASSERT(vfs.create_symlink("/loop/self", "/loop/self"), "create self symlink failed");
    auto node_self = vfs.resolve_path("/loop/self");
    TEST_ASSERT(node_self == nullptr, "Self-referencing symlink must return nullptr");

    // Mutual cycle: /loop/a -> /loop/b, /loop/b -> /loop/a
    TEST_ASSERT(vfs.create_symlink("/loop/b", "/loop/a"), "create /loop/a failed");
    TEST_ASSERT(vfs.create_symlink("/loop/a", "/loop/b"), "create /loop/b failed");
    auto node_cycle = vfs.resolve_path("/loop/a");
    TEST_ASSERT(node_cycle == nullptr, "Mutual cyclic symlinks must return nullptr without crash");

    std::cout << "  Passed (Protected against symlink recursion and loops)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Multi-Process File Descriptor Isolation
// -----------------------------------------------------------------------------
bool test_multiprocess_fd_isolation() {
    std::cout << "[Test 4] Multi-Process File Descriptor Isolation...\n";

    DarwinBsdSubsystem bsd;
    const std::string hosts_content = "127.0.0.1 localhost\n::1 ip6-localhost\n";
    bsd.vfs().write_file("/etc/hosts", hosts_content.data(), hosts_content.size());

    std::uint32_t pid1 = bsd.create_process(1, "/bin/sh");
    std::uint32_t pid2 = bsd.create_process(1, "/bin/sh");

    int fd1 = bsd.open(pid1, "/etc/hosts", 0);
    int fd2 = bsd.open(pid2, "/etc/hosts", 0);

    TEST_ASSERT(fd1 == 3, "pid1 fd must be 3");
    TEST_ASSERT(fd2 == 3, "pid2 fd must be 3 (per-process namespace)");

    // Read 10 bytes from pid1
    char buf1[11]{0};
    std::int64_t n1 = bsd.read(pid1, fd1, buf1, 10);
    TEST_ASSERT(n1 == 10, "pid1 read must return 10 bytes");
    TEST_ASSERT(std::string(buf1) == "127.0.0.1 ", "pid1 content mismatch");

    // pid2 must still be at offset 0
    char buf2[11]{0};
    std::int64_t n2 = bsd.read(pid2, fd2, buf2, 10);
    TEST_ASSERT(n2 == 10, "pid2 read must return 10 bytes from offset 0");
    TEST_ASSERT(std::string(buf2) == "127.0.0.1 ", "pid2 content mismatch");

    // Close in pid1 must not affect pid2
    TEST_ASSERT(bsd.close(pid1, fd1) == 0, "pid1 close must succeed");
    char buf3[11]{0};
    std::int64_t n3 = bsd.read(pid2, fd2, buf3, 9);
    TEST_ASSERT(n3 == 9, "pid2 read must continue working after pid1 close");
    TEST_ASSERT(std::string(buf3) == "localhost", "pid2 subsequent content mismatch");

    std::cout << "  Passed (Verified full process-level file descriptor isolation)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Directory Iteration via getdirentries64
// -----------------------------------------------------------------------------
bool test_directory_iteration_getdirentries64() {
    std::cout << "[Test 5] Directory Iteration (getdirentries64)...\n";

    DarwinBsdSubsystem bsd;
    bsd.vfs().create_directory("/test_dir");
    bsd.vfs().write_file("/test_dir/alpha.txt", "A", 1);
    bsd.vfs().write_file("/test_dir/beta.bin", "B", 1);
    bsd.vfs().create_directory("/test_dir/sub");

    std::uint32_t pid = bsd.create_process(1, "/bin/ls");
    int dir_fd = bsd.open(pid, "/test_dir", 0);
    TEST_ASSERT(dir_fd >= 0, "open directory failed");

    std::vector<std::uint8_t> buffer(4096);
    std::int64_t basep = 0;
    int bytes = bsd.getdirentries64(pid, dir_fd, buffer.data(), buffer.size(), &basep);
    TEST_ASSERT(bytes > 0, "getdirentries64 must return bytes");

    std::vector<std::string> names;
    std::size_t offset = 0;
    while (offset + sizeof(DarwinDirent64Raw) <= static_cast<std::size_t>(bytes)) {
        const auto* raw = reinterpret_cast<const DarwinDirent64Raw*>(buffer.data() + offset);
        names.push_back(std::string(raw->d_name));
        offset += raw->d_reclen;
    }

    bool has_dot = false;
    bool has_dotdot = false;
    bool has_alpha = false;
    bool has_beta = false;
    bool has_sub = false;

    for (const auto& name : names) {
        if (name == ".") has_dot = true;
        if (name == "..") has_dotdot = true;
        if (name == "alpha.txt") has_alpha = true;
        if (name == "beta.bin") has_beta = true;
        if (name == "sub") has_sub = true;
    }

    TEST_ASSERT(has_dot, "Missing '.' entry");
    TEST_ASSERT(has_dotdot, "Missing '..' entry");
    TEST_ASSERT(has_alpha, "Missing 'alpha.txt' entry");
    TEST_ASSERT(has_beta, "Missing 'beta.bin' entry");
    TEST_ASSERT(has_sub, "Missing 'sub' entry");

    // Second call must report EOF (0 bytes)
    int bytes_eof = bsd.getdirentries64(pid, dir_fd, buffer.data(), buffer.size(), &basep);
    TEST_ASSERT(bytes_eof == 0, "Second getdirentries64 must return 0 (EOF)");

    std::cout << "  Passed (Validated Darwin Dirent64 binary record iteration)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Pipe Creation and Unidirectional IPC
// -----------------------------------------------------------------------------
bool test_pipe_unidirectional_ipc() {
    std::cout << "[Test 6] Pipe Creation and Unidirectional IPC...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/bin/sh");

    int fds[2]{-1, -1};
    TEST_ASSERT(bsd.pipe(pid, fds) == 0, "pipe creation must succeed");
    TEST_ASSERT(fds[0] == 3 && fds[1] == 4, "Pipe fds must be 3 (read) and 4 (write)");

    const std::string msg = "StreamingDarwinPipe123";
    std::int64_t written = bsd.write(pid, fds[1], msg.data(), msg.size());
    TEST_ASSERT(written == static_cast<std::int64_t>(msg.size()), "Pipe write failed");

    // Read end
    char buf[32]{0};
    std::int64_t r = bsd.read(pid, fds[0], buf, sizeof(buf));
    TEST_ASSERT(r == static_cast<std::int64_t>(msg.size()), "Pipe read failed");
    TEST_ASSERT(std::string(buf) == msg, "Pipe payload mismatch");

    // Write-only fd cannot be read
    TEST_ASSERT(bsd.read(pid, fds[1], buf, sizeof(buf)) == -1, "Reading write-end must fail");

    // Read-only fd cannot be written
    TEST_ASSERT(bsd.write(pid, fds[0], "X", 1) == -1, "Writing read-end must fail");

    // Close write-end -> next read is EOF (0)
    TEST_ASSERT(bsd.close(pid, fds[1]) == 0, "Close write-end failed");
    TEST_ASSERT(bsd.read(pid, fds[0], buf, sizeof(buf)) == 0, "Read after write-end closed must return 0");

    std::cout << "  Passed (Validated unidirectional FIFO pipe buffering and lifecycle)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: File Descriptor Duplication (dup & dup2)
// -----------------------------------------------------------------------------
bool test_file_descriptor_duplication() {
    std::cout << "[Test 7] File Descriptor Duplication (dup and dup2)...\n";

    DarwinBsdSubsystem bsd;
    const std::string text = "AlphaBetaGammaDelta";
    bsd.vfs().write_file("/data.txt", text.data(), text.size());

    std::uint32_t pid = bsd.create_process(1, "/bin/sh");
    int orig_fd = bsd.open(pid, "/data.txt", 0);
    TEST_ASSERT(orig_fd == 3, "Initial open must be fd 3");

    // dup -> allocates next available fd (4)
    int dup_fd = bsd.dup(pid, orig_fd);
    TEST_ASSERT(dup_fd == 4, "dup must allocate next available fd (4)");

    // dup2 -> assign to specific descriptor 15
    int dup2_fd = bsd.dup2(pid, orig_fd, 15);
    TEST_ASSERT(dup2_fd == 15, "dup2 must return target descriptor 15");

    // Read from dup2_fd
    char buf[6]{0};
    TEST_ASSERT(bsd.read(pid, dup2_fd, buf, 5) == 5, "Read from dup2_fd failed");
    TEST_ASSERT(std::string(buf) == "Alpha", "Content read mismatch");

    // Close original descriptor
    TEST_ASSERT(bsd.close(pid, orig_fd) == 0, "Close orig_fd failed");

    // dup2_fd (15) must remain valid and operational
    char buf2[5]{0};
    TEST_ASSERT(bsd.read(pid, dup2_fd, buf2, 4) == 4, "Read after orig close failed");
    TEST_ASSERT(std::string(buf2) == "Beta", "Offset progression mismatch");

    std::cout << "  Passed (Validated dup and dup2 fd cloning semantics)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 182: VFS AND FILE DESCRIPTOR INTEGRATION SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_path_canonicalization_and_normalization()) passed++; else failed++;
    if (test_symlink_creation_and_traversal()) passed++; else failed++;
    if (test_symlink_cycle_detection()) passed++; else failed++;
    if (test_multiprocess_fd_isolation()) passed++; else failed++;
    if (test_directory_iteration_getdirentries64()) passed++; else failed++;
    if (test_pipe_unidirectional_ipc()) passed++; else failed++;
    if (test_file_descriptor_duplication()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 182 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}