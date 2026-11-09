#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/apple_boot_artifact_inventory.h"

#define STAGE95_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Stage 95 Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::loader;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Template Inventory Generation for iPhone 15 & iOS 17.5.1
// -----------------------------------------------------------------------------
bool test_iphone15_template_generation() {
    std::cout << "[1] test_iphone15_template_generation...\n";

    auto inv = AppleBootArtifactInventory::create_iphone15_template("C:/iOS_17_5_1");
    STAGE95_ASSERT(inv.count() >= 7, "Inventory must contain at least 7 boot components");

    for (const auto& a : inv.all_artifacts()) {
        STAGE95_ASSERT(a.target_device == "iPhone15,4", "All artifacts target iPhone15,4");
        STAGE95_ASSERT(a.target_os_version.find("iOS 17.5.1") != std::string::npos, "Target OS is iOS 17.5.1");
    }

    std::cout << "[PASS] Template Inventory Generation for iPhone 15 & iOS 17.5.1\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Artifact Classification
// -----------------------------------------------------------------------------
bool test_artifact_classification() {
    std::cout << "[2] test_artifact_classification...\n";

    auto inv = AppleBootArtifactInventory::create_iphone15_template();
    STAGE95_ASSERT(inv.find_by_kind(AppleArtifactKind::KernelCollection) != nullptr, "KernelCollection exists");
    STAGE95_ASSERT(inv.find_by_kind(AppleArtifactKind::DeviceTree) != nullptr, "DeviceTree exists");
    STAGE95_ASSERT(inv.find_by_kind(AppleArtifactKind::Ramdisk) != nullptr, "Ramdisk exists");
    STAGE95_ASSERT(inv.find_by_kind(AppleArtifactKind::ApfsSystemVolume) != nullptr, "APFS System Volume exists");
    STAGE95_ASSERT(inv.find_by_kind(AppleArtifactKind::SepFirmware) != nullptr, "SEP firmware exists");

    std::cout << "[PASS] Artifact Classification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Querying Artifacts by Kind and by Artifact ID
// -----------------------------------------------------------------------------
bool test_querying_artifacts() {
    std::cout << "[3] test_querying_artifacts...\n";

    auto inv = AppleBootArtifactInventory::create_iphone15_template();
    const auto* kernel = inv.find_by_id("kernelcache.release.iphone15");
    STAGE95_ASSERT(kernel != nullptr, "Kernel found by ID");
    STAGE95_ASSERT(kernel->kind == AppleArtifactKind::KernelCollection, "Kind is KernelCollection");

    const auto* dt = inv.find_by_id("DeviceTree.d73ap.im4p");
    STAGE95_ASSERT(dt != nullptr, "DeviceTree found by ID");
    STAGE95_ASSERT(dt->kind == AppleArtifactKind::DeviceTree, "Kind is DeviceTree");

    STAGE95_ASSERT(inv.find_by_id("nonexistent_id") == nullptr, "Returns nullptr for unknown ID");

    std::cout << "[PASS] Querying Artifacts by Kind and by Artifact ID\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Custom Artifact Registration and Ingestion Updates
// -----------------------------------------------------------------------------
bool test_custom_artifact_registration() {
    std::cout << "[4] test_custom_artifact_registration...\n";

    AppleBootArtifactInventory inv;
    STAGE95_ASSERT(inv.count() == 0, "Initial inventory is empty");

    AppleBootArtifact custom;
    custom.artifact_id = "custom_kernel.bin";
    custom.kind = AppleArtifactKind::KernelCollection;
    custom.file_size = 35000000;
    custom.is_present = true;
    inv.register_artifact(custom);

    STAGE95_ASSERT(inv.count() == 1, "Count is 1 after registration");
    STAGE95_ASSERT(inv.count_present() == 1, "Present count is 1");

    // Update existing artifact
    custom.file_size = 36000000;
    inv.register_artifact(custom);
    STAGE95_ASSERT(inv.count() == 1, "Count remains 1 after update");
    STAGE95_ASSERT(inv.find_by_id("custom_kernel.bin")->file_size == 36000000, "Size updated");

    std::cout << "[PASS] Custom Artifact Registration and Ingestion Updates\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Encrypted Artifact Detection & Security Warning
// -----------------------------------------------------------------------------
bool test_encrypted_artifact_detection() {
    std::cout << "[5] test_encrypted_artifact_detection...\n";

    auto inv = AppleBootArtifactInventory::create_iphone15_template();
    const auto* sep = inv.find_by_kind(AppleArtifactKind::SepFirmware);
    STAGE95_ASSERT(sep != nullptr && sep->is_encrypted, "SEP firmware correctly marked encrypted");

    // If kernel is marked encrypted, boot readiness must reject it
    AppleBootArtifact enc_kernel;
    enc_kernel.artifact_id = "kernelcache.encrypted";
    enc_kernel.kind = AppleArtifactKind::KernelCollection;
    enc_kernel.is_present = true;
    enc_kernel.is_encrypted = true;

    AppleBootArtifactInventory enc_inv;
    enc_inv.register_artifact(enc_kernel);

    std::string report;
    STAGE95_ASSERT(!enc_inv.verify_boot_readiness(report), "Readiness rejects encrypted kernel");
    STAGE95_ASSERT(report.find("[BLOCKED]") != std::string::npos, "Report contains [BLOCKED]");

    std::cout << "[PASS] Encrypted Artifact Detection & Security Warning\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Boot Readiness Evaluation (Missing Component Detection)
// -----------------------------------------------------------------------------
bool test_missing_component_detection() {
    std::cout << "[6] test_missing_component_detection...\n";

    AppleBootArtifactInventory inv = AppleBootArtifactInventory::create_iphone15_template();
    // Default template has is_present = false
    std::string report;
    STAGE95_ASSERT(!inv.verify_boot_readiness(report), "Readiness must fail when artifacts not present");
    STAGE95_ASSERT(report.find("[MISSING] KernelCollection") != std::string::npos, "Flags missing kernel");
    STAGE95_ASSERT(report.find("[MISSING] DeviceTree") != std::string::npos, "Flags missing DeviceTree");

    std::cout << "[PASS] Boot Readiness Evaluation (Missing Component Detection)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Full Boot Readiness Resolution when Required Set is Admitted
// -----------------------------------------------------------------------------
bool test_full_boot_readiness_resolution() {
    std::cout << "[7] test_full_boot_readiness_resolution...\n";

    AppleBootArtifactInventory inv;

    AppleBootArtifact kernel;
    kernel.artifact_id = "kernelcache";
    kernel.kind = AppleArtifactKind::KernelCollection;
    kernel.is_present = true;
    kernel.is_admitted = true;
    inv.register_artifact(kernel);

    AppleBootArtifact dt;
    dt.artifact_id = "DeviceTree";
    dt.kind = AppleArtifactKind::DeviceTree;
    dt.is_present = true;
    dt.is_admitted = true;
    inv.register_artifact(dt);

    AppleBootArtifact rd;
    rd.artifact_id = "RestoreRamDisk";
    rd.kind = AppleArtifactKind::Ramdisk;
    rd.is_present = true;
    rd.is_admitted = true;
    inv.register_artifact(rd);

    std::string report;
    STAGE95_ASSERT(inv.verify_boot_readiness(report), "Readiness succeeds when core set is present");
    STAGE95_ASSERT(report.find("[READY] KernelCollection present") != std::string::npos, "Kernel reported ready");
    STAGE95_ASSERT(report.find("[READY] DeviceTree present") != std::string::npos, "DeviceTree reported ready");
    STAGE95_ASSERT(report.find("[READY] Root filesystem volume verified") != std::string::npos, "RootFS reported ready");

    std::cout << "[PASS] Full Boot Readiness Resolution when Required Set is Admitted\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Machine-Readable Inventory Markdown Report Generation
// -----------------------------------------------------------------------------
bool test_inventory_report_generation() {
    std::cout << "[8] test_inventory_report_generation...\n";

    auto inv = AppleBootArtifactInventory::create_iphone15_template();
    const std::string md = inv.dump_inventory_report();
    STAGE95_ASSERT(!md.empty(), "Markdown report must not be empty");
    STAGE95_ASSERT(md.find("# Apple iOS Boot Artifact Inventory") != std::string::npos, "Header in report");
    STAGE95_ASSERT(md.find("kernelcache.release.iphone15") != std::string::npos, "Contains kernel entry");
    STAGE95_ASSERT(md.find("DeviceTree.d73ap.im4p") != std::string::npos, "Contains DeviceTree entry");

    std::cout << "[PASS] Machine-Readable Inventory Markdown Report Generation\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 95: Genuine iOS Boot Artifact Inventory Tests\n";
    std::cout << "========================================================\n\n";

    bool success = true;
    success &= test_iphone15_template_generation();
    success &= test_artifact_classification();
    success &= test_querying_artifacts();
    success &= test_custom_artifact_registration();
    success &= test_encrypted_artifact_detection();
    success &= test_missing_component_detection();
    success &= test_full_boot_readiness_resolution();
    success &= test_inventory_report_generation();

    if (success) {
        std::cout << "ALL 8 STAGE 95 BOOT ARTIFACT INVENTORY SUITES PASSED!\n";
        return 0;
    } else {
        std::cerr << "STAGE 95 TEST FAILED!\n";
        return 1;
    }
}
