#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "firmware/apple_boot_args.h"

#define DT_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::device;
using namespace winios::firmware;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Apple Boot Arguments Structure and Validation
// -----------------------------------------------------------------------------
bool test_apple_boot_args_structure_and_validation() {
    std::cout << "[1] test_apple_boot_args_structure_and_validation...\n";

    AppleBootVideo video;
    video.v_baseAddr = 0x50000000ULL;
    video.v_width = 1179;
    video.v_height = 2556;
    video.v_depth = 32;
    video.v_rowBytes = 1179 * 4;

    auto args = AppleBootArgsBuilder::create(
        0x40000000ULL,               // physBase
        0xFFFFFFF007004000ULL,       // virtBase
        6ULL * 1024 * 1024 * 1024,   // 6 GB RAM
        0x41000000ULL,               // topOfKernelData
        0x47FE0000ULL,               // deviceTreeP
        0x4000,                      // deviceTreeLength
        "-v debug=0x144 serial=3",
        video);

    std::string err;
    DT_ASSERT(AppleBootArgsBuilder::validate(args, &err), "Boot args validation succeeded");
    DT_ASSERT(args.Revision == kAppleBootArgsRevision2, "Revision 2");
    DT_ASSERT(args.Version == kAppleBootArgsVersion2, "Version 2");
    DT_ASSERT(args.physBase == 0x40000000ULL, "physBase is 0x40000000");
    DT_ASSERT(args.virtBase == 0xFFFFFFF007004000ULL, "virtBase matches kernel mapping");
    DT_ASSERT(args.memSize == 6ULL * 1024 * 1024 * 1024, "memSize is 6GB");
    DT_ASSERT(args.Video.v_width == 1179 && args.Video.v_height == 2556, "Video dimensions match iPhone 15");
    DT_ASSERT(std::string(args.CommandLine) == "-v debug=0x144 serial=3", "CommandLine matches");

    std::cout << "[PASS] Apple Boot Arguments Structure and Validation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Apple DeviceTree Node Hierarchy and Path Queries
// -----------------------------------------------------------------------------
bool test_apple_devicetree_node_hierarchy() {
    std::cout << "[2] test_apple_devicetree_node_hierarchy...\n";

    AppleDeviceTreeNode root("");
    root.add_property_string("model", "TestModel");
    root.add_property_u32("test-u32", 12345u);
    root.add_property_u64("test-u64", 0x1122334455667788ULL);
    root.add_property_empty("test-empty");

    auto* child = root.add_child_node("arm-io");
    child->add_property_string("compatible", "apple,arm-io");

    auto* sub = child->add_child_node("aic");
    sub->add_property_string("compatible", "apple,aic-2");

    DT_ASSERT(root.find_property("model") != nullptr, "Found property 'model'");
    DT_ASSERT(root.find_property("non-existent") == nullptr, "Non-existent property is null");

    // Path queries
    const auto* found_child = root.find_path("/arm-io");
    DT_ASSERT(found_child != nullptr && found_child->name() == "arm-io", "find_path('/arm-io')");

    const auto* found_sub = root.find_path("arm-io/aic");
    DT_ASSERT(found_sub != nullptr && found_sub->name() == "aic", "find_path('arm-io/aic')");

    DT_ASSERT(root.find_path("arm-io/missing") == nullptr, "Missing path returns null");

    std::cout << "[PASS] Apple DeviceTree Node Hierarchy and Path Queries\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Apple DeviceTree Serialization and Roundtrip
// -----------------------------------------------------------------------------
bool test_apple_devicetree_serialization_roundtrip() {
    std::cout << "[3] test_apple_devicetree_serialization_roundtrip...\n";

    AppleDeviceTreeNode orig("root");
    orig.add_property_string("name", "root");
    orig.add_property_string("model", "AppleMockSoC");
    orig.add_property_u32("timebase", 24000000u);

    auto* ch = orig.add_child_node("chosen");
    ch->add_property_string("name", "chosen");
    ch->add_property_string("boot-args", "-v serial=3");

    auto binary = AppleDeviceTreeBuilder::serialize(orig);
    DT_ASSERT(!binary.empty(), "Serialized binary is non-empty");

    std::string err;
    auto restored = AppleDeviceTreeBuilder::deserialize(binary.data(), binary.size(), &err);
    DT_ASSERT(restored != nullptr, "Deserialization succeeded");
    DT_ASSERT(err.empty(), "No error on deserialization");

    const auto* prop_model = restored->find_property("model");
    DT_ASSERT(prop_model != nullptr, "Restored property 'model' exists");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(prop_model->data.data())) == "AppleMockSoC", "Model matches");

    const auto* res_ch = restored->find_path("/chosen");
    DT_ASSERT(res_ch != nullptr, "Restored child '/chosen' exists");
    const auto* prop_args = res_ch->find_property("boot-args");
    DT_ASSERT(prop_args != nullptr, "Restored property 'boot-args' exists");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(prop_args->data.data())) == "-v serial=3", "boot-args matches");

    std::cout << "[PASS] Apple DeviceTree Serialization and Roundtrip\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: iPhone 15 Profile Generation (iPhone15,4 / A16 Bionic / T8120)
