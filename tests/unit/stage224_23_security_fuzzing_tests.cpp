#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "packaging/app_package.h"
#include "storage/isolated_storage.h"
#include "security/capability_manager.h"
#include "runtime/guest_ipc.h"
#include "kernel/app_supervisor.h"
#include "services/configuration_service.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

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
using namespace winios::runtime;
using namespace winios::kernel;
using namespace winios::services;
using namespace winios::vfs;

// ============================================================================
// 1. Package Fuzzing & Malformation Defense Tests
// ============================================================================
void test_package_fuzzing() {
    std::cout << "[Fuzz Test 1] Malformed Package & Path Traversal Fuzzing...\n";

    // 1a. Corrupted / Invalid Magic
    {
        uint8_t garbage[128] = { 'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C', 0x02, 0x00, 0x00, 0x00 };
        AppPackage pkg;
        auto res = AppPackage::validate_package_data(garbage, sizeof(garbage), &pkg);
        TEST_ASSERT(res == PackageValidationResult::InvalidMagic);
    }

    // 1b. Truncated Header (< 16 bytes)
    {
        uint8_t tiny[12] = { 'W', 'I', 'O', 'S', 'P', 'K', 'G', '2' };
        AppPackage pkg;
        auto res = AppPackage::validate_package_data(tiny, sizeof(tiny), &pkg);
        TEST_ASSERT(res == PackageValidationResult::InvalidMagic || res == PackageValidationResult::PayloadTruncated);
    }

    // 1c. Oversized Manifest Length (> 64KB)
    {
        uint8_t bad_hdr[32];
        std::memcpy(bad_hdr, "WIOSPKG2", 8);
        uint32_t ver = 2;
        uint32_t mlen = 128 * 1024; // 128KB > 64KB ceiling
        uint32_t fc = 0;
        std::memcpy(bad_hdr + 8, &ver, 4);
        std::memcpy(bad_hdr + 12, &mlen, 4);
        std::memcpy(bad_hdr + 16, &fc, 4);

        AppPackage pkg;
        auto res = AppPackage::validate_package_data(bad_hdr, sizeof(bad_hdr), &pkg);
        TEST_ASSERT(res == PackageValidationResult::InvalidManifest);
    }

    // 1d. Path Traversal in Package File
    {
        AppPackage pkg;
        pkg.manifest.format_version = "2.1";
        pkg.manifest.bundle_id = "com.winios.fuzz";
        pkg.manifest.name = "Fuzz Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.executable_name = "bin/main";

        pkg.add_file("bin/main", { 0xCF, 0xFA, 0xED, 0xFE });
        pkg.add_file("../../etc/passwd", { 0xDE, 0xAD, 0xBE, 0xEF });

        auto packed = pkg.pack();
        AppPackage unpacked;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt);
    }

    // 1e. Absolute Path Traversal
    {
        AppPackage pkg;
        pkg.manifest.format_version = "2.1";
        pkg.manifest.bundle_id = "com.winios.fuzz";
        pkg.manifest.name = "Fuzz Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.executable_name = "bin/main";

        pkg.add_file("bin/main", { 0xCF, 0xFA, 0xED, 0xFE });
        pkg.add_file("/windows/system32/cmd.exe", { 0x12, 0x34 });

        auto packed = pkg.pack();
        AppPackage unpacked;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt);
    }

    // 1f. Duplicate File Paths
    {
        AppPackage pkg;
        pkg.manifest.format_version = "2.1";
        pkg.manifest.bundle_id = "com.winios.fuzz";
        pkg.manifest.name = "Fuzz Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.executable_name = "bin/main";

        pkg.add_file("bin/main", { 0xCF, 0xFA, 0xED, 0xFE });
        pkg.add_file("bin/main", { 0x11, 0x22 }); // Duplicate!

        auto packed = pkg.pack();
        AppPackage unpacked;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
        TEST_ASSERT(res == PackageValidationResult::DuplicateFileEntry);
    }

    // 1g. Missing Executable in Package
    {
        AppPackage pkg;
        pkg.manifest.format_version = "2.1";
        pkg.manifest.bundle_id = "com.winios.fuzz";
        pkg.manifest.name = "Fuzz Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.executable_name = "bin/missing_binary";

        pkg.add_file("res/image.png", { 0x89, 0x50, 0x4E, 0x47 });

        auto packed = pkg.pack();
        AppPackage unpacked;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
        TEST_ASSERT(res == PackageValidationResult::ExecutableNotFound);
    }

    // 1h. Non-ARM64 Architecture Rejection
    {
        AppPackage pkg;
        pkg.manifest.format_version = "2.1";
        pkg.manifest.bundle_id = "com.winios.fuzz";
        pkg.manifest.name = "Fuzz Test";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.architecture = "x86_64";
        pkg.manifest.executable_name = "bin/main";

        pkg.add_file("bin/main", { 0xCF, 0xFA, 0xED, 0xFE });

        auto packed = pkg.pack();
        AppPackage unpacked;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
        TEST_ASSERT(res == PackageValidationResult::UnsupportedArchitecture);
    }

    std::cout << "  -> Package fuzzing tests PASSED.\n";
}

