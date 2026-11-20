#include "tests/support/test_assert.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "graphics/window_manager.h"
#include "cpu/cpu.h"

#include <iostream>
#include <memory>

using namespace winios;
using namespace winios::kernel;

namespace {

struct TestHarness {
    CPU cpu;
    Memory mem{0x2000000};
    memory::PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    VirtualConsole console;
    UserSyscallDispatcher disp{console};
    std::shared_ptr<graphics::WindowManager> wm;
    std::unique_ptr<memory::UserAddressSpace> uas;

    TestHarness() {
        ppm.initialize(false);
        sched.initialize();
        cpu.reset();
        wm = std::make_shared<graphics::WindowManager>();
        disp.set_window_manager(wm.get());
        disp.set_abi_mode(SyscallAbi::DarwinStrict);

        const std::uint64_t root_l2 = ppm.allocate_page();
        uas = std::make_unique<memory::UserAddressSpace>(mem, ppm, root_l2, "test_vm");
    }
};

void test_abi_default_and_query() {
    std::cout << "[Test 1] ABI Default State and Query...\n";
    TestHarness h;
    BsdProcess proc(100, 1, "test_app", nullptr);
    proc.set_address_space(h.uas.get());

    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_0,
                "Process must default to Category C ABI v1.0");

    // Syscall 508 before negotiation must return ENOSYS
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, 1); // dummy wid
    h.cpu.write_register(1, 0x1000);
    bool r508 = h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!r508 && h.cpu.read_register(0) == kErrnoENOSYS,
                "Unnegotiated process must receive ENOSYS for syscall 508");

    // Query/negotiate ABI version (arg0 = 0)
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, 0);
    bool rok = h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(rok, "GetAbiVersion must succeed");
    TEST_ASSERT(h.cpu.read_register(0) == kWiniosCategoryCAbiVersion,
                "Negotiated version must match platform ABI version");
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbiVersion,
                "Process state must update to negotiated ABI version");

    std::cout << "  -> Passed ABI default and query\n";
}

void test_unsupported_version_rejection() {
    std::cout << "[Test 2] Unsupported Version Rejection (EINVAL)...\n";
    TestHarness h;
    BsdProcess proc(101, 1, "bad_ver_app", nullptr);
    proc.set_address_space(h.uas.get());

    // Request malformed/ancient version 0x1
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, 0x00000001);
    bool r_bad = h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!r_bad, "Malformed ABI request must fail");
    TEST_ASSERT(h.cpu.read_register(0) == kErrnoEINVAL, "Must return EINVAL for bad version");
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_0,
                "Process ABI must remain unchanged on rejection");

    std::cout << "  -> Passed unsupported version rejection\n";
}

void test_downgrade_prevention() {
    std::cout << "[Test 3] Downgrade Prevention (EPERM)...\n";
    TestHarness h;
    BsdProcess proc(102, 1, "downgrade_app", nullptr);
    proc.set_address_space(h.uas.get());

    // First negotiate to ABI v1.1
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, kWiniosCategoryCAbi_v1_1);
    TEST_ASSERT(h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_1);

    // Attempt to downgrade to v1.0
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, kWiniosCategoryCAbi_v1_0);
    bool r_down = h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!r_down, "Downgrade attempt must fail");
    TEST_ASSERT(h.cpu.read_register(0) == kErrnoEPERM, "Must return EPERM on downgrade");
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_1,
                "Process ABI must not be downgraded");

    // Repeated negotiation to same or higher version must succeed
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, kWiniosCategoryCAbi_v1_1);
    TEST_ASSERT(h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == kWiniosCategoryCAbi_v1_1);

    std::cout << "  -> Passed downgrade prevention\n";
}

void test_negotiated_syscall508_behavior() {
    std::cout << "[Test 4] Negotiated Syscall 508 Behavior (EBADF)...\n";
    TestHarness h;
    BsdProcess proc(103, 1, "geom_app", nullptr);
    proc.set_address_space(h.uas.get());

    // Negotiate to ABI v1.1
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, 0);
    TEST_ASSERT(h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched));

    // Calling 508 on non-existent window must return EBADF, NOT ENOSYS
    h.cpu.reset();
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, 9999); // Non-existent wid
    h.cpu.write_register(1, 0x1000);
    bool r = h.disp.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!r, "Call on non-existent window must fail");
    TEST_ASSERT(h.cpu.read_register(0) == kErrnoEBADF,
                "Negotiated call on non-existent window must return EBADF");

    std::cout << "  -> Passed negotiated syscall 508 behavior\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 239.7: ABI Negotiation & Conformance Suite       \n";
    std::cout << "========================================================\n";

    test_abi_default_and_query();
    test_unsupported_version_rejection();
    test_downgrade_prevention();
    test_negotiated_syscall508_behavior();

    std::cout << "\nALL STAGE 239.7 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
