#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Category enumeration conforming to Stage 153 specification
enum class ArtifactReadinessCategory {
    SyntheticArtifact,
    UnencryptedArtifact,
    EncryptedArtifact,
    PersonalizedArtifact,
    AuthorizedArtifactAdmitted,
    Blocked
};

[[nodiscard]] const char* category_to_string(ArtifactReadinessCategory cat) noexcept {
    switch (cat) {
        case ArtifactReadinessCategory::SyntheticArtifact:          return "SYNTHETIC_ARTIFACT";
        case ArtifactReadinessCategory::UnencryptedArtifact:        return "UNENCRYPTED_ARTIFACT";
        case ArtifactReadinessCategory::EncryptedArtifact:          return "ENCRYPTED_ARTIFACT";
        case ArtifactReadinessCategory::PersonalizedArtifact:       return "PERSONALIZED_ARTIFACT";
        case ArtifactReadinessCategory::AuthorizedArtifactAdmitted: return "AUTHORIZED_ARTIFACT_ADMITTED";
        case ArtifactReadinessCategory::Blocked:                    return "BLOCKED";
    }
    return "UNKNOWN";
}

// Helper to construct an encrypted IM4P container simulation
std::vector<std::uint8_t> make_encrypted_im4p() {
    std::vector<std::uint8_t> buf(512, 0);
    // IM4P ASCII tag
    buf[0] = 'I'; buf[1] = 'M'; buf[2] = '4'; buf[3] = 'P';
    // Type: krnl
    buf[4] = 'k'; buf[5] = 'r'; buf[6] = 'n'; buf[7] = 'l';
    // Description string containing keybag indicator
    const char desc[] = "Encrypted Kernel Collection (KBAG: AES-256-CBC)";
    std::memcpy(buf.data() + 8, desc, sizeof(desc));
    return buf;
}

// Helper to construct an ASN.1 wrapped IMG4 personalized container
std::vector<std::uint8_t> make_personalized_img4() {
    std::vector<std::uint8_t> buf(1024, 0);
    buf[0] = 0x30; // ASN.1 Sequence
    buf[1] = 0x82; buf[2] = 0x03; buf[3] = 0xE8; // Length
    // Embed 'IMG4' or 'IM4P' marker inside sequence
    buf[4] = 'I'; buf[5] = 'M'; buf[6] = 'G'; buf[7] = '4';
    buf[16] = 'I'; buf[17] = 'M'; buf[18] = '4'; buf[19] = 'M'; // Manifest
    buf[32] = 'I'; buf[33] = 'M'; buf[34] = '4'; buf[35] = 'P'; // Payload
    return buf;
}

// Test 1: SYNTHETIC_ARTIFACT Admission Policy Control
bool test_synthetic_artifact_admission() {
    std::cout << "[Test 1] SYNTHETIC_ARTIFACT Admission Policy Control...\n";

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    AppleBootArtifact art;
    art.artifact_id = "test_synthetic_app";
    art.kind = AppleArtifactKind::NativeAppBinary;
    art.is_synthetic = true;
    art.provenance = ArtifactProvenance::SyntheticHarness;
    art.target_device = "iPhone15,4";

    // Policy 1: Developer mode allows synthetic artifacts
    AdmissionPolicy dev_policy;
    dev_policy.allow_synthetic_artifacts = true;
    dev_policy.reject_synthetic_as_real = true;
    AppleArtifactAdmissionController dev_ctrl(dev_policy);

    auto res_dev = dev_ctrl.evaluate_artifact(art, macho.data(), macho.size());
    TEST_ASSERT(res_dev.admitted, "Synthetic artifact admitted in developer mode");
    TEST_ASSERT(res_dev.decision == AdmissionDecision::Admitted, "Decision is Admitted");

    // Policy 2: Strict production mode rejects synthetic artifacts
    AdmissionPolicy prod_policy;
    prod_policy.allow_synthetic_artifacts = false;
    AppleArtifactAdmissionController prod_ctrl(prod_policy);

    auto res_prod = prod_ctrl.evaluate_artifact(art, macho.data(), macho.size());
    TEST_ASSERT(!res_prod.admitted, "Synthetic artifact rejected in strict production mode");

    std::cout << "  -> PASS: SYNTHETIC_ARTIFACT Policy Control Verified\n";
    return true;
}

