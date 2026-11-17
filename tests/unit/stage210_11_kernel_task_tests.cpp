#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "memory/xnu_vm_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;
using namespace winios::loader;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_task_manager() {
    std::cout << "[Test 1] Stage 210.11: KernelTask & KernelTaskManager Lifecycle...\n";

    KernelTaskManager tm;
    auto kmap = std::make_shared<XnuVmMap>(0x0ULL, 0x800000ULL, "kernel_map");

    // 1. Create kernel_task (PID 0)
    auto kt = tm.create_kernel_task(kmap);
    TEST_CHECK(kt != nullptr, "kernel_task created");
    TEST_CHECK(kt->pid() == 0, "kernel_task PID is 0");
    TEST_CHECK(kt->name() == "kernel_task", "kernel_task name verified");
    TEST_CHECK(tm.task_count() == 1, "TaskManager task count is 1");

    // 2. Add thread to kernel_task
    auto t1 = std::make_shared<KernelThread>(1, "kernel_thread", 0x1F80ULL, 0, 0xC000ULL, 0x4000ULL, 16);
    TEST_CHECK(kt->add_thread(t1), "Add TID 1 to kernel_task");
    TEST_CHECK(kt->thread_count() == 1, "kernel_task has 1 thread");
    TEST_CHECK(kt->get_thread(1) == t1, "get_thread(1) returns t1");

    // Duplicate thread rejection
    TEST_CHECK(!kt->add_thread(t1), "Duplicate thread addition rejected");

    // 3. Create user task (PID 1)
    auto umap = std::make_shared<XnuVmMap>(0x10000000ULL, 0x20000000ULL, "user_map");
    auto ut = tm.create_task("test_process", umap);
    TEST_CHECK(ut != nullptr, "user task created");
    TEST_CHECK(ut->pid() == 1, "user task PID is 1");
    TEST_CHECK(tm.task_count() == 2, "TaskManager task count is 2");

    auto t2 = std::make_shared<KernelThread>(2, "user_main_thread", 0x10000ULL, 0, 0x20000ULL, 0x4000ULL, 16, true);
    TEST_CHECK(ut->add_thread(t2), "Add TID 2 to user task");

    // 4. Suspend and resume task
    TEST_CHECK(ut->suspend(), "Suspend user task");
    TEST_CHECK(ut->state() == TaskState::Suspended, "Task state is Suspended");
    TEST_CHECK(ut->suspend_count() == 1, "Suspend count is 1");

    TEST_CHECK(ut->resume(), "Resume user task");
    TEST_CHECK(ut->state() == TaskState::Active, "Task state back to Active");
    TEST_CHECK(ut->suspend_count() == 0, "Suspend count back to 0");

    // 5. Terminate task
    TEST_CHECK(tm.terminate_task(1), "Terminate user task");
    TEST_CHECK(ut->state() == TaskState::Terminated, "Task state is Terminated");
    TEST_CHECK(t2->state() == ThreadState::Terminated, "Thread state cascaded to Terminated");
}

void test_guest_kernel_task_memory() {
    std::cout << "[Test 2] Stage 210.11: In-Memory Darwin kernel_task Header Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    KernelBootCoordinator::prepare_and_boot(cpu, mem, config);
    for (std::size_t i = 0; i < 15000; ++i) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(mem);
    }

    // Inspect guest kernel_task header at 0x29600
    const std::uint64_t magic = mem.read_u64(0x29600ULL);
    const std::uint32_t pid = mem.read_u32(0x29608ULL);
    const std::uint32_t thread_count = mem.read_u32(0x2960CULL);
    const std::uint64_t map_addr = mem.read_u64(0x29610ULL);

    TEST_CHECK(magic == 0x5441534B5F54534BULL, "kernel_task magic matches 'TASK_TSK'");
    TEST_CHECK(pid == 0, "kernel_task PID is 0");
    TEST_CHECK(thread_count == 1, "kernel_task thread count is 1");
    TEST_CHECK(map_addr == 0x29200ULL, "kernel_task map points to 0x29200");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.11 Kernel Task Tests ===\n";
    test_kernel_task_manager();
    test_guest_kernel_task_memory();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.11: All kernel task tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.11: " << g_failures << " tests failed.\n";
        return 1;
    }
}
