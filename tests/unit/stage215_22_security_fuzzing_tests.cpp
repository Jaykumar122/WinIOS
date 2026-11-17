#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/guest_memory_validator.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "runtime/guest_ui_runtime.h"
#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::packaging;
using namespace winios::runtime;
using namespace winios::security;
using namespace winios::storage;
using namespace winios::vfs;

namespace {

int g_vectors_passed = 0;

void record_vector(int num, const std::string& desc, bool success) {
    if (success) {
        std::cout << "  [PASS] Vector " << num << ": " << desc << "\n";
        g_vectors_passed++;
    } else {
        std::cerr << "  [FAIL] Vector " << num << ": " << desc << "\n";
        assert(false);
    }
}

} // anonymous namespace

void run_34_vector_security_suite() {
    std::cout << "Running WinIOS 34-Vector Adversarial Security & Sandboxing Suite...\n";

    // Setup base environment
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "sec_uas");

    const std::uint64_t p_rw = ppm.allocate_page();
    const std::uint64_t p_ro = ppm.allocate_page();
    uas.map_user_page(0x10000, p_rw, PagePermissions::UserReadWrite);
    uas.map_user_page(0x14000, p_ro, PagePermissions::UserRead);

    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto caps = std::make_shared<CapabilityManager>();
    auto clip = std::make_shared<SystemClipboard>(caps);
    auto registry = std::make_shared<ApplicationRegistry>();
    auto installer = std::make_shared<PackageInstaller>(vfs, registry);
    auto win_mgr = std::make_shared<WindowManager>();
    auto surf_mgr = std::make_shared<SurfaceResourceManager>();
    auto input_router = std::make_unique<input::InputRouter>(win_mgr);

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    disp.set_window_manager(win_mgr.get());
    disp.set_surface_resource_manager(surf_mgr.get());
    disp.set_input_router(input_router.get());

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    BsdProcess procA(2, 1, "procA");
    BsdProcess procB(3, 1, "procB");
    CPU cpu;
    cpu.reset();

    // =========================================================================
    // Category 1: Memory Validation Framework (Vectors 1 - 6)
    // =========================================================================
    // Vector 1: Null pointer rejection
    record_vector(1, "Null pointer buffer validation rejected",
                  GuestMemoryValidator::validate_buffer(uas, 0, 64, false) == ValidationResult::NullPointer);

    // Vector 2: Integer overflow rejection
    record_vector(2, "Integer overflow (va + size wrap) rejected",
                  GuestMemoryValidator::validate_buffer(uas, 0xFFFFFFFFFFFFFFF0ULL, 32, false) == ValidationResult::IntegerOverflow);

    // Vector 3: Userspace boundary exceed (> 0x80000000)
    record_vector(3, "Buffer exceeding 2 GiB userspace limit rejected",
                  GuestMemoryValidator::validate_buffer(uas, 0x80000000ULL, 16, false) == ValidationResult::OutOfBounds);

    // Vector 4: Misaligned 4-byte pointer rejection
    record_vector(4, "Misaligned 4-byte pointer rejected",
                  GuestMemoryValidator::validate_buffer(uas, 0x10001, 16, false, 4) == ValidationResult::MisalignedPointer);

    // Vector 5: Misaligned 8-byte pointer rejection
    record_vector(5, "Misaligned 8-byte pointer rejected",
                  GuestMemoryValidator::validate_buffer(uas, 0x10004, 16, false, 8) == ValidationResult::MisalignedPointer);

    // Vector 6: Write to read-only page rejected
    record_vector(6, "Write to read-only memory page rejected with PermissionDenied",
                  GuestMemoryValidator::validate_buffer(uas, 0x14000, 16, true) == ValidationResult::PermissionDenied);

    // =========================================================================
    // Category 2: Isolated Storage & Path Traversal (Vectors 7 - 12)
    // =========================================================================
    IsolatedStorage storeA(vfs, "com.winios.appA");
    IsolatedStorage storeB(vfs, "com.winios.appB", 512 /* 512 bytes */);

    // Vector 7: Relative traversal '../'
    record_vector(7, "Relative path traversal '../' rejected", !storeA.is_path_safe("../secret.txt"));

    // Vector 8: Windows path traversal '..\\'
    record_vector(8, "Windows path traversal '..\\' rejected", !storeA.is_path_safe("..\\secret.txt"));

    // Vector 9: Absolute root path '/'
    record_vector(9, "Absolute root path '/etc/shadow' rejected", !storeA.is_path_safe("/etc/shadow"));

    // Vector 10: Nested traversal 'dir/../../escape'
    record_vector(10, "Nested path traversal 'sub/../../escape' rejected", !storeA.is_path_safe("sub/../../escape"));

    // Vector 11: Cross-application private data isolation
    assert(storeA.atomic_write("a.dat", "DATA_A", 6) == StorageResult::Success);
    std::vector<std::uint8_t> read_b;
    record_vector(11, "Cross-application private storage access returns FileNotFound",
                  storeB.read("a.dat", read_b) == StorageResult::FileNotFound);

    // Vector 12: Quota enforcement
    std::vector<std::uint8_t> over_quota(1024, 0xAA);
    record_vector(12, "Writing data exceeding storage quota returns QuotaExceeded",
                  storeB.atomic_write("overflow.bin", over_quota.data(), over_quota.size()) == StorageResult::QuotaExceeded);

    // =========================================================================
    // Category 3: Capabilities & System Clipboard (Vectors 13 - 18)
    // =========================================================================
    // Vector 13: Unprivileged PID has no capabilities
    record_vector(13, "Unprivileged process defaults to zero capabilities", !caps->check(99, Capability::ClipboardRead));

    // Vector 14: Unauthorized clipboard write rejected
    record_vector(14, "Unauthorized clipboard write rejected", !clip->set_text(99, "unauth"));

    // Vector 15: Authorized clipboard write succeeds
    caps->grant(20, Capability::ClipboardWrite);
    record_vector(15, "Authorized clipboard write succeeds", clip->set_text(20, "authorized_data"));

    // Vector 16: Unauthorized clipboard read rejected
    std::string out_clip;
    record_vector(16, "Unauthorized clipboard read rejected", !clip->get_text(99, out_clip));

    // Vector 17: Authorized clipboard read succeeds
    caps->grant(30, Capability::ClipboardRead);
    record_vector(17, "Authorized clipboard read receives data", clip->get_text(30, out_clip) && out_clip == "authorized_data");

    // Vector 18: Clipboard audit log contains all transactions
    record_vector(18, "Clipboard audit log records caller PID and authorization status", clip->audit_log().size() >= 4);

    // =========================================================================
    // Category 4: Packaging and Installer Integrity (Vectors 19 - 24)
    // =========================================================================
    // Vector 19: Empty bundle_id rejected
    PackageManifest m_bad_id{};
    m_bad_id.bundle_id = "";
    m_bad_id.name = "Test";
    m_bad_id.executable_name = "test.bin";
    record_vector(19, "Package with empty bundle_id rejected by manifest validator", !m_bad_id.is_valid());

    // Vector 20: Missing executable in files list rejected
    PackageManifest m_no_exec{};
    m_no_exec.bundle_id = "com.winios.noexec";
    m_no_exec.name = "No Exec";
    m_no_exec.executable_name = "exec.bin";
    record_vector(20, "Package with missing executable rejected by manifest validator", !m_no_exec.is_valid());

    // Vector 21: Tampered checksum detected
    AppPackage pkg_tampered;
    pkg_tampered.manifest.bundle_id = "com.winios.tampered";
    pkg_tampered.manifest.name = "Tampered";
    pkg_tampered.manifest.executable_name = "bin.bin";
    pkg_tampered.add_file("bin.bin", {0x11, 0x22});
    pkg_tampered.manifest.files[0].sha256_hex = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    record_vector(21, "Tampered SHA-256 rejected by package installer",
                  installer->install(pkg_tampered) == InstallStatus::IntegrityCheckFailed);

    // Vector 22: Staging rollback after failed installation
    record_vector(22, "Failed package installation leaves zero staging artifacts",
                  !vfs->exists("/staging/com.winios.tampered") && !registry->is_installed("com.winios.tampered"));

    // Vector 23: Successful package installation
    AppPackage pkg_valid;
    pkg_valid.manifest.bundle_id = "com.winios.valid";
    pkg_valid.manifest.name = "Valid";
    pkg_valid.manifest.executable_name = "valid.bin";
    pkg_valid.add_file("valid.bin", {0xAA, 0xBB});
    record_vector(23, "Valid package installed and registered successfully",
                  installer->install(pkg_valid) == InstallStatus::Success && registry->is_installed("com.winios.valid"));

    // Vector 24: Duplicate installation rejected
    record_vector(24, "Duplicate package installation rejected with AlreadyInstalled",
                  installer->install(pkg_valid) == InstallStatus::AlreadyInstalled);

    // =========================================================================
    // Category 5: Display and Window Access Control (Vectors 25 - 28)
    // =========================================================================
    std::shared_ptr<DisplaySurface> surfA;
    surf_mgr->create_surface(procA.pid(), 200, 200, PixelFormat::B8G8R8A8_UNORM, surfA);
    std::uint32_t widA = win_mgr->create_window(procA.pid(), "WinA", {0, 0, 200, 200}, surfA);

    // Vector 25: ProcB cannot destroy ProcA window (EBADF)
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, widA);
    disp.handle_syscall(cpu, procB, uas, sched);
    record_vector(25, "Cross-process window destroy rejected with EBADF",
                  cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF));

    // Vector 26: ProcB cannot copy pixels into ProcA surface (AccessDenied)
    std::vector<std::uint8_t> dummy_px(200 * 200 * 4, 0);
    record_vector(26, "Cross-process surface copy rejected with AccessDenied",
                  surfA->copy_pixels(procB.pid(), dummy_px.data(), dummy_px.size()) == SurfaceResultCode::AccessDenied);

    // Vector 27: ProcB cannot fill rect in ProcA surface (AccessDenied)
    record_vector(27, "Cross-process surface fill rejected with AccessDenied",
                  surfA->fill_rect(procB.pid(), {0, 0, 50, 50}, 0xFF00FF00) == SurfaceResultCode::AccessDenied);

    // Vector 28: Zero dimension window creation rejected (EINVAL)
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0);
    cpu.write_register(1, 0);
    cpu.write_register(2, 0);
    cpu.write_register(3, 0);
    cpu.write_register(4, 0); // width = 0
    cpu.write_register(5, 100);
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(28, "Window creation with zero width rejected with EINVAL",
                  cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));

    // =========================================================================
    // Category 6: Syscall Dispatching and Register Fuzzing (Vectors 29 - 34)
    // =========================================================================
    // Vector 29: Syscall 508 rejected with ENOSYS
    cpu.write_register(16, 508);
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(29, "Syscall 508 returns ENOSYS", cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS));

    // Vector 30: Syscall 9999 returns ENOSYS
    cpu.write_register(16, 9999);
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(30, "Syscall 9999 returns ENOSYS", cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS));

    // Vector 31: Syscall 502 with unmapped buffer returns EFAULT
    cpu.write_register(16, kSysWiniosSurfaceUpdate);
    cpu.write_register(0, widA);
    cpu.write_register(1, 0x90000); // Unmapped
    cpu.write_register(2, 200 * 200 * 4);
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(31, "Surface update with unmapped VA returns EFAULT",
                  cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));

    // Vector 32: Syscall 504 with undersized buffer returns EINVAL
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, 0x10000);
    cpu.write_register(1, 8); // Too small for GuestInputEvent (32 bytes)
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(32, "Poll event with undersized buffer returns EINVAL",
                  cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));

    // Vector 33: Syscall 504 with misaligned buffer returns EFAULT
    cpu.write_register(16, kSysWiniosPollEvent);
    cpu.write_register(0, 0x10004); // Not 8-byte aligned
    cpu.write_register(1, sizeof(GuestInputEvent));
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(33, "Poll event with misaligned pointer returns EFAULT",
                  cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));

    // Vector 34: Fast ABI error representation (negative errno in X0)
    disp.set_abi_mode(SyscallAbi::WinIOSFast);
    cpu.write_register(0, 508);
    disp.handle_syscall(cpu, procA, uas, sched);
    record_vector(34, "Fast ABI returns negative errno in X0",
                  static_cast<std::int64_t>(cpu.read_register(0)) == -kErrnoENOSYS);

    std::cout << "\nResults: " << g_vectors_passed << " / 34 vectors passed!\n";
    assert(g_vectors_passed == 34);
}

int main() {
    run_34_vector_security_suite();
    return 0;
}
