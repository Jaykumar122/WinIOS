#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/linux_image_loader.h"
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

// Test 1: Valid ARM64e Launchd Mach-O Admission
bool test_valid_launchd_admission() {
    std::cout << "[Test 1] Valid ARM64e /sbin/launchd Mach-O Admission...\n";

    AppleArtifactAdmissionController controller;
    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    AppleBootArtifact artifact;
    artifact.artifact_id = "launchd_arm64e";
    artifact.kind = AppleArtifactKind::LaunchdBinary;
    artifact.provenance = ArtifactProvenance::SyntheticHarness;
    artifact.is_synthetic = true;

    auto res = controller.evaluate_artifact(artifact, macho.data(), macho.size());
    TEST_ASSERT(res.admitted, "Valid ARM64e launchd binary must be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Admitted, "Decision must be Admitted");
    TEST_ASSERT(!res.computed_sha256.empty(), "SHA256 must be computed");

    std::cout << "  Passed (Admitted with SHA256: " << res.computed_sha256.substr(0, 16) << "...)\n";
    return true;
}

// Test 2: Encrypted IM4P Payload Rejection for Userspace
bool test_encrypted_userspace_rejection() {
    std::cout << "[Test 2] Encrypted IM4P Userspace Payload Rejection...\n";

    AppleArtifactAdmissionController controller;

    // Synthetic IM4P container with KBAG
    std::vector<std::uint8_t> im4p(256, 0);
    im4p[0] = 'I'; im4p[1] = 'M'; im4p[2] = '4'; im4p[3] = 'P';
    im4p[16] = 'k'; im4p[17] = 'b'; im4p[18] = 'a'; im4p[19] = 'g';

    AppleBootArtifact artifact;
    artifact.artifact_id = "encrypted_launchd";
    artifact.kind = AppleArtifactKind::LaunchdBinary;
    artifact.is_encrypted = true;

    auto res = controller.evaluate_artifact(artifact, im4p.data(), im4p.size());
    TEST_ASSERT(!res.admitted, "Encrypted payload must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Encrypted, "Decision must be Rejected_Encrypted");

    std::cout << "  Passed (Safely rejected encrypted IM4P userspace payload)\n";
    return true;
}

// Test 3: SHA-256 Digest Verification & Checksum Mismatch Rejection
bool test_sha256_checksum_verification() {
    std::cout << "[Test 3] SHA-256 Checksum Verification & Tamper Detection...\n";

    AppleArtifactAdmissionController controller;
    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    std::string valid_hash = LinuxImageLoader::compute_sha256(macho.data(), macho.size());

    // Case A: Matching hash
    AppleBootArtifact valid_art;
    valid_art.artifact_id = "launchd_verified";
    valid_art.kind = AppleArtifactKind::LaunchdBinary;
    valid_art.sha256_hex = valid_hash;

    auto res_ok = controller.evaluate_artifact(valid_art, macho.data(), macho.size());
    TEST_ASSERT(res_ok.admitted, "Artifact with matching SHA-256 must be admitted");

    // Case B: Corrupted/tampered hash
    AppleBootArtifact tampered_art = valid_art;
    tampered_art.sha256_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    auto res_tampered = controller.evaluate_artifact(tampered_art, macho.data(), macho.size());
    TEST_ASSERT(!res_tampered.admitted, "Tampered artifact must NOT be admitted");
    TEST_ASSERT(res_tampered.decision == AdmissionDecision::Rejected_ChecksumMismatch,
                "Decision must be Rejected_ChecksumMismatch");

    std::cout << "  Passed (Tamper detection confirmed)\n";
    return true;
}

// Test 4: Architecture Compatibility Enforcement (ARM64 vs x86_64)
bool test_architecture_compatibility_enforcement() {
    std::cout << "[Test 4] Architecture Compatibility Enforcement (ARM64 only)...\n";

    AppleArtifactAdmissionController controller;
    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    // Corrupt CPU type to x86_64 (0x01000007)
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(macho.data());
    hdr->cputype = 0x01000007u; // CPU_TYPE_X86_64

    AppleBootArtifact artifact;
    artifact.artifact_id = "x86_launchd";
    artifact.kind = AppleArtifactKind::LaunchdBinary;

    auto res = controller.evaluate_artifact(artifact, macho.data(), macho.size());
    TEST_ASSERT(!res.admitted, "x86_64 binary must NOT be admitted on ARM64e platform");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_ArchitectureMismatch,
                "Decision must be Rejected_ArchitectureMismatch");

    std::cout << "  Passed (Non-ARM64 architecture rejected)\n";
    return true;
}

// Test 5: Synthetic Masquerading Prevention (Strict Provenance Guard)
bool test_synthetic_masquerading_prevention() {
    std::cout << "[Test 5] Synthetic-as-Real Masquerading Prevention...\n";

    AppleArtifactAdmissionController controller;
    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    // Malicious or misconfigured claim: synthetic fixture claiming to be DecryptedReal
    AppleBootArtifact artifact;
    artifact.artifact_id = "fake_real_launchd";
    artifact.kind = AppleArtifactKind::LaunchdBinary;
    artifact.provenance = ArtifactProvenance::DecryptedReal;
    artifact.is_synthetic = true; // Still synthetic!

    auto res = controller.evaluate_artifact(artifact, macho.data(), macho.size());
    TEST_ASSERT(!res.admitted, "Synthetic fixture claiming DecryptedReal must be rejected");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_SyntheticAsReal,
                "Decision must be Rejected_SyntheticAsReal");

    std::cout << "  Passed (Synthetic fixture blocked from claiming DecryptedReal)\n";
    return true;
}

