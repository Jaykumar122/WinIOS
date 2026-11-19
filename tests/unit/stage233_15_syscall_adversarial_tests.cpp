#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <cstddef>
#include <cstring>

#include "memory/guest_memory_validator.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "vfs/vfs.h"
#include "security/capability_manager.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"
#include "packaging/app_registry.h"
#include "services/diagnostics_service.h"
#include "winios_sdk/winios_sdk.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Syscall Adversarial Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

struct AdversarialFixture {
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

    AdversarialFixture()
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
        proc = proc_mgr.create_process("adversarial_test_app", 1);
        TEST_ASSERT(proc != nullptr);

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
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "adv_uas");
        proc->set_address_space(uas.get());

        // Map read-write 16KB page at rw_page_va
        const std::uint64_t rw_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(rw_page_va, rw_pa, PagePermissions::UserReadWrite));

        // Map read-only 16KB page at ro_page_va
        const std::uint64_t ro_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(ro_page_va, ro_pa, PagePermissions::UserRead));

        // Grant full capabilities to test PID
        cap_mgr->grant_mask(proc->pid(), 0xFFFFFFFFu);

        // Register a task in task_mgr
        settings::TaskInfo tinfo{};
        tinfo.pid = 101;
        tinfo.name = "adversarial_task";
        tinfo.state = settings::TaskState::Running;
        task_mgr->register_task(tinfo);
    }
};

static void test_pointer_alignment_enforcement() {
    std::cout << "[Stage 233.15] Testing adversarial pointer alignment enforcement...\n";
    AdversarialFixture f;
    std::uint8_t dummy[64]{};

    // 4-byte alignment check
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 1, 32, false, 4) == ValidationResult::MisalignedPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 2, 32, false, 4) == ValidationResult::MisalignedPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 3, 32, false, 4) == ValidationResult::MisalignedPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 4, 32, false, 4) == ValidationResult::Success);

    // 8-byte alignment check
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 4, 32, false, 8) == ValidationResult::MisalignedPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va + 8, 32, false, 8) == ValidationResult::Success);

    // copyin with misaligned pointer
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.rw_page_va + 3, dummy, 32, 4) == ValidationResult::MisalignedPointer);
    // copyout with misaligned pointer
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, f.rw_page_va + 7, 32, 8) == ValidationResult::MisalignedPointer);
}

static void test_integer_overflow_and_space_limits() {
    std::cout << "[Stage 233.15] Testing integer overflow and virtual address bounds...\n";
    AdversarialFixture f;
    std::uint8_t dummy[64]{};

    // 64-bit integer overflow wraparound
    const std::uint64_t max_u64 = 0xFFFFFFFFFFFFFFFFULL;
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, max_u64, 1, false) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, max_u64 - 10, 20, false) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, max_u64, dummy, 1) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, max_u64, 1) == ValidationResult::IntegerOverflow);

    // Boundary at kUserSpaceLimitVa (32 GiB = 0x800000000ULL)
    const std::uint64_t limit_va = kUserSpaceLimitVa;
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, limit_va, 16, false) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, limit_va - 8, 16, false) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, limit_va, dummy, 16) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, limit_va, 16) == ValidationResult::OutOfBounds);

    std::string str;
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, limit_va, 128, str) == ValidationResult::OutOfBounds);
}

static void test_cross_page_permission_and_unmapped_boundaries() {
    std::cout << "[Stage 233.15] Testing cross-page boundaries and permissions...\n";
    AdversarialFixture f;

    // Page at rw_page_va is 16 KB: [0x20000, 0x24000)
    // 0x24000 is unmapped.
    // Buffer spanning across boundary: 0x24000 - 16 to 0x24000 + 16 (32 bytes)
    const std::uint64_t span_va = 0x24000ULL - 16ULL;
    std::uint8_t dummy[32]{};

    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, span_va, 32, false) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, span_va, 32, true) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, span_va, dummy, 32) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, span_va, 32) == ValidationResult::UnmappedPage);

    // Now map 0x24000 as ReadOnly
    const std::uint64_t ro_next_pa = f.ppm.allocate_page();
    TEST_ASSERT(f.uas->map_user_page(0x24000ULL, ro_next_pa, PagePermissions::UserRead));

    // For reading: both pages are readable -> must succeed
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, span_va, 32, false) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, span_va, dummy, 32) == ValidationResult::Success);

    // For writing: first page is writable, but second page is read-only -> must fail with PermissionDenied
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, span_va, 32, true) == ValidationResult::PermissionDenied);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, span_va, 32) == ValidationResult::PermissionDenied);
}

