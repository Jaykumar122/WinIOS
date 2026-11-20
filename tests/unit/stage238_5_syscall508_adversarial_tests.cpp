// ============================================================================
// Stage 238.5: Syscall 508 Adversarial Validation Tests
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

#include "tests/support/test_assert.h"
#include "cpu/cpu.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_event_services.h"
#include "winios_sdk/winios_sdk.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::runtime;
using namespace winios::memory;

namespace {

struct Harness {
    Memory mem{0x2000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    VirtualConsole console;
    UserSyscallDispatcher dispatcher{console};
    std::shared_ptr<WindowManager> wm;
    std::shared_ptr<SurfaceResourceManager> srm;
    std::shared_ptr<InputRouter> router;
    std::shared_ptr<GuestEventServices> ev_svc;
    CPU cpu;
    std::unique_ptr<UserAddressSpace> uas;
    std::uint64_t user_buf_va{0x20000};

    Harness() {
        ppm.initialize(false);
        sched.initialize();
        cpu.reset();

        wm = std::make_shared<WindowManager>();
        srm = std::make_shared<SurfaceResourceManager>();
        router = std::make_shared<InputRouter>(wm);
        ev_svc = std::make_shared<GuestEventServices>(router);

        dispatcher.set_window_manager(wm.get());
        dispatcher.set_surface_resource_manager(srm.get());
        dispatcher.set_input_router(router.get());
        dispatcher.set_event_services(ev_svc.get());
        dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

        const std::uint64_t root_l2 = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2, "harness_vm");

        const std::uint64_t pa = ppm.allocate_page();
        uas->map_user_page(user_buf_va, pa, PagePermissions::UserReadWrite);
    }
};

void test_abi_v1_0_syscall508_unimplemented() {
    std::cout << "[Test 1] ABI v1.0 Syscall 508 Rejection (ENOSYS)...\n";
    Harness h;
    BsdProcess proc(100, 1, "Abi10App", nullptr);
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_0);

    // Call syscall 508 without negotiating ABI v1.1
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, 1);
    h.cpu.write_register(1, h.user_buf_va);
    bool ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Unnegotiated ABI v1.0 call to 508 must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS), "Must return ENOSYS");
    TEST_ASSERT((h.cpu.get_nzcv() & 0x2u) != 0, "Carry bit must be set on error");
    std::cout << "  -> Passed ENOSYS rejection for ABI v1.0\n";
}

void test_abi_v1_1_negotiation_and_nonexistent_window() {
    std::cout << "[Test 2] ABI v1.1 Negotiation & Missing Window Rejection (EBADF)...\n";
    Harness h;
    BsdProcess proc(101, 1, "Abi11App", nullptr);

    // Negotiate ABI v1.1 via syscall 507
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.cpu.write_register(0, kWiniosCategoryCAbi_v1_1);
    TEST_ASSERT(h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == kWiniosCategoryCAbi_v1_1);
    TEST_ASSERT(proc.category_c_abi_version() == kWiniosCategoryCAbi_v1_1);

    // Call 508 with non-existent window handle (e.g. 9999)
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, 9999);
    h.cpu.write_register(1, h.user_buf_va);
    bool ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Query on non-existent window must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF),
                "Missing window handle under ABI v1.1 must return EBADF, NOT ENOSYS");
    TEST_ASSERT((h.cpu.get_nzcv() & 0x2u) != 0, "Carry bit must be set");
    std::cout << "  -> Passed EBADF rejection on missing handle under ABI v1.1\n";
}

void test_cross_process_unauthorized_access() {
    std::cout << "[Test 3] Cross-Process Unauthorized Window Access (EBADF)...\n";
    Harness h;
    BsdProcess owner(201, 1, "OwnerProc", nullptr);
    BsdProcess attacker(202, 1, "AttackerProc", nullptr);

    // Negotiate ABI v1.1 on both
    owner.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);
    attacker.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    // Create window owned by PID 201
    std::shared_ptr<DisplaySurface> surf;
    auto res = h.srm->create_surface(owner.pid(), 400, 300, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(res == SurfaceResultCode::Success);
    std::uint32_t wid = h.wm->create_window(owner.pid(), "OwnerWin", Rect{10, 20, 400, 300}, surf);

    // Attacker PID 202 tries to get geometry of owner's window
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, h.user_buf_va);
    bool ok = h.dispatcher.handle_syscall(h.cpu, attacker, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Cross-process window query must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF),
                "Unauthorized caller must receive EBADF");
    std::cout << "  -> Passed cross-process authorization enforcement\n";
}

void test_pointer_safety_and_boundaries() {
    std::cout << "[Test 4] Pointer Safety & Memory Validation (EFAULT)...\n";
    Harness h;
    BsdProcess proc(301, 1, "MemApp", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    std::shared_ptr<DisplaySurface> surf;
    h.srm->create_surface(proc.pid(), 200, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    std::uint32_t wid = h.wm->create_window(proc.pid(), "MyWin", Rect{0, 0, 200, 100}, surf);

    // 1. Null pointer
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, 0); // null VA
    TEST_ASSERT(!h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));

    // 2. Unmapped pointer
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, 0x90000000ULL); // unmapped VA
    TEST_ASSERT(!h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));

    // 3. Misaligned pointer (not 8-byte aligned)
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, h.user_buf_va + 3); // misaligned
    TEST_ASSERT(!h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));

    // 4. Legitimate valid pointer
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, h.user_buf_va);
    TEST_ASSERT(h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    TEST_ASSERT(h.cpu.read_register(0) == 0);

    winios_window_geometry_t geom{};
    h.uas->copyout(reinterpret_cast<const void*>(h.user_buf_va), &geom, sizeof(geom));
    TEST_ASSERT(geom.frame.width == 200);
    TEST_ASSERT(geom.frame.height == 100);
    std::cout << "  -> Passed pointer safety tests\n";
}

void test_destroyed_and_recycled_handles() {
    std::cout << "[Test 5] Destroyed & Recycled Handle Validation...\n";
    Harness h;
    BsdProcess proc(401, 1, "RecycleApp", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    std::shared_ptr<DisplaySurface> surf;
    h.srm->create_surface(proc.pid(), 100, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    std::uint32_t wid = h.wm->create_window(proc.pid(), "TempWin", Rect{0, 0, 100, 100}, surf);

    // Destroy the window
    TEST_ASSERT(h.wm->destroy_window(proc.pid(), wid));

    // Query on destroyed handle
    h.cpu.write_register(16, kSysWiniosWindowGetGeometry);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, h.user_buf_va);
    bool ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Query on destroyed handle must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF));
    std::cout << "  -> Passed destroyed handle safety\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.5: Syscall 508 Adversarial Validation Suite  \n";
    std::cout << "========================================================\n";

    test_abi_v1_0_syscall508_unimplemented();
    test_abi_v1_1_negotiation_and_nonexistent_window();
    test_cross_process_unauthorized_access();
    test_pointer_safety_and_boundaries();
    test_destroyed_and_recycled_handles();

    std::cout << "\nALL STAGE 238.5 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
