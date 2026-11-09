#include <cstdint>
#include <cstdlib>
#include <cstring>
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

// Test 1: Process Lifecycle: Running -> Zombie -> Reaped
bool test_process_lifecycle_zombie_reap() {
    std::cout << "[Test 1] Process Lifecycle: Running -> Zombie -> Reaped...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = bsd.create_process(1, "/usr/libexec/diagnosticd", 501);
    TEST_ASSERT(pid > 1, "Process must have valid PID > 1");

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc != nullptr, "Process must exist in process table");
    TEST_ASSERT(proc->state == ProcessState::Running, "Initial state must be Running");

    // Exit process with exit code 42
    bsd.exit_process(pid, 42);
    TEST_ASSERT(proc->state == ProcessState::Zombie, "State must become Zombie upon exit");
    TEST_ASSERT(proc->exit_code == 42, "Exit code 42 must be recorded");

    // Parent (PID 1) reaps child via waitpid
    int status = 0;
    std::int32_t reaped_pid = bsd.waitpid(1, static_cast<std::int32_t>(pid), status, 0);
    TEST_ASSERT(reaped_pid == static_cast<std::int32_t>(pid), "waitpid must return child PID");
    TEST_ASSERT(status == 42, "reaped status must match exit code 42");
    TEST_ASSERT(proc->state == ProcessState::Terminated, "State must become Terminated after waitpid");

    return true;
}

// Test 2: Thread Creation, Register Context & TPIDR_EL0
bool test_thread_creation_and_tpidr() {
    std::cout << "[Test 2] Thread Creation, Register Context & TPIDR_EL0...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = bsd.create_process(1, "/usr/sbin/syslogd", 0);

    // Create a secondary worker thread
    const std::uint64_t worker_entry = 0x100004500ULL;
    const std::uint64_t worker_sp    = 0x00007FFFF0000000ULL;
    const std::uint64_t worker_tls   = 0x00007FFFF0001000ULL;

    std::uint64_t tid2 = bsd.create_thread(pid, worker_entry, worker_sp, worker_tls);
    TEST_ASSERT(tid2 == 2, "Second thread must have TID 2");

    DarwinThreadContext ctx{};
    TEST_ASSERT(bsd.get_thread_context(pid, tid2, ctx), "Must retrieve worker thread context");
    TEST_ASSERT(ctx.tid == tid2, "TID match");
    TEST_ASSERT(ctx.pc == worker_entry, "PC must match entry address");
    TEST_ASSERT(ctx.sp == worker_sp, "SP must match stack pointer");
    TEST_ASSERT(ctx.tpidr_el0 == worker_tls, "TPIDR_EL0 must match thread-local storage address");

    // Modify registers in context
    ctx.x[0] = 0xCAFEBABEULL;
    ctx.x[1] = 0xDEADBEEFULL;
    TEST_ASSERT(bsd.set_thread_context(pid, tid2, ctx), "set_thread_context must succeed");

    DarwinThreadContext updated_ctx{};
    TEST_ASSERT(bsd.get_thread_context(pid, tid2, updated_ctx), "get_thread_context must succeed");
    TEST_ASSERT(updated_ctx.x[0] == 0xCAFEBABEULL, "X0 must reflect updated register value");
    TEST_ASSERT(updated_ctx.x[1] == 0xDEADBEEFULL, "X1 must reflect updated register value");

    return true;
}

// Test 3: Process Fork State Inheritance
bool test_fork_state_inheritance() {
    std::cout << "[Test 3] Process Fork State Inheritance...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/usr/bin/python3", 501);
    auto parent = bsd.get_process(parent_pid);
    TEST_ASSERT(parent != nullptr, "Parent process must exist");

    // Open file in parent
    bsd.vfs().write_file("/tmp/shared.txt", "parent data", 11);
    int fd = bsd.open(parent_pid, "/tmp/shared.txt", 0);
    TEST_ASSERT(fd >= 3, "Valid FD opened in parent");

    std::uint32_t child_pid = bsd.fork(parent_pid);
    TEST_ASSERT(child_pid > parent_pid, "Child PID must be greater than parent PID");

    auto child = bsd.get_process(child_pid);
    TEST_ASSERT(child != nullptr, "Child process must exist");
    TEST_ASSERT(child->ppid == parent_pid, "Child PPID must match parent PID");
    TEST_ASSERT(child->uid == parent->uid, "Child must inherit UID");
    TEST_ASSERT(child->gid == parent->gid, "Child must inherit GID");
    TEST_ASSERT(child->fds.find(fd) != child->fds.end(), "Child must inherit parent's file descriptor");

    return true;
}

