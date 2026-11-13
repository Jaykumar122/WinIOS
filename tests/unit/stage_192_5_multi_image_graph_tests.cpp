// =============================================================================
// stage_192_5_multi_image_graph_tests.cpp
//
// Campaign 192 Stage 192.5:
//   Multi-Image Dependency Graph and @rpath Expansion Tests
//
//   Audits and validates:
//     1. Topological dependency ordering (linear chains and diamond DAGs)
//     2. Cycle detection (self-loops, 2-node mutual cycles, and 3-node loops)
//     3. @executable_path expansion relative to main binary location
//     4. @loader_path expansion relative to loading image path
//     5. Multi-candidate @rpath fallback chains with nested path tokens
//     6. Strict separation of required vs weak missing dependencies
//     7. Real compiled multi-image artifacts dependency graph extraction
//        (main_chain_app -> libmid.dylib -> libleaf.dylib)
// =============================================================================

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "loader/macho_dependency_graph.h"
#include "loader/real_artifact_harness.h"

using namespace winios;
using namespace winios::loader;

namespace {

int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n"; \
            ++g_failed; \
        } \
    } while (0)

#define CHECK_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << (msg) << " (" #cond ")\n"; \
            ++g_failed; \
        } \
    } while (0)

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto sz = f.tellg();
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: Topological Ordering in Linear Chain & Diamond DAG
// -----------------------------------------------------------------------------
void test_topological_ordering() {
    std::cout << "[Test 1] Topological dependency ordering (linear & diamond DAG)...\n";
    MachODependencyGraph graph;

    // Linear chain: App -> LibMid -> LibLeaf
    MachODependencyNode leaf;
    leaf.id = "/usr/lib/libleaf.dylib";

    MachODependencyNode mid;
    mid.id = "/usr/lib/libmid.dylib";
    mid.dependencies.push_back(MachODependencyEdge{leaf.id, leaf.id, false, false, 0, 0});

    MachODependencyNode app;
    app.id = "/usr/bin/app";
    app.dependencies.push_back(MachODependencyEdge{mid.id, mid.id, false, false, 0, 0});

    graph.add_node(leaf);
    graph.add_node(mid);
    graph.add_node(app);

    auto res = graph.resolve_dependencies(app.id, {leaf.id, mid.id, app.id});
    CHECK(res.success);
    CHECK(!res.has_cycle);
    CHECK(res.load_order.size() == 3);
    CHECK(res.load_order[0] == leaf.id); // Dependencies first
    CHECK(res.load_order[1] == mid.id);
    CHECK(res.load_order[2] == app.id);  // Root last

    // Diamond DAG:
    //      App
    //     /   \
    //   LibA  LibB
    //     \   /
    //    LibShared
    MachODependencyGraph diamond;
    MachODependencyNode shared;
    shared.id = "/usr/lib/libshared.dylib";

    MachODependencyNode libA;
    libA.id = "/usr/lib/libA.dylib";
    libA.dependencies.push_back(MachODependencyEdge{shared.id, shared.id, false, false, 0, 0});

    MachODependencyNode libB;
    libB.id = "/usr/lib/libB.dylib";
    libB.dependencies.push_back(MachODependencyEdge{shared.id, shared.id, false, false, 0, 0});

    MachODependencyNode rootApp;
    rootApp.id = "/usr/bin/diamond_app";
    rootApp.dependencies.push_back(MachODependencyEdge{libA.id, libA.id, false, false, 0, 0});
    rootApp.dependencies.push_back(MachODependencyEdge{libB.id, libB.id, false, false, 0, 0});

    diamond.add_node(shared);
    diamond.add_node(libA);
    diamond.add_node(libB);
    diamond.add_node(rootApp);

    auto dres = diamond.resolve_dependencies(rootApp.id, {shared.id, libA.id, libB.id, rootApp.id});
    CHECK(dres.success);
    CHECK(!dres.has_cycle);
    CHECK(dres.load_order.size() == 4);
    // LibShared must be loaded strictly before LibA and LibB
    CHECK(dres.load_order[0] == shared.id);
    CHECK(dres.load_order.back() == rootApp.id);
}

