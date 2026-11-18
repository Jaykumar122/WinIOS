#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/bsd_process.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

void test_process_state_transitions() {
    std::cout << "[Test 1] Stage 219.2: Process State Machine & Lifecycle Transitions...\n";

    BsdProcess proc(100, 1, "test_proc");
    TEST_ASSERT(proc.pid() == 100);
    TEST_ASSERT(proc.ppid() == 1);
    TEST_ASSERT(proc.state() == ProcessState::Starting);
    TEST_ASSERT(!proc.is_terminated());

    proc.set_state(ProcessState::Runnable);
    TEST_ASSERT(proc.state() == ProcessState::Runnable);

    proc.set_state(ProcessState::Sleeping);
    TEST_ASSERT(proc.state() == ProcessState::Sleeping);

    proc.set_state(ProcessState::Stopped);
    TEST_ASSERT(proc.state() == ProcessState::Stopped);

    proc.set_state(ProcessState::Runnable);
    TEST_ASSERT(proc.state() == ProcessState::Runnable);

    proc.terminate(42);
    TEST_ASSERT(proc.state() == ProcessState::Zombie);
    TEST_ASSERT(proc.exit_code() == 42);
    TEST_ASSERT(proc.is_terminated());

    std::cout << "  -> Process state transitions validated successfully.\n";
}

