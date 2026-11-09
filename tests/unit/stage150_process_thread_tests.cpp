#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "runtime/darwin_bsd_subsystem.h"

using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Process Table and PID 1 /sbin/launchd Initialization
bool test_pid1_initialization() {
    std::cout << "[Test 1] PID 1 /sbin/launchd Process & Root Thread Initialization...\n";

    DarwinBsdSubsystem bsd;
    auto proc1 = bsd.get_process(1);
    TEST_ASSERT(proc1 != nullptr, "PID 1 must exist");
    TEST_ASSERT(proc1->pid == 1, "Process PID must be 1");
    TEST_ASSERT(proc1->ppid == 0, "PID 1 parent PID must be 0 (kernel)");
    TEST_ASSERT(proc1->uid == 0, "PID 1 must execute as root (UID 0)");
    TEST_ASSERT(proc1->gid == 0, "PID 1 GID must be 0");
    TEST_ASSERT(proc1->state == ProcessState::Running, "PID 1 must be in Running state");
    TEST_ASSERT(proc1->executable_path == "/sbin/launchd", "Executable must be /sbin/launchd");
    TEST_ASSERT(proc1->threads.size() >= 1, "PID 1 must have at least one thread");
    TEST_ASSERT(proc1->threads[0].tid == 1, "Root thread TID must be 1");
    TEST_ASSERT(proc1->threads[0].sp == 0x00007FFFFFE00000ULL, "Stack pointer initialized in upper userland");

    std::cout << "  -> PASS: PID 1 /sbin/launchd Root Process Verified\n";
    return true;
}

// Test 2: Process Creation and Hierarchy
bool test_process_hierarchy() {
    std::cout << "[Test 2] Process Creation & Parent-Child Hierarchy...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t daemon_pid = bsd.create_process(1, "/usr/libexec/securityd", 0);
    TEST_ASSERT(daemon_pid > 1, "Child process must have PID > 1");

    auto daemon = bsd.get_process(daemon_pid);
    TEST_ASSERT(daemon != nullptr, "Daemon process must be retrievable");
    TEST_ASSERT(daemon->pid == daemon_pid, "PID match");
    TEST_ASSERT(daemon->ppid == 1, "Parent PID must be 1 (launchd)");
    TEST_ASSERT(daemon->executable_path == "/usr/libexec/securityd", "Path match");
    TEST_ASSERT(daemon->state == ProcessState::Running, "Initial state must be Running");

    std::cout << "  -> PASS: Parent-Child Hierarchy Verified\n";
    return true;
}

// Test 3: Process Cloning via fork()
bool test_fork_cloning() {
    std::cout << "[Test 3] Process State Duplication via fork()...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/usr/sbin/notifyd", 501);
    auto parent = bsd.get_process(parent_pid);
    TEST_ASSERT(parent != nullptr, "Parent must exist");

    // Add VM region and open an FD in parent before forking
    int fd = bsd.open(parent_pid, "/dev/null", 0x2); // O_RDWR
    TEST_ASSERT(fd >= 3, "Valid FD opened in parent");

    std::uint32_t child_pid = bsd.fork(parent_pid);
    TEST_ASSERT(child_pid > parent_pid, "Child PID must be distinct and greater than parent PID");

    auto child = bsd.get_process(child_pid);
    TEST_ASSERT(child != nullptr, "Child process exists");
    TEST_ASSERT(child->pid == child_pid, "Child PID match");
    TEST_ASSERT(child->ppid == parent_pid, "Child parent PID must be parent_pid");
    TEST_ASSERT(child->uid == 501, "Child inherits UID");
    TEST_ASSERT(child->fds.find(fd) != child->fds.end(), "Child inherits parent open file descriptors");
    TEST_ASSERT(child->threads.size() == parent->threads.size(), "Child inherits thread count");

    std::cout << "  -> PASS: fork() Process Cloning Verified\n";
    return true;
}

// Test 4: Process Termination, Exit Codes, and SIGCHLD Delivery
bool test_exit_and_sigchld() {
    std::cout << "[Test 4] Process Termination, Exit Codes & SIGCHLD...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/System/Library/CoreServices/SpringBoard.app/SpringBoard", 501);
    std::uint32_t app_pid = bsd.create_process(parent_pid, "/Applications/Preferences.app/Preferences", 501);

    auto app = bsd.get_process(app_pid);
    TEST_ASSERT(app != nullptr, "App process created");

    // Exit child process with exit code 42
    bsd.exit_process(app_pid, 42);

    TEST_ASSERT(app->state == ProcessState::Zombie, "Exited process must transition to Zombie state");
    TEST_ASSERT(app->exit_code == 42, "Exit code must be preserved in process table");

    // Parent must receive SIGCHLD (signal 20)
    TEST_ASSERT(bsd.has_pending_signal(parent_pid, 20), "Parent must have pending SIGCHLD (20)");

    std::cout << "  -> PASS: Process Exit and SIGCHLD Notification Verified\n";
    return true;
}

