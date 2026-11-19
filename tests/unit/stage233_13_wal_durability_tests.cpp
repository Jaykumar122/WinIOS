#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>

#include "packaging/package_installer.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "crypto/sha256.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "WAL Durability Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

static AppPackage make_test_pkg(const std::string& bundle_id, const std::string& version, const std::string& content) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "DurabilityApp";
    pkg.manifest.version = version;
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";

    std::vector<std::uint8_t> bin_data(content.begin(), content.end());
    pkg.add_file("bin/main", bin_data);
    std::vector<std::uint8_t> res_data = {'D', 'A', 'T', 'A'};
    pkg.add_file("res/config.json", res_data);

    return pkg;
}

static void test_crash_at_all_install_states() {
    std::cout << "[Stage 233.13] Testing crash fault-injection across 8 installer states...\n";

    const PackageInstaller::InstallerState states_to_test[] = {
        PackageInstaller::InstallerState::Initiated,
        PackageInstaller::InstallerState::Validating,
        PackageInstaller::InstallerState::ManifestParsed,
        PackageInstaller::InstallerState::SecurityVerified,
        PackageInstaller::InstallerState::Staged,
        PackageInstaller::InstallerState::Extracting,
        PackageInstaller::InstallerState::Extracted,
        PackageInstaller::InstallerState::Registering
    };

    for (auto state : states_to_test) {
        auto vfs = std::make_shared<VirtualFileSystem>();
        auto reg = std::make_shared<ApplicationRegistry>();
        PackageInstaller installer(vfs, reg);

        installer.inject_fault_at_state(state);
        AppPackage pkg = make_test_pkg("com.fault.app", "1.0.0", "CRASH_PAYLOAD");

        InstallStatus status = installer.install(pkg);
        TEST_ASSERT(status == InstallStatus::CommitFailed);

        // Verify staging directory exists with journal
        std::vector<VfsDirEntry> staging_entries;
        TEST_ASSERT(vfs->readdir("/staging", staging_entries));
        int tx_count = 0;
        for (const auto& e : staging_entries) {
            if (e.name != "." && e.name != "..") tx_count++;
        }
        TEST_ASSERT(tx_count == 1);

        // Verify target application directory was NOT committed
        TEST_ASSERT(!vfs->exists("/Applications/com.fault.app.app"));
        TEST_ASSERT(!reg->is_installed("com.fault.app"));

        // Simulate crash recovery upon restart
        PackageInstaller recovery_installer(vfs, reg);
        auto report = recovery_installer.reconcile_and_recover();
        TEST_ASSERT(report.abandoned_transactions_cleaned == 1);

        // Verify staging directory is now clean
        std::vector<VfsDirEntry> cleaned_staging;
        TEST_ASSERT(vfs->readdir("/staging", cleaned_staging));
        int remaining = 0;
        for (const auto& e : cleaned_staging) {
            if (e.name != "." && e.name != "..") remaining++;
        }
        TEST_ASSERT(remaining == 0);
        TEST_ASSERT(!reg->is_installed("com.fault.app"));
    }
}

