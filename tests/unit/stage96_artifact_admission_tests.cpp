#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/linux_image_loader.h"
#include "loader/macho_loader.h"
#include "device/apple_devicetree.h"

using namespace winios::loader;
using namespace winios::device;

#define STAGE96_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 96 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to create a synthetic 64-bit Mach-O kernelcache
std::vector<std::uint8_t> create_synthetic_macho_arm64(
    std::uint32_t cputype = kCpuTypeARM64,
    std::uint32_t cpusubtype = 2u, // ARM64e
    std::uint64_t seg_vmaddr = 0xFFFFFE0007004000ULL, // 16 KB aligned
    std::uint64_t seg_vmsize = 0x200000ULL) {
    std::size_t total_size = sizeof(MachOHeader64) + sizeof(MachOSegmentCommand64) + 0x4000;
    std::vector<std::uint8_t> buf(total_size, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = cputype;
    hdr->cpusubtype = cpusubtype;
    hdr->filetype = kMachOFileTypeFileset;
    hdr->ncmds = 1;
    hdr->sizeofcmds = sizeof(MachOSegmentCommand64);
    hdr->flags = 0x00200000; // MH_PIE

    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + sizeof(MachOHeader64));
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg->segname, "__TEXT", 6);
    seg->vmaddr = seg_vmaddr;
    seg->vmsize = seg_vmsize;
    seg->fileoff = 0;
    seg->filesize = 0x4000;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;

    return buf;
}

// Helper to create a synthetic Apple DeviceTree blob for iPhone 15
std::vector<std::uint8_t> create_synthetic_devicetree_blob(
    std::uint32_t board_id = 0x18,
    std::uint32_t chip_id = 0x8120,
    const std::string& model = "iPhone15,4") {
    AppleDeviceTreeNode root("");
    root.add_property_string("model", model);
    root.add_property_u32("board-id", board_id);
    root.add_property_u32("chip-id", chip_id);
    root.add_property_u32("timebase-frequency", 24000000u);
    return AppleDeviceTreeBuilder::serialize(root);
}

