#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/app_package.h"
#include "packaging/canonical_path_validator.h"
#include "crypto/sha256.h"
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

static AppPackage create_base_package(const std::string& bundle_id) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "ConfinementTestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";

    std::vector<std::uint8_t> exec_bytes = {0xCF, 0xFA, 0xED, 0xFE}; // Mach-O 64 magic
    pkg.add_file("bin/main", exec_bytes);
    pkg.add_file("res/config.json", {'{', '}'});
    return pkg;
}

static void test_legitimate_package_installation() {
    std::cout << "[Test 1] Legitimate package installation inside sandbox...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg = create_base_package("com.winios.confinement.ok");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success, "Legitimate package must install successfully");
    TEST_ASSERT(vfs->exists("/Applications/com.winios.confinement.ok.app/bin/main"), "Executable installed in VFS");
    TEST_ASSERT(vfs->exists("/Applications/com.winios.confinement.ok.app/res/config.json"), "Resource installed in VFS");
    std::cout << "  Passed!\n";
}

static void test_path_traversal_manifest_rejection() {
    std::cout << "[Test 2] Path traversal in manifest rejection...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg = create_base_package("com.winios.confinement.traversal");
    pkg.add_file("../escape.txt", {'H', 'a', 'c', 'k'});

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::InvalidManifest, "Traversal package must be rejected");
    TEST_ASSERT(!vfs->exists("/Applications/escape.txt"), "Escaped file must not exist in VFS");
    TEST_ASSERT(!vfs->exists("/Applications/com.winios.confinement.traversal"), "Target dir must not be committed");
    std::cout << "  Passed!\n";
}

static void test_windows_device_manifest_rejection() {
    std::cout << "[Test 3] Windows reserved device name in manifest rejection...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg = create_base_package("com.winios.confinement.con");
    pkg.add_file("bin/CON", {'D', 'E', 'V'});

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::InvalidManifest, "DOS device name CON must be rejected");
    std::cout << "  Passed!\n";
}

static void test_alternate_data_stream_manifest_rejection() {
    std::cout << "[Test 4] Alternate data stream in manifest rejection...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg = create_base_package("com.winios.confinement.ads");
    pkg.add_file("bin/main:hidden", {'E', 'X', 'E'});

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::InvalidManifest, "ADS stream must be rejected");
    std::cout << "  Passed!\n";
}

static void test_drive_letter_manifest_rejection() {
    std::cout << "[Test 5] Drive letter in manifest rejection...\n";
    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    AppPackage pkg = create_base_package("com.winios.confinement.drive");
    pkg.add_file("C:/windows/cmd.exe", {'E', 'V', 'I', 'L'});

    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::InvalidManifest, "Drive letter path must be rejected");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.11 Package Installer Confinement Integration Tests ===" << std::endl;
    test_legitimate_package_installation();
    test_path_traversal_manifest_rejection();
    test_windows_device_manifest_rejection();
    test_alternate_data_stream_manifest_rejection();
    test_drive_letter_manifest_rejection();
    std::cout << "=== All Stage 235.11 tests PASSED successfully ===" << std::endl;
    return 0;
}
