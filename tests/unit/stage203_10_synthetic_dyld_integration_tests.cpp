// ==============================================================================
// WinIOS Campaign 203 Stage 203.10: Synthetic Dyld Integration Tests
// ==============================================================================
// Comprehensive End-to-End Integration Test of Phase B:
// 1. Multi-image dependency topology (App -> libA -> libB, libSystem)
// 2. Transitive re-exports (libA re-exports libB)
// 3. Weak dependency and weak import resilience (missing optional library and symbol)
// 4. Deterministic topological load order
// 5. Chained fixups processing with ARM64e PAC authentication (IA key, addr diversity)
// 6. Two-level namespace resolution across direct and re-exported libraries
// 7. Initializer validation and deterministic dependency sequencing
// 8. Duplicate run prevention and idempotency
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cpu/pointer_auth.h"
#include "loader/dyld_engine.h"
#include "loader/macho_dependency_graph.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;
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

void test_synthetic_dyld_full_pipeline() {
    std::cout << "[Stage 203.10] Comprehensive Synthetic Dyld Pipeline Integration...\n";

    // -------------------------------------------------------------------------
    // Step 1: Set up Dependency Graph for Multi-Image Workload
    // -------------------------------------------------------------------------
    MachODependencyGraph graph;

    // Node 1: libSystem.B.dylib (Leaf)
    MachODependencyNode node_sys;
    node_sys.id = "/usr/lib/libSystem.B.dylib";
    node_sys.install_name = "/usr/lib/libSystem.B.dylib";
    graph.add_node(node_sys);

    // Node 2: libB.dylib (Leaf utility)
    MachODependencyNode node_b;
    node_b.id = "/usr/lib/libB.dylib";
    node_b.install_name = "/usr/lib/libB.dylib";
    graph.add_node(node_b);

    // Node 3: libA.dylib (Depends on libSystem, re-exports libB)
    MachODependencyNode node_a;
    node_a.id = "/usr/lib/libA.dylib";
    node_a.install_name = "/usr/lib/libA.dylib";
    node_a.dependencies.push_back({"/usr/lib/libSystem.B.dylib", "/usr/lib/libSystem.B.dylib", false, false, false, 1, 1});
    node_a.dependencies.push_back({"/usr/lib/libB.dylib", "/usr/lib/libB.dylib", false, true, false, 1, 1}); // is_reexport = true
    graph.add_node(node_a);

    // Node 4: App (Root executable)
    // Depends on libA and weakly depends on libOptionalAnalytics
    MachODependencyNode node_app;
    node_app.id = "/Applications/App.app/App";
    node_app.dependencies.push_back({"/usr/lib/libA.dylib", "/usr/lib/libA.dylib", false, false, false, 1, 1});
    node_app.dependencies.push_back({"/usr/lib/libOptionalAnalytics.dylib", "/usr/lib/libOptionalAnalytics.dylib", true, false, false, 1, 1}); // is_weak = true
    graph.add_node(node_app);

    std::set<std::string> available_libs = {
        "/usr/lib/libSystem.B.dylib",
        "/usr/lib/libB.dylib",
        "/usr/lib/libA.dylib",
        "/Applications/App.app/App"
        // Note: libOptionalAnalytics.dylib is missing!
    };

    auto dep_res = graph.resolve_dependencies("/Applications/App.app/App", available_libs);

    TEST_CHECK(dep_res.success, "Dependency resolution must succeed despite missing weak library");
    TEST_CHECK(!dep_res.has_cycle, "Dependency graph must have no cycles");
    TEST_CHECK(dep_res.missing_required.empty(), "No required libraries may be missing");
    TEST_CHECK(dep_res.missing_weak.size() == 1, "Must track missing optional analytics");
    TEST_CHECK(dep_res.missing_weak[0] == "/usr/lib/libOptionalAnalytics.dylib", "Missing weak name match");

    // Verify topological load order:
    // libSystem and libB must precede libA; libA must precede App
    auto idx_sys = std::find(dep_res.load_order.begin(), dep_res.load_order.end(), "/usr/lib/libSystem.B.dylib");
    auto idx_b = std::find(dep_res.load_order.begin(), dep_res.load_order.end(), "/usr/lib/libB.dylib");
    auto idx_a = std::find(dep_res.load_order.begin(), dep_res.load_order.end(), "/usr/lib/libA.dylib");
    auto idx_app = std::find(dep_res.load_order.begin(), dep_res.load_order.end(), "/Applications/App.app/App");

    TEST_CHECK(idx_sys != dep_res.load_order.end(), "libSystem in load order");
    TEST_CHECK(idx_b != dep_res.load_order.end(), "libB in load order");
    TEST_CHECK(idx_a != dep_res.load_order.end(), "libA in load order");
    TEST_CHECK(idx_app != dep_res.load_order.end(), "App in load order");

    TEST_CHECK(idx_sys < idx_a, "libSystem loaded before libA");
    TEST_CHECK(idx_b < idx_a, "libB loaded before libA");
    TEST_CHECK(idx_a < idx_app, "libA loaded before App");

    // -------------------------------------------------------------------------
    // Step 2: Initialize DyldEngine, Modules & Symbols
    // -------------------------------------------------------------------------
    DyldEngine dyld;
    dyld.clear_modules();

    // Module 1: libSystem
    DyldLoadedModule mod_sys;
    mod_sys.name = "libSystem.B.dylib";
    mod_sys.install_name = "/usr/lib/libSystem.B.dylib";
    mod_sys.base_address = 0x180000000ULL;
    mod_sys.slide = 0x4000ULL;
    mod_sys.exports["_mach_msg_trap"] = 0x180001080ULL;
    mod_sys.initializers = {0x180001000ULL};
    dyld.register_module(mod_sys);

    // Module 2: libB
    DyldLoadedModule mod_b;
    mod_b.name = "libB.dylib";
    mod_b.install_name = "/usr/lib/libB.dylib";
    mod_b.base_address = 0x190000000ULL;
    mod_b.slide = 0x8000ULL;
    mod_b.exports["_compute_core_hash"] = 0x190002000ULL;
    mod_b.initializers = {0x190001000ULL};
    dyld.register_module(mod_b);

    // Module 3: libA (re-exports libB)
    DyldLoadedModule mod_a;
    mod_a.name = "libA.dylib";
    mod_a.install_name = "/usr/lib/libA.dylib";
    mod_a.base_address = 0x1A0000000ULL;
    mod_a.slide = 0x10000ULL;
    mod_a.reexports.push_back("/usr/lib/libB.dylib");
    mod_a.exports["_frameworkA_service"] = 0x1A0002000ULL;
    mod_a.initializers = {0x1A0001000ULL};
    dyld.register_module(mod_a);

    // Module 4: App
    DyldLoadedModule mod_app;
    mod_app.name = "App";
    mod_app.install_name = "/Applications/App.app/App";
    mod_app.base_address = 0x100000000ULL;
    mod_app.slide = 0x0ULL;
    mod_app.initializers = {0x100001000ULL};
    dyld.register_module(mod_app);

    // -------------------------------------------------------------------------
    // Step 3: Two-Level Symbol Resolution & Re-Export Verification
    // -------------------------------------------------------------------------
    std::optional<std::uint64_t> resolved_addr;
    std::string err;

    // Ordinal of libA from App's perspective
    std::uint32_t ord_liba = dyld.get_module_ordinal("/usr/lib/libA.dylib");
    TEST_CHECK(ord_liba == 3, "libA ordinal is 3");

    // 1. Direct resolution in libA
    DyldLinkStatus s1 = dyld.resolve_symbol_two_level(
        "_frameworkA_service", static_cast<std::int32_t>(ord_liba), false, resolved_addr, err);
    TEST_CHECK(s1 == DyldLinkStatus::Success, "Direct resolution in libA must succeed");
    TEST_CHECK(resolved_addr.has_value() && *resolved_addr == (0x1A0002000ULL + 0x10000ULL), "Direct symbol matches with slide");

    // 2. Transitive re-export resolution: query "_compute_core_hash" on libA (which re-exports libB)
    DyldLinkStatus s2 = dyld.resolve_symbol_two_level(
        "_compute_core_hash", static_cast<std::int32_t>(ord_liba), false, resolved_addr, err);
    TEST_CHECK(s2 == DyldLinkStatus::Success, "Transitive re-export resolution must succeed");
    TEST_CHECK(resolved_addr.has_value() && *resolved_addr == (0x190002000ULL + 0x8000ULL), "Re-exported symbol matches with libB slide");

    // 3. Weak symbol resolution on missing symbol -> binds to 0
    DyldLinkStatus s3 = dyld.resolve_symbol_two_level(
        "_optional_track_event", static_cast<std::int32_t>(ord_liba), true, resolved_addr, err);
    TEST_CHECK(s3 == DyldLinkStatus::Success, "Weak missing symbol must succeed");
    TEST_CHECK(resolved_addr.has_value() && *resolved_addr == 0, "Weak missing symbol resolves to NULL");

    // -------------------------------------------------------------------------
    // Step 4: Chained Fixups & ARM64e PAC Signing
    // -------------------------------------------------------------------------
    Arm64ePointerAuth pac;
    pac.set_key(PacKeyType::InstructionA, 0x1111222233334444ULL, 0x5555666677778888ULL);
    dyld.set_pointer_auth(&pac);

    Memory guest_mem(0x20000); // 128KB guest memory

    // Build synthetic chained fixup blob for App:
    // Page 0 at 0x4000 has 2 chained pointers:
    // Pointer 1 at 0x4020: auth bind -> imports "_frameworkA_service" from libA (ord 3), signs with IA key, div 0x1234, addr_div=1
    // Pointer 2 at 0x4030: unauth rebase -> target 0x100001000
    DyldFixtureBuilder::SyntheticSegmentFixups seg_app;
    seg_app.segment_offset = 0x4000;
    seg_app.page_size = 0x4000;
    seg_app.pointer_format = DYLD_CHAINED_PTR_ARM64E;
    seg_app.page_starts = {0x20}; // starts at 0x4020

    std::vector<DyldFixtureBuilder::SyntheticImportDescriptor> imports = {
        {"_frameworkA_service", static_cast<std::uint8_t>(ord_liba), false}
    };

    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_multi_lib(imports, {seg_app});

    // Write raw pointer at 0x4020: auth bind, ord=0, div=0x1234, addr_div=1, key=0(IA), next=2 (stride 16 -> 0x4030)
    std::uint64_t raw_ptr1 = encode_dyld_chained_ptr_arm64e_auth_bind(0, 0x1234, true, 0, 2);
    guest_mem.write_u64(0x4020, raw_ptr1);

    // Write raw pointer at 0x4030: rebase, target=0x100001000, next=0 (end)
    std::uint64_t raw_ptr2 = encode_dyld_chained_ptr_arm64e_rebase(0x100001000ULL, 0);
    guest_mem.write_u64(0x4030, raw_ptr2);

    DyldLinkStatus fixup_status = dyld.process_chained_fixups(
        fixups_blob.data(), fixups_blob.size(), guest_mem, 0x100000000ULL, 0x0ULL, err);
    TEST_CHECK(fixup_status == DyldLinkStatus::Success, "Chained fixups processing must succeed");

    // Verify Pointer 1 was resolved and signed with PAC
    std::uint64_t signed_ptr1 = guest_mem.read_u64(0x4020);
    const std::uint64_t expected_raw_target = 0x1A0002000ULL + 0x10000ULL;
    const std::uint64_t modifier1 = 0x4020ULL ^ 0x1234ULL;
    std::uint64_t authed = pac.authenticate_pointer(signed_ptr1, modifier1, PacKeyType::InstructionA);
    TEST_CHECK(!pac.has_fault(), "PAC verification of authenticated chained pointer must succeed");
    TEST_CHECK(authed == expected_raw_target, "Authenticated address must match expected raw target");
    TEST_CHECK(Arm64ePointerAuth::strip_pointer(signed_ptr1) == expected_raw_target, "Stripped target address must match framework service");

    // Verify Pointer 2 was rebased
    std::uint64_t rebased_ptr2 = guest_mem.read_u64(0x4030);
    TEST_CHECK(rebased_ptr2 == 0x100001000ULL, "Rebased pointer 2 target match");

    // -------------------------------------------------------------------------
    // Step 5: Initializer Sequencing Across Images
    // -------------------------------------------------------------------------
    std::vector<std::string> init_names;
    auto init_order = dyld.get_initialization_order(dep_res.load_order, init_names);

    TEST_CHECK(init_order.size() == 4, "Must collect initializers from all 4 images");
    TEST_CHECK(init_names.size() == 4, "Must record 4 initialized modules");

    // Verify strict dependency order:
    // 1. libSystem.B: 0x180001000 + slide(0x4000) = 0x180005000
    // 2. libB:        0x190001000 + slide(0x8000) = 0x190009000
    // 3. libA:        0x1A0001000 + slide(0x10000) = 0x1A0011000
    // 4. App:         0x100001000 + slide(0)      = 0x100001000
    if (init_order.size() == 4) {
        TEST_CHECK(init_order[0] == 0x180005000ULL, "libSystem initializer first");
        TEST_CHECK(init_order[1] == 0x190009000ULL, "libB initializer second");
        TEST_CHECK(init_order[2] == 0x1A0011000ULL, "libA initializer third");
        TEST_CHECK(init_order[3] == 0x100001000ULL, "App initializer fourth");
    }

    // Call again to verify idempotency (no duplicate initialization)
    std::vector<std::string> init_names2;
    auto init_order2 = dyld.get_initialization_order(dep_res.load_order, init_names2);
    TEST_CHECK(init_order2.empty(), "Already initialized images must not run initializers again");

    std::cout << "  -> Synthetic Dyld pipeline integration fully verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.10: Synthetic Dyld Integration Tests\n";
    std::cout << "==================================================================\n";

    test_synthetic_dyld_full_pipeline();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.10 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
