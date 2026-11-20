#include "tests/support/test_assert.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/syscall_registry.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#include <iostream>
#include <memory>

using namespace winios;
using namespace winios::kernel;

namespace {

void test_syscall_registry_lookup() {
    std::cout << "[Test 1] Syscall Registry Lookup & Descriptors...\n";

    // Known POSIX call: exit (1)
    auto desc1 = lookup_syscall(1);
    TEST_ASSERT(desc1.has_value());
    TEST_ASSERT(desc1->name == "exit");
    TEST_ASSERT(desc1->category == SyscallCategory::PosixCore);
    TEST_ASSERT(desc1->arg_count == 1);

    // Known Category C call: winios_get_abi_version (507)
    auto desc507 = lookup_syscall(507);
    TEST_ASSERT(desc507.has_value());
    TEST_ASSERT(desc507->name == "winios_get_abi_version");
    TEST_ASSERT(desc507->min_abi_version == 0x00010000);

    // Known Category C v1.1 call: winios_window_get_geometry (508)
    auto desc508 = lookup_syscall(508);
    TEST_ASSERT(desc508.has_value());
    TEST_ASSERT(desc508->name == "winios_window_get_geometry");
    TEST_ASSERT(desc508->min_abi_version == 0x00010100);

    // Unknown syscall number (e.g. 9999)
    auto desc_unk = lookup_syscall(9999);
    TEST_ASSERT(!desc_unk.has_value());

    std::cout << "  -> Passed syscall registry lookup\n";
}

void test_dispatcher_unknown_syscall_rejection() {
    std::cout << "[Test 2] Dispatcher Unknown Syscall Rejection (ENOSYS)...\n";
    CPU cpu;
    Memory mem{0x2000000};
    memory::PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    ppm.initialize(false);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    sched.initialize();
    VirtualConsole console;
    UserSyscallDispatcher disp{console};
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    const std::uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "test_vm");
    BsdProcess proc(200, 1, "test_proc", nullptr);
    proc.set_address_space(&uas);

    // Test a range of unimplemented/unknown syscall numbers
    const std::uint64_t unknown_calls[] = { 888, 999, 1234, 5555, 9999 };
    for (std::uint64_t unk : unknown_calls) {
        cpu.reset();
        cpu.write_register(16, unk);
        bool res = disp.handle_syscall(cpu, proc, uas, sched);
        TEST_ASSERT(!res, "Unknown syscall must return false");
        TEST_ASSERT(cpu.read_register(0) == kErrnoENOSYS,
                    ("Unknown syscall " + std::to_string(unk) + " must return ENOSYS").c_str());
    }

    std::cout << "  -> Passed dispatcher unknown syscall rejection\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 239.8: Syscall Registry & Contract Suite         \n";
    std::cout << "========================================================\n";

    test_syscall_registry_lookup();
    test_dispatcher_unknown_syscall_rejection();

    std::cout << "\nALL STAGE 239.8 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