// -----------------------------------------------------------------------------
// Test 2: Circular Dependency Detection (Self-Loop, 2-Node, 3-Node)
// -----------------------------------------------------------------------------
void test_circular_dependency_detection() {
    std::cout << "[Test 2] Circular dependency detection...\n";

    // 1. Self loop (App -> App)
    {
        MachODependencyGraph g;
        MachODependencyNode self_loop;
        self_loop.id = "/usr/bin/loop";
        self_loop.dependencies.push_back(MachODependencyEdge{self_loop.id, self_loop.id, false, false, 0, 0});
        g.add_node(self_loop);

        auto res = g.resolve_dependencies(self_loop.id, {self_loop.id});
        CHECK(!res.success);
        CHECK(res.has_cycle);
        CHECK(res.error_message.find("cycle") != std::string::npos);
    }

    // 2. Mutual 2-Node Cycle (A -> B -> A)
    {
        MachODependencyGraph g;
        MachODependencyNode nodeA;
        nodeA.id = "A";
        nodeA.dependencies.push_back(MachODependencyEdge{"B", "B", false, false, 0, 0});

        MachODependencyNode nodeB;
        nodeB.id = "B";
        nodeB.dependencies.push_back(MachODependencyEdge{"A", "A", false, false, 0, 0});

        g.add_node(nodeA);
        g.add_node(nodeB);

        auto res = g.resolve_dependencies("A", {"A", "B"});
        CHECK(!res.success);
        CHECK(res.has_cycle);
        CHECK(res.cycle_nodes.size() >= 2);
    }

    // 3. 3-Node Transitive Loop (A -> B -> C -> A)
    {
        MachODependencyGraph g;
        MachODependencyNode nodeA{"A", "", "", {}, {MachODependencyEdge{"B", "B", false, false, 0, 0}}, false, false, false, 0};
        MachODependencyNode nodeB{"B", "", "", {}, {MachODependencyEdge{"C", "C", false, false, 0, 0}}, false, false, false, 0};
        MachODependencyNode nodeC{"C", "", "", {}, {MachODependencyEdge{"A", "A", false, false, 0, 0}}, false, false, false, 0};

        g.add_node(nodeA);
        g.add_node(nodeB);
        g.add_node(nodeC);

        auto res = g.resolve_dependencies("A", {"A", "B", "C"});
        CHECK(!res.success);
        CHECK(res.has_cycle);
    }
}

// -----------------------------------------------------------------------------
// Test 3: @executable_path and @loader_path Expansion
// -----------------------------------------------------------------------------
void test_path_token_expansion() {
    std::cout << "[Test 3] @executable_path and @loader_path expansion...\n";
    MachODependencyGraph g;

    // 1. @executable_path replacement
    std::string resolved_exec = g.resolve_path(
        "@executable_path/Frameworks/MyKit.framework/MyKit",
        "/Applications/Safari.app/Contents/MacOS/Safari",
        {});
    CHECK(resolved_exec == "/Applications/Safari.app/Contents/MacOS/Frameworks/MyKit.framework/MyKit");

    // 2. @loader_path replacement
    std::string resolved_loader = g.resolve_path(
        "@loader_path/plugins/plugin.dylib",
        "/usr/bin/main_app",
        {},
        "/System/Library/Frameworks/CoreFoo.framework/CoreFoo");
    CHECK(resolved_loader == "/System/Library/Frameworks/CoreFoo.framework/plugins/plugin.dylib");

    // 3. @loader_path fallback to executable_path when loader_path is empty
    std::string loader_fallback = g.resolve_path(
        "@loader_path/helpers/helper",
        "/usr/bin/main_app",
        {});
    CHECK(loader_fallback == "/usr/bin/helpers/helper");
}

// -----------------------------------------------------------------------------
// Test 4: Multi-Candidate @rpath Fallback Chains
// -----------------------------------------------------------------------------
void test_rpath_fallback_chains() {
    std::cout << "[Test 4] Multi-candidate @rpath fallback chains...\n";
    MachODependencyGraph g;

    std::vector<std::string> rpaths = {
        "@executable_path/../Frameworks",
        "@loader_path/Libraries",
        "/usr/local/lib"
    };

    auto candidates = g.resolve_rpath_fallbacks(
        "@rpath/libcrypto.dylib",
        "/Applications/App.app/Contents/MacOS/App",
        rpaths,
        "/Applications/App.app/Contents/PlugIns/Plug.dylib");

    // Must generate all 3 expanded paths plus the system fallback in order
    CHECK(candidates.size() == 4);
    CHECK(candidates[0] == "/Applications/App.app/Contents/MacOS/../Frameworks/libcrypto.dylib");
    CHECK(candidates[1] == "/Applications/App.app/Contents/PlugIns/Libraries/libcrypto.dylib");
    CHECK(candidates[2] == "/usr/local/lib/libcrypto.dylib");
    CHECK(candidates[3] == "/usr/lib/libcrypto.dylib");
}