// Test 4: File Descriptor Table Isolation After Fork
bool test_fd_table_isolation_after_fork() {
    std::cout << "[Test 4] File Descriptor Table Isolation After Fork...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/usr/bin/worker", 501);

    bsd.vfs().write_file("/tmp/p.txt", "123", 3);
    int p_fd = bsd.open(parent_pid, "/tmp/p.txt", 0);
    TEST_ASSERT(p_fd >= 3, "Parent file opened");

    std::uint32_t child_pid = bsd.fork(parent_pid);

    // Close descriptor in child
    int close_res = bsd.close(child_pid, p_fd);
    TEST_ASSERT(close_res == 0, "Closing descriptor in child must succeed");

    // Parent descriptor must STILL be open!
    auto parent = bsd.get_process(parent_pid);
    TEST_ASSERT(parent->fds.find(p_fd) != parent->fds.end(),
                "Parent descriptor table must remain unaffected by child close");

    // Open new file in child
    bsd.vfs().write_file("/tmp/c.txt", "456", 3);
    int c_fd = bsd.open(child_pid, "/tmp/c.txt", 0);
    TEST_ASSERT(c_fd >= 3, "Child opened new file");

    // Parent must NOT have child's newly opened descriptor
    TEST_ASSERT(parent->fds.find(c_fd) == parent->fds.end() || c_fd == p_fd,
                "Parent must not inherit files opened after fork in child");

    return true;
}

// Test 5: Signal Delivery and Pending Signal Mask
bool test_signal_delivery_and_mask() {
    std::cout << "[Test 5] Signal Delivery and Pending Signal Mask...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/libexec/cfprefsd", 501);

    TEST_ASSERT(!bsd.has_pending_signal(pid, 15), "No signal initially pending");

    // Send SIGTERM (15)
    TEST_ASSERT(bsd.kill(pid, 15), "kill(SIGTERM) must succeed");
    TEST_ASSERT(bsd.has_pending_signal(pid, 15), "SIGTERM must be pending");

    // Send SIGKILL (9)
    TEST_ASSERT(bsd.kill(pid, 9), "kill(SIGKILL) must succeed");
    TEST_ASSERT(bsd.has_pending_signal(pid, 9), "SIGKILL must be pending");

    return true;
}

// Test 6: waitpid WNOHANG Conformance
bool test_waitpid_wnohang() {
    std::cout << "[Test 6] waitpid WNOHANG Conformance...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t child_pid = bsd.create_process(1, "/usr/bin/daemon", 0);

    int status = 0;
    // 1 = WNOHANG in POSIX / Darwin
    const int kWaitOptionWNohang = 1;

    // While child is running, waitpid with WNOHANG must return 0
    std::int32_t res = bsd.waitpid(1, static_cast<std::int32_t>(child_pid), status, kWaitOptionWNohang);
    TEST_ASSERT(res == 0, "waitpid(WNOHANG) on running process must return 0");

    // Now exit child
    bsd.exit_process(child_pid, 0);

    // waitpid with WNOHANG on zombie child must reap it and return child PID
    res = bsd.waitpid(1, static_cast<std::int32_t>(child_pid), status, kWaitOptionWNohang);
    TEST_ASSERT(res == static_cast<std::int32_t>(child_pid), "waitpid must reap zombie child and return child PID");
    TEST_ASSERT(status == 0, "Status must be 0");

    return true;
}

// Test 7: Multithreaded Concurrency State Tracking
bool test_multithread_state_tracking() {
    std::cout << "[Test 7] Multithreaded Concurrency State Tracking...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/System/Library/CoreServices/SpringBoard.app/SpringBoard", 501);

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc != nullptr, "SpringBoard process exists");
    TEST_ASSERT(proc->threads.size() == 1, "Starts with 1 main thread");

    // Spawn 3 additional threads
    std::uint64_t tid2 = bsd.create_thread(pid, 0x10001000ULL, 0x7FFF0000ULL, 0x1000ULL);
    std::uint64_t tid3 = bsd.create_thread(pid, 0x10002000ULL, 0x7FFE0000ULL, 0x2000ULL);
    std::uint64_t tid4 = bsd.create_thread(pid, 0x10003000ULL, 0x7FFD0000ULL, 0x3000ULL);

    TEST_ASSERT(tid2 == 2 && tid3 == 3 && tid4 == 4, "TIDs must be consecutive (2, 3, 4)");
    TEST_ASSERT(proc->threads.size() == 4, "Process must track exactly 4 threads");

    DarwinThreadContext ctx3{};
    TEST_ASSERT(bsd.get_thread_context(pid, tid3, ctx3), "Retrieve thread 3 context");
    TEST_ASSERT(ctx3.pc == 0x10002000ULL, "Thread 3 PC match");
    TEST_ASSERT(ctx3.sp == 0x7FFE0000ULL, "Thread 3 SP match");
    TEST_ASSERT(ctx3.tpidr_el0 == 0x2000ULL, "Thread 3 TPIDR_EL0 match");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 165: Darwin Process and Thread Conformance\n";
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

    run_test(test_process_lifecycle_zombie_reap, "Process Lifecycle: Running -> Zombie -> Reaped");
    run_test(test_thread_creation_and_tpidr, "Thread Creation, Context & TPIDR_EL0");
    run_test(test_fork_state_inheritance, "Process Fork State Inheritance");
    run_test(test_fd_table_isolation_after_fork, "File Descriptor Table Isolation After Fork");
    run_test(test_signal_delivery_and_mask, "Signal Delivery & Pending Signal Mask");
    run_test(test_waitpid_wnohang, "waitpid WNOHANG Conformance");
    run_test(test_multithread_state_tracking, "Multithreaded State Tracking");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
