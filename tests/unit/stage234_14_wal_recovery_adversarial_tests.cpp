#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/app_package.h"
#include "crypto/sha256.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

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

// -----------------------------------------------------------------------------
// Test 1: Recovery Idempotence (Repeated Reconcile Runs)
// -----------------------------------------------------------------------------
static void test_recovery_idempotence() {
    std::cout << "[Stage 234.14] Test 1: Testing Recovery Idempotence...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // Simulate an abandoned staging transaction
    vfs->mkdir("/staging");
    vfs->mkdir("/staging/tx_abandoned");
    std::string journal = "{\n  \"tx_id\": \"tx_abandoned\",\n  \"bundle_id\": \"com.test.app\",\n  \"state\": \"Extracting\",\n  \"is_update\": false,\n  \"target_dir\": \"/Applications/com.test.app.app\",\n  \"backup_dir\": \"\",\n  \"checksum\": \"\"\n}\n";
    vfs->create_file("/staging/tx_abandoned/journal.json");
    vfs->write_file("/staging/tx_abandoned/journal.json", journal.data(), journal.size());

    // Run 1: Should clean up the 1 abandoned transaction
    auto rep1 = installer.reconcile_and_recover();
    TEST_ASSERT(rep1.abandoned_transactions_cleaned == 1, "Run 1 cleaned 1 abandoned transaction");
    TEST_ASSERT(rep1.total_recovered() == 1, "Run 1 total recovered == 1");

    // Run 2: Immediately running again must be a NO-OP (idempotent)
    auto rep2 = installer.reconcile_and_recover();
    TEST_ASSERT(rep2.total_recovered() == 0, "Run 2 recovered 0 (idempotent)");

    // Run 3: Third run must also be a NO-OP
    auto rep3 = installer.reconcile_and_recover();
    TEST_ASSERT(rep3.total_recovered() == 0, "Run 3 recovered 0 (idempotent)");

    std::cout << "  -> Recovery idempotence strictly verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Truncated & Corrupt Journal Files
// -----------------------------------------------------------------------------
static void test_truncated_and_corrupt_journals() {
    std::cout << "[Stage 234.14] Test 2: Testing Truncated and Corrupt Journals...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    vfs->mkdir("/staging");

    // 2.1 Severely truncated journal (file truncated mid-field)
    vfs->mkdir("/staging/tx_trunc");
    std::string trunc_json = "{\n  \"tx_id\": \"tx_trunc\",\n  \"bundle_id\": \"com.trunc";
    vfs->create_file("/staging/tx_trunc/journal.json");
    vfs->write_file("/staging/tx_trunc/journal.json", trunc_json.data(), trunc_json.size());

    // 2.2 Corrupted checksum in journal
    vfs->mkdir("/staging/tx_badchk");
    std::string badchk_json = "{\n  \"tx_id\": \"tx_badchk\",\n  \"bundle_id\": \"com.badchk\",\n  \"state\": \"Extracting\",\n  \"is_update\": false,\n  \"target_dir\": \"/Applications/com.badchk.app\",\n  \"backup_dir\": \"\",\n  \"checksum\": \"DEADBEEF00000000000000000000000000000000000000000000000000000000\"\n}\n";
    vfs->create_file("/staging/tx_badchk/journal.json");
    vfs->write_file("/staging/tx_badchk/journal.json", badchk_json.data(), badchk_json.size());

    auto rep = installer.reconcile_and_recover();
    // Both corrupted / truncated journals should be safely detected and cleaned up
    TEST_ASSERT(rep.abandoned_transactions_cleaned == 2, "Both corrupted/truncated journals cleaned up");
    TEST_ASSERT(!vfs->exists("/staging/tx_trunc"), "Truncated tx directory removed");
    TEST_ASSERT(!vfs->exists("/staging/tx_badchk"), "Bad checksum tx directory removed");

    std::cout << "  -> Truncated and bad checksum recovery verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Crash Recovery with Backup Directory Restoration
// -----------------------------------------------------------------------------
static void test_backup_restoration_adversarial() {
    std::cout << "[Stage 234.14] Test 3: Testing Backup Restoration on Update Crash...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    vfs->mkdir("/staging");
    vfs->mkdir("/backup");
    vfs->mkdir("/backup/backup_app");
    vfs->create_file("/backup/backup_app/bin_main");
    const char data[] = "ORIGINAL_APP_PAYLOAD";
    vfs->write_file("/backup/backup_app/bin_main", data, sizeof(data));

    // Staging journal pointing to backup during an update crash
    vfs->mkdir("/staging/tx_update");
    std::string tx_id = "tx_update";
    std::string bundle_id = "com.update.app";
    std::string state_str = "Extracting";
    bool is_update = true;
    std::string target_dir = "/Applications/com.update.app.app";
    std::string backup_dir = "/backup/backup_app";

    std::string digest_input = tx_id + "|" + bundle_id + "|" + state_str + "|" +
                               (is_update ? "1" : "0") + "|" + target_dir + "|" + backup_dir;
    std::string chk = crypto::Sha256::hash_hex(
        reinterpret_cast<const std::uint8_t*>(digest_input.data()), digest_input.size());

    std::string journal = "{\n  \"tx_id\": \"" + tx_id + "\",\n  \"bundle_id\": \"" + bundle_id + "\",\n  \"state\": \"" + state_str + "\",\n  \"is_update\": true,\n  \"target_dir\": \"" + target_dir + "\",\n  \"backup_dir\": \"" + backup_dir + "\",\n  \"checksum\": \"" + chk + "\"\n}\n";
    vfs->create_file("/staging/tx_update/journal.json");
    vfs->write_file("/staging/tx_update/journal.json", journal.data(), journal.size());

    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.backups_restored == 1, "Backup was restored to target directory");
    TEST_ASSERT(vfs->exists(target_dir), "Target directory restored");
    TEST_ASSERT(vfs->exists(target_dir + "/bin_main"), "Restored target directory contains original app payload");
    TEST_ASSERT(!vfs->exists("/staging/tx_update"), "Staging transaction cleaned up");

    std::cout << "  -> Backup restoration verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Registry and Filesystem Out-of-Sync Reconciliation
// -----------------------------------------------------------------------------
static void test_registry_filesystem_reconciliation() {
    std::cout << "[Stage 234.14] Test 4: Testing Registry / Filesystem Desync Reconciliation...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto reg = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, reg);

    // 4.1 Missing filesystem payload for registered app -> flagged Corrupted
    AppRegistration ghost_app{};
    ghost_app.bundle_id = "com.ghost.app";
    ghost_app.name = "Ghost";
    ghost_app.exec_path = "bin/main";
    ghost_app.install_dir = "/Applications/com.ghost.app.app";
    ghost_app.is_active = true;
    ghost_app.state = InstallationState::Installed;
    bool reg_ok = reg->register_app(ghost_app);
    TEST_ASSERT(reg_ok, "Ghost app registered successfully");

    // 4.2 Unindexed app on filesystem -> discovered & registered
    vfs->mkdir("/Applications");
    vfs->mkdir("/Applications/com.discovered.app.app");
    vfs->create_file("/Applications/com.discovered.app.app/bin_main");
    vfs->create_file("/Applications/com.discovered.app.app/info_plist");

    auto rep = installer.reconcile_and_recover();
    TEST_ASSERT(rep.corrupted_apps_flagged == 1, "Ghost app flagged as corrupted");
    TEST_ASSERT(!reg->is_active("com.ghost.app"), "Ghost app deactivated");
    TEST_ASSERT(reg->get_installation_state("com.ghost.app") == InstallationState::Corrupted, "Ghost app state Corrupted");

    TEST_ASSERT(rep.unindexed_apps_discovered == 1, "Unindexed filesystem app discovered");
    TEST_ASSERT(reg->is_installed("com.discovered.app"), "Discovered app registered in registry");
    TEST_ASSERT(reg->is_active("com.discovered.app"), "Discovered app activated");

    std::cout << "  -> Registry / filesystem desync reconciliation verified.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.14: WAL Recovery Adversarial Test Suite              \n";
    std::cout << "=================================================================\n";

    test_recovery_idempotence();
    test_truncated_and_corrupt_journals();
    test_backup_restoration_adversarial();
    test_registry_filesystem_reconciliation();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL WAL RECOVERY ADVERSARIAL TESTS PASSED!                     \n";
    std::cout << "=================================================================\n";
    return 0;
}
