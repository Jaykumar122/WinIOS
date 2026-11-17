#include <iostream>
#include <vector>
#include <cstdint>

#include "kernel/bsd_process.h"
#include "kernel/task.h"
#include "kernel/thread.h"

using namespace winios;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_bsd_process_lifecycle() {
    std::cout << "[Test 1] Stage 211.5: BsdProcess Lifecycle & State Transitions...\n";

    BsdProcessManager mgr;
    TEST_CHECK(mgr.initialize(), "Process manager initialized");
    TEST_CHECK(mgr.process_count() == 1, "Initial process table contains PID 0 (kernel_process)");

    const auto* p0 = mgr.get_process(0);
    TEST_CHECK(p0 != nullptr, "PID 0 found");
    TEST_CHECK(p0->pid() == 0, "PID is 0");
    TEST_CHECK(p0->name() == "kernel_task", "PID 0 name is 'kernel_task'");
    TEST_CHECK(p0->state() == ProcessState::Runnable, "PID 0 is Runnable");

    // Create child process PID 1
    auto* p1 = mgr.create_process("init_process", 0);
    TEST_CHECK(p1 != nullptr, "Process PID 1 created");
    TEST_CHECK(p1->pid() == 1, "Allocated PID is 1");
    TEST_CHECK(p1->ppid() == 0, "Parent PID is 0");
    TEST_CHECK(p1->state() == ProcessState::Runnable, "PID 1 is Runnable");
    TEST_CHECK(mgr.process_count() == 2, "Process count is 2");

    // Create child process PID 2
    auto* p2 = mgr.create_process("worker_app", 1);
    TEST_CHECK(p2 != nullptr, "Process PID 2 created");
    TEST_CHECK(p2->pid() == 2, "Allocated PID is 2");
    TEST_CHECK(p2->ppid() == 1, "Parent PID is 1");

    // Thread association
    KernelThreadManager thread_mgr;
    auto* t1 = thread_mgr.create_thread("worker_t1", 0x1000ULL, 2, 0x70000ULL, 0x4000ULL, 16, true);
    p2->add_thread(t1);
    TEST_CHECK(p2->threads().size() == 1, "Thread associated with PID 2");

    // Terminate PID 2
    TEST_CHECK(mgr.terminate_process(2, 42), "Terminate PID 2 with code 42");
    TEST_CHECK(p2->is_terminated(), "PID 2 is terminated");
    TEST_CHECK(p2->state() == ProcessState::Zombie, "PID 2 transitioned to Zombie state");
    TEST_CHECK(p2->exit_code() == 42, "Exit code recorded as 42");
    TEST_CHECK(t1->state() == ThreadState::Terminated, "Associated thread terminated");

    // Cannot terminate PID 0
    TEST_CHECK(!mgr.terminate_process(0, 1), "Terminating PID 0 rejected");

    // Cannot reap non-terminated process
    TEST_CHECK(!mgr.reap_process(1), "Reaping active PID 1 rejected");

    // Reap zombie process PID 2
    TEST_CHECK(mgr.reap_process(2), "Reap zombie PID 2");
    TEST_CHECK(mgr.get_process(2) == nullptr, "PID 2 removed after reaping");
    TEST_CHECK(mgr.process_count() == 2, "Process count reduced to 2");
}

void test_file_descriptor_table() {
    std::cout << "[Test 2] Stage 211.5: FileDescriptorTable Standard Unix FDs & Allocation...\n";

    FileDescriptorTable fds;
    TEST_CHECK(fds.open_count() == 3, "Standard 0, 1, 2 open initially");

    TEST_CHECK(fds.is_valid_fd(0), "FD 0 (stdin) valid");
    TEST_CHECK(fds.is_valid_fd(1), "FD 1 (stdout) valid");
    TEST_CHECK(fds.is_valid_fd(2), "FD 2 (stderr) valid");

    const auto* f_in = fds.get_fd(0);
    const auto* f_out = fds.get_fd(1);
    TEST_CHECK(f_in->readable && !f_in->writable, "Stdin is readable only");
    TEST_CHECK(!f_out->readable && f_out->writable, "Stdout is writable only");

    // Allocate next FD (must be 3)
    const int fd3 = fds.allocate_fd(FileType::Regular, true, true, "/tmp/test.dat");
    TEST_CHECK(fd3 == 3, "Allocated FD is 3");
    TEST_CHECK(fds.open_count() == 4, "Open count is 4");

    // Close FD 1
    TEST_CHECK(fds.close_fd(1), "Close FD 1");
    TEST_CHECK(!fds.is_valid_fd(1), "FD 1 is now closed");

    // Next allocate reuses lowest available slot (FD 1)
    const int reused = fds.allocate_fd(FileType::Console, false, true, "/dev/console");
    TEST_CHECK(reused == 1, "Reused lowest free FD (1)");

    // Closing non-open or invalid FD
    TEST_CHECK(!fds.close_fd(100), "Closing out-of-bounds FD rejected");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.5 BSD Process Tests ===\n";
    test_bsd_process_lifecycle();
    test_file_descriptor_table();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.5: All BSD process tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.5: " << g_failures << " tests failed.\n";
        return 1;
    }
}