static void test_crash_during_update_with_backup_recovery() {
    std::cout << "[Stage 233.13] Testing simulated crash during update and backup recovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // 1. Cleanly install v1
    AppPackage pkg_v1 = make_test_pkg("com.backup.app", "1.0.0", "V1_STABLE_DATA");
    TEST_ASSERT(installer.install(pkg_v1) == InstallStatus::Success);
    TEST_ASSERT(reg->is_installed("com.backup.app"));

    // 2. Simulate crash during update when target has been moved to backup and staging is interrupted
    // We create the simulated interrupted update state:
    // /backup/tx_update_test contains the old v1
    // /Applications/com.backup.app.app is missing (moved)
    // /staging/tx_update_test has journal with is_update=true, state=Extracting
    const std::string tx_id = "tx_crash_sim_01";
    const std::string staging_tx = "/staging/" + tx_id;
    const std::string backup_dir = "/backup/" + tx_id;
    const std::string target_dir = "/Applications/com.backup.app.app";

    vfs->mkdir("/backup");
    vfs->rename(target_dir, backup_dir);
    TEST_ASSERT(!vfs->exists(target_dir));
    TEST_ASSERT(vfs->exists(backup_dir));

    vfs->mkdir(staging_tx);
    std::string journal_body =
        "{\n"
        "  \"tx_id\": \"" + tx_id + "\",\n"
        "  \"bundle_id\": \"com.backup.app\",\n"
        "  \"state\": \"Extracting\",\n"
        "  \"is_update\": true,\n"
        "  \"target_dir\": \"" + target_dir + "\",\n"
        "  \"backup_dir\": \"" + backup_dir + "\",\n"
        "  \"timestamp_us\": 1000\n"
        "}";
    std::string digest = crypto::Sha256::hash_hex(reinterpret_cast<const std::uint8_t*>(journal_body.data()), journal_body.size());
    std::string full_journal = "{\"checksum\":\"" + digest + "\",\"entry\":" + journal_body + "}";
    vfs->create_file(staging_tx + "/journal.json");
    vfs->write_file(staging_tx + "/journal.json", reinterpret_cast<const std::uint8_t*>(full_journal.data()), full_journal.size());

    // 3. Run reconcile_and_recover
    PackageInstaller recovery_installer(vfs, reg);
    auto report = recovery_installer.reconcile_and_recover();

    TEST_ASSERT(report.backups_restored == 1);
    TEST_ASSERT(report.abandoned_transactions_cleaned == 1);

    // Verify v1 is fully restored at target_dir
    TEST_ASSERT(vfs->exists(target_dir));
    std::vector<std::uint8_t> read_data;
    TEST_ASSERT(vfs->read_file(target_dir + "/bin/main", read_data));
    std::string restored_content(read_data.begin(), read_data.end());
    TEST_ASSERT(restored_content == "V1_STABLE_DATA");

    // Verify staging and backup are cleaned
    TEST_ASSERT(!vfs->exists(staging_tx));
    TEST_ASSERT(!vfs->exists(backup_dir));
}

static void test_corrupted_journal_checksum_recovery() {
    std::cout << "[Stage 233.13] Testing recovery with corrupted journal checksum...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();

    const std::string tx_id = "tx_corrupt_journal";
    const std::string staging_tx = "/staging/" + tx_id;
    vfs->mkdir("/staging");
    vfs->mkdir(staging_tx);

    std::string journal_body =
        "{\n"
        "  \"tx_id\": \"" + tx_id + "\",\n"
        "  \"bundle_id\": \"com.corrupt.app\",\n"
        "  \"state\": \"Staged\",\n"
        "  \"is_update\": false,\n"
        "  \"target_dir\": \"/Applications/com.corrupt.app.app\",\n"
        "  \"backup_dir\": \"\",\n"
        "  \"timestamp_us\": 1000\n"
        "}";
    // Bad checksum intentionally injected
    std::string bad_checksum = "0000000000000000000000000000000000000000000000000000000000000000";
    std::string full_journal = "{\"checksum\":\"" + bad_checksum + "\",\"entry\":" + journal_body + "}";
    vfs->create_file(staging_tx + "/journal.json");
    vfs->write_file(staging_tx + "/journal.json", reinterpret_cast<const std::uint8_t*>(full_journal.data()), full_journal.size());

    PackageInstaller recovery_installer(vfs, reg);
    auto report = recovery_installer.reconcile_and_recover();

    TEST_ASSERT(report.abandoned_transactions_cleaned == 1);
    TEST_ASSERT(!vfs->exists(staging_tx));
}

