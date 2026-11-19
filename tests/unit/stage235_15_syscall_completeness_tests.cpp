#include "cpu/cpu.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "memory/guest_memory_validator.h"
#include "memory/user_address_space.h"
#include "memory/physical_page_manager.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"
#include "packaging/app_registry.h"
#include "services/diagnostics_service.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

struct SyscallCompletenessFixture {
    CPU cpu;
    Memory mem;
    PhysicalPageManager ppm;
    KernelThreadManager thread_mgr;
    PriorityScheduler sched;
    VirtualConsole console;
    UserSyscallDispatcher dispatcher;
    std::shared_ptr<vfs::VirtualFileSystem> vfs;
    std::shared_ptr<security::CapabilityManager> cap_mgr;
    BsdProcessManager proc_mgr;
    BsdProcess* proc{nullptr};
    std::shared_ptr<settings::SystemStateManager> state_mgr;
    std::shared_ptr<settings::ControlCenterService> cc_svc;
    std::shared_ptr<settings::TaskManager> task_mgr;
    std::shared_ptr<settings::AccessibilityService> a11y_svc;
    std::shared_ptr<packaging::ApplicationRegistry> app_reg;
    std::shared_ptr<services::DiagnosticsService> diag_svc;
    std::unique_ptr<UserAddressSpace> uas;

    const std::uint64_t rw_page_va{0x20000ULL};
    const std::uint64_t ro_page_va{0x40000ULL};

    SyscallCompletenessFixture()
        : mem(0x1000000ULL),
          ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL),
          sched(thread_mgr, mem),
          dispatcher(console)
    {
        ppm.initialize(false);
        sched.initialize();
        dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

        vfs = std::make_shared<vfs::VirtualFileSystem>();
        cap_mgr = std::make_shared<security::CapabilityManager>();
        dispatcher.set_capability_manager(cap_mgr.get());

        proc_mgr.initialize();
        proc = proc_mgr.create_process("completeness_proc", 1);
        TEST_ASSERT(proc != nullptr, "Process created");

        state_mgr = std::make_shared<settings::SystemStateManager>(vfs);
        cc_svc = std::make_shared<settings::ControlCenterService>(state_mgr.get());
        task_mgr = std::make_shared<settings::TaskManager>();
        a11y_svc = std::make_shared<settings::AccessibilityService>(state_mgr.get());
        app_reg = std::make_shared<packaging::ApplicationRegistry>();
        diag_svc = std::make_shared<services::DiagnosticsService>();

        dispatcher.set_system_state_manager(state_mgr.get());
        dispatcher.set_control_center_service(cc_svc.get());
        dispatcher.set_task_manager(task_mgr.get());
        dispatcher.set_accessibility_service(a11y_svc.get());
        dispatcher.set_app_registry(app_reg.get());
        dispatcher.set_diagnostics_service(diag_svc.get());

        const std::uint64_t root_l2_pa = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "test_uas");
        proc->set_address_space(uas.get());

        // Map read-write 16KB page
        const std::uint64_t rw_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(rw_page_va, rw_pa, PagePermissions::UserReadWrite), "Map RW");

        // Map read-only 16KB page
        const std::uint64_t ro_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(ro_page_va, ro_pa, PagePermissions::UserRead), "Map RO");

        cap_mgr->grant_mask(proc->pid(), 0xFFFFFFFFu);
    }
};

static void test_process_lifecycle_syscalls() {
    std::cout << "[Test 1] Process lifecycle syscalls (getpid, getppid)...\n";
    SyscallCompletenessFixture f;

    // getpid (syscall 20)
    f.cpu.write_register(16, kSysGetPid);
    f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    std::uint64_t ret = f.cpu.read_register(0);
    TEST_ASSERT(static_cast<std::int32_t>(ret) == f.proc->pid(), "getpid returned correct PID");

    // getppid (syscall 39)
    f.cpu.write_register(16, kSysGetPpid);
    f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    ret = f.cpu.read_register(0);
    TEST_ASSERT(static_cast<std::int32_t>(ret) == f.proc->ppid(), "getppid returned correct PPID");
    std::cout << "  Passed!\n";
}

static void test_invalid_pointer_fault_propagation() {
    std::cout << "[Test 2] Invalid pointer EFAULT propagation...\n";
    SyscallCompletenessFixture f;

    // read (syscall 3) with NULL buffer
    f.cpu.write_register(16, kSysRead);
    f.cpu.write_register(0, 0); // fd 0 (stdin)
    f.cpu.write_register(1, 0); // NULL buffer
    f.cpu.write_register(2, 16); // 16 bytes
    bool ok_read = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok_read || f.cpu.read_register(0) != 0, "read with NULL buffer must fail");

    // write (syscall 4) with NULL buffer
    f.cpu.write_register(16, kSysWrite);
    f.cpu.write_register(0, 1); // stdout
    f.cpu.write_register(1, 0); // NULL buffer
    f.cpu.write_register(2, 16);
    bool ok_write = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok_write || f.cpu.read_register(0) != 0, "write with NULL buffer must fail");
    std::cout << "  Passed!\n";
}

static void test_ro_page_write_protection() {
    std::cout << "[Test 3] Read-only page write protection...\n";
    SyscallCompletenessFixture f;

    // Attempting to read into read-only page must fail
    f.cpu.write_register(16, kSysRead);
    f.cpu.write_register(0, 0);
    f.cpu.write_register(1, f.ro_page_va); // RO buffer
    f.cpu.write_register(2, 64);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Reading into read-only buffer must fail");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.15 Syscall Completeness Tests ===" << std::endl;
    test_process_lifecycle_syscalls();
    test_invalid_pointer_fault_propagation();
    test_ro_page_write_protection();
    std::cout << "=== All Stage 235.15 tests PASSED successfully ===" << std::endl;
    return 0;
}
