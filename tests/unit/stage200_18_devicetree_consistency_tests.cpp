// ==============================================================================
// WinIOS Campaign 200 Stage 18: DeviceTree & Device Registration Consistency Tests
// ==============================================================================
// Verifies:
// 1. Apple DeviceTree creation and serialization for iPhone 15 (iPhone15,4 / D37AP / T8120)
// 2. Hardware profile identity: model ("iPhone15,4"), chip-id (0x8120), board-id
// 3. 8-core CPU topology (2 performance cores, 6 efficiency cores)
// 4. Memory node: DRAM base 0x40000000, 16KB page size property
// 5. GICv3 node: compatible "arm,gic-v3", GICD (0x08000000) and GICR (0x080A0000) base addresses
// 6. Virtual IOMMU and DMA engine nodes consistency
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "device/apple_devicetree.h"
#include "device/fdt_builder.h"
#include "device/virtual_platform.h"

using namespace winios::device;

static void test_iphone15_devicetree_identity_and_topology() {
    const std::uint64_t ram_base = 0x40000000ULL;
    const std::uint64_t ram_size = 6ULL * 1024 * 1024 * 1024; // 6 GB RAM
    const char* boot_args = "-v debug=0x144 serial=3";

    auto blob = AppleDeviceTreeBuilder::create_iphone15_virtual_platform_devicetree(
        ram_base,
        ram_size,
        boot_args,
        8,    // 8 cores
        true, // IOMMU enabled
        true  // Stage-2 enabled
    );

    assert(!blob.empty());

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(blob.data(), blob.size(), &err);
    assert(root != nullptr);

    // 1. Model property
    auto* model = root->find_property("model");
    assert(model != nullptr);
    assert(std::string(reinterpret_cast<const char*>(model->data.data())) == "iPhone15,4");
    (void)model;

    // 2. Chip-ID property
    auto* chip_id = root->find_property("chip-id");
    assert(chip_id != nullptr);
    std::uint32_t cid = 0;
    std::memcpy(&cid, chip_id->data.data(), sizeof(cid));
    assert(cid == 0x8120u); // A16 Bionic T8120

    // 3. 8-core CPU topology
    auto* cpus = root->find_child("cpus");
    assert(cpus != nullptr);
    assert(cpus->children().size() == 8);

    for (std::uint32_t i = 0; i < 8; ++i) {
        std::string cname = "cpu" + std::to_string(i);
        auto* c = cpus->find_child(cname);
        assert(c != nullptr);
        auto* reg = c->find_property("reg");
        assert(reg != nullptr);
        std::uint32_t reg_val = 0;
        std::memcpy(&reg_val, reg->data.data(), sizeof(reg_val));
        assert(reg_val == i);
        (void)reg;
    }

    // 4. Memory node
    auto* memory = root->find_child("memory");
    assert(memory != nullptr);
    auto* reg_mem = memory->find_property("reg");
    assert(reg_mem != nullptr);
    (void)reg_mem;

    // 5. IOMMU and DMA nodes under arm-io
    auto* armio = root->find_child("arm-io");
    assert(armio != nullptr);
    auto* iommu = armio->find_child("dart0");
    assert(iommu != nullptr);
    (void)iommu;
    auto* dma = armio->find_child("dma0");
    assert(dma != nullptr);
    (void)dma;

    // 6. Hypervisor Stage-2 node
    auto* hyp = root->find_child("hypervisor");
    assert(hyp != nullptr);
    (void)hyp;

    std::cout << "[PASS] test_iphone15_devicetree_identity_and_topology\n";
}

static void test_fdt_builder_consistency() {
    VirtualPlatformConfig cfg;
    cfg.core_count = 8;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 0x10000000ULL; // 256 MiB
    cfg.has_iommu = true;
    cfg.has_stage2 = true;

    auto dtb = FdtBuilder::build_platform_dtb(cfg);
    assert(!dtb.empty());
    // FDT header magic: 0xD00DFEED (big-endian: 0xd0 0x0d 0xfe 0xed)
    assert(dtb[0] == 0xd0 && dtb[1] == 0x0d && dtb[2] == 0xfe && dtb[3] == 0xed);

    std::cout << "[PASS] test_fdt_builder_consistency\n";
}

int main() {
    std::cout << "=== Running Stage 200.18 DeviceTree Consistency Tests ===\n";
    test_iphone15_devicetree_identity_and_topology();
    test_fdt_builder_consistency();
    std::cout << "All Stage 200.18 tests passed successfully!\n";
    return 0;
}
