#include <iostream>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "security/developer_keystore.h"
#include "security/publisher_trust_store.h"

using namespace winios::security;

namespace fs = std::filesystem;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

static void test_generate_identity() {
    auto id = DeveloperKeyStore::generate_identity(
        "com.acme.labs",
        "Acme Labs Inc.",
        "KEY-ACME-001",
        PublisherRole::Developer,
        1700000000,
        1800000000
    );

    TEST_ASSERT(id.is_valid());
    TEST_ASSERT(id.publisher_id == "com.acme.labs");
    TEST_ASSERT(id.organization_name == "Acme Labs Inc.");
    TEST_ASSERT(id.key_id == "KEY-ACME-001");
    TEST_ASSERT(id.role == PublisherRole::Developer);
    TEST_ASSERT(id.algorithm == SignatureAlgorithm::Ed25519);
    TEST_ASSERT(id.valid_from == 1700000000);
    TEST_ASSERT(id.valid_until == 1800000000);
    TEST_ASSERT(id.private_key_seed.size() == 32);
    TEST_ASSERT(id.public_key.size() == 32);

    // Verify public key is non-zero
    bool all_zero = true;
    for (auto b : id.public_key) {
        if (b != 0) { all_zero = false; break; }
    }
    TEST_ASSERT(!all_zero);

    std::cout << "[PASS] test_generate_identity\n";
}

static void test_wipe_private_key() {
    auto id = DeveloperKeyStore::generate_identity(
        "com.wipe.test", "Wipe Org", "KEY-WIPE-001"
    );
    TEST_ASSERT(id.private_key_seed.size() == 32);
    id.wipe_private_key();
    TEST_ASSERT(id.private_key_seed.empty());
    TEST_ASSERT(!id.is_valid());
    std::cout << "[PASS] test_wipe_private_key\n";
}

static void test_export_public_key_and_safety() {
    auto id = DeveloperKeyStore::generate_identity(
        "com.acme.security", "Acme Security", "KEY-SEC-001"
    );

    PublisherKey pub = DeveloperKeyStore::export_public_key(id);
    TEST_ASSERT(pub.publisher_id == id.publisher_id);
    TEST_ASSERT(pub.organization_name == id.organization_name);
    TEST_ASSERT(pub.key_id == id.key_id);
    TEST_ASSERT(pub.role == id.role);
    TEST_ASSERT(pub.algorithm == SignatureAlgorithm::Ed25519);
    TEST_ASSERT(pub.key_bytes.size() == 32);
    TEST_ASSERT(pub.key_bytes == id.public_key);

    // Ensure pub key does NOT match private key seed
    TEST_ASSERT(pub.key_bytes != id.private_key_seed);

    // Test text export
    std::string text = DeveloperKeyStore::export_public_key_text(id);
    TEST_ASSERT(text.find("publisher_id=com.acme.security") != std::string::npos);
    TEST_ASSERT(text.find("key_id=KEY-SEC-001") != std::string::npos);
    TEST_ASSERT(text.find("algorithm=Ed25519") != std::string::npos);

    // Verify it deserializes cleanly into PublisherTrustStore
    PublisherTrustStore store;
    bool ok = store.deserialize(text);
    TEST_ASSERT(ok);
    TEST_ASSERT(store.total_keys() == 1);
    auto lookup = store.get_key("KEY-SEC-001");
    TEST_ASSERT(lookup.has_value());
    TEST_ASSERT(lookup->publisher_id == "com.acme.security");
    TEST_ASSERT(lookup->key_bytes == id.public_key);

    std::cout << "[PASS] test_export_public_key_and_safety\n";
}

static void test_dpapi_protect_unprotect() {
    std::vector<std::uint8_t> secret = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    std::vector<std::uint8_t> encrypted;
    bool ok_enc = DeveloperKeyStore::dpapi_protect(secret, encrypted);
    TEST_ASSERT(ok_enc);
    TEST_ASSERT(!encrypted.empty());
    TEST_ASSERT(encrypted != secret);

    std::vector<std::uint8_t> decrypted;
    bool ok_dec = DeveloperKeyStore::dpapi_unprotect(encrypted, decrypted);
    TEST_ASSERT(ok_dec);
    TEST_ASSERT(decrypted == secret);

    std::cout << "[PASS] test_dpapi_protect_unprotect\n";
}

