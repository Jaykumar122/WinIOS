#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"

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

bool test_default_vfs_hierarchy() {
    std::cout << "[Test 1] Darwin VFS Root Filesystem Structure...\n";

    DarwinBsdSubsystem bsd;
    const auto& vfs = bsd.vfs();

    TEST_ASSERT(vfs.resolve_path("/") != nullptr, "Root directory must exist");
    TEST_ASSERT(vfs.resolve_path("/sbin") != nullptr, "/sbin must exist");
    TEST_ASSERT(vfs.resolve_path("/bin") != nullptr, "/bin must exist");
    TEST_ASSERT(vfs.resolve_path("/usr/lib") != nullptr, "/usr/lib must exist");
    TEST_ASSERT(vfs.resolve_path("/System/Library/Frameworks") != nullptr, "Frameworks dir must exist");
    TEST_ASSERT(vfs.resolve_path("/dev/null") != nullptr, "/dev/null must exist");

    std::cout << "  Passed (Darwin VFS tree validated)\n";
    return true;
}

bool test_vfs_file_create_and_read() {
    std::cout << "[Test 2] VFS File Creation, Write and Read...\n";

    DarwinBsdSubsystem bsd;
    auto& vfs = bsd.vfs();

    const char text[] = "Darwin userspace initialized on Apple A16 Bionic";
    bool ok_write = vfs.write_file("/private/var/test.txt", text, sizeof(text));
    TEST_ASSERT(ok_write, "write_file must succeed");

    auto read_back = vfs.read_file("/private/var/test.txt");
    TEST_ASSERT(read_back.has_value(), "read_file must succeed");
    TEST_ASSERT(read_back->size() == sizeof(text), "File size match");
    TEST_ASSERT(std::memcmp(read_back->data(), text, sizeof(text)) == 0, "File content match");

    std::cout << "  Passed (File written and verified)\n";
    return true;
}

bool test_process_creation_and_pid1() {
    std::cout << "[Test 3] Process Lifecycle: PID 1 /sbin/launchd & Securityd...\n";

    DarwinBsdSubsystem bsd;
    auto p1 = bsd.get_process(1);
    TEST_ASSERT(p1 != nullptr, "PID 1 must exist");
    TEST_ASSERT(p1->executable_path == "/sbin/launchd", "PID 1 executable must be /sbin/launchd");
    TEST_ASSERT(p1->state == ProcessState::Running, "PID 1 must be running");

    std::uint32_t p2_pid = bsd.create_process(1, "/usr/libexec/securityd", 0);
    TEST_ASSERT(p2_pid > 1, "Child process must have PID > 1");
    auto p2 = bsd.get_process(p2_pid);
    TEST_ASSERT(p2 != nullptr, "Process lookup must succeed");
    TEST_ASSERT(p2->ppid == 1, "Parent PID must be 1");

    std::cout << "  Passed (PID 1 and securityd process active)\n";
    return true;
}

bool test_fork_process() {
    std::cout << "[Test 4] fork() Process Cloning...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t ppid = bsd.create_process(1, "/usr/sbin/notifyd", 0);
    std::uint32_t child_pid = bsd.fork(ppid);
    TEST_ASSERT(child_pid > ppid, "Forked PID must be greater than parent");

    auto child = bsd.get_process(child_pid);
    TEST_ASSERT(child != nullptr, "Child process must exist");
    TEST_ASSERT(child->ppid == ppid, "Child PPID must match parent");
    TEST_ASSERT(child->executable_path == "/usr/sbin/notifyd", "Child inherits executable path");
    TEST_ASSERT(child->fds.size() == 3, "Child inherits open standard file descriptors");

    std::cout << "  Passed (forked child PID " << child_pid << " from parent " << ppid << ")\n";
    return true;
}

bool test_file_descriptor_open_read_write() {
    std::cout << "[Test 5] File Descriptor Operations (open, write, read, close)...\n";

    DarwinBsdSubsystem bsd;
    auto& vfs = bsd.vfs();
    const char initial_data[] = "HELLO_DARWIN_IPC";
    vfs.write_file("/private/var/log.txt", initial_data, sizeof(initial_data));

    std::uint32_t pid = bsd.create_process(1, "/usr/sbin/syslogd", 0);
    int fd = bsd.open(pid, "/private/var/log.txt", 0);
    TEST_ASSERT(fd >= 3, "Opened fd must be >= 3");

    char buf[32] = {0};
    auto bytes_read = bsd.read(pid, fd, buf, sizeof(buf));
    TEST_ASSERT(bytes_read == sizeof(initial_data), "read() byte count match");
    TEST_ASSERT(std::strcmp(buf, initial_data) == 0, "read() content match");

    int rc = bsd.close(pid, fd);
    TEST_ASSERT(rc == 0, "close() must succeed");

    std::cout << "  Passed (Read " << bytes_read << " bytes via fd " << fd << ")\n";
    return true;
}

