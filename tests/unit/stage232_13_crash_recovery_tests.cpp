#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>

#include "packaging/package_installer.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::vfs;

static AppPackage create_sample_package(const std::string& bundle_id, const std::string& version, const std::string& bin_content) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "TestCrashApp";
    pkg.manifest.display_name = "Crash App";
    pkg.manifest.version = version;
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";
    pkg.manifest.min_sdk_version_u32 = 0x02030000;
    pkg.manifest.target_abi_version = "2.4";

    std::vector<std::uint8_t> bin_bytes(bin_content.begin(), bin_content.end());
    pkg.add_file("bin/main", bin_bytes);

    std::vector<std::uint8_t> config_bytes = {'{', '"', 'o', 'k', '"', ':', '1', '}'};
    pkg.add_file("res/config.json", config_bytes);

    return pkg;
}

static void test_crash_injection_all_precommit_states() {
    std::cout << "[Stage 232.13] Testing crash injection across all pre-commit states...\n";

    const std::vector<PackageInstaller::InstallerState> precommit_states = {
        PackageInstaller::InstallerState::Initiated,
        PackageInstaller::InstallerState::Validating,
        PackageInstaller::InstallerState::ManifestParsed,
        PackageInstaller::InstallerState::SecurityVerified,
        PackageInstaller::InstallerState::Staged,
        PackageInstaller::InstallerState::Extracting,
        PackageInstaller::InstallerState::Extracted,
        PackageInstaller::InstallerState::Registering
    };

    for (auto state : precommit_states) {
        auto vfs = std::make_shared<VirtualFileSystem>();
        auto reg = std::make_shared<ApplicationRegistry>();
        PackageInstaller installer(vfs, reg);

        installer.inject_fault_at_state(state);
        AppPackage pkg = create_sample_package("com.sample.crashpre", "1.0.0", "CRASH_PAYLOAD");

        auto status = installer.install(pkg);
        TEST_ASSERT(status != InstallStatus::Success);

        // Staging directory must contain abandoned transaction journal
        std::vector<VfsDirEntry> staging_entries;
        bool found_tx = false;
        if (vfs->readdir("/staging", staging_entries)) {
            for (const auto& e : staging_entries) {
                if (e.name != "." && e.name != "..") {
                    found_tx = true;
                    TEST_ASSERT(vfs->exists("/staging/" + e.name + "/journal.json"));
                }
            }
        }
        TEST_ASSERT(found_tx);

        // Simulate crash recovery on fresh boot
        PackageInstaller recovery_agent(vfs, reg);
        auto report = recovery_agent.reconcile_and_recover();
        TEST_ASSERT(report.abandoned_transactions_cleaned >= 1);

        // Verify total cleanup: zero staging remnants, zero target remnants, app not in registry
        std::vector<VfsDirEntry> post_staging;
        if (vfs->readdir("/staging", post_staging)) {
            int leftovers = 0;
            for (const auto& pe : post_staging) {
                if (pe.name != "." && pe.name != "..") leftovers++;
            }
            TEST_ASSERT(leftovers == 0);
        }
        TEST_ASSERT(!vfs->exists("/Applications/com.sample.crashpre.app"));
        TEST_ASSERT(!reg->is_installed("com.sample.crashpre"));
    }
}

static void test_crash_at_committed_state() {
    std::cout << "[Stage 232.13] Testing crash at Committed state and recovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Inject fault right at Committed state (simulating power failure right after journal update)
    installer.inject_fault_at_state(PackageInstaller::InstallerState::Committed);
    AppPackage pkg = create_sample_package("com.sample.commitcrash", "1.0.0", "COMMITTED_PAYLOAD");

    auto status = installer.install(pkg);
    TEST_ASSERT(status != InstallStatus::Success);

    // Staging contains Committed journal
    std::vector<VfsDirEntry> staging_entries;
    bool found_staging = false;
    if (vfs->readdir("/staging", staging_entries)) {
        for (const auto& e : staging_entries) {
            if (e.name != "." && e.name != "..") {
                found_staging = true;
                std::vector<std::uint8_t> jdata;
                TEST_ASSERT(vfs->read_file("/staging/" + e.name + "/journal.json", jdata));
                std::string jtext(jdata.begin(), jdata.end());
                TEST_ASSERT(jtext.find("\"state\": \"Committed\"") != std::string::npos);
            }
        }
    }
    TEST_ASSERT(found_staging);

    // App files exist in /Applications
    TEST_ASSERT(vfs->exists("/Applications/com.sample.commitcrash.app/bin/main"));

    // Simulate recovery
    PackageInstaller recovery_agent(vfs, reg);
    auto report = recovery_agent.reconcile_and_recover();
    TEST_ASSERT(report.committed_transactions_finalized == 1);

    // Verify app is registered, active, and staging is wiped
    TEST_ASSERT(reg->is_installed("com.sample.commitcrash"));
    auto app = reg->get_app("com.sample.commitcrash");
    TEST_ASSERT(app.has_value());
    TEST_ASSERT(app->is_active);

    std::vector<VfsDirEntry> post_staging;
    if (vfs->readdir("/staging", post_staging)) {
        int cnt = 0;
        for (const auto& pe : post_staging) {
            if (pe.name != "." && pe.name != "..") cnt++;
        }
        TEST_ASSERT(cnt == 0);
    }
}

