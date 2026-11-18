#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

#include "winios_sdk/winios_sdk.h"
#include "loader/macho_user_loader.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "runtime/guest_allocator.h"
#include "runtime/guest_storage_api.h"
#include "runtime/guest_ipc.h"
#include "runtime/app_preferences.h"
#include "runtime/execution_boundary.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::storage;

void test_wx_permission_enforcement() {
    std::cout << "[Adversarial 1] Testing W^X permission enforcement...\n";

    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "wx_security_vm");

    uint64_t code_va = 0x100000000ULL;
    uint64_t code_pa = ppm.allocate_page();
    uas.map_user_page(code_va, code_pa, memory::PagePermissions::UserExecute);

    uint64_t data_va = 0x100010000ULL;
    uint64_t data_pa = ppm.allocate_page();
    uas.map_user_page(data_va, data_pa, memory::PagePermissions::UserReadWrite);

    // Code page is mapped and readable, but NOT writable
    TEST_ASSERT(uas.validate_user_buffer(code_va, 0x4000, false));
    TEST_ASSERT(!uas.validate_user_buffer(code_va, 0x4000, true));

    // Data page is mapped and writable
    TEST_ASSERT(uas.validate_user_buffer(data_va, 0x4000, true));

    // Unmapped page is neither readable nor writable
    uint64_t unmapped_va = 0x100020000ULL;
    TEST_ASSERT(!uas.validate_user_buffer(unmapped_va, 0x4000, false));
    TEST_ASSERT(!uas.validate_user_buffer(unmapped_va, 0x4000, true));

    std::cout << "[PASS] test_wx_permission_enforcement\n";
}

void test_macho_loader_adversarial_rejections() {
    std::cout << "[Adversarial 2] Testing Mach-O Loader malformed binary rejections...\n";

    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "macho_rejection_vm");

    // Attack 1: Empty payload
    std::vector<uint8_t> empty_bin;
    auto res1 = MachoUserLoader::load_image(empty_bin, uas, ppm, mem);
    TEST_ASSERT(!res1.valid);

    // Attack 2: Bad magic
    std::vector<uint8_t> bad_magic(1024, 0xAA);
    auto res2 = MachoUserLoader::load_image(bad_magic, uas, ppm, mem);
    TEST_ASSERT(!res2.valid);

    // Attack 3: Truncated header
    std::vector<uint8_t> trunc_header = { 0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01 };
    auto res3 = MachoUserLoader::load_image(trunc_header, uas, ppm, mem);
    TEST_ASSERT(!res3.valid);

    // Attack 4: Non-16KB aligned segment
    auto good_bin = MachoUserLoader::get_embedded_test_workload();
    TEST_ASSERT(!good_bin.empty());
    std::vector<uint8_t> corrupted_align = good_bin;
    bool found_segment = false;
    for (size_t i = 32; i + 72 < corrupted_align.size(); i += 8) {
        uint32_t cmd = *reinterpret_cast<const uint32_t*>(&corrupted_align[i]);
        if (cmd == 0x19) { // LC_SEGMENT_64
            const char* segname = reinterpret_cast<const char*>(&corrupted_align[i + 8]);
            if (std::strcmp(segname, "__TEXT") == 0) {
                uint64_t* vmaddr_ptr = reinterpret_cast<uint64_t*>(&corrupted_align[i + 24]);
                *vmaddr_ptr = 0x100002000ULL; // Not aligned to 16KB (0x4000)
                found_segment = true;
                break;
            }
        }
    }
    if (found_segment) {
        auto res4 = MachoUserLoader::load_image(corrupted_align, uas, ppm, mem);
        TEST_ASSERT(!res4.valid); // Must be rejected due to unaligned segment!
    }

    std::cout << "[PASS] test_macho_loader_adversarial_rejections\n";
}

