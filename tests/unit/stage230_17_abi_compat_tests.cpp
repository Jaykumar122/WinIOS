#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "vfs/vfs.h"
#include "packaging/app_registry.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "winios_sdk/winios_sdk.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_structure_sizes_and_abi_constants() {
    std::cout << "[Test 1] ABI structure sizes and SDK version constants...\n";
    // SDK v2.4 constants
    TEST_ASSERT(WINIOS_SDK_VERSION_2_4 == 0x00020400);
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION_2_4 == 0x00020400);
    TEST_ASSERT(SYS_WINIOS_APP_INFO == 592);
    TEST_ASSERT(SYS_WINIOS_APP_LIST == 593);

    // Structure sizes
    TEST_ASSERT(sizeof(winios_app_info_t) == 128);
    TEST_ASSERT(sizeof(winios_app_list_req_t) == 24);
    TEST_ASSERT(sizeof(winios_settings_snapshot_t) == 48);
    TEST_ASSERT(sizeof(winios_diag_event_record_t) == 48);

    std::cout << "  -> ABI sizes and constants verified.\n";
}

int main() {
    std::cout << "=== Running Stage 230 ABI & Syscall Compatibility Matrix Tests ===\n";
    test_structure_sizes_and_abi_constants();

    CPU cpu;
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto cm = std::make_shared<security::CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("abi_test_app", 1);
    TEST_ASSERT(proc != nullptr);

    // Grant all capabilities for conformance testing
    cm->grant_mask(proc->pid(), 0xFFFFFFFFu);

    auto app_reg = std::make_shared<packaging::ApplicationRegistry>();
    dispatcher.set_app_registry(app_reg.get());

    auto diag_svc = std::make_shared<services::DiagnosticsService>();
    dispatcher.set_diagnostics_service(diag_svc.get());

    const uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");
    proc->set_address_space(&uas);

    // Map 2 user pages: 0x30000 and 0x34000
    const uint64_t page0_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, page0_pa, PagePermissions::UserReadWrite);

    const uint64_t page1_pa = ppm.allocate_page();
    uas.map_user_page(0x34000ULL, page1_pa, PagePermissions::UserReadWrite);

    // Populate test apps in registry
    {
        packaging::AppRegistration r1{};
        r1.bundle_id = "com.winios.app1";
        r1.name = "App1";
        r1.display_name = "First Application";
        r1.version = "1.0.0";
        r1.exec_path = "bin/app1";
        r1.min_sdk_version = 0x00020300; // SDK v2.3
        r1.package_version = 2;
        r1.state = packaging::InstallationState::Installed;
        r1.storage_quota_bytes = 1048576;
        bool reg1 = app_reg->register_app(r1);
        TEST_ASSERT(reg1);

        packaging::AppRegistration r2{};
        r2.bundle_id = "com.winios.app2";
        r2.name = "App2";
        r2.display_name = "Second Application";
        r2.version = "2.0.0";
        r2.exec_path = "bin/app2";
        r2.min_sdk_version = 0x00020400; // SDK v2.4
        r2.package_version = 3;
        r2.state = packaging::InstallationState::Installed;
        r2.storage_quota_bytes = 2097152;
        bool reg2 = app_reg->register_app(r2);
        TEST_ASSERT(reg2);
    }

    // 2. Syscall 592 (kSysWiniosAppInfo) tests
    {
        std::cout << "[Test 2] Testing Syscall 592 (kSysWiniosAppInfo)...\n";
        // Write bundle ID string "com.winios.app2" at 0x30000
        const char* bid = "com.winios.app2";
        std::size_t bid_len = std::strlen(bid) + 1;
        uas.copyin(0x30000ULL, bid, bid_len);

        // Success case: arg0 = 0x30000, arg1 = 0x30100 (output struct)
        cpu.write_register(16, kSysWiniosAppInfo);
        cpu.write_register(0, 0x30000ULL);
        cpu.write_register(1, 0x30100ULL);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);

        winios_app_info_t info{};
        uas.copyout(reinterpret_cast<const void*>(0x30100ULL), &info, sizeof(info));
        TEST_ASSERT(std::string(info.bundle_id) == "com.winios.app2");
        TEST_ASSERT(std::string(info.display_name) == "Second Application");
        TEST_ASSERT(std::string(info.version) == "2.0.0");
        TEST_ASSERT(info.min_sdk_version == 0x00020400);
        TEST_ASSERT(info.state == static_cast<uint32_t>(packaging::InstallationState::Installed));
        TEST_ASSERT(info.storage_quota_bytes == 2097152);

        // Error case: Nonexistent app -> ENOENT
        const char* bad_bid = "com.winios.nonexistent";
        uas.copyin(0x30200ULL, bad_bid, std::strlen(bad_bid) + 1);
        cpu.write_register(16, kSysWiniosAppInfo);
        cpu.write_register(0, 0x30200ULL);
        cpu.write_register(1, 0x30100ULL);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoENOENT);

        // Error case: Null bundle id pointer -> EFAULT
        cpu.write_register(16, kSysWiniosAppInfo);
        cpu.write_register(0, 0);
        cpu.write_register(1, 0x30100ULL);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);
    }

    // 3. Syscall 593 (kSysWiniosAppList) tests
    {
        std::cout << "[Test 3] Testing Syscall 593 (kSysWiniosAppList)...\n";
        // Setup request at 0x30300
        winios_app_list_req_t req{};
        req.max_entries = 8;
        req.active_only = 0;
        req.buffer_va = 0x34000ULL; // Output buffer in page1
        uas.copyin(0x30300ULL, &req, sizeof(req));

        cpu.write_register(16, kSysWiniosAppList);
        cpu.write_register(0, 0x30300ULL);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 2);

        winios_app_list_req_t out_req{};
        uas.copyout(reinterpret_cast<const void*>(0x30300ULL), &out_req, sizeof(out_req));
        TEST_ASSERT(out_req.returned_entries == 2);

        winios_app_info_t entries[2]{};
        uas.copyout(reinterpret_cast<const void*>(0x34000ULL), entries, sizeof(entries));
        TEST_ASSERT(std::string(entries[0].bundle_id) == "com.winios.app1");
        TEST_ASSERT(entries[0].min_sdk_version == 0x00020300);
        TEST_ASSERT(std::string(entries[1].bundle_id) == "com.winios.app2");
        TEST_ASSERT(entries[1].min_sdk_version == 0x00020400);

        // Error case: Null request pointer -> EFAULT
        cpu.write_register(16, kSysWiniosAppList);
        cpu.write_register(0, 0);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);
    }

    // 4. Out of bounds Category C Syscall (594)
    {
        std::cout << "[Test 4] Testing beyond-range Syscall 594...\n";
        cpu.write_register(16, 594);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoENOSYS);
    }

    std::cout << "=== All Stage 230.17 ABI & Syscall Compatibility Matrix Tests PASSED ===\n";
    return 0;
}
