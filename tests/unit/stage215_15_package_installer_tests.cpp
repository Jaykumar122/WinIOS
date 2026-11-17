#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "vfs/vfs.h"

using namespace winios::packaging;
using namespace winios::vfs;

void test_package_pack_unpack() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.calculator";
    pkg.manifest.name = "Calculator";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_os_version = "215.0";
    pkg.manifest.executable_name = "calc.bin";
    pkg.manifest.permissions = {"display.window", "storage.private"};

    std::vector<std::uint8_t> dummy_bin = {0x7F, 'E', 'L', 'F', 0x01, 0x02, 0x03};
    pkg.add_file("calc.bin", dummy_bin);

    std::vector<std::uint8_t> dummy_icon = {0x89, 'P', 'N', 'G'};
    pkg.add_file("res/icon.png", dummy_icon);

    assert(pkg.manifest.is_valid());

    // Pack to container
    auto container = pkg.pack();
    assert(!container.empty());

    // Unpack from container
    AppPackage unpacked;
    assert(AppPackage::unpack(container.data(), container.size(), unpacked));
    assert(unpacked.manifest.bundle_id == "com.winios.calculator");
    assert(unpacked.manifest.name == "Calculator");
    assert(unpacked.manifest.executable_name == "calc.bin");
    assert(unpacked.manifest.files.size() == 2);
    assert(unpacked.manifest.files[0].data == dummy_bin);
    assert(unpacked.manifest.files[1].data == dummy_icon);

    std::cout << "[PASS] test_package_pack_unpack passed\n";
}

void test_transactional_installer_and_rollback() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    // 1. Valid package install
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.notes";
    pkg.manifest.name = "Notes";
    pkg.manifest.executable_name = "notes.bin";
    pkg.manifest.permissions = {"storage.private"};
    pkg.add_file("notes.bin", {0xAA, 0xBB, 0xCC});

    auto res = installer.install(pkg);
    assert(res == InstallStatus::Success);
    assert(registry->is_installed("com.winios.notes"));
    assert(vfs->exists("/Applications/com.winios.notes.app/notes.bin"));

    // 2. Duplicate install rejected
    auto res_dup = installer.install(pkg);
    assert(res_dup == InstallStatus::AlreadyInstalled);

    // 3. Corrupted package rollback test
    AppPackage bad_pkg;
    bad_pkg.manifest.bundle_id = "com.winios.bad";
    bad_pkg.manifest.name = "Bad App";
    bad_pkg.manifest.executable_name = "bad.bin";
    bad_pkg.add_file("bad.bin", {0x01, 0x02});
    // Tamper with sha256 to trigger integrity failure
    bad_pkg.manifest.files[0].sha256_hex = "0000000000000000000000000000000000000000000000000000000000000000";

    auto res_bad = installer.install(bad_pkg);
    assert(res_bad == InstallStatus::IntegrityCheckFailed);
    // Verified rollback: not in registry, no files in /Applications or /staging
    assert(!registry->is_installed("com.winios.bad"));
    assert(!vfs->exists("/Applications/com.winios.bad.app"));
    assert(!vfs->exists("/staging/com.winios.bad"));

    // 4. Uninstall test
    auto res_uninst = installer.uninstall("com.winios.notes");
    assert(res_uninst == InstallStatus::Success);
    assert(!registry->is_installed("com.winios.notes"));
    assert(!vfs->exists("/Applications/com.winios.notes.app"));

    std::cout << "[PASS] test_transactional_installer_and_rollback passed\n";
}

void test_registry_persistence() {
    ApplicationRegistry reg;
    AppRegistration app1{};
    app1.bundle_id = "com.winios.calc";
    app1.name = "Calculator";
    app1.version = "1.2.0";
    app1.exec_path = "calc.bin";
    app1.install_dir = "/Applications/com.winios.calc.app";
    app1.permissions = {"display.window"};
    assert(reg.register_app(app1));

    // Serialize
    const std::string serialized = reg.serialize();
    assert(!serialized.empty());

    // Deserialize into fresh registry
    ApplicationRegistry reg2;
    assert(reg2.deserialize(serialized));
    assert(reg2.is_installed("com.winios.calc"));
    auto fetched = reg2.get_app("com.winios.calc");
    assert(fetched.has_value());
    assert(fetched->name == "Calculator");
    assert(fetched->version == "1.2.0");

    std::cout << "[PASS] test_registry_persistence passed\n";
}

int main() {
    std::cout << "Starting Stage 215.15 Package Installer Tests...\n";
    test_package_pack_unpack();
    test_transactional_installer_and_rollback();
    test_registry_persistence();
    std::cout << "All Stage 215.15 Package Installer tests PASSED!\n";
    return 0;
}