// -----------------------------------------------------------------------------
bool test_iphone15_devicetree_profile() {
    std::cout << "[4] test_iphone15_devicetree_profile...\n";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL,
        6ULL * 1024 * 1024 * 1024,
        "-v debug=0x144 serial=3");

    DT_ASSERT(!dtb.empty(), "iPhone 15 DTB generated");

    std::string err;
    auto tree = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size(), &err);
    DT_ASSERT(tree != nullptr, "iPhone 15 DTB deserialized");

    // Root properties
    const auto* prop_model = tree->find_property("model");
    DT_ASSERT(prop_model != nullptr, "Property 'model' exists");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(prop_model->data.data())) == "iPhone15,4",
              "Model is iPhone15,4");

    const auto* prop_target = tree->find_property("target-type");
    DT_ASSERT(prop_target != nullptr, "Property 'target-type' exists");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(prop_target->data.data())) == "D37AP",
              "Target type is D37AP (iPhone 15 board)");

    const auto* prop_chip = tree->find_property("chip-id");
    DT_ASSERT(prop_chip != nullptr && prop_chip->data.size() == 4, "Property 'chip-id' exists");
    std::uint32_t chip_id = *reinterpret_cast<const std::uint32_t*>(prop_chip->data.data());
    DT_ASSERT(chip_id == 0x8120u, "Chip ID is 0x8120 (A16 Bionic / T8120)");

    const auto* prop_board = tree->find_property("board-id");
    DT_ASSERT(prop_board != nullptr && prop_board->data.size() == 4, "Property 'board-id' exists");
    std::uint32_t board_id = *reinterpret_cast<const std::uint32_t*>(prop_board->data.data());
    DT_ASSERT(board_id == 0x18u, "Board ID is 0x18");

    // Also verify iPhone 14 Pro DTB fixture (iPhone15,2 / D73AP)
    auto dtb14 = AppleDeviceTreeBuilder::create_iphone14_pro_devicetree();
    auto tree14 = AppleDeviceTreeBuilder::deserialize(dtb14.data(), dtb14.size(), &err);
    DT_ASSERT(tree14 != nullptr, "iPhone 14 Pro DTB deserialized");
    const auto* p14_model = tree14->find_property("model");
    DT_ASSERT(p14_model != nullptr && std::string(reinterpret_cast<const char*>(p14_model->data.data())) == "iPhone15,2",
              "iPhone 14 Pro model is iPhone15,2");
    const auto* p14_target = tree14->find_property("target-type");
    DT_ASSERT(p14_target != nullptr && std::string(reinterpret_cast<const char*>(p14_target->data.data())) == "D73AP",
              "iPhone 14 Pro target is D73AP");

    std::cout << "[PASS] iPhone 15 & iPhone 14 Pro Profile Generation Verified\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: iPhone 15 CPU Topology (2 Everest + 4 Sawtooth Cores)
// -----------------------------------------------------------------------------
bool test_iphone15_cpu_topology() {
    std::cout << "[5] test_iphone15_cpu_topology...\n";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto tree = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    DT_ASSERT(tree != nullptr, "Tree deserialized");

    const auto* cpus = tree->find_path("/cpus");
    DT_ASSERT(cpus != nullptr, "Found '/cpus'");
    DT_ASSERT(cpus->children().size() == 6, "A16 Bionic has exactly 6 CPU cores");

    // Performance Cores: cpu0, cpu1 (Everest)
    const auto* cpu0 = cpus->find_child("cpu0");
    DT_ASSERT(cpu0 != nullptr, "Found cpu0");
    const auto* c0_comp = cpu0->find_property("compatible");
    DT_ASSERT(c0_comp != nullptr, "cpu0 has compatible property");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(c0_comp->data.data())) == "apple,everest",
              "cpu0 is Performance core 'apple,everest'");

    // Efficiency Cores: cpu2..cpu5 (Sawtooth)
    const auto* cpu2 = cpus->find_child("cpu2");
    DT_ASSERT(cpu2 != nullptr, "Found cpu2");
    const auto* c2_comp = cpu2->find_property("compatible");
    DT_ASSERT(c2_comp != nullptr, "cpu2 has compatible property");
    DT_ASSERT(std::string(reinterpret_cast<const char*>(c2_comp->data.data())) == "apple,sawtooth",
              "cpu2 is Efficiency core 'apple,sawtooth'");

    std::cout << "[PASS] iPhone 15 CPU Topology (2 Everest + 4 Sawtooth Cores)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: iPhone 15 Apple Silicon Peripheral Devices (AIC 2, PMGR, Framebuffer)
