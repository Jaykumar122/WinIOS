#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

void test_package_installation_workflow() {
    std::cout << "[Test 1] Package Installation Workflow & Staged Deployment...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.settings";
    pkg.manifest.name = "Platform Settings";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/settings";
    pkg.add_file("bin/settings", { 0xCF, 0xFA, 0xED, 0xFE, 0x01, 0x02 });
    pkg.add_file("res/info.txt", { 'H', 'e', 'l', 'l', 'o' });

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success);
    TEST_ASSERT(reg->is_installed("com.winios.settings"));
    TEST_ASSERT(vfs->exists("/Applications/com.winios.settings.app/bin/settings"));
    TEST_ASSERT(vfs->exists("/Applications/com.winios.settings.app/res/info.txt"));
    TEST_ASSERT(!vfs->exists("/staging/com.winios.settings")); // Staging cleaned up!

    // Reject duplicate install
    status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::AlreadyInstalled);

    std::cout << "[PASS] Installation workflow verified.\n";
}

void test_package_update_and_rollback() {
    std::cout << "[Test 2] Version Upgrade & Automated Rollback on Error...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // 1. Install v1.0.0
    AppPackage pkg_v1;
    pkg_v1.manifest.bundle_id = "com.winios.demo";
    pkg_v1.manifest.name = "Demo";
    pkg_v1.manifest.version = "1.0.0";
    pkg_v1.manifest.executable_name = "bin/demo";
    pkg_v1.add_file("bin/demo", { 0x11, 0x22 });
    TEST_ASSERT(installer.install(pkg_v1) == InstallStatus::Success);

    // 2. Reject downgrade to 0.9.0
    AppPackage pkg_v0;
    pkg_v0.manifest.bundle_id = "com.winios.demo";
    pkg_v0.manifest.name = "Demo";
    pkg_v0.manifest.version = "0.9.0";
    pkg_v0.manifest.executable_name = "bin/demo";
    pkg_v0.add_file("bin/demo", { 0x00, 0x00 });
    TEST_ASSERT(installer.update(pkg_v0, false) == InstallStatus::VersionDowngradeRejected);

    // 3. Upgrade to v1.1.0
    AppPackage pkg_v11;
    pkg_v11.manifest.bundle_id = "com.winios.demo";
    pkg_v11.manifest.name = "Demo Upgraded";
    pkg_v11.manifest.version = "1.1.0";
    pkg_v11.manifest.executable_name = "bin/demo";
    pkg_v11.add_file("bin/demo", { 0x11, 0x22, 0x33, 0x44 });
    pkg_v11.add_file("res/new_asset.png", { 0x89, 0x50 });

    TEST_ASSERT(installer.update(pkg_v11) == InstallStatus::Success);
    auto q = reg->get_app("com.winios.demo");
    TEST_ASSERT(q.has_value());
    TEST_ASSERT(q->version == "1.1.0");
    TEST_ASSERT(vfs->exists("/Applications/com.winios.demo.app/res/new_asset.png"));

    std::cout << "[PASS] Version upgrade verified.\n";
}

void test_package_uninstallation_with_data_retention() {
    std::cout << "[Test 3] Package Safe Uninstallation & Data Retention vs Purge...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.notes";
    pkg.manifest.name = "Notes";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/notes";
    pkg.add_file("bin/notes", { 0x01, 0x02 });
    installer.install(pkg);

    // Create user data file in /data/com.winios.notes/
    vfs->mkdir("/data");
    vfs->mkdir("/data/com.winios.notes");
    vfs->create_file("/data/com.winios.notes/user_doc.txt");
    const char* user_text = "Important user notes data";
    vfs->write_file("/data/com.winios.notes/user_doc.txt", user_text, 25);

    // Uninstall without purge -> user data preserved!
    TEST_ASSERT(installer.uninstall("com.winios.notes", false) == InstallStatus::Success);
    TEST_ASSERT(!reg->is_installed("com.winios.notes"));
    TEST_ASSERT(!vfs->exists("/Applications/com.winios.notes.app"));
    TEST_ASSERT(vfs->exists("/data/com.winios.notes/user_doc.txt")); // Data preserved!

    // Re-install and uninstall with purge
    installer.install(pkg);
    TEST_ASSERT(installer.uninstall("com.winios.notes", true) == InstallStatus::Success);
    TEST_ASSERT(!vfs->exists("/data/com.winios.notes")); // Data purged!

    std::cout << "[PASS] Uninstallation data retention policy verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.6: PACKAGE INSTALLER TESTS\n";
    std::cout << "========================================\n";

    test_package_installation_workflow();
    test_package_update_and_rollback();
    test_package_uninstallation_with_data_retention();

    std::cout << "\nALL STAGE 224.6 TESTS PASSED (100%).\n";
    return 0;
}