static void test_update_crash_restores_backup() {
    std::cout << "[Stage 232.13] Testing update crash recovery restores pristine backup...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // 1. Install v1
    AppPackage v1 = create_sample_package("com.sample.upcrash", "1.0.0", "BINARY_VERSION_1_0_0");
    auto status1 = installer.install(v1);
    TEST_ASSERT(status1 == InstallStatus::Success);

    // Verify v1 installed
    std::vector<std::uint8_t> v1_read;
    TEST_ASSERT(vfs->read_file("/Applications/com.sample.upcrash.app/bin/main", v1_read));
    TEST_ASSERT(std::string(v1_read.begin(), v1_read.end()) == "BINARY_VERSION_1_0_0");

    // 2. Inject fault during update at Extracting
    installer.inject_fault_at_state(PackageInstaller::InstallerState::Extracting);
    AppPackage v2 = create_sample_package("com.sample.upcrash", "2.0.0", "BINARY_VERSION_2_0_0");
    auto status2 = installer.update(v2);
    TEST_ASSERT(status2 != InstallStatus::Success);

    // Run recovery
    PackageInstaller recovery_agent(vfs, reg);
    auto rep = recovery_agent.reconcile_and_recover();
    (void)rep;

    // Verify v1 still intact
    std::vector<std::uint8_t> v1_check;
    TEST_ASSERT(vfs->read_file("/Applications/com.sample.upcrash.app/bin/main", v1_check));
    TEST_ASSERT(std::string(v1_check.begin(), v1_check.end()) == "BINARY_VERSION_1_0_0");
    auto reg_check = reg->get_app("com.sample.upcrash");
    TEST_ASSERT(reg_check.has_value());
    TEST_ASSERT(reg_check->version == "1.0.0");

    // Verify staging is clean
    std::vector<VfsDirEntry> staging_entries;
    if (vfs->readdir("/staging", staging_entries)) {
        int cnt = 0;
        for (const auto& e : staging_entries) {
            if (e.name != "." && e.name != "..") cnt++;
        }
        TEST_ASSERT(cnt == 0);
    }
}

static void test_journal_tampering_defense() {
    std::cout << "[Stage 232.13] Testing journal tampering defense and recovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Create a staging entry with a tampered checksum
    vfs->mkdir("/staging");
    vfs->mkdir("/staging/tx_tampered");
    vfs->create_file("/staging/tx_tampered/journal.json");
    std::string tampered_journal = "{\n"
                                  "  \"tx_id\": \"tx_tampered\",\n"
                                  "  \"bundle_id\": \"com.sample.tampered\",\n"
                                  "  \"state\": \"Extracted\",\n"
                                  "  \"is_update\": false,\n"
                                  "  \"target_dir\": \"/Applications/com.sample.tampered.app\",\n"
                                  "  \"backup_dir\": \"\",\n"
                                  "  \"checksum\": \"0000000000000000000000000000000000000000000000000000000000000000\"\n"
                                  "}\n";
    vfs->write_file("/staging/tx_tampered/journal.json", tampered_journal.data(), tampered_journal.size());
    vfs->mkdir("/staging/tx_tampered/bundle");
    vfs->create_file("/staging/tx_tampered/bundle/malicious.bin");

    auto rep = installer.reconcile_and_recover();
    // Tampered entry must be safely discarded as abandoned, never committed
    TEST_ASSERT(rep.abandoned_transactions_cleaned == 1);
    TEST_ASSERT(!vfs->exists("/staging/tx_tampered"));
    TEST_ASSERT(!reg->is_installed("com.sample.tampered"));
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "STAGE 232.13: Crash Injection and Recovery Harness Tests\n";
    std::cout << "========================================================\n";

    test_crash_injection_all_precommit_states();
    test_crash_at_committed_state();
    test_update_crash_restores_backup();
    test_journal_tampering_defense();

    std::cout << "All Stage 232.13 tests passed successfully!\n";
    return 0;
}
