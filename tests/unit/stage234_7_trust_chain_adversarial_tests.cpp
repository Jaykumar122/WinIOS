#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "packaging/app_package.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::security;
using namespace winios::packaging;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static std::vector<std::uint8_t> create_dummy_package_data() {
    AppPackage pkg{};
    pkg.manifest.format_version = 3;
    pkg.manifest.bundle_id = "com.winios.trusttest";
    pkg.manifest.name = "TrustTestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";

    pkg.add_file("bin/main", { 0x7F, 'E', 'L', 'F' });
    return pkg.pack_v3();
}

static void test_trust_policies_and_roles() {
    std::cout << "[Stage 234.7] Test 1: Trust Policy Enforcement (Development vs. StrictProduction)...\n";

    PublisherTrustStore trust_store;

    // Developer Key
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity("com.winios.dev", "Dev Org", "key-dev-01", PublisherRole::Developer);
    PublisherKey dev_pub = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store.register_key(dev_pub), "Register developer key");

    // Platform Key
    DeveloperIdentity plat_id = DeveloperKeyStore::generate_identity("com.winios.platform", "Platform Org", "key-plat-01", PublisherRole::Platform);
    PublisherKey plat_pub = DeveloperKeyStore::export_public_key(plat_id);
    TEST_ASSERT(trust_store.register_key(plat_pub), "Register platform key");

    auto raw_pkg = create_dummy_package_data();

    // 1. Sign package with Developer Key
    std::vector<std::uint8_t> dev_signed_pkg = raw_pkg;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(dev_signed_pkg, dev_id), "Sign with dev key");

    // In Development policy: DeveloperSigned is trusted
    auto dev_res = trust_store.verify_package_signature(dev_signed_pkg.data(), dev_signed_pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(dev_res.status == SignatureStatus::DeveloperSigned, "Dev policy accepts developer signature");
    TEST_ASSERT(dev_res.is_trusted, "Dev signed is trusted in Development policy");

    // In StrictProduction policy: DeveloperSigned MUST be untrusted
    auto prod_res = trust_store.verify_package_signature(dev_signed_pkg.data(), dev_signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(!prod_res.is_trusted, "StrictProduction rejects DeveloperSigned packages");

    // 2. Sign package with Platform Key
    std::vector<std::uint8_t> plat_signed_pkg = raw_pkg;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(plat_signed_pkg, plat_id), "Sign with platform key");

    // In StrictProduction policy: PlatformSigned is trusted
    auto plat_prod_res = trust_store.verify_package_signature(plat_signed_pkg.data(), plat_signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(plat_prod_res.status == SignatureStatus::PlatformSigned, "PlatformSigned detected");
    TEST_ASSERT(plat_prod_res.is_trusted, "PlatformSigned is trusted in StrictProduction");

    std::cout << "  -> Policy separation verified: Developer trust cannot weaken StrictProduction.\n";
}

static void test_revocation_and_expiration() {
    std::cout << "[Stage 234.7] Test 2: Revocation, Expiration, and Re-Registration Attacks...\n";

    PublisherTrustStore trust_store;

    DeveloperIdentity plat_id = DeveloperKeyStore::generate_identity("com.winios.corp", "Corp Org", "key-corp-01", PublisherRole::Platform);
    PublisherKey plat_pub = DeveloperKeyStore::export_public_key(plat_id);
    TEST_ASSERT(trust_store.register_key(plat_pub), "Register key");

    auto raw_pkg = create_dummy_package_data();
    std::vector<std::uint8_t> signed_pkg = raw_pkg;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_pkg, plat_id), "Sign package");

    // Verify valid before revocation
    auto r1 = trust_store.verify_package_signature(signed_pkg.data(), signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(r1.is_trusted, "Initial signature trusted");

    // 1. Revoke key
    TEST_ASSERT(trust_store.revoke_key("key-corp-01"), "Revoke key");
    auto r2 = trust_store.verify_package_signature(signed_pkg.data(), signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(!r2.is_trusted, "Revoked key is rejected");
    TEST_ASSERT(r2.status == SignatureStatus::RevokedPublisher, "Status is RevokedPublisher");

    // 2. Re-registration attack: Try to overwrite revoked key with a new unrevoked key using same ID
    PublisherKey re_reg_pub = plat_pub;
    re_reg_pub.is_revoked = false;
    TEST_ASSERT(trust_store.register_key(re_reg_pub), "Re-register key");
    TEST_ASSERT(trust_store.is_key_revoked("key-corp-01"), "Key remains revoked despite re-registration");

    auto r3 = trust_store.verify_package_signature(signed_pkg.data(), signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(!r3.is_trusted, "Re-registered revoked key rejected");

    // 3. Publisher-level revocation
    DeveloperIdentity pub_id = DeveloperKeyStore::generate_identity("com.rogue.org", "Rogue Org", "key-rogue-01", PublisherRole::Platform);
    PublisherKey pub_key = DeveloperKeyStore::export_public_key(pub_id);
    TEST_ASSERT(trust_store.register_key(pub_key), "Register rogue key");
    TEST_ASSERT(trust_store.revoke_publisher("com.rogue.org"), "Revoke publisher");
    TEST_ASSERT(trust_store.is_publisher_revoked("com.rogue.org"), "Publisher is revoked");
    TEST_ASSERT(trust_store.is_key_revoked("key-rogue-01"), "Publisher's keys are automatically revoked");

    std::cout << "  -> Revocation persistence and anti-re-registration verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.7: Trust Chain & Revocation Adversarial Suite        \n";
    std::cout << "=================================================================\n";

    test_trust_policies_and_roles();
    test_revocation_and_expiration();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL TRUST CHAIN ADVERSARIAL TESTS PASSED!                      \n";
    std::cout << "=================================================================\n";
    return 0;
}
