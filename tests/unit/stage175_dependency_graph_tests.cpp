#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "loader/macho_corpus_builder.h"
#include "loader/macho_dependency_graph.h"
#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Mach-O Load Command Parsing Bounds & Extraction
bool test_load_command_parsing_extraction() {
    std::cout << "[Test 1] Mach-O Load Command Parsing Bounds & Extraction...\n";

    auto bin = MachOCorpusRegistry::create_dynamic_chained_fixup();

    MachODependencyGraph graph;
    std::string err;
    bool ok = graph.add_binary(bin.binary_data.data(), bin.binary_data.size(), "root_app", err);
    TEST_ASSERT(ok, "Parsing binary load commands must succeed");

    const auto* node = graph.get_node("root_app");
    TEST_ASSERT(node != nullptr, "Node root_app exists");
    TEST_ASSERT(node->dylinker == "/usr/lib/dyld", "Dylinker matches /usr/lib/dyld");
    TEST_ASSERT(node->has_main, "Node has LC_MAIN");
    TEST_ASSERT(node->has_chained_fixups, "Node has LC_DYLD_CHAINED_FIXUPS");
    TEST_ASSERT(node->dependencies.size() >= 1, "At least 1 dependency parsed");
    TEST_ASSERT(node->dependencies[0].raw_path == "/usr/lib/libSystem.B.dylib", "Dependency 0 matches libSystem");

    return true;
}

// Test 2: RPATH Expansion (@rpath and @executable_path)
bool test_rpath_expansion() {
    std::cout << "[Test 2] RPATH Expansion (@rpath and @executable_path)...\n";

    MachODependencyGraph graph;

    // 1. @executable_path
    std::string resolved_exec = graph.resolve_path(
        "@executable_path/Frameworks/MyKit.framework/MyKit",
        "/Applications/App.app/App",
        {}
    );
    TEST_ASSERT(resolved_exec == "/Applications/App.app/Frameworks/MyKit.framework/MyKit",
                "@executable_path resolved relative to binary directory");

    // 2. @rpath with active RPATHs
    std::vector<std::string> rpaths = {"/usr/local/lib", "/opt/homebrew/lib"};
    std::string resolved_rpath = graph.resolve_path("@rpath/libcustom.dylib", "/usr/bin/tool", rpaths);
    TEST_ASSERT(resolved_rpath == "/usr/local/lib/libcustom.dylib",
                "@rpath resolved using first matching active RPATH");

    // 3. Fallback when no rpaths provided
    std::string fallback = graph.resolve_path("@rpath/libcustom.dylib", "/usr/bin/tool", {});
    TEST_ASSERT(fallback == "/usr/lib/libcustom.dylib", "@rpath fallback to /usr/lib");

    return true;
}

// Test 3: Topological Dependency Ordering
bool test_topological_dependency_ordering() {
    std::cout << "[Test 3] Topological Dependency Ordering...\n";

    MachODependencyGraph graph;

    // Build DAG: App -> UI -> Core -> libSystem
    MachODependencyNode app;
    app.id = "App";
    app.dependencies.push_back({"UI", "UI", false, false, 0, 0});

    MachODependencyNode ui;
    ui.id = "UI";
    ui.dependencies.push_back({"Core", "Core", false, false, 0, 0});

    MachODependencyNode core;
    core.id = "Core";
    core.dependencies.push_back({"libSystem", "libSystem", false, false, 0, 0});

    MachODependencyNode libsys;
    libsys.id = "libSystem";

    graph.add_node(app);
    graph.add_node(ui);
    graph.add_node(core);
    graph.add_node(libsys);

    std::set<std::string> available = {"App", "UI", "Core", "libSystem"};
    auto res = graph.resolve_dependencies("App", available);

    TEST_ASSERT(res.success, "Dependency resolution succeeded");
    TEST_ASSERT(!res.has_cycle, "No cycle in DAG");
    TEST_ASSERT(res.load_order.size() == 4, "4 modules in load order");

    // Order: libSystem, Core, UI, App (dependencies first)
    TEST_ASSERT(res.load_order[0] == "libSystem", "libSystem loaded first");
    TEST_ASSERT(res.load_order[1] == "Core", "Core loaded second");
    TEST_ASSERT(res.load_order[2] == "UI", "UI loaded third");
    TEST_ASSERT(res.load_order[3] == "App", "App loaded last");

    return true;
}

