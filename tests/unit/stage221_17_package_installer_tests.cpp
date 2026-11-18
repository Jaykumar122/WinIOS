#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "packaging/package_installer.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;

void test_package_installer_lifecycle() {
    std::cout << "[Test 1] Package Installation and Uninstallation...\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg{};
    pkg.manifest.format_version = "2.0";
    pkg.manifest.bundle_id = "com.winios.demo";
    pkg.manifest.name = "Demo App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/demo";
    pkg.manifest.permissions = { "display.window", "storage.private" };

    std::vector<uint8_t> demo_code = { 0xCF, 0xFA, 0xED, 0xFE, 0x01, 0x02, 0x03, 0x04 };
    pkg.add_file("bin/demo", demo_code);
    pkg.add_file("res/config.txt", { 'k', '=', 'v' });

    // 1. Install
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success);
    TEST_ASSERT(registry->is_installed("com.winios.demo"));
    TEST_ASSERT(vfs->exists("/Applications/com.winios.demo.app/bin/demo"));
    TEST_ASSERT(vfs->exists("/Applications/com.winios.demo.app/res/config.txt"));

    // 2. Duplicate install rejected
    auto status2 = installer.install(pkg);
    TEST_ASSERT(status2 == InstallStatus::AlreadyInstalled);

    // 3. Uninstall
    auto u_status = installer.uninstall("com.winios.demo", false);
    TEST_ASSERT(u_status == InstallStatus::Success);
    TEST_ASSERT(!registry->is_installed("com.winios.demo"));
    TEST_ASSERT(!vfs->exists("/Applications/com.winios.demo.app/bin/demo"));

    std::cout << "[PASS] Package installation lifecycle passed.\n";
}

void test_package_installer_integrity_failure() {
    std::cout << "[Test 2] Integrity Check Rejection on Tampered Payload...\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg{};
    pkg.manifest.format_version = "2.0";
    pkg.manifest.bundle_id = "com.winios.tampered";
    pkg.manifest.name = "Tampered App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/app";

    std::vector<uint8_t> good_code = { 0xAA, 0xBB, 0xCC, 0xDD };
    pkg.add_file("bin/app", good_code);

    // Tamper with data after SHA256 was computed
    pkg.manifest.files[0].data[0] = 0xFF;

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::IntegrityCheckFailed);
    TEST_ASSERT(!registry->is_installed("com.winios.tampered"));

    std::cout << "[PASS] Integrity check rejection passed.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.17: Package Validation & Installation Tests ===\n";
    test_package_installer_lifecycle();
    test_package_installer_integrity_failure();
    std::cout << "[ALL PASS] Stage 221.17 tests passed successfully.\n";
    return 0;
}