bool test_execve_macho_binary() {
    std::cout << "[Test 6] execve() Mach-O Binary Image Replacement...\n";

    DarwinBsdSubsystem bsd;
    Memory mem(0x100000); // 1 MB guest RAM

    // Generate synthetic ARM64 Mach-O executable
    auto macho_bytes = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    bsd.vfs().write_file("/Applications/TestApp.app/TestApp", macho_bytes.data(), macho_bytes.size());

    std::uint32_t pid = bsd.create_process(1, "/bin/sh", 501);
    bool ok_exec = bsd.execve(pid, "/Applications/TestApp.app/TestApp", {"TestApp"}, {}, mem);
    TEST_ASSERT(ok_exec, "execve must succeed for valid Mach-O");

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc->executable_path == "/Applications/TestApp.app/TestApp", "Executable path updated");
    TEST_ASSERT(!proc->vm_regions.empty(), "VM regions populated from Mach-O segments");

    std::cout << "  Passed (Replaced process image via execve)\n";
    return true;
}

bool test_mmap_anonymous_pages() {
    std::cout << "[Test 7] mmap() Anonymous Page Allocation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/libexec/lsd", 0);

    std::uint64_t addr = bsd.mmap(pid, 0, 0x8000, 3, 0x1000, -1, 0); // 32 KB anonymous map
    TEST_ASSERT(addr != 0, "mmap must return non-zero address");
    TEST_ASSERT((addr & 0x3FFFULL) == 0, "Address must be 16 KB page-aligned");

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(!proc->vm_regions.empty(), "Process must track new VM region");

    std::cout << "  Passed (Allocated 32 KB at 0x" << std::hex << addr << std::dec << ")\n";
    return true;
}

bool test_munmap_pages() {
    std::cout << "[Test 8] munmap() Virtual Memory Deallocation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/libexec/lsd", 0);

    std::uint64_t addr = bsd.mmap(pid, 0, 0x4000, 3, 0x1000, -1, 0);
    bool ok_unmap = bsd.munmap(pid, addr, 0x4000);
    TEST_ASSERT(ok_unmap, "munmap must succeed");

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc->vm_regions.empty(), "VM region must be removed");

    std::cout << "  Passed (munmap deallocated region)\n";
    return true;
}

bool test_signal_delivery_sigchld_and_waitpid() {
    std::cout << "[Test 9] SIGCHLD Signal Delivery and waitpid() Reaping...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent = bsd.create_process(1, "/usr/sbin/launchd_parent", 0);
    std::uint32_t child  = bsd.create_process(parent, "/usr/sbin/worker", 0);

    // Child terminates with exit code 42
    bsd.exit_process(child, 42);

    // Parent must receive SIGCHLD
    TEST_ASSERT(bsd.has_pending_signal(parent, kDarwinSigChld), "Parent must receive SIGCHLD");

    int exit_status = 0;
    std::int32_t reaped_pid = bsd.waitpid(parent, static_cast<std::int32_t>(child), exit_status, 0);
    TEST_ASSERT(reaped_pid == static_cast<std::int32_t>(child), "waitpid must return child PID");
    TEST_ASSERT(exit_status == 42, "waitpid must return exit code 42");

    auto child_proc = bsd.get_process(child);
    TEST_ASSERT(child_proc->state == ProcessState::Terminated, "Child must be Terminated after reaping");

    std::cout << "  Passed (Reaped child " << child << " with exit code " << exit_status << ")\n";
    return true;
}

bool test_sigkill_termination() {
    std::cout << "[Test 10] SIGKILL Forced Termination...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/sbin/runaway_task", 0);
    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc->state == ProcessState::Running, "Task initially running");

    bool ok_kill = bsd.kill(pid, kDarwinSigKill);
    TEST_ASSERT(ok_kill, "kill() must succeed");
    TEST_ASSERT(proc->state == ProcessState::Terminated, "SIGKILL must terminate process immediately");

    std::cout << "  Passed (Task terminated via SIGKILL)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 125: DARWIN BSD/POSIX USERSPACE SUBSYSTEM TESTS   \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: Darwin VFS, Process Lifecycle, mmap & Signals          \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_default_vfs_hierarchy()) passed++;
    if (test_vfs_file_create_and_read()) passed++;
    if (test_process_creation_and_pid1()) passed++;
    if (test_fork_process()) passed++;
    if (test_file_descriptor_open_read_write()) passed++;
    if (test_execve_macho_binary()) passed++;
    if (test_mmap_anonymous_pages()) passed++;
    if (test_munmap_pages()) passed++;
    if (test_signal_delivery_sigchld_and_waitpid()) passed++;
    if (test_sigkill_termination()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 125 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
