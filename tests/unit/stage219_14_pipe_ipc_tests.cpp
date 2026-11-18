#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/pipe.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

void test_pipe_syscall_lifecycle() {
    std::cout << "[Test 1] Stage 219.14: Pipe Syscall Interface (SYS_pipe, read, write, close)...\n";

    Memory mem(0x400000); // 4 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "pipe_test_vm");

    uint64_t user_data_page = ppm.allocate_page();
    const uint64_t user_data_va = 0x10004000ULL;
    TEST_ASSERT(uas.map_user_page(user_data_va, user_data_page, PagePermissions::UserReadWrite));

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcess proc(100, 1, "pipe_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. Invoke SYS_pipe (SYS 42), passing buffer at user_data_va for int fds[2]
    cpu.write_register(16, kSysPipe);
    cpu.write_register(0, user_data_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));

    int fds[2]{0, 0};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(user_data_va), fds, sizeof(fds)));
    const int read_fd = fds[0];
    const int write_fd = fds[1];
    TEST_ASSERT(read_fd >= 3 && write_fd >= 3);
    TEST_ASSERT(read_fd != write_fd);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(read_fd));
    TEST_ASSERT(cpu.read_register(1) == static_cast<uint64_t>(write_fd));

    // 2. Write payload to pipe via SYS_write (SYS 4)
    const char write_msg[] = "Darwin Pipe Syscall Payload 2026";
    const size_t write_len = sizeof(write_msg);
    const uint64_t write_buf_va = user_data_va + 64;
    TEST_ASSERT(uas.copyin(write_buf_va, write_msg, write_len));

    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, write_fd);
    cpu.write_register(1, write_buf_va);
    cpu.write_register(2, write_len);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == write_len);

    // 3. Read payload from pipe via SYS_read (SYS 3)
    const uint64_t read_buf_va = user_data_va + 128;
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, read_fd);
    cpu.write_register(1, read_buf_va);
    cpu.write_register(2, 64);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == write_len);

    char read_msg[64]{0};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(read_buf_va), read_msg, write_len));
    TEST_ASSERT(std::strcmp(read_msg, write_msg) == 0);

    // 4. Close write end via SYS_close (SYS 6)
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, write_fd);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // 5. Read after writer close should yield EOF (0 bytes)
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, read_fd);
    cpu.write_register(1, read_buf_va);
    cpu.write_register(2, 64);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // EOF

    // 6. Close read end
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, read_fd);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));

    std::cout << "  -> End-to-end pipe creation, write, read, and EOF close verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.14: Pipe Syscall Interface Tests\n";
    std::cout << "=================================================================\n";

    test_pipe_syscall_lifecycle();

    std::cout << "\n[PASS] All Stage 219.14 tests passed successfully!\n";
    return 0;
}