// Test 2: Fraudulent Provenance Rejection (Synthetic Claiming Real)
bool test_synthetic_claiming_real_rejection() {
    std::cout << "[Test 2] Synthetic Claiming Genuine Provenance Rejection...\n";

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    AppleBootArtifact art;
    art.artifact_id = "fake_kernelcache";
    art.kind = AppleArtifactKind::NativeAppBinary;
    art.is_synthetic = true; // Synthetic fixture
    art.provenance = ArtifactProvenance::DecryptedReal; // Falsely claims real decrypted!

    AdmissionPolicy policy;
    policy.allow_synthetic_artifacts = true;
    policy.reject_synthetic_as_real = true;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, macho.data(), macho.size());
    TEST_ASSERT(!res.admitted, "Fraudulent synthetic artifact must be rejected");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_SyntheticAsReal, 
                "Decision must be Rejected_SyntheticAsReal");

    std::cout << "  -> PASS: Fraudulent Provenance Rejection Verified\n";
    return true;
}

// Test 3: UNENCRYPTED_ARTIFACT Admission (Genuine Decrypted Mach-O)
bool test_unencrypted_artifact_admission() {
    std::cout << "[Test 3] UNENCRYPTED_ARTIFACT / AUTHORIZED_ARTIFACT_ADMITTED...\n";

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    AppleBootArtifact art;
    art.artifact_id = "usr_lib_dyld";
    art.kind = AppleArtifactKind::DyldSharedCache;
    art.is_synthetic = false;
    art.is_encrypted = false;
    art.provenance = ArtifactProvenance::DecryptedReal;
    art.target_device = "iPhone15,4";

    AdmissionPolicy policy;
    policy.allow_synthetic_artifacts = true; // allow unencrypted raw macho
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, macho.data(), macho.size());
    TEST_ASSERT(res.admitted, "Unencrypted ARM64e binary admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Admitted, "Decision must be Admitted");

    std::cout << "  -> PASS: UNENCRYPTED_ARTIFACT Admitted Successfully\n";
    return true;
}

// Test 4: ENCRYPTED_ARTIFACT and SEP GID Boundary (BLOCKED)
bool test_encrypted_im4p_sep_boundary() {
    std::cout << "[Test 4] ENCRYPTED_ARTIFACT & SEP GID Boundary (BLOCKED)...\n";

    auto im4p = make_encrypted_im4p();
    AppleBootArtifact art;
    art.artifact_id = "sep_firmware_d73";
    art.kind = AppleArtifactKind::SepFirmware;
    art.is_encrypted = true;
    art.provenance = ArtifactProvenance::EncryptedApple;

    AdmissionPolicy policy;
    policy.reject_encrypted_im4p = true;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, im4p.data(), im4p.size());
    TEST_ASSERT(!res.admitted, "Encrypted IM4P must be rejected at SEP boundary");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Encrypted, "Decision must be Rejected_Encrypted");
    TEST_ASSERT(!res.remediation.empty(), "Remediation instructions must guide decryption");

    std::cout << "  -> PASS: ENCRYPTED_ARTIFACT Blocked at SEP Boundary\n";
    return true;
}

// Test 5: PERSONALIZED_ARTIFACT (IMG4 Container Detection)
bool test_personalized_img4_container() {
    std::cout << "[Test 5] PERSONALIZED_ARTIFACT (IMG4 Container Handling)...\n";

    auto img4 = make_personalized_img4();
    AppleBootArtifact art;
    art.artifact_id = "iboot_personalized";
    art.kind = AppleArtifactKind::Bootloader_iBoot;
    art.target_device = "iPhone15,4";

    AdmissionPolicy policy;
    policy.reject_encrypted_im4p = true;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, img4.data(), img4.size());
    // Since this IMG4 contains an embedded IM4P payload without decryption keys, it is rejected
    TEST_ASSERT(!res.admitted, "Personalized container with encrypted payload rejected");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Encrypted, "Rejected_Encrypted verdict");

    std::cout << "  -> PASS: PERSONALIZED_ARTIFACT Evaluation Verified\n";
    return true;
}

