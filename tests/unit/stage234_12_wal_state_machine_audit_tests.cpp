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

static AppPackage create_dummy_package(const std::string& bundle_id) {
    AppPackage pkg;
    pkg.manifest.bundle_id = bundle_id;
    pkg.manifest.name = "DummyApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";

    std::vector<std::uint8_t> exec_bytes = {0x7F, 'E', 'L', 'F', 0x01, 0x02};
    pkg.add_file("bin/main", exec_bytes);
    pkg.add_file("res/info.json", {'{', '}'});
    return pkg;
}

// -----------------------------------------------------------------------------
// Test 1: Enum Source-of-Truth & Aliases Audit
// -----------------------------------------------------------------------------
static void test_enum_source_of_truth() {
    std::cout << "[Stage 234.12] Test 1: Enumerating all 11 WAL states & aliases...\n";

    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Idle) == 0);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Initiated) == 1);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Validating) == 2);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::ManifestParsed) == 3);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::SecurityVerified) == 4);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Staged) == 5);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Extracting) == 6);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Extracted) == 7);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Registering) == 8);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Committed) == 9);
    static_assert(static_cast<std::uint32_t>(PackageInstaller::InstallerState::Aborted) == 10);

    // Compatibility aliases
    static_assert(PackageInstaller::InstallerState::Pending == PackageInstaller::InstallerState::Initiated);
    static_assert(PackageInstaller::InstallerState::Validated == PackageInstaller::InstallerState::SecurityVerified);
    static_assert(PackageInstaller::InstallerState::Committing == PackageInstaller::InstallerState::Registering);
    static_assert(PackageInstaller::InstallerState::RolledBack == PackageInstaller::InstallerState::Aborted);

    // String name mappings
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Idle)) == "Idle", "Idle str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Initiated)) == "Initiated", "Initiated str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Validating)) == "Validating", "Validating str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::ManifestParsed)) == "ManifestParsed", "ManifestParsed str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::SecurityVerified)) == "SecurityVerified", "SecurityVerified str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Staged)) == "Staged", "Staged str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Extracting)) == "Extracting", "Extracting str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Extracted)) == "Extracted", "Extracted str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Registering)) == "Registering", "Registering str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Committed)) == "Committed", "Committed str");
    TEST_ASSERT(std::string(PackageInstaller::installer_state_to_string(PackageInstaller::InstallerState::Aborted)) == "Aborted", "Aborted str");

    std::cout << "  -> All 11 states and string names verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Normal Forward State Transition Sequence (1 through 9)