// ============================================================================
// 2. Storage Traversal & Quota Exhaustion Fuzzing
// ============================================================================
void test_storage_fuzzing() {
    std::cout << "[Fuzz Test 2] Storage Path Traversal & Quota Exhaustion...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    IsolatedStorage storage(vfs, "com.winios.fuzzbox", 2000);

    // 2a. Path traversal attempts
    TEST_ASSERT(!storage.is_path_safe("../../etc/shadow"));
    TEST_ASSERT(!storage.is_path_safe("/root/secrets.txt"));
    TEST_ASSERT(!storage.is_path_safe("sub/../../../escape.bin"));

    // 2b. Valid write within quota
    std::vector<uint8_t> data1(1200, 0xAA);
    TEST_ASSERT(storage.atomic_write("sub/data1.bin", data1.data(), data1.size()) == StorageResult::Success);
    TEST_ASSERT(storage.current_usage_bytes() == 1200);

    // 2c. Write exceeding quota (1200 + 1000 = 2200 > 2000)
    std::vector<uint8_t> data2(1000, 0xBB);
    TEST_ASSERT(storage.atomic_write("sub/data2.bin", data2.data(), data2.size()) == StorageResult::QuotaExceeded);
    TEST_ASSERT(storage.current_usage_bytes() == 1200);

    // 2d. In-place overwrite accounting: replacing 1200 with 1500 (usage becomes 1500 <= 2000)
    std::vector<uint8_t> data1_new(1500, 0xCC);
    TEST_ASSERT(storage.atomic_write("sub/data1.bin", data1_new.data(), data1_new.size()) == StorageResult::Success);
    TEST_ASSERT(storage.current_usage_bytes() == 1500);

    // 2e. Interrupted write cleanup / transaction recovery
    std::string tmp_path = "/data/com.winios.fuzzbox/sub/doc.bin.tmp.123";
    vfs->create_file(tmp_path);
    TEST_ASSERT(vfs->exists(tmp_path));

    std::size_t rec = storage.recover_transactions();
    TEST_ASSERT(rec == 1);
    TEST_ASSERT(!vfs->exists(tmp_path));

    std::cout << "  -> Storage fuzzing tests PASSED.\n";
}

// ============================================================================
// 3. Capability Escalation & Identity Verification Tests
// ============================================================================
void test_capability_security() {
    std::cout << "[Fuzz Test 3] Capability Enforcement & Forged Identity Defense...\n";

    CapabilityManager cm;
    cm.bind_identity(100, "com.winios.untrusted");

    // Process 100 has no special capabilities
    auto id100 = cm.get_identity(100);
    TEST_ASSERT(id100.has_value());
    TEST_ASSERT(id100->pid == 100);
    TEST_ASSERT(id100->bundle_id == "com.winios.untrusted");
    TEST_ASSERT(id100->security_token != 0);

    // Verify ungranted capability checks fail
    TEST_ASSERT(!cm.check(100, Capability::ConfigAccess));
    TEST_ASSERT(!cm.check(100, Capability::DiagnosticsAccess));
    TEST_ASSERT(!cm.check(100, Capability::DisplayWindow));

    // Grant StoragePrivate only
    cm.grant(100, Capability::StoragePrivate);

    // Verify cross-bundle storage access prevention
    TEST_ASSERT(cm.check_storage_access(100, "com.winios.untrusted")); // Own private storage
    TEST_ASSERT(!cm.check_storage_access(100, "com.winios.settings")); // Other app's private storage

    // Audit statistics check
    auto stats = cm.get_audit_stats();
    TEST_ASSERT(stats.total_checks > 0);
    TEST_ASSERT(stats.total_denials > 0);
    TEST_ASSERT(stats.total_grants > 0);

    std::cout << "  -> Capability security tests PASSED.\n";
}

