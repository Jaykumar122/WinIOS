#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/app_package.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static AppPackage make_pkg(const std::string& bundle_id, const std::string& version = "1.0.0") {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "WALTestApp";
    pkg.manifest.version = version;
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";
    pkg.add_file("bin/main", {0xCF, 0xFA, 0xED, 0xFE, 0x01, 0x02});
    pkg.add_file("res/strings.json", {'{', '}'});
    return pkg;
}

static void test_state_machine_transition_sequence() {
    std::cout << "[Test 1] 11-State transition sequence recording...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    std::vector<PackageInstaller::InstallerState> visited;
    installer.set_fault_injection_hook([&](PackageInstaller::InstallerState st) {
        visited.push_back(st);
    });

    AppPackage pkg = make_pkg("com.winios.wal.seq");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success, "Install must succeed");

    // Expected transition sequence:
    // Initiated -> Validating -> ManifestParsed -> SecurityVerified -> Staged -> Extracting -> Extracted -> Registering -> Committed
    TEST_ASSERT(visited.size() == 9, "Must visit exactly 9 transaction states on success");
    TEST_ASSERT(visited[0] == PackageInstaller::InstallerState::Initiated, "Step 1: Initiated");
    TEST_ASSERT(visited[1] == PackageInstaller::InstallerState::Validating, "Step 2: Validating");
    TEST_ASSERT(visited[2] == PackageInstaller::InstallerState::ManifestParsed, "Step 3: ManifestParsed");
    TEST_ASSERT(visited[3] == PackageInstaller::InstallerState::SecurityVerified, "Step 4: SecurityVerified");
    TEST_ASSERT(visited[4] == PackageInstaller::InstallerState::Staged, "Step 5: Staged");
    TEST_ASSERT(visited[5] == PackageInstaller::InstallerState::Extracting, "Step 6: Extracting");
    TEST_ASSERT(visited[6] == PackageInstaller::InstallerState::Extracted, "Step 7: Extracted");
    TEST_ASSERT(visited[7] == PackageInstaller::InstallerState::Registering, "Step 8: Registering");
    TEST_ASSERT(visited[8] == PackageInstaller::InstallerState::Committed, "Step 9: Committed");
    std::cout << "  Passed!\n";
}

static void test_fault_injection_at_each_state() {
    std::cout << "[Test 2] Fault injection and rollback across all pre-commit states...\n";
    PackageInstaller::InstallerState pre_commit_states[] = {
        PackageInstaller::InstallerState::Initiated,
        PackageInstaller::InstallerState::Validating,
        PackageInstaller::InstallerState::ManifestParsed,
        PackageInstaller::InstallerState::SecurityVerified,
        PackageInstaller::InstallerState::Staged,
        PackageInstaller::InstallerState::Extracting,
        PackageInstaller::InstallerState::Extracted,
        PackageInstaller::InstallerState::Registering
    };

    for (auto target_state : pre_commit_states) {
        auto vfs = std::make_shared<VirtualFileSystem>();
        auto registry = std::make_shared<ApplicationRegistry>();
        PackageInstaller installer(vfs, registry);

        installer.inject_fault_at_state(target_state);

        AppPackage pkg = make_pkg("com.winios.wal.fault");
        auto status = installer.install(pkg);

        TEST_ASSERT(status != InstallStatus::Success, "Installation must fail under injected fault");
        TEST_ASSERT(!registry->is_installed("com.winios.wal.fault"), "App must not be registered");
        TEST_ASSERT(!vfs->exists("/Applications/com.winios.wal.fault.app"), "Target directory must not exist");
    }
    std::cout << "  Passed!\n";
}

static void test_crash_recovery_reconciliation() {
    std::cout << "[Test 3] Crash recovery reconciliation...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    // Simulate an interrupted installation: orphaned staging directory with journal
    std::string tx_dir = "/staging/tx_deadbeef";
    vfs->mkdir("/staging");
    vfs->mkdir(tx_dir);
    vfs->mkdir(tx_dir + "/bundle");
    vfs->create_file(tx_dir + "/bundle/partial.bin");
    vfs->write_file(tx_dir + "/bundle/partial.bin", "junk", 4);

    // Write journal indicating interrupted Extracting state
    std::string journal_json =
        "{\n"
        "  \"tx_id\": \"tx_deadbeef\",\n"
        "  \"bundle_id\": \"com.winios.crashed\",\n"
        "  \"state\": \"Extracting\",\n"
        "  \"is_update\": false,\n"
        "  \"target_dir\": \"/Applications/com.winios.crashed.app\",\n"
        "  \"backup_dir\": \"\",\n"
        "  \"checksum\": \"dummy\"\n"
        "}\n";
    vfs->create_file(tx_dir + "/journal.json");
    vfs->write_file(tx_dir + "/journal.json", journal_json.data(), journal_json.size());

    TEST_ASSERT(vfs->exists(tx_dir), "Orphaned staging directory exists before recovery");

    auto report = installer.reconcile_and_recover();
    TEST_ASSERT(report.abandoned_transactions_cleaned >= 1, "Abandoned transaction must be cleaned");
    TEST_ASSERT(!vfs->exists(tx_dir), "Orphaned staging directory must be deleted after recovery");
    std::cout << "  Passed!\n";
}

static void test_update_rollback_preserves_original() {
    std::cout << "[Test 4] Update rollback preserves original application...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    // 1. Install v1.0.0
    AppPackage v1 = make_pkg("com.winios.wal.update", "1.0.0");
    auto st1 = installer.install(v1);
    TEST_ASSERT(st1 == InstallStatus::Success, "v1.0.0 must install successfully");
    TEST_ASSERT(registry->is_installed("com.winios.wal.update"), "v1.0.0 registered");

    // 2. Attempt update to v2.0.0 with fault injected at CommitMove
    installer.set_injected_failure(PackageInstaller::InjectedFailureStage::CommitMove);
    AppPackage v2 = make_pkg("com.winios.wal.update", "2.0.0");
    auto st2 = installer.update(v2);
    TEST_ASSERT(st2 == InstallStatus::CommitFailed || st2 == InstallStatus::RollbackSuccess,
                "Update must abort and rollback");

    // 3. Verify original v1.0.0 is intact
    TEST_ASSERT(registry->is_installed("com.winios.wal.update"), "Original app still registered");
    auto meta = registry->get_app("com.winios.wal.update");
    TEST_ASSERT(meta.has_value() && meta->version == "1.0.0", "Version remains 1.0.0");
    TEST_ASSERT(vfs->exists("/Applications/com.winios.wal.update.app/bin/main"), "Original binary intact");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.12 WAL State Machine Revalidation Tests ===" << std::endl;
    test_state_machine_transition_sequence();
    test_fault_injection_at_each_state();
    test_crash_recovery_reconciliation();
    test_update_rollback_preserves_original();
    std::cout << "=== All Stage 235.12 tests PASSED successfully ===" << std::endl;
    return 0;
}
