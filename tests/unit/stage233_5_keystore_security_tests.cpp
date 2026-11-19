#include "security/developer_keystore.h"
#include "security/security_utils.h"
#include "crypto/ed25519.h"
#include "crypto/sha256.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::security;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Test 1: Identity Generation & Export
// -----------------------------------------------------------------------------
static void test_identity_generation_and_export() {
    std::cout << "[Test 1] Developer Identity Generation & Public Key Export...\n";

    auto id = DeveloperKeyStore::generate_identity(
        "com.example.research",
        "Example Research Lab",
        "key-2026-alpha",
        PublisherRole::Developer,
        1700000000,
        1800000000
    );

    TEST_ASSERT(id.is_valid(), "Identity is valid");
    TEST_ASSERT(id.publisher_id == "com.example.research", "Publisher ID matches");
    TEST_ASSERT(id.organization_name == "Example Research Lab", "Org name matches");
    TEST_ASSERT(id.key_id == "key-2026-alpha", "Key ID matches");
    TEST_ASSERT(id.role == PublisherRole::Developer, "Role is Developer");
    TEST_ASSERT(id.private_key_seed.size() == 32, "Private seed is 32 bytes");
    TEST_ASSERT(id.public_key.size() == 32, "Public key is 32 bytes");

    // Export public key
    auto pub = DeveloperKeyStore::export_public_key(id);
    TEST_ASSERT(pub.publisher_id == id.publisher_id, "Pub key publisher ID");
    TEST_ASSERT(pub.key_bytes == id.public_key, "Pub key bytes match");
    TEST_ASSERT(!pub.is_revoked, "Not revoked by default");

    // Wipe test
    id.wipe_private_key();
    TEST_ASSERT(id.private_key_seed.empty(), "Private seed empty after wipe");
    TEST_ASSERT(!id.is_valid(), "Identity invalid after wipe");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Protection Mode None (Unencrypted)
// -----------------------------------------------------------------------------
static void test_keystore_protection_none() {
    std::cout << "[Test 2] Keystore Protection: None...\n";

    fs::path temp_file = fs::temp_directory_path() / "test_keystore_none.devkey";

    auto id = DeveloperKeyStore::generate_identity(
        "com.winios.test.none",
        "Test Lab",
        "key-none-01"
    );

    TEST_ASSERT(DeveloperKeyStore::save_to_file(temp_file.string(), id, KeystoreProtection::None),
                "Save with protection None");

    auto loaded = DeveloperKeyStore::load_from_file(temp_file.string());
    TEST_ASSERT(loaded.has_value(), "Load keystore None");
    TEST_ASSERT(loaded->publisher_id == id.publisher_id, "Loaded publisher ID");
    TEST_ASSERT(loaded->public_key == id.public_key, "Loaded public key");
    TEST_ASSERT(loaded->private_key_seed == id.private_key_seed, "Loaded private seed");

    fs::remove(temp_file);
    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Protection Mode Passphrase (PBKDF2 + HMAC-SHA256 Authenticated Stream)
// -----------------------------------------------------------------------------
static void test_keystore_protection_passphrase() {
    std::cout << "[Test 3] Keystore Protection: Passphrase...\n";

    fs::path temp_file = fs::temp_directory_path() / "test_keystore_pass.devkey";
    std::string passphrase = "CorrectHorseBatteryStaple2026!";

    auto id = DeveloperKeyStore::generate_identity(
        "com.winios.test.pass",
        "Passphrase Org",
        "key-pass-01"
    );

    // Save with passphrase
    TEST_ASSERT(DeveloperKeyStore::save_to_file(temp_file.string(), id,
                                               KeystoreProtection::Passphrase, passphrase),
                "Save with Passphrase");

    // Load with correct passphrase
    auto loaded = DeveloperKeyStore::load_from_file(temp_file.string(), passphrase);
    TEST_ASSERT(loaded.has_value(), "Load keystore with correct passphrase");
    TEST_ASSERT(loaded->private_key_seed == id.private_key_seed, "Decrypted seed matches");
    TEST_ASSERT(loaded->public_key == id.public_key, "Public key matches");

    // Load with wrong passphrase
    auto loaded_wrong = DeveloperKeyStore::load_from_file(temp_file.string(), "WrongPassphrase123!");
    TEST_ASSERT(!loaded_wrong.has_value(), "Load keystore with wrong passphrase must fail");

    // Load with empty passphrase
    auto loaded_empty = DeveloperKeyStore::load_from_file(temp_file.string(), "");
    TEST_ASSERT(!loaded_empty.has_value(), "Load keystore with empty passphrase must fail");

    fs::remove(temp_file);
    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Protection Mode DPAPI (Windows DPAPI CryptProtectData)
// -----------------------------------------------------------------------------
static void test_keystore_protection_dpapi() {
    std::cout << "[Test 4] Keystore Protection: DPAPI...\n";

    fs::path temp_file = fs::temp_directory_path() / "test_keystore_dpapi.devkey";

    auto id = DeveloperKeyStore::generate_identity(
        "com.winios.test.dpapi",
        "DPAPI Org",
        "key-dpapi-01"
    );

    TEST_ASSERT(DeveloperKeyStore::save_to_file(temp_file.string(), id, KeystoreProtection::DPAPI),
                "Save with DPAPI");

    auto loaded = DeveloperKeyStore::load_from_file(temp_file.string());
    TEST_ASSERT(loaded.has_value(), "Load keystore with DPAPI");
    TEST_ASSERT(loaded->private_key_seed == id.private_key_seed, "DPAPI decrypted seed matches");
    TEST_ASSERT(loaded->public_key == id.public_key, "Public key matches");

    fs::remove(temp_file);
    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Protection Mode DPAPI + Passphrase (Dual-Layer Defense)
// -----------------------------------------------------------------------------
static void test_keystore_protection_dpapi_and_passphrase() {
    std::cout << "[Test 5] Keystore Protection: DPAPI + Passphrase...\n";

    fs::path temp_file = fs::temp_directory_path() / "test_keystore_dual.devkey";
    std::string passphrase = "DualLayerKeyProtectionPassphrase987#";

    auto id = DeveloperKeyStore::generate_identity(
        "com.winios.test.dual",
        "Dual Security Org",
        "key-dual-01"
    );

    TEST_ASSERT(DeveloperKeyStore::save_to_file(temp_file.string(), id,
                                               KeystoreProtection::DPAPIAndPassphrase, passphrase),
                "Save with DPAPI + Passphrase");

    // Load with correct passphrase
    auto loaded = DeveloperKeyStore::load_from_file(temp_file.string(), passphrase);
    TEST_ASSERT(loaded.has_value(), "Load dual-protected keystore with correct passphrase");
    TEST_ASSERT(loaded->private_key_seed == id.private_key_seed, "Dual-protected seed matches");

    // Load with wrong passphrase
    auto loaded_wrong = DeveloperKeyStore::load_from_file(temp_file.string(), "IncorrectDualPass!");
    TEST_ASSERT(!loaded_wrong.has_value(), "Dual-protected load with wrong passphrase must fail");

    fs::remove(temp_file);
    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Adversarial Tampering & Corruption Tests
// -----------------------------------------------------------------------------
static void test_keystore_adversarial_tampering() {
    std::cout << "[Test 6] Keystore Adversarial Tampering & Corruption Tests...\n";

    fs::path temp_file = fs::temp_directory_path() / "test_keystore_tamper.devkey";
    std::string passphrase = "SecureTamperTestPassphrase!";

    auto id = DeveloperKeyStore::generate_identity(
        "com.winios.tamper",
        "Tamper Detection Lab",
        "key-tamper-01"
    );

    TEST_ASSERT(DeveloperKeyStore::save_to_file(temp_file.string(), id,
                                               KeystoreProtection::Passphrase, passphrase),
                "Save original keystore");

    // Read full file content
    std::ifstream ifs(temp_file.string(), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // 1. File Truncation Attack
    {
        fs::path trunc_file = fs::temp_directory_path() / "trunc.devkey";
        std::ofstream ofs(trunc_file.string(), std::ios::binary);
        ofs.write(content.data(), static_cast<std::streamsize>(content.size() / 2));
        ofs.close();

        auto res = DeveloperKeyStore::load_from_file(trunc_file.string(), passphrase);
        TEST_ASSERT(!res.has_value(), "Truncated keystore must fail to load");
        fs::remove(trunc_file);
    }

    // 2. Checksum Tamper Attack: Modify metadata (e.g. role) without updating checksum
    {
        fs::path mod_file = fs::temp_directory_path() / "mod_meta.devkey";
        std::string mod_content = content;
        auto pos = mod_content.find("role=Developer");
        TEST_ASSERT(pos != std::string::npos, "Find role in keystore");
        mod_content.replace(pos, 14, "role=Platform "); // tamper role to Platform!

        std::ofstream ofs(mod_file.string(), std::ios::binary);
        ofs.write(mod_content.data(), static_cast<std::streamsize>(mod_content.size()));
        ofs.close();

        auto res = DeveloperKeyStore::load_from_file(mod_file.string(), passphrase);
        TEST_ASSERT(!res.has_value(), "Tampered metadata without checksum update must fail");
        fs::remove(mod_file);
    }

    // 3. MAC Tag Corruption Attack
    {
        fs::path tag_file = fs::temp_directory_path() / "bad_tag.devkey";
        std::string tag_content = content;
        auto pos = tag_content.find("tag_hex=");
        TEST_ASSERT(pos != std::string::npos, "Find tag_hex");
        // Flip one nibble in tag_hex and recompute checksum so checksum passes but MAC fails
        tag_content[pos + 10] = (tag_content[pos + 10] == 'a') ? 'b' : 'a';

        // Recompute checksum_hex over modified text
        auto chk_pos = tag_content.rfind("checksum_hex=");
        std::string text_for_chk = tag_content.substr(0, chk_pos);
        std::uint8_t new_chk[32];
        crypto::Sha256 sha;
        sha.update(text_for_chk.data(), text_for_chk.size());
        sha.finalize(new_chk);
        std::string new_chk_hex = crypto::Ed25519::to_hex(new_chk, 32);
        tag_content = text_for_chk + "checksum_hex=" + new_chk_hex + "\n";

        std::ofstream ofs(tag_file.string(), std::ios::binary);
        ofs.write(tag_content.data(), static_cast<std::streamsize>(tag_content.size()));
        ofs.close();

        auto res = DeveloperKeyStore::load_from_file(tag_file.string(), passphrase);
        TEST_ASSERT(!res.has_value(), "Corrupted MAC tag must fail decryption/authentication");
        fs::remove(tag_file);
    }

    // 4. Nonexistent File
    {
        auto res = DeveloperKeyStore::load_from_file("C:/nonexistent_path/keys/does_not_exist.devkey");
        TEST_ASSERT(!res.has_value(), "Nonexistent file must return nullopt");
    }

    fs::remove(temp_file);
    std::cout << "  -> Passed.\n";
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

    std::cout << "=== Stage 233.5: Keystore Security Tests ===\n";

    test_identity_generation_and_export();
    test_keystore_protection_none();
    test_keystore_protection_passphrase();
    test_keystore_protection_dpapi();
    test_keystore_protection_dpapi_and_passphrase();
    test_keystore_adversarial_tampering();

    std::cout << "=== All Stage 233.5 Keystore Security Tests PASSED ===\n";
    return 0;
}