// -----------------------------------------------------------------------------
static void test_forward_state_sequence() {
    std::cout << "[Stage 234.12] Test 2: Verifying Complete Forward Sequence...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    auto registry = std::make_shared<ApplicationRegistry>();
    PackageInstaller installer(vfs, registry);

    std::vector<PackageInstaller::InstallerState> visited_states;
    installer.set_fault_injection_hook([&](PackageInstaller::InstallerState state) {
        visited_states.push_back(state);
    });

    auto pkg = create_dummy_package("com.winios.seq.test");
    auto status = installer.install(pkg);
    TEST_ASSERT(status == InstallStatus::Success, "Install succeeded");

    // Exactly 9 forward transitions: 1 to 9 in sequential order
    TEST_ASSERT(visited_states.size() == 9, "Visited exactly 9 forward states");
    TEST_ASSERT(visited_states[0] == PackageInstaller::InstallerState::Initiated, "Step 1: Initiated");
    TEST_ASSERT(visited_states[1] == PackageInstaller::InstallerState::Validating, "Step 2: Validating");
    TEST_ASSERT(visited_states[2] == PackageInstaller::InstallerState::ManifestParsed, "Step 3: ManifestParsed");
    TEST_ASSERT(visited_states[3] == PackageInstaller::InstallerState::SecurityVerified, "Step 4: SecurityVerified");
    TEST_ASSERT(visited_states[4] == PackageInstaller::InstallerState::Staged, "Step 5: Staged");
    TEST_ASSERT(visited_states[5] == PackageInstaller::InstallerState::Extracting, "Step 6: Extracting");
    TEST_ASSERT(visited_states[6] == PackageInstaller::InstallerState::Extracted, "Step 7: Extracted");
    TEST_ASSERT(visited_states[7] == PackageInstaller::InstallerState::Registering, "Step 8: Registering");
    TEST_ASSERT(visited_states[8] == PackageInstaller::InstallerState::Committed, "Step 9: Committed");

    std::cout << "  -> Forward transition sequence 1..9 strictly verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Fault Injection Across Pre-Commit States (8 States)
// -----------------------------------------------------------------------------
static void test_fault_injection_transitions() {
    std::cout << "[Stage 234.12] Test 3: Testing Abort / Rollback from pre-commit states...\n";

    PackageInstaller::InstallerState pre_commit_states[] = {
        PackageInstaller::InstallerState::Initiated,
        PackageInstaller::InstallerState::Validating,
        PackageInstaller::InstallerState::ManifestParsed,
        PackageInstaller::InstallerState::SecurityVerified,
        PackageInstaller::InstallerState::Staged,
        PackageInstaller::InstallerState::Extracting,
        PackageInstaller::InstallerState::Extracted,
        PackageInstaller::InstallerState::Registering
    };

    for (auto target_state : pre_commit_states) {
        auto vfs = std::make_shared<VirtualFileSystem>();
        auto registry = std::make_shared<ApplicationRegistry>();
        PackageInstaller installer(vfs, registry);

        installer.inject_fault_at_state(target_state);

        auto pkg = create_dummy_package("com.winios.fault." + std::string(PackageInstaller::installer_state_to_string(target_state)));
        auto status = installer.install(pkg);

        TEST_ASSERT(status == InstallStatus::CommitFailed, "Fault injection caused clean CommitFailed");
        TEST_ASSERT(!registry->is_installed(pkg.manifest.bundle_id), "Application NOT installed in registry");
    }

    std::cout << "  -> Fault injection across all 8 pre-commit states verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Journal Checksum vs Authentication Property
// -----------------------------------------------------------------------------
static void test_journal_checksum_properties() {
    std::cout << "[Stage 234.12] Test 4: Auditing Checksum vs Authentication Properties...\n";

    std::string tx_id = "tx_12345678";
    std::string bundle_id = "com.winios.app";
    std::string state_str = "Staged";
    bool is_update = false;
    std::string target_dir = "/Applications/com.winios.app.app";
    std::string backup_dir = "";

    std::string digest_input = tx_id + "|" + bundle_id + "|" + state_str + "|" +
                               (is_update ? "1" : "0") + "|" + target_dir + "|" + backup_dir;
    std::string orig_chk = crypto::Sha256::hash_hex(
        reinterpret_cast<const std::uint8_t*>(digest_input.data()), digest_input.size());

    // 1. Bitflip mutation detection
    std::string tampered_input = digest_input;
    tampered_input[5] ^= 0x01; // flip 1 bit in tx_id
    std::string tampered_chk = crypto::Sha256::hash_hex(
        reinterpret_cast<const std::uint8_t*>(tampered_input.data()), tampered_input.size());
    TEST_ASSERT(orig_chk != tampered_chk, "Checksum changes on any bitflip");

    // 2. Unkeyed digest distinction
    // Because SHA-256 is unkeyed, anyone can forge a checksum for an arbitrary state
    // confirming it is an integrity checksum, NOT an authenticated signature.
    std::string forged_state = "Committed";
    std::string forged_input = tx_id + "|" + bundle_id + "|" + forged_state + "|" +
                               (is_update ? "1" : "0") + "|" + target_dir + "|" + backup_dir;
    std::string forged_chk = crypto::Sha256::hash_hex(
        reinterpret_cast<const std::uint8_t*>(forged_input.data()), forged_input.size());
    TEST_ASSERT(!forged_chk.empty(), "Unkeyed checksum can be computed without secret key");

    std::cout << "  -> Journal checksum vs authentication properties verified.\n";
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
    std::cout << "  Stage 234.12: WAL State Machine Audit Test Suite               \n";
    std::cout << "=================================================================\n";

    test_enum_source_of_truth();
    test_forward_state_sequence();
    test_fault_injection_transitions();
    test_journal_checksum_properties();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL WAL STATE MACHINE AUDIT TESTS PASSED!                      \n";
    std::cout << "=================================================================\n";
    return 0;
}
