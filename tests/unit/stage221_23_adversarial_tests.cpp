#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "packaging/app_package.h"
#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "storage/isolated_storage.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/thread.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::storage;
using namespace winios::security;
using namespace winios::kernel;

void test_adversarial_package_manifests() {
    std::cout << "[Test 1] Adversarial Package Manifest Fuzzing & Traversal...\n";

    // 1. Path traversal in bundle_id
    PackageManifest m1{};
    m1.bundle_id = "../../Windows/System32";
    m1.name = "Malicious";
    m1.executable_name = "bin/bad";
    FileEntry fe1{ "bin/bad", 10, "", { 0x90 } };
    m1.files.push_back(fe1);
    TEST_ASSERT(!m1.is_valid());

    // 2. Path traversal in file entry
    PackageManifest m2{};
    m2.bundle_id = "com.winios.bad";
    m2.name = "Malicious2";
    m2.executable_name = "bin/bad";
    FileEntry fe2{ "../../../root/secret", 10, "", { 0x90 } };
    m2.files.push_back(fe2);
    TEST_ASSERT(!m2.is_valid());

    // 3. Absolute path in file entry
    PackageManifest m3{};
    m3.bundle_id = "com.winios.bad3";
    m3.name = "Malicious3";
    m3.executable_name = "/bin/bad";
    FileEntry fe3{ "/bin/bad", 10, "", { 0x90 } };
    m3.files.push_back(fe3);
    TEST_ASSERT(!m3.is_valid());

    // 4. Missing executable
    PackageManifest m4{};
    m4.bundle_id = "com.winios.bad4";
    m4.name = "Malicious4";
    m4.executable_name = "bin/missing";
    FileEntry fe4{ "bin/different", 10, "", { 0x90 } };
    m4.files.push_back(fe4);
    TEST_ASSERT(!m4.is_valid());

    std::cout << "[PASS] Malformed manifest attacks properly rejected.\n";
}

void test_adversarial_storage_quota_and_traversal() {
    std::cout << "[Test 2] Adversarial Storage Traversal & Quota Exhaustion...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/data");

    IsolatedStorage storage(vfs, "com.winios.victim", 2048);

    // Path traversal attempts
    TEST_ASSERT(!storage.is_path_safe("../victim/file"));
    TEST_ASSERT(!storage.is_path_safe("sub/../../escape"));
    TEST_ASSERT(!storage.is_path_safe("/absolute/path"));

    std::vector<uint8_t> dummy(100, 0x55);
    TEST_ASSERT(storage.atomic_write("../escape.txt", dummy.data(), dummy.size()) == StorageResult::InvalidPath);

    // Fill storage up to quota
    std::vector<uint8_t> chunk1(1500, 0x11);
    TEST_ASSERT(storage.atomic_write("part1.bin", chunk1.data(), chunk1.size()) == StorageResult::Success);

    // Attempt write that exceeds remaining quota (1500 + 1000 > 2048)
    std::vector<uint8_t> chunk2(1000, 0x22);
    TEST_ASSERT(storage.atomic_write("part2.bin", chunk2.data(), chunk2.size()) == StorageResult::QuotaExceeded);

    std::cout << "[PASS] Storage traversal and quota attacks thwarted.\n";
}

void test_adversarial_syscall_parameters() {
    std::cout << "[Test 3] Adversarial Syscall Parameter Validation...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "adv_uas");

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    CapabilityManager cm;
    dispatcher.set_capability_manager(&cm);

    BsdProcess proc(55, 1, "VictimProcess");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // 1. Syscall with unmapped null pointer
    cpu.write_register(16, kSysWiniosLog);
    cpu.write_register(0, 0); // NULL entry_va
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 2. Syscall with misaligned pointer
    cpu.write_register(16, kSysWiniosLog);
    cpu.write_register(0, 0x10003); // Odd alignment
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 3. Syscall with kernel address space
    cpu.write_register(16, kSysWiniosLog);
    cpu.write_register(0, 0xFFFF800000000000ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 4. Unauthorized capability invocation: process spawning without permission
    cpu.write_register(16, kSysWiniosSpawnProcess);
    cpu.write_register(0, 0x10000);
    cpu.write_register(1, 10);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);

    std::cout << "[PASS] Syscall parameter bounds properly guarded.\n";
}

void test_five_run_bit_for_bit_determinism() {
    std::cout << "[Test 4] Five-Run Bit-for-Bit Deterministic Packaging...\n";

    std::vector<uint8_t> baseline_output;

    for (int run = 0; run < 5; ++run) {
        AppPackage pkg{};
        pkg.manifest.format_version = "2.0";
        pkg.manifest.bundle_id = "com.winios.deterministic";
        pkg.manifest.name = "Determinism Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.min_os_version = "221.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.min_abi_version = "1.5";
        pkg.manifest.executable_name = "bin/det";
        pkg.manifest.storage_quota_bytes = 10485760;
        pkg.manifest.permissions = { "display.window", "storage.private", "logging" };

        std::vector<uint8_t> bin_data = { 0xCF, 0xFA, 0xED, 0xFE, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22 };
        pkg.add_file("bin/det", bin_data);

        std::vector<uint8_t> packed = pkg.pack();
        if (run == 0) {
            baseline_output = packed;
        } else {
            TEST_ASSERT(packed.size() == baseline_output.size());
            TEST_ASSERT(std::memcmp(packed.data(), baseline_output.data(), packed.size()) == 0);
        }
    }

    std::cout << "[PASS] Deterministic packaging verified across 5 consecutive runs.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.23: Adversarial Security & Determinism Tests ===\n";
    test_adversarial_package_manifests();
    test_adversarial_storage_quota_and_traversal();
    test_adversarial_syscall_parameters();
    test_five_run_bit_for_bit_determinism();
    std::cout << "[ALL PASS] Stage 221.23 tests passed successfully.\n";
    return 0;
}