// -----------------------------------------------------------------------------
bool test_iphone15_armio_peripherals() {
    std::cout << "[6] test_iphone15_armio_peripherals...\n";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 6ULL * 1024 * 1024 * 1024, "-v", 0x50000000ULL);
    auto tree = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    DT_ASSERT(tree != nullptr, "Tree deserialized");

    // 1. AIC 2 Interrupt Controller
    const auto* aic = tree->find_path("/arm-io/aic");
    DT_ASSERT(aic != nullptr, "Found '/arm-io/aic'");
    DT_ASSERT(aic->find_property("interrupt-controller") != nullptr, "AIC has interrupt-controller flag");

    // 2. UART0 Console
    const auto* uart = tree->find_path("/arm-io/uart0");
    DT_ASSERT(uart != nullptr, "Found '/arm-io/uart0'");
    const auto* u_irq = uart->find_property("interrupts");
    DT_ASSERT(u_irq != nullptr && *reinterpret_cast<const std::uint32_t*>(u_irq->data.data()) == 33u,
              "UART0 mapped to interrupt 33");

    // 3. PMGR Power Manager
    const auto* pmgr = tree->find_path("/arm-io/pmgr");
    DT_ASSERT(pmgr != nullptr, "Found '/arm-io/pmgr'");

    // 4. Display Framebuffer
    const auto* disp = tree->find_path("/arm-io/disp0");
    DT_ASSERT(disp != nullptr, "Found '/arm-io/disp0'");
    const auto* p_w = disp->find_property("width");
    const auto* p_h = disp->find_property("height");
    const auto* p_d = disp->find_property("depth");
    DT_ASSERT(p_w && *reinterpret_cast<const std::uint32_t*>(p_w->data.data()) == 1179u, "Width is 1179");
    DT_ASSERT(p_h && *reinterpret_cast<const std::uint32_t*>(p_h->data.data()) == 2556u, "Height is 2556");
    DT_ASSERT(p_d && *reinterpret_cast<const std::uint32_t*>(p_d->data.data()) == 32u, "Depth is 32-bit");

    std::cout << "[PASS] iPhone 15 Apple Silicon Peripheral Devices (AIC 2, PMGR, Framebuffer)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Boot Arguments and DeviceTree Pointer Binding
// -----------------------------------------------------------------------------
bool test_boot_args_and_devicetree_binding() {
    std::cout << "[7] test_boot_args_and_devicetree_binding...\n";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleBootVideo video;
    video.v_baseAddr = 0x50000000ULL;
    video.v_width = 1179;
    video.v_height = 2556;
    video.v_depth = 32;
    video.v_rowBytes = 1179 * 4;

    const std::uint64_t dtb_phys = 0x47FE0000ULL;
    auto args = AppleBootArgsBuilder::create(
        0x40000000ULL,
        0xFFFFFFF007004000ULL,
        6ULL * 1024 * 1024 * 1024,
        0x41000000ULL,
        dtb_phys,
        static_cast<std::uint32_t>(dtb.size()),
        "-v debug=0x144 serial=3",
        video);

    DT_ASSERT(args.deviceTreeP == dtb_phys, "deviceTreeP bound to physical address");
    DT_ASSERT(args.deviceTreeLength == dtb.size(), "deviceTreeLength matches serialized size");

    std::string err;
    DT_ASSERT(AppleBootArgsBuilder::validate(args, &err), "Complete boot args validated");

    std::cout << "[PASS] Boot Arguments and DeviceTree Pointer Binding\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Apple DeviceTree & Boot Arguments Tests ===\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_apple_boot_args_structure_and_validation, "test_apple_boot_args_structure_and_validation");
    run_test(test_apple_devicetree_node_hierarchy, "test_apple_devicetree_node_hierarchy");
    run_test(test_apple_devicetree_serialization_roundtrip, "test_apple_devicetree_serialization_roundtrip");
    run_test(test_iphone15_devicetree_profile, "test_iphone15_devicetree_profile");
    run_test(test_iphone15_cpu_topology, "test_iphone15_cpu_topology");
    run_test(test_iphone15_armio_peripherals, "test_iphone15_armio_peripherals");
    run_test(test_boot_args_and_devicetree_binding, "test_boot_args_and_devicetree_binding");

    std::cout << "\nApple DeviceTree Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