// ============================================================================
// 4. IPC Robustness, Stale Handles & Bounds Tests
// ============================================================================
void test_ipc_robustness() {
    std::cout << "[Fuzz Test 4] IPC Buffer Bounds, Stale Handles & Queue Depth...\n";

    auto& ipc = GuestIpcRouter::Get();
    ipc.Reset();

    uint64_t p1 = ipc.CreatePortV2(10, "fuzz.port");
    TEST_ASSERT(p1 > 0);

    // 4a. Oversized message rejection (> 4096 bytes)
    std::vector<uint8_t> giant_msg(4097, 0xEE);
    TEST_ASSERT(!ipc.SendV2(10, p1, giant_msg.data(), giant_msg.size()));

    // 4b. Queue depth ceiling (32 messages max)
    std::vector<uint8_t> normal_msg = { 1, 2, 3, 4 };
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT(ipc.SendV2(10, p1, normal_msg.data(), normal_msg.size()));
    }
    // 33rd message should fail with queue full
    TEST_ASSERT(!ipc.SendV2(10, p1, normal_msg.data(), normal_msg.size()));

    // 4c. Destroy port and verify handle becomes stale (ABA defense)
    uint64_t old_handle = p1;
    TEST_ASSERT(ipc.DestroyPortV2(p1, 10));

    // Sending to destroyed port should fail
    TEST_ASSERT(!ipc.SendV2(10, old_handle, normal_msg.data(), normal_msg.size()));

    // Create a new port: generation tag ensures new handle is different from old handle
    uint64_t p2 = ipc.CreatePortV2(10);
    TEST_ASSERT(p2 != old_handle);

    // Verify sending to old handle still fails even if index was recycled
    TEST_ASSERT(!ipc.SendV2(10, old_handle, normal_msg.data(), normal_msg.size()));

    ipc.DestroyPortV2(p2, 10);

    std::cout << "  -> IPC robustness tests PASSED.\n";
}

// ============================================================================
// 5. Supervisor Crash Taxonomy & Bounded Fault Ring Tests
// ============================================================================
void test_supervisor_crash_taxonomy() {
    std::cout << "[Fuzz Test 5] Supervisor Fault Capture & Taxonomy Bounding...\n";

    AppSupervisor supervisor(nullptr, nullptr, nullptr, nullptr, nullptr);

    supervisor.register_process(50, "com.winios.faulty");

    // Record various crash causes
    supervisor.record_crash(50, CrashCategory::IllegalInstruction, 132, 0x100040, 100);
    supervisor.record_crash(50, CrashCategory::MemoryFault, 139, 0x0, 200);
    supervisor.record_crash(50, CrashCategory::SecurityViolation, 1, 0x100080, 300);
    supervisor.record_crash(50, CrashCategory::QuotaExceeded, 2, 0x1000C0, 400);

    auto crashes = supervisor.get_recent_crashes(10);
    TEST_ASSERT(crashes.size() == 4);
    // get_recent_crashes returns reverse chronological (most recent first)
    TEST_ASSERT(crashes[0].category == CrashCategory::QuotaExceeded);
    TEST_ASSERT(crashes[1].category == CrashCategory::SecurityViolation);
    TEST_ASSERT(crashes[2].category == CrashCategory::MemoryFault);
    TEST_ASSERT(crashes[3].category == CrashCategory::IllegalInstruction);

    // Test ring buffer bounding (exceeding 64 crash events)
    for (int i = 0; i < 100; ++i) {
        supervisor.record_crash(50, CrashCategory::Abort, 6, 0x200000 + i, 500 + i);
    }

    auto all_crashes = supervisor.get_recent_crashes(200);
    TEST_ASSERT(all_crashes.size() == 64); // Bounded at 64 entries
    TEST_ASSERT(supervisor.total_crashes() == 104);

    // Verify termination cleans up process tracking
    supervisor.force_kill(50, 137);
    TEST_ASSERT(!supervisor.is_running("com.winios.faulty"));

    std::cout << "  -> Supervisor crash taxonomy tests PASSED.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 224.23: SECURITY & FUZZING TEST SUITE\n";
    std::cout << "=================================================================\n\n";

    test_package_fuzzing();
    test_storage_fuzzing();
    test_capability_security();
    test_ipc_robustness();
    test_supervisor_crash_taxonomy();

    std::cout << "\n>>> ALL STAGE 224.23 SECURITY & FUZZING TESTS PASSED! <<<\n";
    return 0;
}