void test_address_space_isolation() {
    std::cout << "[Test 2] Stage 219.3: Address Space Isolation & Page Table Independence...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_a = ppm.allocate_page();
    uint64_t root_b = ppm.allocate_page();
    TEST_ASSERT(root_a != 0);
    TEST_ASSERT(root_b != 0);
    TEST_ASSERT(root_a != root_b);

    auto uas_a = std::make_unique<UserAddressSpace>(mem, ppm, root_a, "proc_a_uas");
    auto uas_b = std::make_unique<UserAddressSpace>(mem, ppm, root_b, "proc_b_uas");

    // Both processes map the same virtual address 0x10000000 (256 MB) to different physical pages
    const uint64_t test_va = 0x10000000ULL;
    uint64_t pa_a = ppm.allocate_page();
    uint64_t pa_b = ppm.allocate_page();
    TEST_ASSERT(pa_a != 0 && pa_b != 0 && pa_a != pa_b);

    TEST_ASSERT(uas_a->map_user_page(test_va, pa_a, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas_b->map_user_page(test_va, pa_b, PagePermissions::UserReadWrite));

    // Verify translations through individual address spaces
    TEST_ASSERT(uas_a->translate(test_va) == pa_a);
    TEST_ASSERT(uas_b->translate(test_va) == pa_b);

    // Write distinct data into the same virtual address in both processes
    const uint32_t val_a = 0xAAAAAAAA;
    const uint32_t val_b = 0xBBBBBBBB;
    TEST_ASSERT(uas_a->copyin(test_va, &val_a, sizeof(val_a)));
    TEST_ASSERT(uas_b->copyin(test_va, &val_b, sizeof(val_b)));

    // Verify memory isolation: Process A sees val_a, Process B sees val_b
    uint32_t read_a = 0;
    uint32_t read_b = 0;
    TEST_ASSERT(uas_a->copyout(reinterpret_cast<const void*>(test_va), &read_a, sizeof(read_a)));
    TEST_ASSERT(uas_b->copyout(reinterpret_cast<const void*>(test_va), &read_b, sizeof(read_b)));
    TEST_ASSERT(read_a == val_a);
    TEST_ASSERT(read_b == val_b);

    // Map a second page ONLY in Process A
    const uint64_t unique_va = 0x20000000ULL;
    uint64_t pa_unique = ppm.allocate_page();
    TEST_ASSERT(uas_a->map_user_page(unique_va, pa_unique, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas_a->translate(unique_va) == pa_unique);

    // Verify Process B does NOT map unique_va
    TEST_ASSERT(uas_b->translate(unique_va) == 0);
    TEST_ASSERT(!uas_b->validate_user_buffer(unique_va, 4, false));

    // Hardware MMU verification: switch TTBR0_EL1 in CPU
    CPU cpu;
    cpu.reset();
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    cpu.get_mmu().set_ttbr0_el1(root_a);
    cpu.get_mmu().invalidate_tlb_all();
    auto translated_mmu_a = cpu.get_mmu().translate(mem, test_va, mmu::AccessKind::Read, 0, nullptr);
    TEST_ASSERT(translated_mmu_a.has_value());
    TEST_ASSERT(*translated_mmu_a == pa_a);

    cpu.get_mmu().set_ttbr0_el1(root_b);
    cpu.get_mmu().invalidate_tlb_all();
    auto translated_mmu_b = cpu.get_mmu().translate(mem, test_va, mmu::AccessKind::Read, 0, nullptr);
    TEST_ASSERT(translated_mmu_b.has_value());
    TEST_ASSERT(*translated_mmu_b == pa_b);

    std::cout << "  -> Hardware MMU TTBR0_EL1 isolation and distinct mappings verified.\n";
}

void test_process_lifecycle_and_reclamation() {
    std::cout << "[Test 3] Stage 219.4: Process Lifecycle, Orphan Reparenting & Resource Reclamation...\n";

    Memory mem(0x800000); // 8 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize(nullptr)); // Creates PID 0 (kernel)
    TEST_ASSERT(proc_mgr.process_count() == 1);

    // Create init process (PID 1)
    BsdProcess* init_proc = proc_mgr.create_process("launchd", 0);
    TEST_ASSERT(init_proc != nullptr);
    TEST_ASSERT(init_proc->pid() == 1);

    // Create Parent process (PID 2)
    uint64_t root_parent = ppm.allocate_page();
    auto uas_parent = std::make_unique<UserAddressSpace>(mem, ppm, root_parent, "parent_vm");
    BsdProcess* parent = proc_mgr.create_process("parent_svc", 1, nullptr, uas_parent.get());
    TEST_ASSERT(parent != nullptr);
    TEST_ASSERT(parent->ppid() == 1);
    TEST_ASSERT(parent->address_space() == uas_parent.get());

    // Create Child process (PID 3)
    uint64_t root_child = ppm.allocate_page();
    auto uas_child = std::make_unique<UserAddressSpace>(mem, ppm, root_child, "child_worker");
    BsdProcess* child = proc_mgr.create_process("child_worker", parent->pid(), nullptr, uas_child.get());
    TEST_ASSERT(child != nullptr);
    TEST_ASSERT(child->ppid() == parent->pid());
    TEST_ASSERT(parent->children().size() == 1);
    TEST_ASSERT(parent->children()[0] == child->pid());

    // Open file descriptors in child
    int fd0 = child->fd_table().allocate_fd(FileType::Regular, true, true, "/tmp/child.log");
    TEST_ASSERT(fd0 >= 0);
    TEST_ASSERT(child->fd_table().is_valid_fd(fd0));

    // Terminate child: exit code 123
    const pid_t child_pid = child->pid();
    TEST_ASSERT(proc_mgr.terminate_process(child_pid, 123));
    TEST_ASSERT(child->is_terminated());
    TEST_ASSERT(child->exit_code() == 123);
    // FD table should be closed on termination
    TEST_ASSERT(!child->fd_table().is_valid_fd(fd0));

    // Parent waits for child (wait4 semantics)
    int status = 0;
    int reaped_pid = proc_mgr.wait_process(parent->pid(), child_pid, &status, 0);
    TEST_ASSERT(reaped_pid == child_pid);
    TEST_ASSERT((status >> 8) == 123);

    // After wait4 reaps the child, it should no longer exist in process manager
    TEST_ASSERT(proc_mgr.get_process(child_pid) == nullptr);
    TEST_ASSERT(parent->children().empty());

    // Test orphan reparenting: create Parent 2 and Grandchild
    BsdProcess* p2 = proc_mgr.create_process("parent_2", 1);
    BsdProcess* orphan = proc_mgr.create_process("orphan_child", p2->pid());
    TEST_ASSERT(orphan->ppid() == p2->pid());
    TEST_ASSERT(p2->children().size() == 1);

    // Terminate p2 before orphan exits
    TEST_ASSERT(proc_mgr.terminate_process(p2->pid(), 0));
    // Orphan should now be reparented to PID 1 (launchd)
    TEST_ASSERT(orphan->ppid() == 1);
    TEST_ASSERT(init_proc->children().size() >= 1);

    std::cout << "  -> Process lifecycle, orphan reparenting, and resource cleanup validated.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.3: Address Space Isolation & Process Lifecycle Tests\n";
    std::cout << "=================================================================\n";

    test_process_state_transitions();
    test_address_space_isolation();
    test_process_lifecycle_and_reclamation();

    std::cout << "\n[PASS] All Stage 219.3 tests passed successfully!\n";
    return 0;
}