// Test 6: Architecture Mismatch Rejection (32-bit Mach-O)
bool test_architecture_mismatch_rejection() {
    std::cout << "[Test 6] Incompatible Architecture Rejection (32-bit Mach-O)...\n";

    std::vector<std::uint8_t> buf(512, 0);
    // 32-bit Mach-O header (little-endian 0xFEEDFACE)
    buf[0] = 0xCE; buf[1] = 0xFA; buf[2] = 0xED; buf[3] = 0xFE; // MH_MAGIC (32-bit)
    buf[4] = 0x0C; buf[5] = 0x00; buf[6] = 0x00; buf[7] = 0x00; // CPU_TYPE_ARM (32-bit)

    AppleBootArtifact art;
    art.artifact_id = "legacy_kernel";
    art.kind = AppleArtifactKind::KernelCollection;

    AdmissionPolicy policy;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, buf.data(), buf.size());
    TEST_ASSERT(!res.admitted, "32-bit binary must be rejected");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_ArchitectureMismatch, 
                "Decision must be Rejected_ArchitectureMismatch");

    std::cout << "  -> PASS: Architecture Mismatch Rejection Verified\n";
    return true;
}

// Test 7: Target Device Conformance (iPhone 15 / iPhone15,4)
bool test_target_device_conformance() {
    std::cout << "[Test 7] Target Device Conformance Check...\n";

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    AppleBootArtifact art;
    art.artifact_id = "ipad_kernel";
    art.kind = AppleArtifactKind::KernelCollection;
    art.target_device = "iPad13,16"; // Wrong device target!

    AdmissionPolicy policy;
    policy.enforce_strict_device = true;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, macho.data(), macho.size());
    TEST_ASSERT(!res.admitted, "Mismatched device artifact must be rejected");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_DeviceMismatch,
                "Decision must be Rejected_DeviceMismatch");

    std::cout << "  -> PASS: Target Device Conformance Verified\n";
    return true;
}

// Test 8: Full Inventory Boot Readiness and Security Gate
bool test_inventory_boot_readiness() {
    std::cout << "[Test 8] Full Boot Artifact Inventory Readiness & Security Gate...\n";

    AppleBootArtifactInventory inventory;

    // Register required artifacts
    AppleBootArtifact kc;
    kc.artifact_id = "kernelcache.release.iphone15";
    kc.kind = AppleArtifactKind::KernelCollection;
    kc.is_present = true;
    kc.is_admitted = false; // Blocked because missing decrypted payload
    inventory.register_artifact(kc);

    AppleBootArtifact dt;
    dt.artifact_id = "DeviceTree.d73ap.im4p";
    dt.kind = AppleArtifactKind::DeviceTree;
    dt.is_present = true;
    dt.is_encrypted = true; // Encrypted with GID key
    inventory.register_artifact(dt);

    std::string report;
    bool ready = inventory.verify_boot_readiness(report);
    // Boot readiness must be FALSE when critical artifacts are unadmitted or encrypted
    TEST_ASSERT(!ready, "System must block boot when artifacts are encrypted or unadmitted");
    TEST_ASSERT(!report.empty(), "Readiness report must document blockers");

    std::cout << "  -> PASS: Boot Inventory Security Gate Verified\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 153: GENUINE ARTIFACT READINESS & SEP BOUNDARY     \n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Security Policy: Strict Admission, SEP GID Boundary & Provenance\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_synthetic_artifact_admission()) passed++;
    if (test_synthetic_claiming_real_rejection()) passed++;
    if (test_unencrypted_artifact_admission()) passed++;
    if (test_encrypted_im4p_sep_boundary()) passed++;
    if (test_personalized_img4_container()) passed++;
    if (test_architecture_mismatch_rejection()) passed++;
    if (test_target_device_conformance()) passed++;
    if (test_inventory_boot_readiness()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 153 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