// -----------------------------------------------------------------------------
// Test 5: Weak vs Required Missing Dependencies
// -----------------------------------------------------------------------------
void test_weak_vs_required_dependencies() {
    std::cout << "[Test 5] Weak vs required missing dependencies...\n";
    MachODependencyGraph g;

    MachODependencyNode app;
    app.id = "/usr/bin/app";
    // Required dependency missing
    app.dependencies.push_back(MachODependencyEdge{
        "/usr/lib/librequired.dylib", "/usr/lib/librequired.dylib", false, false, 0, 0});
    // Weak dependency missing
    app.dependencies.push_back(MachODependencyEdge{
        "/usr/lib/liboptional.dylib", "/usr/lib/liboptional.dylib", true, false, 0, 0});

    g.add_node(app);

    // Case 1: Both missing -> fails because required is missing
    auto res1 = g.resolve_dependencies(app.id, {app.id});
    CHECK(!res1.success);
    CHECK(res1.missing_required.size() == 1);
    CHECK(res1.missing_weak.size() == 1);

    // Case 2: Required available, weak missing -> SUCCEEDS!
    auto res2 = g.resolve_dependencies(app.id, {app.id, "/usr/lib/librequired.dylib"});
    CHECK(res2.success);
    CHECK(res2.missing_required.empty());
    CHECK(res2.missing_weak.size() == 1);
    CHECK(res2.load_order.size() == 2); // Required dependency loaded before app
    CHECK(res2.load_order[0] == "/usr/lib/librequired.dylib");
    CHECK(res2.load_order[1] == app.id);
}

// -----------------------------------------------------------------------------
// Test 6: Real Multi-Image Mach-O Artifact Graph (REAL-009 Chain)
// -----------------------------------------------------------------------------
void test_real_artifact_dependency_graph() {
    std::cout << "[Test 6] Real multi-image Mach-O dependency graph (REAL-009 chain)...\n";

    std::vector<std::string> candidates = {
        "tests/artifacts/bin/",
        "WinIOS/tests/artifacts/bin/",
        "../tests/artifacts/bin/",
        "D:/WinISO26/WinIOS/tests/artifacts/bin/"
    };

    std::string prefix;
    for (const auto& c : candidates) {
        std::ifstream check(c + "main_chain", std::ios::binary);
        if (check.good()) {
            prefix = c;
            break;
        }
    }
    CHECK(!prefix.empty());

    auto app_bytes  = read_binary_file(prefix + "main_chain");
    auto mid_bytes  = read_binary_file(prefix + "libmid.dylib");
    auto leaf_bytes = read_binary_file(prefix + "libleaf.dylib");

    CHECK(!app_bytes.empty());
    CHECK(!mid_bytes.empty());
    CHECK(!leaf_bytes.empty());

    MachODependencyGraph g;
    std::string err;

    CHECK(g.add_binary(leaf_bytes.data(), leaf_bytes.size(), "@rpath/libleaf.dylib", err));
    CHECK(g.add_binary(mid_bytes.data(), mid_bytes.size(), "@rpath/libmid.dylib", err));
    CHECK(g.add_binary(app_bytes.data(), app_bytes.size(), "main_chain", err));

    // Verify node properties
    auto* app_node = g.get_node("main_chain");
    CHECK(app_node != nullptr);
    CHECK(app_node->has_main);
    CHECK(!app_node->rpaths.empty());

    auto* mid_node = g.get_node("@rpath/libmid.dylib");
    CHECK(mid_node != nullptr);
    CHECK(!mid_node->dependencies.empty());

    // Verify resolution
    auto res = g.resolve_dependencies("main_chain", {
        "main_chain", "@rpath/libmid.dylib", "@rpath/libleaf.dylib", "/usr/lib/libSystem.B.dylib"
    });
    CHECK(res.success);
    CHECK(!res.has_cycle);
    CHECK(res.missing_required.empty());

    // Topological order: leaf must precede mid, and mid must precede main_chain
    auto leaf_pos = std::find(res.load_order.begin(), res.load_order.end(), "@rpath/libleaf.dylib");
    auto mid_pos  = std::find(res.load_order.begin(), res.load_order.end(), "@rpath/libmid.dylib");
    auto app_pos  = std::find(res.load_order.begin(), res.load_order.end(), "main_chain");

    CHECK(leaf_pos != res.load_order.end());
    CHECK(mid_pos != res.load_order.end());
    CHECK(app_pos != res.load_order.end());
    CHECK(leaf_pos < mid_pos);
    CHECK(mid_pos < app_pos);
}

} // namespace

int main() {
    try {
        test_topological_ordering();
        test_circular_dependency_detection();
        test_path_token_expansion();
        test_rpath_fallback_chains();
        test_weak_vs_required_dependencies();
        test_real_artifact_dependency_graph();
    } catch (const std::exception& ex) {
        std::cerr << "[EXCEPTION] " << ex.what() << "\n";
        return 1;
    }

    if (g_failed != 0) {
        std::cerr << "stage_192_5_multi_image_graph_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE 192.5 MULTI-IMAGE GRAPH TESTS PASSED\n";
    return 0;
}