// Helper to create an APFS container superblock buffer
std::vector<std::uint8_t> create_synthetic_apfs_buffer() {
    std::vector<std::uint8_t> buf(8192, 0);
    // APFS magic 'NXSB' at byte 0 (0x4253584E)
    std::uint32_t magic = 0x4253584Eu;
    std::memcpy(buf.data(), &magic, 4);
    return buf;
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: Valid 64-bit ARM64 Kernelcache Admission
// -----------------------------------------------------------------------------
static bool test_valid_kernelcache_admission() {
    std::cout << "\n[1] test_valid_kernelcache_admission...\n";

    AppleArtifactAdmissionController controller;
    AppleBootArtifact artifact;
    artifact.artifact_id = "kernelcache";
    artifact.kind = AppleArtifactKind::KernelCollection;
    artifact.target_device = "iPhone15,4";

    auto kernel_blob = create_synthetic_macho_arm64(kCpuTypeARM64, 2u, 0xFFFFFE0007000000ULL, 0x100000);
    AdmissionResult res = controller.evaluate_artifact(artifact, kernel_blob.data(), kernel_blob.size());

    STAGE96_ASSERT(res.admitted, "Valid ARM64e kernelcache should be admitted");
    STAGE96_ASSERT(res.decision == AdmissionDecision::Admitted, "Decision must be Admitted");
    STAGE96_ASSERT(!res.computed_sha256.empty(), "SHA256 must be computed");
    STAGE96_ASSERT(res.size_bytes == kernel_blob.size(), "Size must match input");

    std::cout << "[PASS] Valid 64-bit ARM64 Kernelcache Admission (Decision: "
              << AppleArtifactAdmissionController::decision_to_string(res.decision) << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Architecture Incompatibility Rejection
// -----------------------------------------------------------------------------
static bool test_architecture_rejection() {
    std::cout << "\n[2] test_architecture_rejection...\n";

    AppleArtifactAdmissionController controller;
    AppleBootArtifact artifact;
    artifact.artifact_id = "kernelcache";
    artifact.kind = AppleArtifactKind::KernelCollection;
    artifact.target_device = "iPhone15,4";

    // 1. Rejection of x86_64 binary (cputype = 0x01000007)
    auto x86_blob = create_synthetic_macho_arm64(0x01000007u, 3u);
    AdmissionResult res_x86 = controller.evaluate_artifact(artifact, x86_blob.data(), x86_blob.size());
    STAGE96_ASSERT(!res_x86.admitted, "x86_64 kernelcache must be rejected");
    STAGE96_ASSERT(res_x86.decision == AdmissionDecision::Rejected_ArchitectureMismatch,
                   "Decision must be Rejected_ArchitectureMismatch");

    // 2. Rejection of 32-bit Mach-O (magic = 0xFEEDFACE)
    auto m32_blob = x86_blob;
    *reinterpret_cast<std::uint32_t*>(m32_blob.data()) = kMachOMagic32;
    AdmissionResult res_m32 = controller.evaluate_artifact(artifact, m32_blob.data(), m32_blob.size());
    STAGE96_ASSERT(!res_m32.admitted, "32-bit Mach-O must be rejected");
    STAGE96_ASSERT(res_m32.decision == AdmissionDecision::Rejected_ArchitectureMismatch,
                   "Decision must be Rejected_ArchitectureMismatch");

    std::cout << "[PASS] Architecture Incompatibility Rejection (x86_64 & 32-bit correctly rejected)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Encrypted Im4p Container Rejection with Remediation
// -----------------------------------------------------------------------------
static bool test_encrypted_im4p_rejection() {
    std::cout << "\n[3] test_encrypted_im4p_rejection...\n";

    AppleArtifactAdmissionController controller;
    AppleBootArtifact artifact;
    artifact.artifact_id = "kernelcache";
    artifact.kind = AppleArtifactKind::KernelCollection;
    artifact.target_device = "iPhone15,4";

    // Create a mock encrypted IM4P container with 'IM4P' magic and 'kbag' tag
    std::vector<std::uint8_t> enc_blob(512, 0);
    enc_blob[0] = 'I'; enc_blob[1] = 'M'; enc_blob[2] = '4'; enc_blob[3] = 'P';
    enc_blob[4] = 'k'; enc_blob[5] = 'r'; enc_blob[6] = 'n'; enc_blob[7] = 'l';
    // Add 'KBAG' at offset 32
    enc_blob[32] = 'K'; enc_blob[33] = 'B'; enc_blob[34] = 'A'; enc_blob[35] = 'G';

    AdmissionResult res = controller.evaluate_artifact(artifact, enc_blob.data(), enc_blob.size());
    STAGE96_ASSERT(!res.admitted, "Encrypted Im4p must be rejected");
    STAGE96_ASSERT(res.decision == AdmissionDecision::Rejected_Encrypted,
                   "Decision must be Rejected_Encrypted");
    STAGE96_ASSERT(!res.remediation.empty(), "Remediation guidance must be present");
    STAGE96_ASSERT(res.remediation.find("img4lib") != std::string::npos ||
                   res.remediation.find("Decrypt") != std::string::npos,
                   "Remediation must advise decryption tools");

    std::cout << "[PASS] Encrypted Im4p Container Rejection (Remediation: " << res.remediation << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Target Device and Board Mismatch Rejection
// -----------------------------------------------------------------------------
static bool test_device_mismatch_rejection() {
    std::cout << "\n[4] test_device_mismatch_rejection...\n";

    AppleArtifactAdmissionController controller;

    // 1. DeviceTree with mismatched Board ID (0x10 instead of 0x18)
    AppleBootArtifact dt_artifact;
    dt_artifact.artifact_id = "devicetree";
    dt_artifact.kind = AppleArtifactKind::DeviceTree;
    dt_artifact.target_device = "iPhone15,4";

    auto dt_mismatched = create_synthetic_devicetree_blob(0x10u, 0x8120u, "iPhone14,5");
    AdmissionResult res_dt = controller.evaluate_artifact(dt_artifact, dt_mismatched.data(), dt_mismatched.size());
    STAGE96_ASSERT(!res_dt.admitted, "Mismatched board-id DeviceTree must be rejected");
    STAGE96_ASSERT(res_dt.decision == AdmissionDecision::Rejected_DeviceMismatch,
                   "Decision must be Rejected_DeviceMismatch");

    // 2. Kernelcache metadata with mismatched target device
    AppleBootArtifact kc_artifact;
    kc_artifact.artifact_id = "kernelcache";
    kc_artifact.kind = AppleArtifactKind::KernelCollection;
    kc_artifact.target_device = "iPad13,4"; // M1 iPad, not iPhone 15

    auto kc_blob = create_synthetic_macho_arm64();
    AdmissionResult res_kc = controller.evaluate_artifact(kc_artifact, kc_blob.data(), kc_blob.size());
    STAGE96_ASSERT(!res_kc.admitted, "Mismatched target device kernelcache must be rejected");
    STAGE96_ASSERT(res_kc.decision == AdmissionDecision::Rejected_DeviceMismatch,
                   "Decision must be Rejected_DeviceMismatch");

    std::cout << "[PASS] Target Device and Board Mismatch Rejection\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Truncated and Corrupt Binary Rejection
// -----------------------------------------------------------------------------
static bool test_corrupt_and_truncated_rejection() {
    std::cout << "\n[5] test_corrupt_and_truncated_rejection...\n";

    AppleArtifactAdmissionController controller;
    AppleBootArtifact artifact;
    artifact.artifact_id = "kernelcache";
    artifact.kind = AppleArtifactKind::KernelCollection;
    artifact.target_device = "iPhone15,4";

    // 1. Truncated header (only 16 bytes, below 32-byte MachOHeader64)
    std::vector<std::uint8_t> trunc_buf(16, 0);
    AdmissionResult res_trunc = controller.evaluate_artifact(artifact, trunc_buf.data(), trunc_buf.size());
    STAGE96_ASSERT(!res_trunc.admitted, "Truncated buffer must be rejected");
    STAGE96_ASSERT(res_trunc.decision == AdmissionDecision::Rejected_Truncated,
                   "Decision must be Rejected_Truncated");

    // 2. Corrupt magic (0xDEADBEEF)
    std::vector<std::uint8_t> corrupt_buf(128, 0);
    *reinterpret_cast<std::uint32_t*>(corrupt_buf.data()) = 0xDEADBEEFu;
    AdmissionResult res_corrupt = controller.evaluate_artifact(artifact, corrupt_buf.data(), corrupt_buf.size());
    STAGE96_ASSERT(!res_corrupt.admitted, "Corrupt magic buffer must be rejected");
    STAGE96_ASSERT(res_corrupt.decision == AdmissionDecision::Rejected_MalformedHeader,
                   "Decision must be Rejected_MalformedHeader");

    std::cout << "[PASS] Truncated and Corrupt Binary Rejection\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: SHA-256 Checksum Verification
// -----------------------------------------------------------------------------
static bool test_sha256_verification() {
    std::cout << "\n[6] test_sha256_verification...\n";

    AppleArtifactAdmissionController controller;
    auto kernel_blob = create_synthetic_macho_arm64();
    std::string expected_hash = LinuxImageLoader::compute_sha256(kernel_blob.data(), kernel_blob.size());

    // 1. Matching SHA-256 -> Admitted
    AppleBootArtifact valid_art;
    valid_art.artifact_id = "kernelcache";
    valid_art.kind = AppleArtifactKind::KernelCollection;
    valid_art.target_device = "iPhone15,4";
    valid_art.sha256_hex = expected_hash;

    AdmissionResult res_valid = controller.evaluate_artifact(valid_art, kernel_blob.data(), kernel_blob.size());
    STAGE96_ASSERT(res_valid.admitted, "Matching SHA256 must be admitted");
    STAGE96_ASSERT(res_valid.decision == AdmissionDecision::Admitted, "Decision must be Admitted");
    STAGE96_ASSERT(res_valid.computed_sha256 == expected_hash, "Hash must match expected");

    // 2. Mismatched SHA-256 (tampered payload) -> Checksum Mismatch
    valid_art.sha256_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    AdmissionResult res_tampered = controller.evaluate_artifact(valid_art, kernel_blob.data(), kernel_blob.size());
    STAGE96_ASSERT(!res_tampered.admitted, "Tampered payload must be rejected");
    STAGE96_ASSERT(res_tampered.decision == AdmissionDecision::Rejected_ChecksumMismatch,
                   "Decision must be Rejected_ChecksumMismatch");

    std::cout << "[PASS] SHA-256 Checksum Verification (integrity validation confirmed)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Full Inventory Admission & Boot Readiness Resolution
// -----------------------------------------------------------------------------
static bool test_full_inventory_admission_evaluation() {
    std::cout << "\n[7] test_full_inventory_admission_evaluation...\n";

    AppleBootArtifactInventory inventory = AppleBootArtifactInventory::create_iphone15_template();
    AppleArtifactAdmissionController controller;

    // Prepare valid payloads for all required components
    std::map<std::string, std::vector<std::uint8_t>> loaded_blobs;
    loaded_blobs["kernelcache"] = create_synthetic_macho_arm64();
    loaded_blobs["devicetree"] = create_synthetic_devicetree_blob(0x18, 0x8120, "iPhone15,4");
    loaded_blobs["ramdisk"] = create_synthetic_apfs_buffer();
    loaded_blobs["apfs_sysvol"] = create_synthetic_apfs_buffer();
    loaded_blobs["trust_cache"] = std::vector<std::uint8_t>(64, 0x1);
    loaded_blobs["bootloader_iboot"] = std::vector<std::uint8_t>(256, 0xAA);

    auto results = controller.evaluate_inventory(inventory, loaded_blobs);
    STAGE96_ASSERT(!results.empty(), "Results must not be empty");

    std::string summary;
    bool bootable = controller.is_inventory_bootable(results, summary);
    if (!bootable) {
        std::cerr << "[TEST FAILED] is_inventory_bootable returned false: " << summary << "\n";
    }
    STAGE96_ASSERT(bootable, "Full inventory with valid payloads must be bootable");
    std::cout << "Inventory Bootability Summary: " << summary << "\n";

    // Now simulate missing required kernelcache
    loaded_blobs.erase("kernelcache");
    auto fail_results = controller.evaluate_inventory(inventory, loaded_blobs);
    std::string fail_summary;
    bool fail_bootable = controller.is_inventory_bootable(fail_results, fail_summary);
    STAGE96_ASSERT(!fail_bootable, "Missing kernelcache must fail boot readiness");
    STAGE96_ASSERT(fail_summary.find("kernelcache") != std::string::npos,
                   "Summary must name rejected kernelcache");

    std::cout << "[PASS] Full Inventory Admission & Boot Readiness Resolution\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Admission Markdown Report Generation
// -----------------------------------------------------------------------------
static bool test_admission_report_generation() {
    std::cout << "\n[8] test_admission_report_generation...\n";

    AppleBootArtifactInventory inventory = AppleBootArtifactInventory::create_iphone15_template();
    AppleArtifactAdmissionController controller;

    std::map<std::string, std::vector<std::uint8_t>> loaded_blobs;
    loaded_blobs["kernelcache"] = create_synthetic_macho_arm64();
    loaded_blobs["devicetree"] = create_synthetic_devicetree_blob(0x18, 0x8120, "iPhone15,4");
    loaded_blobs["ramdisk"] = create_synthetic_apfs_buffer();

    auto results = controller.evaluate_inventory(inventory, loaded_blobs);
    std::string report = controller.generate_admission_report(results);

    STAGE96_ASSERT(!report.empty(), "Report must not be empty");
    STAGE96_ASSERT(report.find("# Genuine iOS Artifact Admission Report") != std::string::npos,
                   "Header must be present");
    STAGE96_ASSERT(report.find("kernelcache") != std::string::npos, "Kernelcache row must be present");
    STAGE96_ASSERT(report.find("ADMITTED") != std::string::npos, "Admitted keyword must be present");
    STAGE96_ASSERT(report.find("REJECTED") != std::string::npos, "Rejected keyword must be present");

    std::cout << "[PASS] Admission Markdown Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 96: Artifact Compatibility & Admission Tests\n";
    std::cout << "========================================================\n";

    if (!test_valid_kernelcache_admission()) return 1;
    if (!test_architecture_rejection()) return 1;
    if (!test_encrypted_im4p_rejection()) return 1;
    if (!test_device_mismatch_rejection()) return 1;
    if (!test_corrupt_and_truncated_rejection()) return 1;
    if (!test_sha256_verification()) return 1;
    if (!test_full_inventory_admission_evaluation()) return 1;
    if (!test_admission_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 96 ARTIFACT ADMISSION SUITES PASSED!\n";
    return 0;
}