// Test 5: waitpid() Child Status Harvesting and Reaping
bool test_waitpid_harvesting() {
    std::cout << "[Test 5] waitpid() Child Harvesting and Reaping...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/usr/bin/parent_service", 0);
    std::uint32_t child_pid = bsd.create_process(parent_pid, "/usr/bin/child_worker", 0);

    // Before exit, waitpid returns -1 (child not exited)
    int status = 0;
    std::int32_t reaped_pre = bsd.waitpid(parent_pid, static_cast<std::int32_t>(child_pid), status, 0);
    TEST_ASSERT(reaped_pre == -1, "waitpid must return -1 if child has not exited");

    // Child exits with status 0
    bsd.exit_process(child_pid, 0);

    // Harvest child via waitpid
    std::int32_t reaped_pid = bsd.waitpid(parent_pid, static_cast<std::int32_t>(child_pid), status, 0);
    TEST_ASSERT(reaped_pid == static_cast<std::int32_t>(child_pid), "waitpid must return reaped child PID");
    TEST_ASSERT(status == 0, "Harvested exit status must be 0");

    auto child = bsd.get_process(child_pid);
    TEST_ASSERT(child->state == ProcessState::Terminated, "Reaped child must transition to Terminated state");

    std::cout << "  -> PASS: waitpid() Reaping and Status Harvesting Verified\n";
    return true;
}

// Test 6: Thread Creation, Stack Pointer, and TPIDR_EL0 Binding
bool test_thread_creation_and_tls() {
    std::cout << "[Test 6] Thread Creation, Stack Allocation & TPIDR_EL0 (TLS)...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/libexec/lsd", 501);

    const std::uint64_t thread_entry = 0x0000000100042000ULL;
    const std::uint64_t thread_sp    = 0x00007FFFFFD00000ULL;
    const std::uint64_t thread_tls   = 0x0000000180050000ULL; // TPIDR_EL0

    std::uint64_t tid = bsd.create_thread(pid, thread_entry, thread_sp, thread_tls);
    TEST_ASSERT(tid >= 2, "Secondary thread TID must be >= 2");

    DarwinThreadContext ctx;
    bool ok = bsd.get_thread_context(pid, tid, ctx);
    TEST_ASSERT(ok, "Thread context must be retrievable");
    TEST_ASSERT(ctx.tid == tid, "TID match");
    TEST_ASSERT(ctx.pc == thread_entry, "Program counter matches entry point");
    TEST_ASSERT(ctx.sp == thread_sp, "Stack pointer matches allocated stack");
    TEST_ASSERT(ctx.tpidr_el0 == thread_tls, "TPIDR_EL0 matches thread-local storage address");

    std::cout << "  -> PASS: Thread Creation and TLS Binding Verified\n";
    return true;
}

// Test 7: Thread Context Switching & Register Preservation
bool test_thread_context_registers() {
    std::cout << "[Test 7] Thread Context Register Manipulation & Preservation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/libexec/cfprefsd", 501);

    std::uint64_t tid = bsd.create_thread(pid, 0x10001000ULL, 0x7FFFFFA00000ULL, 0x18000000ULL);
    DarwinThreadContext ctx;
    TEST_ASSERT(bsd.get_thread_context(pid, tid, ctx), "Get context");

    // Populate registers X0 through X30 with distinct known test values
    for (std::size_t r = 0; r < 31; ++r) {
        ctx.x[r] = 0xCAFE0000ULL + static_cast<std::uint64_t>(r);
    }
    ctx.pc = 0x10001080ULL;

    TEST_ASSERT(bsd.set_thread_context(pid, tid, ctx), "Set context");

    // Retrieve again and verify exact register preservation
    DarwinThreadContext read_back;
    TEST_ASSERT(bsd.get_thread_context(pid, tid, read_back), "Get updated context");
    TEST_ASSERT(read_back.pc == 0x10001080ULL, "PC preserved");
    for (std::size_t r = 0; r < 31; ++r) {
        TEST_ASSERT(read_back.x[r] == (0xCAFE0000ULL + r), "General purpose register preserved");
    }

    std::cout << "  -> PASS: Thread Register Context Preservation Verified\n";
    return true;
}

// Test 8: Process and Thread Address Space Isolation
bool test_process_thread_isolation() {
    std::cout << "[Test 8] Process & Thread Isolation Boundaries...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t proc_a = bsd.create_process(1, "/usr/bin/service_a", 501);
    std::uint32_t proc_b = bsd.create_process(1, "/usr/bin/service_b", 501);

    // Open file in proc_a
    int fd_a = bsd.open(proc_a, "/dev/null", 0x1);
    TEST_ASSERT(fd_a >= 3, "proc_a opened file");

    // proc_b must not see proc_a's file descriptor
    auto b = bsd.get_process(proc_b);
    TEST_ASSERT(b->fds.find(fd_a) == b->fds.end(), "proc_b must not have proc_a's custom fd");

    // Thread contexts must be isolated between processes
    DarwinThreadContext dummy;
    bool found_cross = bsd.get_thread_context(proc_b, 9999, dummy);
    TEST_ASSERT(!found_cross, "Invalid TID in proc_b must return false");

    std::cout << "  -> PASS: Process and Thread Isolation Boundaries Verified\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 150: DARWIN PROCESS & THREAD FOUNDATION TESTS     \n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Subsystem: Darwin BSD Process Table, Fork, Exec & Threads      \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_pid1_initialization()) passed++;
    if (test_process_hierarchy()) passed++;
    if (test_fork_cloning()) passed++;
    if (test_exit_and_sigchld()) passed++;
    if (test_waitpid_harvesting()) passed++;
    if (test_thread_creation_and_tls()) passed++;
    if (test_thread_context_registers()) passed++;
    if (test_process_thread_isolation()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 150 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
