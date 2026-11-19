#include <iostream>
#include <vector>
#include <string>
#include <memory>

#include "packaging/package_installer.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Installer Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

static AppPackage make_test_pkg(const std::string& bundle_id, const std::string& version, const std::string& content) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "CrashApp";
    pkg.manifest.version = version;
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";

    std::vector<std::uint8_t> bin_data(content.begin(), content.end());
    pkg.add_file("bin/main", bin_data);
    std::vector<std::uint8_t> res_data = {'R', 'E', 'S'};
    pkg.add_file("res/config.json", res_data);

    return pkg;
}

static void test_successful_installation_lifecycle() {
    std::cout << "[Stage 231.7] Testing successful installation lifecycle...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    AppPackage pkg = make_test_pkg("com.sample.txapp", "1.0.0", "MAIN_BINARY_V1");

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success);

    // Verify bundle directory exists
    TEST_ASSERT(vfs->exists("/Applications/com.sample.txapp.app"));
    TEST_ASSERT(vfs->exists("/Applications/com.sample.txapp.app/bin/main"));
    TEST_ASSERT(vfs->exists("/Applications/com.sample.txapp.app/res/config.json"));

    // Verify content
    std::vector<std::uint8_t> read_data;
    TEST_ASSERT(vfs->read_file("/Applications/com.sample.txapp.app/bin/main", read_data));
    std::string content(read_data.begin(), read_data.end());
    TEST_ASSERT(content == "MAIN_BINARY_V1");

    // Verify registry
    TEST_ASSERT(reg->is_installed("com.sample.txapp"));
    auto app_info = reg->get_app("com.sample.txapp");
    TEST_ASSERT(app_info.has_value());
    TEST_ASSERT(app_info->version == "1.0.0");

    // Verify staging is cleaned up
    std::vector<VfsDirEntry> staging_entries;
    if (vfs->readdir("/staging", staging_entries)) {
        int non_dot = 0;
        for (const auto& e : staging_entries) {
            if (e.name != "." && e.name != "..") non_dot++;
        }
        TEST_ASSERT(non_dot == 0); // No leftover staging directories
    }
}

static void test_staging_write_failure_simulation() {
    std::cout << "[Stage 231.7] Testing simulated staging write failure and rollback...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Inject failure at staging
    installer.set_injected_failure(PackageInstaller::InjectedFailureStage::StagingWrite);

    AppPackage pkg = make_test_pkg("com.sample.failwrite", "1.0.0", "BINARY_SHOULD_FAIL");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::StagingFailed);

    // Verify target directory was NEVER created
    TEST_ASSERT(!vfs->exists("/Applications/com.sample.failwrite.app"));

    // Verify registry is untouched
    TEST_ASSERT(!reg->is_installed("com.sample.failwrite"));

    // Verify staging is cleaned up
    std::vector<VfsDirEntry> entries;
    if (vfs->readdir("/staging", entries)) {
        int non_dot = 0;
        for (const auto& e : entries) {
            if (e.name != "." && e.name != "..") non_dot++;
        }
        TEST_ASSERT(non_dot == 0);
    }
}

static void test_precommit_integrity_failure_simulation() {
    std::cout << "[Stage 231.7] Testing pre-commit integrity check failure...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    installer.set_injected_failure(PackageInstaller::InjectedFailureStage::PreCommitIntegrity);

    AppPackage pkg = make_test_pkg("com.sample.failprecommit", "1.0.0", "BINARY_PRECOMMIT_FAIL");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::IntegrityCheckFailed);

    TEST_ASSERT(!vfs->exists("/Applications/com.sample.failprecommit.app"));
    TEST_ASSERT(!reg->is_installed("com.sample.failprecommit"));
}

static void test_update_failure_with_backup_restoration() {
    std::cout << "[Stage 231.7] Testing update rollback and backup restoration...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // 1. Install v1 cleanly
    AppPackage pkg_v1 = make_test_pkg("com.sample.updateapp", "1.0.0", "V1_ORIGINAL_CONTENT");
    TEST_ASSERT(installer.install(pkg_v1) == InstallStatus::Success);
    TEST_ASSERT(reg->is_installed("com.sample.updateapp"));

    // 2. Prepare v2 with injected failure at registry commit
    installer.set_injected_failure(PackageInstaller::InjectedFailureStage::RegistryCommit);
    AppPackage pkg_v2 = make_test_pkg("com.sample.updateapp", "2.0.0", "V2_NEW_CONTENT");

    auto status = installer.update(pkg_v2);
    // Should successfully rollback to backup v1
    TEST_ASSERT(status == InstallStatus::RollbackSuccess);

    // 3. Verify original v1 installation was restored bit-for-bit
    TEST_ASSERT(vfs->exists("/Applications/com.sample.updateapp.app/bin/main"));
    std::vector<std::uint8_t> read_data;
    TEST_ASSERT(vfs->read_file("/Applications/com.sample.updateapp.app/bin/main", read_data));
    std::string content(read_data.begin(), read_data.end());
    TEST_ASSERT(content == "V1_ORIGINAL_CONTENT"); // Preserved!

    // Verify registry still reflects v1
    auto info = reg->get_app("com.sample.updateapp");
    TEST_ASSERT(info.has_value());
    TEST_ASSERT(info->version == "1.0.0");

    // Verify backup directory cleaned up
    std::vector<VfsDirEntry> backup_entries;
    if (vfs->readdir("/backup", backup_entries)) {
        int non_dot = 0;
        for (const auto& e : backup_entries) {
            if (e.name != "." && e.name != "..") non_dot++;
        }
        TEST_ASSERT(non_dot == 0);
    }
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.7: Crash-Consistent Installer Tests     \n";
    std::cout << "====================================================\n";

    test_successful_installation_lifecycle();
    test_staging_write_failure_simulation();
    test_precommit_integrity_failure_simulation();
    test_update_failure_with_backup_restoration();

    std::cout << "\n[Stage 231.7] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
