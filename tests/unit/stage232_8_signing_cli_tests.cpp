#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "tools/winios_app_cli.h"

using namespace winios::tools;
namespace fs = std::filesystem;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

static void test_cli_full_workflow() {
    fs::path temp_dir = fs::temp_directory_path() / "winios_stage232_8_test";
    fs::create_directories(temp_dir);

    WiniosAppCli cli;
    std::ostringstream out;
    std::ostringstream err;

    fs::path devkey_path = temp_dir / "dev.devkey";
    fs::path pub_path = temp_dir / "dev.pub";
    fs::path ts_path = temp_dir / "trust_store.ini";
    fs::path raw_pkg_path = temp_dir / "raw.wiospkg";
    fs::path signed_pkg_path = temp_dir / "signed.wiospkg";

    // Create raw package
    {
        std::vector<uint8_t> data(128, 0x55);
        std::memcpy(data.data(), "WIOSPKG3", 8);
        std::ofstream ofs(raw_pkg_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    // 1. keygen
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "keygen",
            "--publisher-id", "com.cli.developer",
            "--org", "CLI Labs",
            "--key-id", "KEY-CLI-01",
            "--out", devkey_path.string(),
            "--protection", "none",
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(fs::exists(devkey_path));
        TEST_ASSERT(out.str().find("\"status\":\"ok\"") != std::string::npos);
        std::cout << "[PASS] CLI keygen\n";
    }

    // 2. key-export-pub
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "key-export-pub",
            "--key", devkey_path.string(),
            "--out", pub_path.string(),
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(fs::exists(pub_path));
        TEST_ASSERT(out.str().find("\"status\":\"ok\"") != std::string::npos);
        std::cout << "[PASS] CLI key-export-pub\n";
    }

    // 3. trust-add-key
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "trust-add-key",
            "--trust-store", ts_path.string(),
            "--key-file", pub_path.string(),
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(fs::exists(ts_path));
        TEST_ASSERT(out.str().find("\"total_keys\":1") != std::string::npos);
        std::cout << "[PASS] CLI trust-add-key\n";
    }

    // 4. app-sign
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "app-sign",
            "--pkg", raw_pkg_path.string(),
            "--key", devkey_path.string(),
            "--out", signed_pkg_path.string(),
            "--flags", "7",
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(fs::exists(signed_pkg_path));
        TEST_ASSERT(out.str().find("\"trailer_version\":2") != std::string::npos);
        std::cout << "[PASS] CLI app-sign\n";
    }

    // 5. app-verify (Dev policy -> Pass)
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "app-verify",
            "--pkg", signed_pkg_path.string(),
            "--trust-store", ts_path.string(),
            "--policy", "dev",
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(out.str().find("\"trusted\":true") != std::string::npos);
        TEST_ASSERT(out.str().find("\"trailer_version\":2") != std::string::npos);
        std::cout << "[PASS] CLI app-verify (dev policy)\n";
    }

    // 6. app-verify (Prod policy -> Fail)
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "app-verify",
            "--pkg", signed_pkg_path.string(),
            "--trust-store", ts_path.string(),
            "--policy", "prod",
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::ExecutionFailure);
        TEST_ASSERT(out.str().find("\"trusted\":false") != std::string::npos);
        std::cout << "[PASS] CLI app-verify (prod policy rejected dev key)\n";
    }

    // 7. trust-revoke
    {
        out.str(""); err.str("");
        std::vector<std::string> args = {
            "trust-revoke",
            "--trust-store", ts_path.string(),
            "--publisher-id", "com.cli.developer",
            "--json"
        };
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::Success);
        TEST_ASSERT(out.str().find("\"status\":\"ok\"") != std::string::npos);

        // Verify now fails under Dev policy due to revocation
        out.str(""); err.str("");
        std::vector<std::string> verify_args = {
            "app-verify",
            "--pkg", signed_pkg_path.string(),
            "--trust-store", ts_path.string(),
            "--policy", "dev",
            "--json"
        };
        int v_rc = cli.execute(verify_args, out, err);
        TEST_ASSERT(v_rc == CliExitCode::ExecutionFailure);
        TEST_ASSERT(out.str().find("RevokedPublisher") != std::string::npos);
        std::cout << "[PASS] CLI trust-revoke enforcement\n";
    }

    // 8. Invalid args tests
    {
        out.str(""); err.str("");
        std::vector<std::string> args = { "keygen" }; // Missing required args
        int rc = cli.execute(args, out, err);
        TEST_ASSERT(rc == CliExitCode::InvalidArgs);

        out.str(""); err.str("");
        std::vector<std::string> args2 = { "app-verify" }; // Missing pkg
        int rc2 = cli.execute(args2, out, err);
        TEST_ASSERT(rc2 == CliExitCode::InvalidArgs);
        std::cout << "[PASS] CLI invalid args validation\n";
    }

    fs::remove_all(temp_dir);
}

int main() {
    std::cout << "=== WinIOS Stage 232.8 Package Signing & Verification CLI Tests ===\n";
    test_cli_full_workflow();
    std::cout << "All Stage 232.8 tests passed successfully!\n";
    return 0;
}
