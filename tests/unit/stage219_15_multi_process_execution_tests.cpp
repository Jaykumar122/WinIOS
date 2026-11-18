#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/user_syscall_dispatcher.h"
#include "kernel/multi_process_scheduler.h"
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

void test_two_process_pipe_ipc_and_isolation() {
    std::cout << "[Test 1] Stage 219.15: Two-Process Pipe IPC & Memory Boundary Isolation...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    // Allocate independent page tables for Process 1 and Process 2
    uint64_t root_prod = ppm.allocate_page();
    uint64_t root_cons = ppm.allocate_page();
    TEST_ASSERT(root_prod != 0 && root_cons != 0 && root_prod != root_cons);

    UserAddressSpace uas_prod(mem, ppm, root_prod, "producer_vm");
    UserAddressSpace uas_cons(mem, ppm, root_cons, "consumer_vm");

    // Both processes map virtual address 0x10000000 to DIFFERENT physical pages
    uint64_t prod_data_pa = ppm.allocate_page();
    uint64_t cons_data_pa = ppm.allocate_page();
    TEST_ASSERT(prod_data_pa != cons_data_pa);

    const uint64_t test_va = 0x10000000ULL;
    TEST_ASSERT(uas_prod.map_user_page(test_va, prod_data_pa, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas_cons.map_user_page(test_va, cons_data_pa, PagePermissions::UserReadWrite));

    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    BsdProcess* prod = proc_mgr.create_process("producer_process", 1, nullptr, &uas_prod);
    BsdProcess* cons = proc_mgr.create_process("consumer_process", 1, nullptr, &uas_cons);

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    scheduler.register_process_address_space(prod->pid(), &uas_prod);
    scheduler.register_process_address_space(cons->pid(), &uas_cons);

    // Create shared pipe between Producer and Consumer
    auto shared_pipe = std::make_shared<Pipe>();

    int prod_write_fd = prod->fd_table().allocate_fd(FileType::MemoryPipe, false, true, "pipe:w");
    auto* w_desc = prod->fd_table().get_fd_mut(prod_write_fd);
    w_desc->pipe = shared_pipe;
    w_desc->is_pipe_writer = true;

    int cons_read_fd = cons->fd_table().allocate_fd(FileType::MemoryPipe, true, false, "pipe:r");
    auto* r_desc = cons->fd_table().get_fd_mut(cons_read_fd);
    r_desc->pipe = shared_pipe;
    r_desc->is_pipe_reader = true;

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. Producer writes data from its VA (test_va)
    const char secret_msg[] = "Cross-Process Isolation Token #4096";
    const size_t msg_len = sizeof(secret_msg);
    TEST_ASSERT(uas_prod.copyin(test_va, secret_msg, msg_len));

    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, prod_write_fd);
    cpu.write_register(1, test_va);
    cpu.write_register(2, msg_len);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *prod, uas_prod, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == msg_len);

    // 2. Verify that Consumer's VA (test_va) currently holds all zeros (independent address space)
    char cons_initial[64]{0};
    TEST_ASSERT(uas_cons.copyout(reinterpret_cast<const void*>(test_va), cons_initial, msg_len));
    TEST_ASSERT(std::memcmp(cons_initial, secret_msg, msg_len) != 0);

    // 3. Switch context to Consumer and invoke SYS_read
    cpu.get_mmu().set_ttbr0_el1(root_cons);
    cpu.get_mmu().invalidate_tlb_all();

    cpu.write_register(16, kSysRead);
    cpu.write_register(0, cons_read_fd);
    cpu.write_register(1, test_va);
    cpu.write_register(2, 64);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *cons, uas_cons, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == msg_len);

    // 4. Verify Consumer received message into its private memory via pipe
    char cons_received[64]{0};
    TEST_ASSERT(uas_cons.copyout(reinterpret_cast<const void*>(test_va), cons_received, msg_len));
    TEST_ASSERT(std::strcmp(cons_received, secret_msg) == 0);

    std::cout << "  -> Cross-process IPC through kernel pipe with full address isolation verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.15: Multi-Process Execution Validation Tests\n";
    std::cout << "=================================================================\n";

    test_two_process_pipe_ipc_and_isolation();

    std::cout << "\n[PASS] Stage 219.15 tests passed successfully!\n";
    return 0;
}
