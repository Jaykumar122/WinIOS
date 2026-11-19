#include "security/developer_keystore.h"
#include "security/publisher_trust_store.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::security;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_keystore_threat_boundaries() {
    std::cout << "[Stage 234.6] Test 1: Inaccessible, Corrupted & Substituted Keystore Negative Tests...\n";

    const std::string test_dir = "build/test_keystore_threat";
    std::filesystem::create_directories(test_dir);

    // Case 1a: Non-existent file
    {
        auto res = DeveloperKeyStore::load_from_file(test_dir + "/non_existent.json");
        TEST_ASSERT(!res.has_value(), "Non-existent file safely returns nullopt");
    }

    // Case 1b: Truncated / invalid JSON
    {
        std::string bad_json_path = test_dir + "/bad_json.json";
        std::ofstream ofs(bad_json_path);
        ofs << "{ \"publisher_id\": \"com.test\", \"private_key\": ";
        ofs.close();

        auto res = DeveloperKeyStore::load_from_file(bad_json_path);
        TEST_ASSERT(!res.has_value(), "Malformed JSON safely returns nullopt");
    }

    // Case 1c: Substituted public key attack (public key does not match private key seed)
    {
        DeveloperIdentity id = DeveloperKeyStore::generate_identity("com.threat.pub", "Threat Org", "key-threat-01");
        std::string key_file = test_dir + "/substituted_pub.json";
        TEST_ASSERT(DeveloperKeyStore::save_to_file(key_file, id, KeystoreProtection::None), "Save keyfile");

        // Tamper with the JSON file: replace public key with another key
        std::string json_content;
        {
            std::ifstream ifs(key_file);
            json_content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        }

        // Replace public key bytes with zeroes
        std::string pub_zero_hex = "0000000000000000000000000000000000000000000000000000000000000000";
        auto pos = json_content.find("\"public_key\": \"");
        if (pos != std::string::npos) {
            pos += 15;
            json_content.replace(pos, 64, pub_zero_hex);
        }
        {
            std::ofstream ofs(key_file);
            ofs << json_content;
        }

        // Loading must fail because decrypted seed does not derive this public key!
        auto tampered_load = DeveloperKeyStore::load_from_file(key_file);
        TEST_ASSERT(!tampered_load.has_value(), "Substituted public key detected and rejected by seed cross-check");
    }

    // Case 1d: Corrupted DPAPI ciphertext
    {
        DeveloperIdentity id = DeveloperKeyStore::generate_identity("com.threat.dpapi", "DPAPI Org", "key-dpapi-01");
        std::string dpapi_file = test_dir + "/corrupt_dpapi.json";
        TEST_ASSERT(DeveloperKeyStore::save_to_file(dpapi_file, id, KeystoreProtection::DPAPI), "Save with DPAPI");

        // Read and corrupt base64 ciphertext
        std::string json_content;
        {
            std::ifstream ifs(dpapi_file);
            json_content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        }
        auto pos = json_content.find("\"private_key_ciphertext\": \"");
        if (pos != std::string::npos) {
            pos += 27;
            json_content[pos] = (json_content[pos] == 'A') ? 'B' : 'A'; // Bit flip in base64
        }
        {
            std::ofstream ofs(dpapi_file);
            ofs << json_content;
        }

        auto bad_dpapi_load = DeveloperKeyStore::load_from_file(dpapi_file);
        TEST_ASSERT(!bad_dpapi_load.has_value(), "Corrupted DPAPI ciphertext rejected");
    }

    // Case 1e: Wrong passphrase on Passphrase-protected keystore
    {
        DeveloperIdentity id = DeveloperKeyStore::generate_identity("com.threat.pass", "Pass Org", "key-pass-01");
        std::string pass_file = test_dir + "/pass_protected.json";
        TEST_ASSERT(DeveloperKeyStore::save_to_file(pass_file, id, KeystoreProtection::Passphrase, "CorrectHorseBatteryStaple"), "Save with passphrase");

        auto wrong_pass_load = DeveloperKeyStore::load_from_file(pass_file, "WrongPassword");
        TEST_ASSERT(!wrong_pass_load.has_value(), "Wrong passphrase rejected");

        auto correct_pass_load = DeveloperKeyStore::load_from_file(pass_file, "CorrectHorseBatteryStaple");
        TEST_ASSERT(correct_pass_load.has_value(), "Correct passphrase loads key");
    }

    std::cout << "  -> All keystore negative and threat tests passed.\n";
}

static void test_key_separation_invariant() {
    std::cout << "[Stage 234.6] Test 2: Trust Store Key Separation Invariant...\n";

    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity("com.winios.dev", "Winios Dev", "key-dev-99");
    TEST_ASSERT(dev_id.private_key_seed.size() == 32, "Private seed exists in developer identity");

    PublisherKey pub_key = DeveloperKeyStore::export_public_key(dev_id);

    // Invariant: pub_key MUST NOT contain private key material
    TEST_ASSERT(pub_key.key_bytes.size() == 32, "Public key is strictly 32 bytes");
    TEST_ASSERT(constant_time_equals(pub_key.key_bytes.data(), dev_id.public_key.data(), 32), "Public key matches");
    TEST_ASSERT(!constant_time_equals(pub_key.key_bytes.data(), dev_id.private_key_seed.data(), 32), "Public key is mathematically distinct from private seed");

    // Register into PublisherTrustStore
    PublisherTrustStore trust_store;
    TEST_ASSERT(trust_store.register_key(pub_key), "Register public key in trust store");

    auto retrieved_key = trust_store.get_key("key-dev-99");
    TEST_ASSERT(retrieved_key.has_value(), "Key retrieved from trust store");
    TEST_ASSERT(retrieved_key->key_bytes.size() == 32, "Retrieved key is strictly public key (32 bytes)");

    std::cout << "  -> Trust store key separation verified: Zero private key leakage.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.6: DPAPI and Keystore Threat Model Test Suite        \n";
    std::cout << "=================================================================\n";

    test_keystore_threat_boundaries();
    test_key_separation_invariant();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL KEYSTORE THREAT MODEL TESTS PASSED!                        \n";
    std::cout << "=================================================================\n";
    return 0;
}
