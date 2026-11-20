#include "tests/support/test_assert.h"
#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/app_package.h"
#include "vfs/vfs.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

namespace {

AppPackage create_test_pkg(const std::string& bundle_id) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "RecoveryTestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";

    std::vector<std::uint8_t> exec_bytes = {0x7F, 'E', 'L', 'F', 0x01, 0x02};
    pkg.add_file("bin/main", exec_bytes);
    pkg.add_file("res/info.json", {'{', '}'});
    return pkg;
}

void test_wal_abort_and_rollback() {
    std::cout << "[Test 1] WAL Abort and Automatic Rollback on Injection...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    // Inject fault at Staged state (state 5)
    installer.inject_fault_at_state(PackageInstaller::InstallerState::Staged);

    auto pkg = create_test_pkg("com.winios.fault.test");
    auto status = installer.install(pkg);

    // Must return an installation error, not success
    TEST_ASSERT(status != InstallStatus::Success, "Fault injected install must not succeed");

    // Verify registry does not contain the aborted application
    TEST_ASSERT(!registry->is_installed("com.winios.fault.test"),
                "Aborted package must not remain in registry");

    std::cout << "  -> Passed WAL abort and rollback\n";
}

void test_wal_reconcile_and_recover() {
    std::cout << "[Test 2] WAL Interrupted Installation Recovery and Reconciliation...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    // Reconcile on clean system
    auto report1 = installer.reconcile_and_recover();
    TEST_ASSERT(report1.total_recovered() == 0, "Clean system must have zero recoveries");

    // Perform successful install
    installer.clear_fault_injection();
    auto pkg = create_test_pkg("com.winios.clean.app");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success, "Clean installation must succeed");
    TEST_ASSERT(registry->is_installed("com.winios.clean.app"), "App must be registered");

    // Reconcile after valid install
    auto report2 = installer.reconcile_and_recover();
    TEST_ASSERT(report2.abandoned_transactions_cleaned == 0, "No abandoned transactions");

    // Clean up interrupted installations call
    auto orphaned = installer.recover_interrupted_installations();
    TEST_ASSERT(orphaned == 0, "No orphaned directories");

    std::cout << "  -> Passed WAL reconcile and recover\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.16: WAL Durability & Crash Recovery Suite   \n";
    std::cout << "========================================================\n";

    test_wal_abort_and_rollback();
    test_wal_reconcile_and_recover();

    std::cout << "\nALL STAGE 238.16 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