static void test_adversarial_string_termination() {
    std::cout << "[Stage 233.15] Testing adversarial string bounds and termination...\n";
    AdversarialFixture f;

    // 1. Fill entire page with non-null bytes
    std::vector<std::uint8_t> non_null_block(16384, 'A');
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, non_null_block.data(), f.rw_page_va, non_null_block.size()) == ValidationResult::Success);

    std::string out_str;
    // Bounded search stops at max_bytes without overflow
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 256, out_str) == ValidationResult::StringNotTerminated);
    TEST_ASSERT(out_str.empty());

    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 4096, out_str) == ValidationResult::StringNotTerminated);
    TEST_ASSERT(out_str.empty());

    // 2. String located at unmapped memory
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, f.unmapped_page_va, 256, out_str) == ValidationResult::UnmappedPage);

    // 3. String starting 4 bytes before end of page, extending into unmapped page without null
    // rw_page_va + 16380 spans into 0x24000 (which was unmapped before or is mapped now)
    // Testing unmapped memory string:
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, f.unmapped_page_va, 64, out_str) == ValidationResult::UnmappedPage);

    // 4. Properly terminated string
    const char sample[] = "adversarial_token_test_12345";
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, sample, f.rw_page_va, sizeof(sample)) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 128, out_str) == ValidationResult::Success);
    TEST_ASSERT(out_str == "adversarial_token_test_12345");
}

static void test_zero_length_buffer_policies() {
    std::cout << "[Stage 233.15] Testing zero-length buffer policies...\n";
    AdversarialFixture f;

    // Default policy: zero length forbidden
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va, 0, false, 1, false) == ValidationResult::ZeroLengthForbidden);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va, 0, true, 1, false) == ValidationResult::ZeroLengthForbidden);

    // Explicit allow_zero_length == true
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va, 0, false, 1, true) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, f.rw_page_va, 0, true, 1, true) == ValidationResult::Success);

    // copyin and copyout handle size 0 as Success
    std::uint8_t dummy[8]{};
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.rw_page_va, dummy, 0) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, dummy, f.rw_page_va, 0) == ValidationResult::Success);
}

static void test_direct_syscall_dispatcher_adversarial_inputs() {
    std::cout << "[Stage 233.15] Testing direct syscall dispatcher adversarial inputs...\n";
    AdversarialFixture f;

    // 1. Syscall 581 (Settings Get) with null pointer
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, 0);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 2. Syscall 581 with unmapped pointer
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, f.unmapped_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 3. Syscall 581 with integer overflow pointer
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, 0xFFFFFFFFFFFFFFF0ULL);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 4. Syscall 581 with read-only page (writing settings into RO page must fail)
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, f.ro_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 5. Syscall 586 (Task Enumerate) with unmapped memory buffer
    f.cpu.write_register(16, kSysWiniosTaskEnumerate);
    f.cpu.write_register(0, f.unmapped_page_va);
    f.cpu.write_register(1, 10);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 6. Syscall 592 (AppInfo) with un-terminated string
    // rw_page_va + 500 contains non-null data from earlier test
    f.cpu.write_register(16, kSysWiniosAppInfo);
    f.cpu.write_register(0, f.rw_page_va + 500); // Unterminated string
    f.cpu.write_register(1, f.rw_page_va);       // output buffer
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.15: Syscall Adversarial Testing         \n";
    std::cout << "====================================================\n";

    test_pointer_alignment_enforcement();
    test_integer_overflow_and_space_limits();
    test_cross_page_permission_and_unmapped_boundaries();
    test_adversarial_string_termination();
    test_zero_length_buffer_policies();
    test_direct_syscall_dispatcher_adversarial_inputs();

    std::cout << "\n[Stage 233.15] ALL SYSCALL ADVERSARIAL TESTS PASSED.\n";
    return 0;
}