// Test 4: Missing Dependency Detection (Required vs Weak)
bool test_missing_dependency_detection() {
    std::cout << "[Test 4] Missing Dependency Detection...\n";

    MachODependencyGraph graph;

    MachODependencyNode app;
    app.id = "App";
    // Required missing dependency
    app.dependencies.push_back({"libMissing.dylib", "libMissing.dylib", false, false, 0, 0});
    // Weak missing dependency
    app.dependencies.push_back({"libWeakOptional.dylib", "libWeakOptional.dylib", true, false, 0, 0});

    graph.add_node(app);

    std::set<std::string> available = {"App"};
    auto res = graph.resolve_dependencies("App", available);

    TEST_ASSERT(!res.success, "Resolution must fail due to missing required dependency");
    TEST_ASSERT(res.missing_required.size() == 1, "Exactly 1 missing required dependency");
    TEST_ASSERT(res.missing_required[0] == "libMissing.dylib", "libMissing identified as missing required");
    TEST_ASSERT(res.missing_weak.size() == 1, "Exactly 1 missing weak dependency");
    TEST_ASSERT(res.missing_weak[0] == "libWeakOptional.dylib", "libWeakOptional identified as missing weak");

    return true;
}

// Test 5: Dependency Cycle Detection & Node Tracking
bool test_cycle_detection() {
    std::cout << "[Test 5] Dependency Cycle Detection & Node Tracking...\n";

    MachODependencyGraph graph;

    // Circular dependency: A -> B -> C -> A
    MachODependencyNode nodeA;
    nodeA.id = "A";
    nodeA.dependencies.push_back({"B", "B", false, false, 0, 0});

    MachODependencyNode nodeB;
    nodeB.id = "B";
    nodeB.dependencies.push_back({"C", "C", false, false, 0, 0});

    MachODependencyNode nodeC;
    nodeC.id = "C";
    nodeC.dependencies.push_back({"A", "A", false, false, 0, 0});

    graph.add_node(nodeA);
    graph.add_node(nodeB);
    graph.add_node(nodeC);

    std::set<std::string> available = {"A", "B", "C"};
    auto res = graph.resolve_dependencies("A", available);

    TEST_ASSERT(!res.success, "Resolution must fail on cyclic dependency");
    TEST_ASSERT(res.has_cycle, "has_cycle flag must be set");
    TEST_ASSERT(!res.cycle_nodes.empty(), "Cycle nodes recorded");

    return true;
}

// Test 6: Re-exported Dylib Tracking
bool test_reexported_dylib_tracking() {
    std::cout << "[Test 6] Re-exported Dylib Tracking...\n";

    MachODependencyGraph graph;

    MachODependencyNode umbrella;
    umbrella.id = "UmbrellaFramework";
    // Re-exported sub-framework
    umbrella.dependencies.push_back({"SubFramework", "SubFramework", false, true, 0, 0});

    graph.add_node(umbrella);

    const auto* node = graph.get_node("UmbrellaFramework");
    TEST_ASSERT(node != nullptr, "UmbrellaFramework node exists");
    TEST_ASSERT(node->dependencies.size() == 1, "1 dependency");
    TEST_ASSERT(node->dependencies[0].is_reexport, "Dependency marked as re-export");

    return true;
}

// Test 7: Malformed / Truncated Load Command Rejection
bool test_malformed_load_command_rejection() {
    std::cout << "[Test 7] Malformed Load Command Rejection...\n";

    auto bin = MachOCorpusRegistry::create_hello_world();

    // Corrupt ncmds to be larger than actual payload
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(bin.binary_data.data());
    hdr->sizeofcmds = static_cast<std::uint32_t>(bin.binary_data.size() + 1000);

    MachODependencyGraph graph;
    std::string err;
    bool ok = graph.add_binary(bin.binary_data.data(), bin.binary_data.size(), "corrupted", err);

    TEST_ASSERT(!ok, "Parsing must fail on sizeofcmds out of bounds");
    TEST_ASSERT(!err.empty(), "Error message generated");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 175: Mach-O Load Command and Dependency Graph\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_load_command_parsing_extraction, "Load Command Parsing & Extraction");
    run_test(test_rpath_expansion, "RPATH Expansion (@rpath & @executable_path)");
    run_test(test_topological_dependency_ordering, "Topological Dependency Ordering");
    run_test(test_missing_dependency_detection, "Missing Dependency Detection");
    run_test(test_cycle_detection, "Dependency Cycle Detection & Node Tracking");
    run_test(test_reexported_dylib_tracking, "Re-exported Dylib Tracking");
    run_test(test_malformed_load_command_rejection, "Malformed Load Command Rejection");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