static void test_committed_transaction_finalization() {
    std::cout << "[Stage 233.13] Testing committed transaction finalization...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();

    const std::string tx_id = "tx_committed_final";
    const std::string staging_tx = "/staging/" + tx_id;
    const std::string target_dir = "/Applications/com.committed.app.app";

    vfs->mkdir("/staging");
    vfs->mkdir(staging_tx);
    vfs->mkdir("/Applications");
    vfs->mkdir(target_dir);
    vfs->mkdir(target_dir + "/bin");
    vfs->create_file(target_dir + "/bin/main");
    const std::string dummy_bin = "EXEC";
    vfs->write_file(target_dir + "/bin/main", reinterpret_cast<const std::uint8_t*>(dummy_bin.data()), dummy_bin.size());

    std::string journal_body =
        "{\n"
        "  \"tx_id\": \"" + tx_id + "\",\n"
        "  \"bundle_id\": \"com.committed.app\",\n"
        "  \"state\": \"Committed\",\n"
        "  \"is_update\": false,\n"
        "  \"target_dir\": \"" + target_dir + "\",\n"
        "  \"backup_dir\": \"\",\n"
        "  \"timestamp_us\": 1000\n"
        "}";
    std::string digest = crypto::Sha256::hash_hex(reinterpret_cast<const std::uint8_t*>(journal_body.data()), journal_body.size());
    std::string full_journal = "{\"checksum\":\"" + digest + "\",\"entry\":" + journal_body + "}";
    vfs->create_file(staging_tx + "/journal.json");
    vfs->write_file(staging_tx + "/journal.json", reinterpret_cast<const std::uint8_t*>(full_journal.data()), full_journal.size());

    TEST_ASSERT(!reg->is_installed("com.committed.app"));

    PackageInstaller recovery_installer(vfs, reg);
    auto report = recovery_installer.reconcile_and_recover();

    TEST_ASSERT(report.committed_transactions_finalized == 1);
    TEST_ASSERT(reg->is_installed("com.committed.app"));
    TEST_ASSERT(!vfs->exists(staging_tx));
}

static void test_corrupted_app_flagging_and_discovery() {
    std::cout << "[Stage 233.13] Testing corrupted app detection and unindexed app discovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();

    // 1. Corrupted app test: registered in registry, but install_dir missing
    AppRegistration missing_app{};
    missing_app.bundle_id = "com.missing.app";
    missing_app.name = "Missing";
    missing_app.exec_path = "bin/main";
    missing_app.install_dir = "/Applications/com.missing.app.app";
    missing_app.version = "1.0.0";
    missing_app.is_active = true;
    missing_app.state = InstallationState::Installed;
    TEST_ASSERT(reg->register_app(missing_app));

    // 2. Unindexed app test: bundle exists in VFS with files, but not in registry
    const std::string unindexed_dir = "/Applications/com.unindexed.app.app";
    vfs->mkdir("/Applications");
    vfs->mkdir(unindexed_dir);
    vfs->mkdir(unindexed_dir + "/bin");
    vfs->create_file(unindexed_dir + "/bin/main");
    std::string exe_data = "EXE";
    vfs->write_file(unindexed_dir + "/bin/main", reinterpret_cast<const std::uint8_t*>(exe_data.data()), exe_data.size());
    vfs->create_file(unindexed_dir + "/manifest.json");
    std::string mf_data = "{}";
    vfs->write_file(unindexed_dir + "/manifest.json", reinterpret_cast<const std::uint8_t*>(mf_data.data()), mf_data.size());

    PackageInstaller recovery_installer(vfs, reg);
    auto report = recovery_installer.reconcile_and_recover();

    TEST_ASSERT(report.corrupted_apps_flagged == 1);
    auto corrupted_info = reg->get_app("com.missing.app");
    TEST_ASSERT(corrupted_info.has_value());
    TEST_ASSERT(corrupted_info->state == InstallationState::Corrupted);
    TEST_ASSERT(!corrupted_info->is_active);

    TEST_ASSERT(report.unindexed_apps_discovered == 1);
    TEST_ASSERT(reg->is_installed("com.unindexed.app"));
    auto discovered_info = reg->get_app("com.unindexed.app");
    TEST_ASSERT(discovered_info.has_value());
    TEST_ASSERT(discovered_info->state == InstallationState::Installed);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.13: WAL Durability & Recovery Tests     \n";
    std::cout << "====================================================\n";

    test_crash_at_all_install_states();
    test_crash_during_update_with_backup_recovery();
    test_corrupted_journal_checksum_recovery();
    test_committed_transaction_finalization();
    test_corrupted_app_flagging_and_discovery();

    std::cout << "\n[Stage 233.13] ALL WAL DURABILITY TESTS PASSED.\n";
    return 0;
}
