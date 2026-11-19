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
        std::cerr << "Recovery Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

static void test_abandoned_staging_recovery() {
    std::cout << "[Stage 231.8] Testing abandoned staging transaction recovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Simulate an abandoned transaction in /staging/tx_abandoned
    vfs->mkdir("/staging");
    vfs->mkdir("/staging/tx_abandoned");
    vfs->create_file("/staging/tx_abandoned/journal.json");
    std::string journal = "{\n  \"tx_id\": \"tx_abandoned\",\n  \"bundle_id\": \"com.sample.abandoned\",\n  \"state\": \"Staged\",\n  \"is_update\": false,\n  \"target_dir\": \"/Applications/com.sample.abandoned.app\",\n  \"backup_dir\": \"\"\n}\n";
    vfs->write_file("/staging/tx_abandoned/journal.json", journal.data(), journal.size());

    // Also some orphaned file in staging
    vfs->mkdir("/staging/tx_abandoned/bundle");
    vfs->create_file("/staging/tx_abandoned/bundle/partial.bin");

    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.abandoned_transactions_cleaned == 1);
    TEST_ASSERT(!vfs->exists("/staging/tx_abandoned"));
    TEST_ASSERT(!reg->is_installed("com.sample.abandoned"));
}

static void test_commit_recovery_target_complete() {
    std::cout << "[Stage 231.8] Testing commit recovery when target bundle is complete...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Target directory exists with executable
    vfs->mkdir("/Applications");
    vfs->mkdir("/Applications/com.sample.crashedcommit.app");
    vfs->create_file("/Applications/com.sample.crashedcommit.app/bin");
    vfs->mkdir("/Applications/com.sample.crashedcommit.app/bin");
    vfs->create_file("/Applications/com.sample.crashedcommit.app/bin/main");
    std::string bin_content = "ELF_OR_MACHO_EXEC";
    vfs->write_file("/Applications/com.sample.crashedcommit.app/bin/main", bin_content.data(), bin_content.size());

    // Staging journal in "Committing" state
    vfs->mkdir("/staging");
    vfs->mkdir("/staging/tx_committing");
    vfs->create_file("/staging/tx_committing/journal.json");
    std::string journal = "{\n  \"tx_id\": \"tx_committing\",\n  \"bundle_id\": \"com.sample.crashedcommit\",\n  \"state\": \"Committing\",\n  \"is_update\": false,\n  \"target_dir\": \"/Applications/com.sample.crashedcommit.app\",\n  \"backup_dir\": \"\"\n}\n";
    vfs->write_file("/staging/tx_committing/journal.json", journal.data(), journal.size());

    // Run recovery
    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.committed_transactions_finalized == 1);
    TEST_ASSERT(!vfs->exists("/staging/tx_committing"));
    TEST_ASSERT(reg->is_installed("com.sample.crashedcommit"));
    auto info = reg->get_app("com.sample.crashedcommit");
    TEST_ASSERT(info.has_value());
    TEST_ASSERT(info->state == InstallationState::Installed);
    TEST_ASSERT(info->is_active == true);
}

static void test_commit_recovery_target_incomplete_restore_backup() {
    std::cout << "[Stage 231.8] Testing commit recovery when target is missing and restoring backup...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Backup directory exists with original v1 app
    vfs->mkdir("/backup");
    vfs->mkdir("/backup/tx_update_crashed");
    vfs->mkdir("/backup/tx_update_crashed/bin");
    vfs->create_file("/backup/tx_update_crashed/bin/main");
    std::string v1_content = "V1_SAVED_BEFORE_CRASH";
    vfs->write_file("/backup/tx_update_crashed/bin/main", v1_content.data(), v1_content.size());

    // Target directory /Applications/com.sample.broken.app does NOT exist (crashed before move)
    vfs->mkdir("/staging");
    vfs->mkdir("/staging/tx_update_crashed");
    vfs->create_file("/staging/tx_update_crashed/journal.json");
    std::string journal = "{\n  \"tx_id\": \"tx_update_crashed\",\n  \"bundle_id\": \"com.sample.broken\",\n  \"state\": \"Committing\",\n  \"is_update\": true,\n  \"target_dir\": \"/Applications/com.sample.broken.app\",\n  \"backup_dir\": \"/backup/tx_update_crashed\"\n}\n";
    vfs->write_file("/staging/tx_update_crashed/journal.json", journal.data(), journal.size());

    // Run recovery
    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.backups_restored == 1);
    TEST_ASSERT(!vfs->exists("/staging/tx_update_crashed"));

    // Verify backup was restored to /Applications/com.sample.broken.app
    TEST_ASSERT(vfs->exists("/Applications/com.sample.broken.app/bin/main"));
    std::vector<std::uint8_t> read_data;
    TEST_ASSERT(vfs->read_file("/Applications/com.sample.broken.app/bin/main", read_data));
    std::string content(read_data.begin(), read_data.end());
    TEST_ASSERT(content == "V1_SAVED_BEFORE_CRASH");
}

static void test_registry_reconciliation_missing_bundle() {
    std::cout << "[Stage 231.8] Testing registry reconciliation for missing on-disk bundle...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Register app in registry
    AppRegistration reg_app{};
    reg_app.bundle_id = "com.sample.ghost";
    reg_app.name = "GhostApp";
    reg_app.exec_path = "bin/main";
    reg_app.install_dir = "/Applications/com.sample.ghost.app";
    reg_app.state = InstallationState::Installed;
    reg_app.is_active = true;
    TEST_ASSERT(reg->register_app(reg_app));

    // Notice: /Applications/com.sample.ghost.app does NOT exist on VFS!
    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.corrupted_apps_flagged == 1);

    auto updated = reg->get_app("com.sample.ghost");
    TEST_ASSERT(updated.has_value());
    TEST_ASSERT(updated->state == InstallationState::Corrupted);
    TEST_ASSERT(updated->is_active == false);
}

static void test_unindexed_bundle_discovery() {
    std::cout << "[Stage 231.8] Testing unindexed on-disk bundle discovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Bundle on disk without registry entry
    vfs->mkdir("/Applications");
    vfs->mkdir("/Applications/com.discovered.tools.app");
    vfs->mkdir("/Applications/com.discovered.tools.app/bin");
    vfs->create_file("/Applications/com.discovered.tools.app/bin/main");
    std::string bin = "BIN";
    vfs->write_file("/Applications/com.discovered.tools.app/bin/main", bin.data(), bin.size());

    TEST_ASSERT(!reg->is_installed("com.discovered.tools"));

    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.unindexed_apps_discovered == 1);
    TEST_ASSERT(reg->is_installed("com.discovered.tools"));

    auto found = reg->get_app("com.discovered.tools");
    TEST_ASSERT(found.has_value());
    TEST_ASSERT(found->bundle_id == "com.discovered.tools");
    TEST_ASSERT(found->state == InstallationState::Installed);
    TEST_ASSERT(found->is_active == true);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.8: Installation Recovery Tests          \n";
    std::cout << "====================================================\n";

    test_abandoned_staging_recovery();
    test_commit_recovery_target_complete();
    test_commit_recovery_target_incomplete_restore_backup();
    test_registry_reconciliation_missing_bundle();
    test_unindexed_bundle_discovery();

    std::cout << "\n[Stage 231.8] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
