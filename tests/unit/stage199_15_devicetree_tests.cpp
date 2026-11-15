// ============================================================================
// Stage 199.15: Device Tree Architecture Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "device/fdt_builder.h"
#include "device/virtual_platform.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios::device;

// 1. Validate Apple DeviceTree serialization and structure for iPhone 15 Virtual Platform
static bool test_apple_devicetree_virtual_platform() {
    auto blob = AppleDeviceTreeBuilder::create_iphone15_virtual_platform_devicetree(
        0x40000000ULL,
        6ULL * 1024 * 1024 * 1024,
        "-v debug=0x144 serial=3",
        8,    // 8 cores
        true, // IOMMU enabled
        true  // Stage-2 enabled
    );

    TEST_ASSERT(!blob.empty(), "Serialized Apple DTB blob must not be empty");

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(blob.data(), blob.size(), &err);
    TEST_ASSERT(root != nullptr, ("Failed to deserialize Apple DeviceTree: " + err).c_str());

    // Check root properties
    auto* model = root->find_property("model");
    TEST_ASSERT(model != nullptr, "model property must exist");
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(model->data.data())) == "iPhone15,4", "Model must be iPhone15,4");

    auto* chip_id = root->find_property("chip-id");
    TEST_ASSERT(chip_id != nullptr, "chip-id property must exist");
    std::uint32_t cid = 0;
    std::memcpy(&cid, chip_id->data.data(), sizeof(cid));
    TEST_ASSERT(cid == 0x8120u, "chip-id must be 0x8120 (A16 Bionic)");

    // Check 8-core CPU topology
    auto* cpus = root->find_child("cpus");
    TEST_ASSERT(cpus != nullptr, "/cpus node must exist");
    TEST_ASSERT(cpus->children().size() == 8, "Expected 8 CPU children in virtual platform DT");

    for (std::uint32_t i = 0; i < 8; ++i) {
        std::string cname = "cpu" + std::to_string(i);
        auto* c = cpus->find_child(cname);
        TEST_ASSERT(c != nullptr, ("Child " + cname + " must exist").c_str());
        auto* comp = c->find_property("compatible");
        TEST_ASSERT(comp != nullptr, "compatible must exist on cpu node");
        std::string comp_str(reinterpret_cast<const char*>(comp->data.data()));
        if (i < 2) {
            TEST_ASSERT(comp_str == "apple,everest", "Cores 0-1 must be apple,everest");
        } else {
            TEST_ASSERT(comp_str == "apple,sawtooth", "Cores 2-7 must be apple,sawtooth");
        }
    }

    // Check /arm-io/dart0 (Virtual IOMMU)
    auto* dart = root->find_path("arm-io/dart0");
    TEST_ASSERT(dart != nullptr, "/arm-io/dart0 node must exist");
    auto* dart_comp = dart->find_property("compatible");
    TEST_ASSERT(dart_comp != nullptr, "dart0 compatible property must exist");
    auto* iommu_cells = dart->find_property("#iommu-cells");
    TEST_ASSERT(iommu_cells != nullptr, "dart0 must specify #iommu-cells");
    std::uint32_t cells = 0;
    std::memcpy(&cells, iommu_cells->data.data(), sizeof(cells));
    TEST_ASSERT(cells == 1u, "#iommu-cells must be 1");

    // Check /arm-io/dma0 (Virtual DMA Engine with IOMMU linkage)
    auto* dma = root->find_path("arm-io/dma0");
    TEST_ASSERT(dma != nullptr, "/arm-io/dma0 node must exist");
    auto* iommus = dma->find_property("iommus");
    TEST_ASSERT(iommus != nullptr, "dma0 must reference iommus");
    TEST_ASSERT(iommus->data.size() == 8, "iommus specifier must be 2 uint32 cells (8 bytes)");

    // Check /hypervisor (Stage-2 MMU node)
    auto* hyp = root->find_child("hypervisor");
    TEST_ASSERT(hyp != nullptr, "/hypervisor node must exist");
    auto* pg = hyp->find_property("page-granule");
    TEST_ASSERT(pg != nullptr, "page-granule property must exist");
    std::uint32_t granule = 0;
    std::memcpy(&granule, pg->data.data(), sizeof(granule));
    TEST_ASSERT(granule == 16384u, "Stage-2 page-granule must be 16384 (16KB)");

    std::cout << "[PASS] test_apple_devicetree_virtual_platform\n";
    return true;
}

// 2. Validate Standard FDT / DTB generation with SMMUv3 and Hypervisor nodes
static bool test_fdt_virtual_platform_generation() {
    VirtualPlatformConfig config;
    config.core_count = 8;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x10000000ULL; // 256 MiB
    config.has_iommu = true;
    config.has_stage2 = true;

    auto dtb = FdtBuilder::build_platform_dtb(config, "console=ttyAMA0 root=/dev/ram0 rw");
    TEST_ASSERT(!dtb.empty(), "Platform DTB must not be empty");

    FdtReader reader(dtb);
    TEST_ASSERT(reader.is_valid(), "Platform DTB must be valid FDT format");

    auto root = reader.root();
    TEST_ASSERT(root != nullptr, "FDT root must exist");

    // Check cpus node has 8 children
    auto* cpus = root->find_child("cpus");
    TEST_ASSERT(cpus != nullptr, "FDT /cpus node must exist");
    TEST_ASSERT(cpus->children.size() == 8, "FDT /cpus must have 8 cores");

    // Check SMMU node
    auto* smmu = root->find_child("smmu@a000000");
    TEST_ASSERT(smmu != nullptr, "smmu@a000000 node must exist");
    auto* smmu_comp = smmu->find_property("compatible");
    TEST_ASSERT(smmu_comp != nullptr, "smmu compatible property must exist");
    std::string comp_val(reinterpret_cast<const char*>(smmu_comp->data.data()));
    TEST_ASSERT(comp_val.find("arm,smmu-v3") != std::string::npos, "SMMU must be arm,smmu-v3 compatible");

    // Check DMA node with iommus property
    auto* dma = root->find_child("dma@10000000");
    TEST_ASSERT(dma != nullptr, "dma@10000000 node must exist");
    auto* dma_iommus = dma->find_property("iommus");
    TEST_ASSERT(dma_iommus != nullptr, "dma iommus property must exist");

    // Check hypervisor node
    auto* hyp = root->find_child("hypervisor");
    TEST_ASSERT(hyp != nullptr, "hypervisor node must exist");
    auto* s2_pg = hyp->find_property("arm,stage2-page-size");
    TEST_ASSERT(s2_pg != nullptr, "arm,stage2-page-size property must exist");

    std::cout << "[PASS] test_fdt_virtual_platform_generation\n";
    return true;
}

int main() {
    std::cout << "--- Stage 199.15: Device Tree Architecture Tests ---\n";
    if (!test_apple_devicetree_virtual_platform()) return 1;
    if (!test_fdt_virtual_platform_generation()) return 1;

    std::cout << "All Stage 199.15 tests passed successfully!\n";
    return 0;
}