static void test_passphrase_encryption_roundtrip() {
    std::vector<std::uint8_t> secret = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
    std::string pass = "CorrectHorseBatteryStaple2026!";
    std::vector<std::uint8_t> salt(16, 0x5A);

    std::vector<std::uint8_t> cipher;
    std::vector<std::uint8_t> tag;

    bool ok_enc = DeveloperKeyStore::encrypt_with_passphrase(secret, pass, salt, cipher, tag);
    TEST_ASSERT(ok_enc);
    TEST_ASSERT(cipher.size() == secret.size());
    TEST_ASSERT(cipher != secret);
    TEST_ASSERT(tag.size() == 32);

    // Decrypt with correct passphrase
    std::vector<std::uint8_t> plain;
    bool ok_dec = DeveloperKeyStore::decrypt_with_passphrase(cipher, pass, salt, tag, plain);
    TEST_ASSERT(ok_dec);
    TEST_ASSERT(plain == secret);

    // Decrypt with wrong passphrase must FAIL
    std::vector<std::uint8_t> fail_plain;
    bool ok_wrong = DeveloperKeyStore::decrypt_with_passphrase(cipher, "WrongPassword123!", salt, tag, fail_plain);
    TEST_ASSERT(!ok_wrong);

    // Tampered tag must FAIL
    tag[0] ^= 0xFF;
    bool ok_tag = DeveloperKeyStore::decrypt_with_passphrase(cipher, pass, salt, tag, fail_plain);
    TEST_ASSERT(!ok_tag);

    std::cout << "[PASS] test_passphrase_encryption_roundtrip\n";
}

static void test_keystore_file_storage() {
    fs::path temp_dir = fs::temp_directory_path() / "winios_stage232_5_test";
    fs::create_directories(temp_dir);

    auto id = DeveloperKeyStore::generate_identity(
        "com.test.storage", "Storage Test Org", "KEY-STORE-001",
        PublisherRole::Developer, 100, 200
    );

    // 1. DPAPI storage test
    fs::path dpapi_path = temp_dir / "test_dpapi.devkey";
    bool ok_save_dpapi = DeveloperKeyStore::save_to_file(dpapi_path.string(), id, KeystoreProtection::DPAPI);
    TEST_ASSERT(ok_save_dpapi);
    TEST_ASSERT(fs::exists(dpapi_path));

    auto loaded_dpapi = DeveloperKeyStore::load_from_file(dpapi_path.string());
    TEST_ASSERT(loaded_dpapi.has_value());
    TEST_ASSERT(loaded_dpapi->publisher_id == id.publisher_id);
    TEST_ASSERT(loaded_dpapi->key_id == id.key_id);
    TEST_ASSERT(loaded_dpapi->public_key == id.public_key);
    TEST_ASSERT(loaded_dpapi->private_key_seed == id.private_key_seed);

    // 2. Passphrase storage test
    fs::path pass_path = temp_dir / "test_pass.devkey";
    std::string pass = "MySecretDevPassword!";
    bool ok_save_pass = DeveloperKeyStore::save_to_file(pass_path.string(), id, KeystoreProtection::Passphrase, pass);
    TEST_ASSERT(ok_save_pass);

    // Load with correct passphrase
    auto loaded_pass = DeveloperKeyStore::load_from_file(pass_path.string(), pass);
    TEST_ASSERT(loaded_pass.has_value());
    TEST_ASSERT(loaded_pass->public_key == id.public_key);
    TEST_ASSERT(loaded_pass->private_key_seed == id.private_key_seed);

    // Load with incorrect passphrase
    auto loaded_wrong = DeveloperKeyStore::load_from_file(pass_path.string(), "WrongPassphrase");
    TEST_ASSERT(!loaded_wrong.has_value());

    // 3. Tamper detection test
    fs::path tamper_path = temp_dir / "test_tamper.devkey";
    DeveloperKeyStore::save_to_file(tamper_path.string(), id, KeystoreProtection::DPAPI);

    // Corrupt one byte of content
    std::string data;
    {
        std::ifstream ifs(tamper_path, std::ios::binary);
        data.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    // Flip a byte in the middle of publisher_id
    auto pos = data.find("com.test.storage");
    TEST_ASSERT(pos != std::string::npos);
    data[pos] = 'X';
    {
        std::ofstream ofs(tamper_path, std::ios::binary);
        ofs.write(data.data(), data.size());
    }

    auto loaded_tamper = DeveloperKeyStore::load_from_file(tamper_path.string());
    TEST_ASSERT(!loaded_tamper.has_value()); // Must reject tampered file

    // 4. Public key save test
    fs::path pub_path = temp_dir / "test.pub";
    bool ok_pub = DeveloperKeyStore::save_public_key_file(pub_path.string(), id);
    TEST_ASSERT(ok_pub);
    TEST_ASSERT(fs::exists(pub_path));

    std::string pub_content;
    {
        std::ifstream ifs(pub_path, std::ios::binary);
        pub_content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    // Ensure public key file does NOT leak private seed
    TEST_ASSERT(pub_content.find("private") == std::string::npos);
    TEST_ASSERT(pub_content.find("seed") == std::string::npos);

    fs::remove_all(temp_dir);
    std::cout << "[PASS] test_keystore_file_storage\n";
}

int main() {
    std::cout << "=== WinIOS Stage 232.5 Key Generation & Secure Storage Tests ===\n";
    test_generate_identity();
    test_wipe_private_key();
    test_export_public_key_and_safety();
    test_dpapi_protect_unprotect();
    test_passphrase_encryption_roundtrip();
    test_keystore_file_storage();
    std::cout << "All Stage 232.5 tests passed successfully!\n";
    return 0;
}