// Test 6: Truncated Mach-O Header Rejection
bool test_truncated_header_rejection() {
    std::cout << "[Test 6] Truncated Mach-O Header Rejection...\n";

    AppleArtifactAdmissionController controller;
    std::vector<std::uint8_t> small_buffer = { 0xCF, 0xFA, 0xED, 0xFE }; // Only 4 bytes

    AppleBootArtifact artifact;
    artifact.artifact_id = "truncated_macho";
    artifact.kind = AppleArtifactKind::LaunchdBinary;

    auto res = controller.evaluate_artifact(artifact, small_buffer.data(), small_buffer.size());
    TEST_ASSERT(!res.admitted, "Truncated buffer must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated, "Decision must be Rejected_Truncated");

    std::cout << "  Passed (Truncated header rejected)\n";
    return true;
}

// Test 7: SpringBoard & WindowServer Binary Admission
bool test_springboard_and_windowserver_admission() {
    std::cout << "[Test 7] SpringBoard & WindowServer Binary Admission...\n";

    AppleArtifactAdmissionController controller;
    auto ws_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto sb_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    AppleBootArtifact ws_art;
    ws_art.artifact_id = "WindowServer";
    ws_art.kind = AppleArtifactKind::WindowServerBinary;
    ws_art.provenance = ArtifactProvenance::SyntheticHarness;
    ws_art.is_synthetic = true;

    auto ws_res = controller.evaluate_artifact(ws_art, ws_macho.data(), ws_macho.size());
    TEST_ASSERT(ws_res.admitted, "WindowServer binary must be admitted");

    AppleBootArtifact sb_art;
    sb_art.artifact_id = "SpringBoard";
    sb_art.kind = AppleArtifactKind::SpringBoardBundle;
    sb_art.provenance = ArtifactProvenance::SyntheticHarness;
    sb_art.is_synthetic = true;

    auto sb_res = controller.evaluate_artifact(sb_art, sb_macho.data(), sb_macho.size());
    TEST_ASSERT(sb_res.admitted, "SpringBoard binary must be admitted");

    std::cout << "  Passed (WindowServer and SpringBoard binaries validated)\n";
    return true;
}

// Test 8: Native Application Binary Admission
bool test_native_app_binary_admission() {
    std::cout << "[Test 8] Native iOS Application Binary Admission...\n";

    AppleArtifactAdmissionController controller;
    auto app_macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    AppleBootArtifact app_art;
    app_art.artifact_id = "MobileSafari";
    app_art.kind = AppleArtifactKind::NativeAppBinary;
    app_art.provenance = ArtifactProvenance::SyntheticHarness;
    app_art.is_synthetic = true;

    auto res = controller.evaluate_artifact(app_art, app_macho.data(), app_macho.size());
    TEST_ASSERT(res.admitted, "Native application binary must be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Admitted, "Decision must be Admitted");

    std::cout << "  Passed (Native application binary admitted)\n";
    return true;
}

// Test 9: Full Boot and Userspace Inventory Evaluation
bool test_full_inventory_evaluation() {
    std::cout << "[Test 9] Comprehensive Boot & Userspace Inventory Evaluation...\n";

    AppleBootArtifactInventory inventory;
    AppleBootArtifact kernel_art;
    kernel_art.artifact_id = "kernelcache";
    kernel_art.kind = AppleArtifactKind::KernelCollection;
    inventory.register_artifact(kernel_art);

    AppleBootArtifact launchd_art;
    launchd_art.artifact_id = "launchd";
    launchd_art.kind = AppleArtifactKind::LaunchdBinary;
    inventory.register_artifact(launchd_art);

    std::map<std::string, std::vector<std::uint8_t>> loaded;
    loaded["launchd"] = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    AppleArtifactAdmissionController controller;
    auto results = controller.evaluate_inventory(inventory, loaded);

    TEST_ASSERT(results.size() == 2, "Must evaluate both inventory items");

    bool found_admitted_launchd = false;
    bool found_missing_kernel = false;

    for (const auto& r : results) {
        if (r.artifact_id == "launchd" && r.admitted) {
            found_admitted_launchd = true;
        }
        if (r.artifact_id == "kernelcache" && !r.admitted) {
            found_missing_kernel = true;
        }
    }

    TEST_ASSERT(found_admitted_launchd, "launchd must be admitted");
    TEST_ASSERT(found_missing_kernel, "Missing kernel must be flagged");

    std::cout << "  Passed (Inventory correctly evaluated mixed presence)\n";
    return true;
}

// Test 10: Markdown Admission Report Generation
bool test_admission_report_generation() {
    std::cout << "[Test 10] Markdown Admission Report Generation...\n";

    AppleArtifactAdmissionController controller;
    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    AppleBootArtifact artifact;
    artifact.artifact_id = "launchd";
    artifact.kind = AppleArtifactKind::LaunchdBinary;

    auto res = controller.evaluate_artifact(artifact, macho.data(), macho.size());
    std::vector<AdmissionResult> results = { res };

    std::string report = controller.generate_admission_report(results);
    TEST_ASSERT(!report.empty(), "Report must be non-empty");
    TEST_ASSERT(report.find("launchd") != std::string::npos, "Report must mention launchd");
    TEST_ASSERT(report.find("ADMITTED") != std::string::npos || report.find("Admitted") != std::string::npos,
                "Report must reflect Admitted status");

    std::cout << "  Passed (Formatted markdown report generated successfully)\n";
    return true;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 131: REAL ARTIFACT ADMISSION & INTEGRITY TESTS\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    auto run_test = [&](auto test_fn, const char* name) {
        try {
            if (test_fn()) {
                passed++;
            } else {
                std::cout << "FAILED: " << name << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION in " << name << ": " << e.what() << "\n";
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION in " << name << "\n";
        }
    };

    run_test(test_valid_launchd_admission, "test_valid_launchd_admission");
    run_test(test_encrypted_userspace_rejection, "test_encrypted_userspace_rejection");
    run_test(test_sha256_checksum_verification, "test_sha256_checksum_verification");
    run_test(test_architecture_compatibility_enforcement, "test_architecture_compatibility_enforcement");
    run_test(test_synthetic_masquerading_prevention, "test_synthetic_masquerading_prevention");
    run_test(test_truncated_header_rejection, "test_truncated_header_rejection");
    run_test(test_springboard_and_windowserver_admission, "test_springboard_and_windowserver_admission");
    run_test(test_native_app_binary_admission, "test_native_app_binary_admission");
    run_test(test_full_inventory_evaluation, "test_full_inventory_evaluation");
    run_test(test_admission_report_generation, "test_admission_report_generation");

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 131 RESULTS: " << passed << "/" << total << " tests passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