void test_syscall_isolation_adversarial() {
    std::cout << "[Adversarial 3] Testing Syscall boundary, path traversal & pointer defenses...\n";

    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "syscall_adversarial_vm");

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("malicious_app", 0);
    TEST_ASSERT(proc != nullptr);

    // Map single user page at 0x100000000
    uint64_t user_va = 0x100000000ULL;
    uint64_t user_pa = ppm.allocate_page();
    uas.map_user_page(user_va, user_pa, memory::PagePermissions::UserReadWrite);

    // Setup Allocator with small quota: 64 KB
    GuestAllocator allocator(0x200000000ULL, 64 * 1024, 16 * 1024, proc->pid());
    dispatcher.set_allocator(&allocator);

    // Setup Storage
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto iso_storage = std::make_shared<IsolatedStorage>(vfs, "com.winios.adversarial");
    GuestStorageApi storage_api(iso_storage, proc->pid());
    dispatcher.set_storage_api(&storage_api);

    CPU cpu;
    cpu.reset();

    // Attack 1: Path Traversal with '..'
    const char* bad_traversal = "../../system/kernel.sys";
    uas.copyin(user_va + 256, bad_traversal, std::strlen(bad_traversal) + 1);

    winios_storage_req_t req{};
    req.path_ptr = user_va + 256;
    req.path_len = static_cast<uint32_t>(std::strlen(bad_traversal));
    uas.copyin(user_va, &req, sizeof(req));

    cpu.write_register(16, kSysWiniosStorageWrite);
    cpu.write_register(0, user_va);
    cpu.write_register(1, user_va + 512);
    cpu.write_register(2, 32);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoEPERM));

    // Attack 2: Path Traversal with backslash
    const char* bad_bs = "..\\..\\windows\\system32\\cmd.exe";
    uas.copyin(user_va + 256, bad_bs, std::strlen(bad_bs) + 1);
    req.path_len = static_cast<uint32_t>(std::strlen(bad_bs));
    uas.copyin(user_va, &req, sizeof(req));

    cpu.write_register(16, kSysWiniosStorageRead);
    cpu.write_register(0, user_va);
    cpu.write_register(1, user_va + 512);
    cpu.write_register(2, 32);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoEPERM));

    // Attack 3: Invalid user pointer (kernel VA / unmapped VA)
    uint64_t invalid_kernel_va = 0xFFFFFF8000000000ULL;
    cpu.write_register(16, kSysWiniosGetRuntimeInfo);
    cpu.write_register(0, invalid_kernel_va); // Kernel VA passed from EL0
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoEFAULT));

    // Attack 4: Heap quota exhaustion
    cpu.write_register(16, kSysWiniosAllocHeap);
    cpu.write_register(0, 128 * 1024); // Exceeds 64 KB total heap capacity
    cpu.write_register(1, 16);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoENOMEM));

    // Attack 5: Unknown/Invalid Syscall ID
    cpu.write_register(16, 99999);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoENOSYS));

    std::cout << "[PASS] test_syscall_isolation_adversarial\n";
}

void test_execution_boundary_tamper_resistance() {
    std::cout << "[Adversarial 4] Testing ExecutionBoundaryEnforcer tamper resistance...\n";

    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    // 1. Attempt instruction retirement from Host Simulation
    {
        ExecutionOriginScope scope(ExecutionOrigin::kHostSimulation);
        bool recorded = enforcer.RecordGuestInstructionRetirement(100);
        TEST_ASSERT(!recorded);
    }
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 1);

    // 2. Attempt instruction retirement from Kernel Syscall Service
    {
        ExecutionOriginScope scope(ExecutionOrigin::kKernelSyscallService);
        bool recorded = enforcer.RecordGuestInstructionRetirement(50);
        TEST_ASSERT(!recorded);
    }
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 2);

    // 3. Attempt instruction retirement from Host Presentation
    {
        ExecutionOriginScope scope(ExecutionOrigin::kHostPresentation);
        bool recorded = enforcer.RecordGuestInstructionRetirement(40);
        TEST_ASSERT(!recorded);
    }
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 3);

    // 4. Attempt instruction retirement from Unknown origin
    {
        ExecutionOriginScope scope(ExecutionOrigin::kUnknown);
        bool recorded = enforcer.RecordGuestInstructionRetirement(25);
        TEST_ASSERT(!recorded);
    }
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 4);

    // 5. Legitimate instruction retirement from GuestCpuRetired
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        bool recorded = enforcer.RecordGuestInstructionRetirement(10);
        TEST_ASSERT(recorded);
    }
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 10);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 4);

    std::cout << "[PASS] test_execution_boundary_tamper_resistance\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.23 Adversarial Security & Robustness Tests ===\n";
    test_wx_permission_enforcement();
    test_macho_loader_adversarial_rejections();
    test_syscall_isolation_adversarial();
    test_execution_boundary_tamper_resistance();
    std::cout << "[ALL PASS] Stage 218.23 Adversarial Security tests succeeded.\n";
    return 0;
}
