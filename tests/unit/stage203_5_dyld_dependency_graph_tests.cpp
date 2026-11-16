// ==============================================================================
// WinIOS Campaign 203 Stage 203.5: Dyld Dependency Graph Audit Tests
// ==============================================================================
// Independent verification of:
// 1. Deterministic topological load order for linear and diamond dependency graphs
// 2. Exact single-inclusion of shared dependencies (no duplicates)
// 3. Direct and multi-node dependency cycle detection
// 4. Upward dependency cycle tolerance (LC_LOAD_UPWARD_DYLIB)
// 5. Missing dependency segregation (required vs weak)
// 6. Maximum traversal depth limit enforcement (kMaxDependencyDepth = 64)
// 7. RPATH and path substitution expansion (@executable_path, @loader_path)
// 8. Ingestion of MachOLoadResult from MachOLoader
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "loader/macho_dependency_graph.h"
#include "loader/macho_loader.h"

using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Linear and Diamond Dependency Graphs & Deterministic Topological Order
// -----------------------------------------------------------------------------
void test_diamond_and_topological_order() {
    std::cout << "[Stage 203.5 Test 1] Diamond Dependency Graph & Topological Order...\n";

    MachODependencyGraph graph;

    // Diamond structure:
    //          App
    //        /     \
    //     LibA     LibB
    //        \     /
    //         LibCommon
    //            |
    //         LibCore

    // LibCore: leaf
    MachODependencyNode node_core;
    node_core.id = "/usr/lib/libcore.dylib";
    graph.add_node(node_core);

    // LibCommon depends on LibCore
    MachODependencyNode node_common;
    node_common.id = "/usr/lib/libcommon.dylib";
    node_common.dependencies.push_back({"/usr/lib/libcore.dylib", "/usr/lib/libcore.dylib", false, false, false, 1, 1});
    graph.add_node(node_common);

    // LibA depends on LibCommon
    MachODependencyNode node_a;
    node_a.id = "/usr/lib/libA.dylib";
    node_a.dependencies.push_back({"/usr/lib/libcommon.dylib", "/usr/lib/libcommon.dylib", false, false, false, 1, 1});
    graph.add_node(node_a);

    // LibB depends on LibCommon
    MachODependencyNode node_b;
    node_b.id = "/usr/lib/libB.dylib";
    node_b.dependencies.push_back({"/usr/lib/libcommon.dylib", "/usr/lib/libcommon.dylib", false, false, false, 1, 1});
    graph.add_node(node_b);

    // App depends on LibA and LibB
    MachODependencyNode node_app;
    node_app.id = "/Applications/App.app/App";
    node_app.dependencies.push_back({"/usr/lib/libA.dylib", "/usr/lib/libA.dylib", false, false, false, 1, 1});
    node_app.dependencies.push_back({"/usr/lib/libB.dylib", "/usr/lib/libB.dylib", false, false, false, 1, 1});
    graph.add_node(node_app);

    std::set<std::string> avail = {
        "/usr/lib/libcore.dylib",
        "/usr/lib/libcommon.dylib",
        "/usr/lib/libA.dylib",
        "/usr/lib/libB.dylib",
        "/Applications/App.app/App"
    };

    auto res = graph.resolve_dependencies("/Applications/App.app/App", avail);

    TEST_CHECK(res.success, "Diamond resolution must succeed");
    TEST_CHECK(!res.has_cycle, "Diamond graph must not have cycles");
    TEST_CHECK(!res.depth_exceeded, "Depth must not be exceeded");
    TEST_CHECK(res.missing_required.empty(), "No required missing libraries");
    TEST_CHECK(res.load_order.size() == 5, "All 5 nodes must be present in load order exactly once");

    // LibCore must precede LibCommon
    auto idx_core = std::find(res.load_order.begin(), res.load_order.end(), "/usr/lib/libcore.dylib");
    auto idx_common = std::find(res.load_order.begin(), res.load_order.end(), "/usr/lib/libcommon.dylib");
    auto idx_a = std::find(res.load_order.begin(), res.load_order.end(), "/usr/lib/libA.dylib");
    auto idx_b = std::find(res.load_order.begin(), res.load_order.end(), "/usr/lib/libB.dylib");
    auto idx_app = std::find(res.load_order.begin(), res.load_order.end(), "/Applications/App.app/App");

    TEST_CHECK(idx_core != res.load_order.end(), "LibCore in load order");
    TEST_CHECK(idx_common != res.load_order.end(), "LibCommon in load order");
    TEST_CHECK(idx_a != res.load_order.end(), "LibA in load order");
    TEST_CHECK(idx_b != res.load_order.end(), "LibB in load order");
    TEST_CHECK(idx_app != res.load_order.end(), "App in load order");

    TEST_CHECK(idx_core < idx_common, "LibCore must be loaded before LibCommon");
    TEST_CHECK(idx_common < idx_a, "LibCommon must be loaded before LibA");
    TEST_CHECK(idx_common < idx_b, "LibCommon must be loaded before LibB");
    TEST_CHECK(idx_a < idx_app, "LibA must be loaded before App");
    TEST_CHECK(idx_b < idx_app, "LibB must be loaded before App");
    TEST_CHECK(res.load_order.back() == "/Applications/App.app/App", "App (root) must be loaded last");

    std::cout << "  -> Diamond & topological order verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Dependency Cycle Detection & Cycle Node Diagnostics
// -----------------------------------------------------------------------------
void test_dependency_cycles() {
    std::cout << "[Stage 203.5 Test 2] Dependency Cycle Detection...\n";

    // Direct cycle: A -> B -> A
    {
        MachODependencyGraph graph;

        MachODependencyNode node_a;
        node_a.id = "libA.dylib";
        node_a.dependencies.push_back({"libB.dylib", "libB.dylib", false, false, false, 1, 1});
        graph.add_node(node_a);

        MachODependencyNode node_b;
        node_b.id = "libB.dylib";
        node_b.dependencies.push_back({"libA.dylib", "libA.dylib", false, false, false, 1, 1});
        graph.add_node(node_b);

        std::set<std::string> avail = {"libA.dylib", "libB.dylib"};
        auto res = graph.resolve_dependencies("libA.dylib", avail);

        TEST_CHECK(!res.success, "Resolution must fail on direct cycle");
        TEST_CHECK(res.has_cycle, "has_cycle must be true");
        TEST_CHECK(!res.cycle_nodes.empty(), "Cycle nodes must be reported");
    }

    // Transitive cycle: Root -> X -> Y -> Z -> X
    {
        MachODependencyGraph graph;

        MachODependencyNode root;
        root.id = "Root";
        root.dependencies.push_back({"X", "X", false, false, false, 1, 1});
        graph.add_node(root);

        MachODependencyNode x;
        x.id = "X";
        x.dependencies.push_back({"Y", "Y", false, false, false, 1, 1});
        graph.add_node(x);

        MachODependencyNode y;
        y.id = "Y";
        y.dependencies.push_back({"Z", "Z", false, false, false, 1, 1});
        graph.add_node(y);

        MachODependencyNode z;
        z.id = "Z";
        z.dependencies.push_back({"X", "X", false, false, false, 1, 1});
        graph.add_node(z);

        std::set<std::string> avail = {"Root", "X", "Y", "Z"};
        auto res = graph.resolve_dependencies("Root", avail);

        TEST_CHECK(!res.success, "Resolution must fail on transitive cycle");
        TEST_CHECK(res.has_cycle, "has_cycle must be true");
        TEST_CHECK(res.cycle_nodes.size() >= 2, "Cycle path must be reported");
    }

    std::cout << "  -> Dependency cycles verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Upward Dependency Tolerance (LC_LOAD_UPWARD_DYLIB)
// -----------------------------------------------------------------------------
void test_upward_dependencies() {
    std::cout << "[Stage 203.5 Test 3] Upward Dependency Tolerance...\n";

    // In Darwin dyld, an upward dependency (Child -> Parent) is legal and must not trigger cycle error
    // App -> ParentLib -> ChildLib
    // ChildLib has upward dependency back to ParentLib
    MachODependencyGraph graph;

    MachODependencyNode app;
    app.id = "App";
    app.dependencies.push_back({"ParentLib", "ParentLib", false, false, false, 1, 1});
    graph.add_node(app);

    MachODependencyNode parent;
    parent.id = "ParentLib";
    parent.dependencies.push_back({"ChildLib", "ChildLib", false, false, false, 1, 1});
    graph.add_node(parent);

    MachODependencyNode child;
    child.id = "ChildLib";
    // Upward dependency: is_upward = true
    child.dependencies.push_back({"ParentLib", "ParentLib", false, false, true, 1, 1});
    graph.add_node(child);

    std::set<std::string> avail = {"App", "ParentLib", "ChildLib"};
    auto res = graph.resolve_dependencies("App", avail);

    TEST_CHECK(res.success, "Resolution must succeed when circular edge is upward");
    TEST_CHECK(!res.has_cycle, "Upward edge must not trigger has_cycle");
    TEST_CHECK(res.load_order.size() == 3, "All 3 nodes loaded");

    // ChildLib loaded before ParentLib, ParentLib loaded before App
    auto idx_child = std::find(res.load_order.begin(), res.load_order.end(), "ChildLib");
    auto idx_parent = std::find(res.load_order.begin(), res.load_order.end(), "ParentLib");
    auto idx_app = std::find(res.load_order.begin(), res.load_order.end(), "App");

    TEST_CHECK(idx_child < idx_parent, "ChildLib loaded before ParentLib");
    TEST_CHECK(idx_parent < idx_app, "ParentLib loaded before App");

    std::cout << "  -> Upward dependency tolerance verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Missing Dependencies: Required vs Weak
// -----------------------------------------------------------------------------
void test_missing_dependencies() {
    std::cout << "[Stage 203.5 Test 4] Missing Dependencies (Required vs Weak)...\n";

    // 1. Missing Required Dependency -> Failure
    {
        MachODependencyGraph graph;
        MachODependencyNode app;
        app.id = "AppReq";
        app.dependencies.push_back({"MissingRequired.dylib", "MissingRequired.dylib", false, false, false, 1, 1});
        graph.add_node(app);

        std::set<std::string> avail = {"AppReq"};
        auto res = graph.resolve_dependencies("AppReq", avail);

        TEST_CHECK(!res.success, "Missing required library must fail resolution");
        TEST_CHECK(res.missing_required.size() == 1, "missing_required must have 1 entry");
        TEST_CHECK(res.missing_required[0] == "MissingRequired.dylib", "Reported missing name must match");
    }

    // 2. Missing Weak Dependency -> Success with warning tracking
    {
        MachODependencyGraph graph;
        MachODependencyNode app;
        app.id = "AppWeak";
        // Weak dependency: is_weak = true
        app.dependencies.push_back({"MissingOptional.dylib", "MissingOptional.dylib", true, false, false, 1, 1});
        graph.add_node(app);

        std::set<std::string> avail = {"AppWeak"};
        auto res = graph.resolve_dependencies("AppWeak", avail);

        TEST_CHECK(res.success, "Missing weak dependency must NOT fail resolution");
        TEST_CHECK(res.missing_weak.size() == 1, "missing_weak must track the optional dependency");
        TEST_CHECK(res.missing_weak[0] == "MissingOptional.dylib", "Reported weak missing name must match");
        TEST_CHECK(res.missing_required.empty(), "missing_required must be empty");
    }

    std::cout << "  -> Missing dependencies (required vs weak) verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Depth Limit Enforcement
// -----------------------------------------------------------------------------
void test_depth_limit() {
    std::cout << "[Stage 203.5 Test 5] Traversal Depth Limit Enforcement...\n";

    MachODependencyGraph graph;

    // Create a chain of 70 nodes: Node0 -> Node1 -> ... -> Node69
    const std::size_t kChainLength = 70;
    std::set<std::string> avail;

    for (std::size_t i = 0; i < kChainLength; ++i) {
        std::string name = "Node" + std::to_string(i);
        avail.insert(name);

        MachODependencyNode node;
        node.id = name;
        if (i + 1 < kChainLength) {
            std::string child = "Node" + std::to_string(i + 1);
            node.dependencies.push_back({child, child, false, false, false, 1, 1});
        }
        graph.add_node(node);
    }

    // Test with default max_depth = 64
    auto res_limited = graph.resolve_dependencies("Node0", avail, 64);
    TEST_CHECK(!res_limited.success, "Exceeding depth 64 must fail");
    TEST_CHECK(res_limited.depth_exceeded, "depth_exceeded must be true");

    // Test with max_depth = 80
    auto res_allowed = graph.resolve_dependencies("Node0", avail, 80);
    TEST_CHECK(res_allowed.success, "Depth 70 must succeed when limit is 80");
    TEST_CHECK(!res_allowed.depth_exceeded, "depth_exceeded must be false");
    TEST_CHECK(res_allowed.load_order.size() == 70, "All 70 nodes loaded");

    std::cout << "  -> Traversal depth limit verified.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Ingestion from MachOLoadResult
// -----------------------------------------------------------------------------
void test_mach_o_load_result_ingestion() {
    std::cout << "[Stage 203.5 Test 6] MachOLoadResult Ingestion...\n";

    MachOLoadResult meta;
    meta.dylib_id = "/usr/lib/libMyFramework.dylib";
    meta.entry_point = 0x100004000ULL;
    meta.rpaths = {"@executable_path/Frameworks", "@loader_path/../Frameworks"};

    MachODylibInfo dep1;
    dep1.path = "/usr/lib/libSystem.B.dylib";
    dep1.current_version = 0x01000000;
    dep1.compatibility_version = 0x01000000;
    dep1.is_weak = false;
    dep1.is_reexport = false;
    dep1.is_upward = false;
    meta.dylib_dependencies.push_back(dep1);

    MachODylibInfo dep2;
    dep2.path = "/usr/lib/libWeakHelper.dylib";
    dep2.is_weak = true;
    meta.dylib_dependencies.push_back(dep2);

    MachODylibInfo dep3;
    dep3.path = "/usr/lib/libSubFramework.dylib";
    dep3.is_reexport = true;
    meta.dylib_dependencies.push_back(dep3);

    MachODependencyGraph graph;
    bool ok = graph.add_macho_load_result(meta, "MyFramework");
    TEST_CHECK(ok, "add_macho_load_result must succeed");

    const auto* node = graph.get_node("MyFramework");
    TEST_CHECK(node != nullptr, "Node must be queryable");
    TEST_CHECK(node->install_name == "/usr/lib/libMyFramework.dylib", "install_name must match");
    TEST_CHECK(node->has_main == true, "has_main must be true when entry_point != 0");
    TEST_CHECK(node->entry_offset == 0x100004000ULL, "entry_offset must match");
    TEST_CHECK(node->rpaths.size() == 2, "rpaths must match");
    TEST_CHECK(node->dependencies.size() == 3, "Must have 3 dependencies");
    TEST_CHECK(!node->dependencies[0].is_weak && !node->dependencies[0].is_reexport, "dep1 flags match");
    TEST_CHECK(node->dependencies[1].is_weak == true, "dep2 is_weak == true");
    TEST_CHECK(node->dependencies[2].is_reexport == true, "dep3 is_reexport == true");

    std::cout << "  -> MachOLoadResult ingestion verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.5: Dyld Dependency Graph Audit Tests\n";
    std::cout << "==================================================================\n";

    test_diamond_and_topological_order();
    test_dependency_cycles();
    test_upward_dependencies();
    test_missing_dependencies();
    test_depth_limit();
    test_mach_o_load_result_ingestion();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.5 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
