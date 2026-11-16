#include <cstdint>
#include <iostream>
#include <string>

#include "device/apple_devicetree.h"
#include "device/kernel_boot_devicetree.h"

using namespace winios::device;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_apple_devicetree_generation_and_validation() {
    std::cout << "[Test 1] Apple DeviceTree Generation & Validation...\n";
    KernelBootDeviceTreeConfig cfg;
    cfg.boot_args = "-v serial=3 debug=0x144";
    cfg.ram_base = 0x800000000ULL;
    cfg.ram_size = 0x80000000ULL; // 2 GB
    cfg.num_cores = 6;
    cfg.p_cores = 2;
    cfg.e_cores = 4;

    auto dt_blob = KernelBootDeviceTreeBuilder::build_apple_devicetree(cfg);
    TEST_CHECK(!dt_blob.empty(), "Generated device tree blob must not be empty");

    std::string err;
    bool valid = KernelBootDeviceTreeBuilder::validate_apple_devicetree(dt_blob.data(), dt_blob.size(), &err);
    TEST_CHECK(valid, "Device tree validation must succeed: " + err);

    auto root = AppleDeviceTreeBuilder::deserialize(dt_blob.data(), dt_blob.size(), &err);
    TEST_CHECK(root != nullptr, "Deserialization must succeed: " + err);

    auto* chosen = root->find_child("chosen");
    TEST_CHECK(chosen != nullptr, "Must find /chosen");
    auto* prop_args = chosen->find_property("boot-args");
    TEST_CHECK(prop_args != nullptr, "Must find boot-args property");
    TEST_CHECK(std::string(reinterpret_cast<const char*>(prop_args->data.data())) == cfg.boot_args,
               "boot-args string must match input config");

    auto* cpus = root->find_child("cpus");
    TEST_CHECK(cpus != nullptr, "Must find /cpus");
    TEST_CHECK(cpus->children().size() == 6, "Must have exactly 6 CPU child nodes");

    auto* cpu0 = cpus->find_child("cpu0");
    TEST_CHECK(cpu0 != nullptr, "Must find cpu0");
    auto* comp0 = cpu0->find_property("compatible");
    TEST_CHECK(comp0 != nullptr && std::string(reinterpret_cast<const char*>(comp0->data.data())) == "apple,everest",
               "cpu0 must be performance core apple,everest");

    auto* cpu2 = cpus->find_child("cpu2");
    TEST_CHECK(cpu2 != nullptr, "Must find cpu2");
    auto* comp2 = cpu2->find_property("compatible");
    TEST_CHECK(comp2 != nullptr && std::string(reinterpret_cast<const char*>(comp2->data.data())) == "apple,sawtooth",
               "cpu2 must be efficiency core apple,sawtooth");

    std::cout << "  -> Apple DeviceTree generation & validation passed.\n";
}

void test_fdt_generation() {
    std::cout << "[Test 2] Standard FDT Blob Generation...\n";
    KernelBootDeviceTreeConfig cfg;
    auto fdt_blob = KernelBootDeviceTreeBuilder::build_fdt(cfg);

    TEST_CHECK(fdt_blob.size() >= 32, "FDT blob must contain at least FDT header");
    std::uint32_t magic = (static_cast<std::uint32_t>(fdt_blob[0]) << 24) |
                          (static_cast<std::uint32_t>(fdt_blob[1]) << 16) |
                          (static_cast<std::uint32_t>(fdt_blob[2]) << 8)  |
                          (static_cast<std::uint32_t>(fdt_blob[3]));
    TEST_CHECK(magic == 0xD00DFEEDu, "FDT magic must be 0xD00DFEED");
    std::cout << "  -> Standard FDT blob generation passed.\n";
}

void test_corrupted_devicetree_rejection() {
    std::cout << "[Test 3] Corrupted DeviceTree Buffer Rejection...\n";
    std::uint8_t garbage[16] = {0xFF, 0xFE, 0xFD, 0xFC};
    std::string err;
    bool valid = KernelBootDeviceTreeBuilder::validate_apple_devicetree(garbage, sizeof(garbage), &err);
    TEST_CHECK(!valid, "Must reject corrupted garbage buffer");
    std::cout << "  -> Corrupted buffer rejection passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.9 Device Tree Boot Tests ===\n";
    test_apple_devicetree_generation_and_validation();
    test_fdt_generation();
    test_corrupted_devicetree_rejection();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.9 DEVICE TREE BOOT TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}
