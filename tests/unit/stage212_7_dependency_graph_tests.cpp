#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/macho_dependency_graph.h"

using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

// Helper to construct a synthetic Mach-O binary with specified dylib commands
std::vector<std::uint8_t> make_macho(
    const std::string& id_dylib,
    const std::vector<std::pair<std::string, bool>>& load_dylibs) {
    std::vector<std::uint8_t> bin(32, 0);

    // mach_header_64
    const std::uint32_t magic = 0xFEEDFACFu;
    const std::int32_t cputype = 0x0100000Cu;
    const std::uint32_t filetype = id_dylib.empty() ? 2u : 6u;
    std::uint32_t ncmds = 0;
    std::uint32_t sizeofcmds = 0;

    std::memcpy(bin.data() + 0, &magic, 4);
    std::memcpy(bin.data() + 4, &cputype, 4);
    std::memcpy(bin.data() + 12, &filetype, 4);

    std::vector<std::uint8_t> cmd_bytes;

    auto append_dylib_cmd = [&](std::uint32_t cmd_type, const std::string& name) {
        const std::size_t raw_size = 24 + name.size() + 1;
        const std::uint32_t cmdsize = static_cast<std::uint32_t>((raw_size + 7) & ~7); // 8-byte align

        std::vector<std::uint8_t> c(cmdsize, 0);
        std::memcpy(c.data() + 0, &cmd_type, 4);
        std::memcpy(c.data() + 4, &cmdsize, 4);
        const std::uint32_t stroff = 24;
        std::memcpy(c.data() + 8, &stroff, 4);
        const std::uint32_t ver = 0x00010000u;
        std::memcpy(c.data() + 16, &ver, 4);
        std::memcpy(c.data() + 20, &ver, 4);
        std::memcpy(c.data() + 24, name.c_str(), name.size() + 1);

        cmd_bytes.insert(cmd_bytes.end(), c.begin(), c.end());
        ++ncmds;
        sizeofcmds += cmdsize;
    };

    if (!id_dylib.empty()) {
        append_dylib_cmd(0x0Du /* LC_ID_DYLIB */, id_dylib);
    }

    for (const auto& [dep_name, is_weak] : load_dylibs) {
        const std::uint32_t cmd_type = is_weak ? 0x80000018u /* LC_LOAD_WEAK_DYLIB */ : 0x0Cu /* LC_LOAD_DYLIB */;
        append_dylib_cmd(cmd_type, dep_name);
    }

    std::memcpy(bin.data() + 16, &ncmds, 4);
    std::memcpy(bin.data() + 20, &sizeofcmds, 4);
    bin.insert(bin.end(), cmd_bytes.begin(), cmd_bytes.end());

    return bin;
}

void test_parse_and_order() {
    std::cout << "[Test 1] Mach-O Dependency Parsing & Topological Load Ordering...\n";

    // Create 3 libraries:
    // app -> libfoo.dylib -> libbar.dylib
    auto bin_bar = make_macho("libbar.dylib", {});
    auto bin_foo = make_macho("libfoo.dylib", {{"libbar.dylib", false}});
    auto bin_app = make_macho("", {{"libfoo.dylib", false}});

    MachoDependencyGraph graph;
    std::string err;
    TEST_CHECK(graph.add_image("libbar.dylib", bin_bar, &err), "Added libbar.dylib");
    TEST_CHECK(graph.add_image("libfoo.dylib", bin_foo, &err), "Added libfoo.dylib");
    TEST_CHECK(graph.add_image("app", bin_app, &err), "Added app");

    std::vector<std::string> order;
    TEST_CHECK(graph.resolve_load_order("app", order, &err), "Resolved load order");
    TEST_CHECK(order.size() == 3, "Order contains 3 nodes");
    if (order.size() == 3) {
        TEST_CHECK(order[0] == "libbar.dylib", "1st is libbar (deepest dependency)");
        TEST_CHECK(order[1] == "libfoo.dylib", "2nd is libfoo");
        TEST_CHECK(order[2] == "app", "3rd is app (root executable)");
    }
}

void test_cycle_detection() {
    std::cout << "[Test 2] Dependency Cycle Detection...\n";

    // libA -> libB -> libA (cycle)
    auto bin_a = make_macho("libA.dylib", {{"libB.dylib", false}});
    auto bin_b = make_macho("libB.dylib", {{"libA.dylib", false}});

    MachoDependencyGraph graph;
    std::string err;
    graph.add_image("libA.dylib", bin_a);
    graph.add_image("libB.dylib", bin_b);

    std::vector<std::string> order;
    TEST_CHECK(!graph.resolve_load_order("libA.dylib", order, &err), "Cycle detected and rejected");
    TEST_CHECK(err.find("cycle") != std::string::npos, "Error mentions cycle");
}

void test_weak_dependency() {
    std::cout << "[Test 3] Weak Dependency vs Missing Dependency...\n";

    // app has weak dependency on missing_optional.dylib -> should succeed
    auto bin_app_weak = make_macho("", {{"missing_optional.dylib", true}});
    MachoDependencyGraph graph1;
    std::string err;
    graph1.add_image("app", bin_app_weak);

    std::vector<std::string> order;
    TEST_CHECK(graph1.resolve_load_order("app", order, &err), "Missing weak dependency allowed");
    TEST_CHECK(order.size() == 1 && order[0] == "app", "Only app in load order");

    // app has strong dependency on missing_required.dylib -> should fail
    auto bin_app_strong = make_macho("", {{"missing_required.dylib", false}});
    MachoDependencyGraph graph2;
    graph2.add_image("app", bin_app_strong);
    TEST_CHECK(!graph2.resolve_load_order("app", order, &err), "Missing required dependency rejected");
    TEST_CHECK(err.find("missing_required.dylib") != std::string::npos, "Error identifies missing library");
}

void test_bounds_and_duplicates() {
    std::cout << "[Test 4] Bounds Limits and Duplicate Prevention...\n";

    MachoDependencyGraph graph;
    auto bin = make_macho("libfoo.dylib", {});
    std::string err;

    TEST_CHECK(graph.add_image("libfoo.dylib", bin, &err), "Initial add succeeds");
    TEST_CHECK(!graph.add_image("libfoo.dylib", bin, &err), "Duplicate registration rejected");
    TEST_CHECK(err.find("Duplicate") != std::string::npos, "Error indicates duplicate");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.7 Mach-O Dependency Graph Tests ===\n";
    test_parse_and_order();
    test_cycle_detection();
    test_weak_dependency();
    test_bounds_and_duplicates();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.7: All Mach-O dependency graph tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.7: " << g_failures << " tests failed.\n";
        return 1;
    }
}
