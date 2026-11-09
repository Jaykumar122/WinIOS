#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "device/fdt_builder.h"

#define STAGE75_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Header Construction, Magic, and Versioning
// -----------------------------------------------------------------------------
bool test_fdt_raw_header_magic_and_version() {
    std::cout << "[1] test_fdt_raw_header_magic_and_version...\n";

    FdtBuilder builder;
    builder.add_property_string("test", "value");
    std::vector<std::uint8_t> dtb = builder.build();

    STAGE75_ASSERT(dtb.size() >= sizeof(FdtRawHeader), "DTB size must be at least header size");

    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Reader should validate fresh DTB");
    STAGE75_ASSERT(reader.version() == 17, "FDT version must be 17");
    STAGE75_ASSERT(reader.total_size() == dtb.size(), "Total size must match buffer size");

    std::cout << "[PASS] Header Construction, Magic, and Versioning\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Memory Reservation Block
// -----------------------------------------------------------------------------
bool test_mem_reserve_block() {
    std::cout << "[2] test_mem_reserve_block...\n";

    FdtBuilder builder;
    builder.add_mem_reserve(0x40000000ULL, 0x10000ULL);
    builder.add_mem_reserve(0x47000000ULL, 0x20000ULL);
    std::vector<std::uint8_t> dtb = builder.build();

    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Reader must validate DTB with reservations");
    STAGE75_ASSERT(reader.mem_reserves().size() == 2, "Must contain 2 reservations");
    STAGE75_ASSERT(reader.mem_reserves()[0].address == 0x40000000ULL, "Reserve 0 address mismatch");
    STAGE75_ASSERT(reader.mem_reserves()[0].size == 0x10000ULL, "Reserve 0 size mismatch");
    STAGE75_ASSERT(reader.mem_reserves()[1].address == 0x47000000ULL, "Reserve 1 address mismatch");
    STAGE75_ASSERT(reader.mem_reserves()[1].size == 0x20000ULL, "Reserve 1 size mismatch");

    std::cout << "[PASS] Memory Reservation Block\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Node Hierarchy Nesting
// -----------------------------------------------------------------------------
bool test_node_hierarchy_nesting() {
    std::cout << "[3] test_node_hierarchy_nesting...\n";

    FdtBuilder builder;
    builder.begin_node("soc");
    builder.begin_node("uart@9000000");
    builder.add_property_string("status", "okay");
    builder.end_node(); // uart
    builder.begin_node("timer@8000000");
    builder.add_property_string("status", "okay");
    builder.end_node(); // timer
    builder.end_node(); // soc

    std::vector<std::uint8_t> dtb = builder.build();
    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Valid DTB with nested nodes");

    const FdtNode* soc = reader.find_node_by_path("/soc");
    STAGE75_ASSERT(soc != nullptr, "/soc node must exist");
    STAGE75_ASSERT(soc->children.size() == 2, "/soc must have 2 children");

    const FdtNode* uart = reader.find_node_by_path("/soc/uart@9000000");
    STAGE75_ASSERT(uart != nullptr, "/soc/uart@9000000 must exist");
    STAGE75_ASSERT(reader.get_string_property("/soc/uart@9000000", "status") == "okay",
                   "status property must match");

    const FdtNode* timer = reader.find_node_by_path("/soc/timer@8000000");
    STAGE75_ASSERT(timer != nullptr, "/soc/timer@8000000 must exist");

    std::cout << "[PASS] Node Hierarchy Nesting\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Property Types and 4-Byte Alignment
// -----------------------------------------------------------------------------
bool test_property_types_and_padding() {
    std::cout << "[4] test_property_types_and_padding...\n";

    FdtBuilder builder;
    builder.begin_node("test_types");
    builder.add_property_empty("empty_prop");
    builder.add_property_u32("u32_prop", 0x12345678u);
    builder.add_property_u64("u64_prop", 0x1122334455667788ULL);
    builder.add_property_u32_array("u32_arr", {10, 20, 30});
    builder.add_property_u64_array("u64_arr", {0xA000ULL, 0xB000ULL});
    builder.add_property_string("str_prop", "antigravity");
    builder.add_property_string_list("str_list", {"foo", "bar", "baz"});
    builder.end_node();

    std::vector<std::uint8_t> dtb = builder.build();
    STAGE75_ASSERT(dtb.size() % 4 == 0, "DTB total size must be 4-byte aligned");

    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Reader should validate types DTB");

    const FdtProperty* empty = reader.find_property_by_path("/test_types", "empty_prop");
    STAGE75_ASSERT(empty != nullptr && empty->data.empty(), "Empty property must have 0 size");

    STAGE75_ASSERT(reader.get_u32_property("/test_types", "u32_prop") == 0x12345678u, "u32 match");
    STAGE75_ASSERT(reader.get_u64_property("/test_types", "u64_prop") == 0x1122334455667788ULL, "u64 match");
    STAGE75_ASSERT(reader.get_string_property("/test_types", "str_prop") == "antigravity", "str match");

    std::cout << "[PASS] Property Types and 4-Byte Alignment\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Strings Block Deduplication and Offsets
// -----------------------------------------------------------------------------
bool test_strings_block_deduplication() {
    std::cout << "[5] test_strings_block_deduplication...\n";

    FdtBuilder builder;
    builder.begin_node("node1");
    builder.add_property_string("compatible", "comp1");
    builder.add_property_u32("reg", 0x1000);
    builder.end_node();

    builder.begin_node("node2");
    builder.add_property_string("compatible", "comp2");
    builder.add_property_u32("reg", 0x2000);
    builder.end_node();

    std::vector<std::uint8_t> dtb = builder.build();
    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Reader validates DTB with duplicate property names");

    STAGE75_ASSERT(reader.get_string_property("/node1", "compatible") == "comp1", "node1 comp");
    STAGE75_ASSERT(reader.get_string_property("/node2", "compatible") == "comp2", "node2 comp");
    STAGE75_ASSERT(reader.get_u32_property("/node1", "reg") == 0x1000, "node1 reg");
    STAGE75_ASSERT(reader.get_u32_property("/node2", "reg") == 0x2000, "node2 reg");

    std::cout << "[PASS] Strings Block Deduplication and Offsets\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Reader Path Query API
// -----------------------------------------------------------------------------
bool test_reader_path_queries() {
    std::cout << "[6] test_reader_path_queries...\n";

    FdtBuilder builder;
    builder.begin_node("a");
    builder.begin_node("b");
    builder.begin_node("c");
    builder.add_property_string("leaf", "found_me");
    builder.end_node();
    builder.end_node();
    builder.end_node();

    std::vector<std::uint8_t> dtb = builder.build();
    FdtReader reader(dtb);

    STAGE75_ASSERT(reader.find_node_by_path("/a/b/c") != nullptr, "Deep path lookup");
    STAGE75_ASSERT(reader.find_node_by_path("/a/b/x") == nullptr, "Non-existent path returns null");
    STAGE75_ASSERT(reader.find_node_by_path("invalid_path") == nullptr, "Path without leading / fails");
    STAGE75_ASSERT(reader.get_string_property("/a/b/c", "leaf") == "found_me", "Leaf property match");
    STAGE75_ASSERT(reader.get_string_property("/a/b/c", "missing", "fallback") == "fallback", "Fallback value");

    std::cout << "[PASS] Reader Path Query API\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Full Virtual Platform DTB Generation
// -----------------------------------------------------------------------------
bool test_platform_dtb_generation() {
    std::cout << "[7] test_platform_dtb_generation...\n";

    VirtualPlatformConfig config;
    config.core_count = 4;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000ULL; // 128 MB

    std::vector<std::uint8_t> dtb = FdtBuilder::build_platform_dtb(
        config, "console=ttyAMA0 earlycon=pl011,0x09000000 root=/dev/ram0 rw", 0x44000000ULL, 0x45000000ULL);

    STAGE75_ASSERT(!dtb.empty(), "Platform DTB must not be empty");

    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Platform DTB must parse cleanly");
    STAGE75_ASSERT(reader.get_string_property("/", "model") == "WinIOS Virtual AArch64 Machine", "Model match");
    STAGE75_ASSERT(reader.get_u32_property("/", "#address-cells") == 2, "#address-cells == 2");
    STAGE75_ASSERT(reader.get_u32_property("/", "#size-cells") == 2, "#size-cells == 2");

    std::cout << "[PASS] Full Virtual Platform DTB Generation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Chosen Node and Initrd Parameters
// -----------------------------------------------------------------------------
bool test_chosen_node_validation() {
    std::cout << "[8] test_chosen_node_validation...\n";

    VirtualPlatformConfig config;
    std::string bootargs = "console=ttyAMA0 loglevel=8 earlycon=pl011,0x09000000 root=/dev/vda rw";
    std::uint64_t initrd_start = 0x45000000ULL;
    std::uint64_t initrd_end = 0x45800000ULL;

    std::vector<std::uint8_t> dtb = FdtBuilder::build_platform_dtb(config, bootargs, initrd_start, initrd_end);
    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Valid DTB");

    STAGE75_ASSERT(reader.get_string_property("/chosen", "bootargs") == bootargs, "bootargs match");
    STAGE75_ASSERT(reader.get_string_property("/chosen", "stdout-path") == "/pl011@9000000", "stdout-path match");
    STAGE75_ASSERT(reader.get_u64_property("/chosen", "linux,initrd-start") == initrd_start, "initrd-start match");
    STAGE75_ASSERT(reader.get_u64_property("/chosen", "linux,initrd-end") == initrd_end, "initrd-end match");

    std::cout << "[PASS] Chosen Node and Initrd Parameters\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Hardware Discovery Topology Verification
// -----------------------------------------------------------------------------
bool test_hardware_discovery_topology() {
    std::cout << "[9] test_hardware_discovery_topology...\n";

    VirtualPlatformConfig config;
    config.core_count = 2;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x10000000ULL; // 256 MB
    config.gicd_base = 0x08000000ULL;
    config.gicr_base = 0x080A0000ULL;
    config.uart_base = 0x09000000ULL;
    config.uart_irq = 33u;

    std::vector<std::uint8_t> dtb = FdtBuilder::build_platform_dtb(config);
    FdtReader reader(dtb);
    STAGE75_ASSERT(reader.is_valid(), "Valid DTB");

    // 1. CPUs node
    const FdtNode* cpus = reader.find_node_by_path("/cpus");
    STAGE75_ASSERT(cpus != nullptr, "/cpus must exist");
    STAGE75_ASSERT(cpus->children.size() == 2, "2 CPUs generated");
    STAGE75_ASSERT(reader.find_node_by_path("/cpus/cpu@0") != nullptr, "cpu@0 exists");
    STAGE75_ASSERT(reader.find_node_by_path("/cpus/cpu@1") != nullptr, "cpu@1 exists");
    STAGE75_ASSERT(reader.get_string_property("/cpus/cpu@0", "enable-method") == "psci", "PSCI enable method");

    // 2. PSCI node
    const FdtNode* psci = reader.find_node_by_path("/psci");
    STAGE75_ASSERT(psci != nullptr, "/psci must exist");
    STAGE75_ASSERT(reader.get_string_property("/psci", "method") == "hvc", "PSCI method HVC");

    // 3. Memory node
    const FdtNode* mem = reader.find_node_by_path("/memory@40000000");
    STAGE75_ASSERT(mem != nullptr, "/memory@40000000 must exist");
    STAGE75_ASSERT(reader.get_string_property("/memory@40000000", "device_type") == "memory", "Memory device type");

    // 4. GICv3 node
    const FdtNode* gic = reader.find_node_by_path("/intc@8000000");
    STAGE75_ASSERT(gic != nullptr, "GIC node must exist");
    STAGE75_ASSERT(reader.get_string_property("/intc@8000000", "compatible") == "arm,gic-v3", "GICv3 compatible");
    STAGE75_ASSERT(reader.find_property_by_path("/intc@8000000", "interrupt-controller") != nullptr, "Interrupt controller prop");

    // 5. Timer node
    const FdtNode* timer = reader.find_node_by_path("/timer");
    STAGE75_ASSERT(timer != nullptr, "Timer node must exist");
    STAGE75_ASSERT(reader.get_string_property("/timer", "compatible") == "arm,armv8-timer", "Timer compatible");

    // 6. UART node
    const FdtNode* uart = reader.find_node_by_path("/pl011@9000000");
    STAGE75_ASSERT(uart != nullptr, "UART node must exist");
    STAGE75_ASSERT(reader.get_string_property("/pl011@9000000", "compatible") == "arm,pl011", "PL011 compatible");

    std::cout << "[PASS] Hardware Discovery Topology Verification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Parser Error Handling and Corrupted Blobs
// -----------------------------------------------------------------------------
bool test_parser_error_handling() {
    std::cout << "[10] test_parser_error_handling...\n";

    // 1. Empty buffer
    {
        std::vector<std::uint8_t> empty;
        FdtReader r(empty);
        STAGE75_ASSERT(!r.is_valid(), "Empty buffer must fail");
    }

    // 2. Buffer too small for header
    {
        std::vector<std::uint8_t> tiny(20, 0);
        FdtReader r(tiny);
        STAGE75_ASSERT(!r.is_valid(), "Buffer < sizeof(FdtRawHeader) must fail");
    }

    // 3. Invalid magic
    {
        FdtBuilder b;
        std::vector<std::uint8_t> dtb = b.build();
        dtb[0] = 0x00; // corrupt magic
        FdtReader r(dtb);
        STAGE75_ASSERT(!r.is_valid(), "Corrupted magic must fail");
        STAGE75_ASSERT(r.error_message().find("magic") != std::string::npos, "Error must mention magic");
    }

    // 4. Truncated total size
    {
        FdtBuilder b;
        std::vector<std::uint8_t> dtb = b.build();
        dtb.resize(dtb.size() - 10); // chop off end
        FdtReader r(dtb);
        STAGE75_ASSERT(!r.is_valid(), "Truncated DTB must fail");
    }

    // 5. Out of bounds path query
    {
        VirtualPlatformConfig config;
        std::vector<std::uint8_t> dtb = FdtBuilder::build_platform_dtb(config);
        FdtReader r(dtb);
        STAGE75_ASSERT(r.is_valid(), "Valid DTB");
        STAGE75_ASSERT(r.find_node_by_path("/non/existent/deep/path") == nullptr, "Non existent path");
        STAGE75_ASSERT(r.find_property_by_path("/chosen", "non_existent_prop") == nullptr, "Non existent prop");
    }

    std::cout << "[PASS] Parser Error Handling and Corrupted Blobs\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 75: Flattened Device Tree (FDT) Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_fdt_raw_header_magic_and_version, "test_fdt_raw_header_magic_and_version");
    run_test(test_mem_reserve_block, "test_mem_reserve_block");
    run_test(test_node_hierarchy_nesting, "test_node_hierarchy_nesting");
    run_test(test_property_types_and_padding, "test_property_types_and_padding");
    run_test(test_strings_block_deduplication, "test_strings_block_deduplication");
    run_test(test_reader_path_queries, "test_reader_path_queries");
    run_test(test_platform_dtb_generation, "test_platform_dtb_generation");
    run_test(test_chosen_node_validation, "test_chosen_node_validation");
    run_test(test_hardware_discovery_topology, "test_hardware_discovery_topology");
    run_test(test_parser_error_handling, "test_parser_error_handling");

    std::cout << "\nStage 75 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
