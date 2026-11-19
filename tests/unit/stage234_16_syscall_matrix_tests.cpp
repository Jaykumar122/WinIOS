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

#if defined(_WIN32)
#include <crtdbg.h>
#endif

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

struct SyscallMatrixFixture {
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
    const std::uint64_t unmapped_page_va{0x80000ULL};
    const std::uint64_t unterminated_str_va{0x20800ULL};

    SyscallMatrixFixture()
        : mem(0x1000000ULL), // 16 MB
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
        proc = proc_mgr.create_process("matrix_test_proc", 1);
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
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "matrix_uas");
        proc->set_address_space(uas.get());

        // Map read-write 16KB page at rw_page_va
        const std::uint64_t rw_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(rw_page_va, rw_pa, PagePermissions::UserReadWrite), "RW page mapped");

        // Map read-only 16KB page at ro_page_va
        const std::uint64_t ro_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(ro_page_va, ro_pa, PagePermissions::UserRead), "RO page mapped");

        // Fill unterminated_str_va with 'X's (no null terminator)
        for (std::size_t i = 0; i < 512; ++i) {
            mem.write_u8(static_cast<std::size_t>(rw_pa + 0x800 + i), 'X');
        }

        // Grant full capabilities to test PID
        cap_mgr->grant_mask(proc->pid(), 0xFFFFFFFFu);

        // Register a test task
        settings::TaskInfo tinfo{};
        tinfo.pid = 101;
        tinfo.name = "matrix_task";
        tinfo.state = settings::TaskState::Running;
        task_mgr->register_task(tinfo);
    }
};

struct SyscallPointerDescriptor {
    std::uint64_t sys_num;
    const char* name;
    int ptr_arg_reg; // 0, 1, 2, or 3
    bool is_write;   // Requires guest writable memory
    bool is_string;  // C-string argument
};

static const SyscallPointerDescriptor kPointerSyscalls[] = {
    { kSysRead,                  "read",                 1, true,  false },
    { kSysWrite,                 "write",                1, false, false },
    { kSysOpen,                  "open",                 0, false, true  },
    { kSysStat64,                "stat64",               0, false, true  },
    { kSysFstat64,               "fstat64",              1, true,  false },
    { kSysWiniosSetWindowTitle,  "set_window_title",     1, false, true  },
    { kSysWiniosStorageRead,     "storage_read",         0, false, true  },
    { kSysWiniosStorageWrite,    "storage_write",        0, false, true  },
    { kSysWiniosIpcSend,         "ipc_send",             1, false, false },
    { kSysWiniosIpcRecv,         "ipc_recv",             1, true,  false },
    { kSysWiniosClipboardGet,    "clipboard_get",        0, true,  false },
    { kSysWiniosClipboardSet,    "clipboard_set",        0, false, false },
    { kSysWiniosSettingsGet,     "settings_get",         0, true,  false },
    { kSysWiniosTaskEnumerate,   "task_enumerate",       0, true,  false },
    { kSysWiniosAccessibilityGet,"accessibility_get",    0, true,  false },
    { kSysWiniosDiagEventQuery,  "diag_event_query",     0, false, false },
    { kSysWiniosAppInfo,         "app_info",             0, false, true  },
    { kSysWiniosAppList,         "app_list",             0, true,  false },
    { kSysWiniosDocStreamRead,   "doc_stream_read",      1, true,  false },
    { kSysWiniosDocStreamWrite,  "doc_stream_write",     1, false, false }
};

// Table-driven verification function
static void verify_syscall_matrix() {
    std::cout << "[Stage 234.16] Executing Table-Driven Pointer Syscall Matrix Tests ("
              << (sizeof(kPointerSyscalls) / sizeof(kPointerSyscalls[0])) << " Syscalls)...\n";

    SyscallMatrixFixture f;
    const std::uint64_t overflow_addr = 0xFFFFFFFFFFFFFFF0ULL;

    for (const auto& desc : kPointerSyscalls) {
        std::cout << "  -> Testing Syscall " << desc.sys_num << " (" << desc.name << ")...\n";

        auto setup_default_args = [&](std::uint64_t ptr_val) {
            f.cpu.write_register(16, desc.sys_num);
            for (int r = 0; r < 6; ++r) {
                f.cpu.write_register(r, (r == desc.ptr_arg_reg) ? ptr_val : f.rw_page_va);
            }
            // For length registers, provide small non-zero size
            if (desc.ptr_arg_reg != 1) f.cpu.write_register(1, 32);
            if (desc.ptr_arg_reg != 2) f.cpu.write_register(2, 32);
        };

        // Condition 1: Null Pointer (0x0)
        {
            setup_default_args(0x0ULL);
            bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
            TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Null pointer rejected");
        }

        // Condition 2: Integer Overflow Pointer
        {
            setup_default_args(overflow_addr);
            bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
            TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Integer overflow pointer rejected");
        }

        // Condition 3: Unmapped Virtual Page
        {
            setup_default_args(f.unmapped_page_va);
            bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
            TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Unmapped virtual address rejected");
        }

        // Condition 4: Read-Only Violation (for writing syscalls)
        if (desc.is_write) {
            setup_default_args(f.ro_page_va);
            bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
            TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Read-only destination pointer rejected");
        }

        // Condition 5: Unterminated String (for string syscalls)
        if (desc.is_string) {
            setup_default_args(f.unterminated_str_va);
            bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
            TEST_ASSERT(!ok || f.cpu.read_register(0) != 0, "Unterminated C-string rejected");
        }
    }

    std::cout << "  -> All table-driven syscall matrix tests passed successfully.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.16: Syscall Boundary Matrix Test Suite               \n";
    std::cout << "=================================================================\n";

    verify_syscall_matrix();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL SYSCALL BOUNDARY MATRIX TESTS PASSED!                      \n";
    std::cout << "=================================================================\n";
    return 0;
}
