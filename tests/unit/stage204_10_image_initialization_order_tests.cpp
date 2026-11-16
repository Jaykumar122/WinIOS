// =============================================================================
// WinIOS — Stage 204.10: Runtime Initialization and Termination Order Unit Tests
// =============================================================================

#include "loader/dyld_engine.h"
#include <iostream>
#include <vector>
#include <string>

using namespace winios::loader;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Bottom-Up Topological Dependency Initialization Order
// -----------------------------------------------------------------------------
void test_topological_initialization_order() {
    std::cout << "[Stage 204.10 Test 1] Topological Initialization Order...\n";

    DyldEngine engine;
    engine.clear_modules();

    // LibBase (leaf)
    DyldLoadedModule lib_base;
    lib_base.name = "libbase.dylib";
    lib_base.initializers = {0x1000ULL};
    lib_base.terminators  = {0x1080ULL};
    engine.register_module(std::move(lib_base));

    // LibA depends on LibBase
    DyldLoadedModule lib_a;
    lib_a.name = "liba.dylib";
    lib_a.dependencies = {"libbase.dylib"};
    lib_a.initializers = {0x2000ULL};
    lib_a.terminators  = {0x2080ULL};
    engine.register_module(std::move(lib_a));

    // LibB depends on LibBase
    DyldLoadedModule lib_b;
    lib_b.name = "libb.dylib";
    lib_b.dependencies = {"libbase.dylib"};
    lib_b.initializers = {0x3000ULL};
    lib_b.terminators  = {0x3080ULL};
    engine.register_module(std::move(lib_b));

    // Main App depends on LibA and LibB
    DyldLoadedModule app;
    app.name = "MainApp";
    app.dependencies = {"liba.dylib", "libb.dylib"};
    app.initializers = {0x4000ULL};
    app.terminators  = {0x4080ULL};
    engine.register_module(std::move(app));

    // Compute topological initializers starting from MainApp
    std::vector<std::string> init_modules;
    auto init_funcs = engine.compute_topological_initializers("MainApp", init_modules);

    TEST_CHECK(init_modules.size() == 4, "All 4 modules scheduled for initialization");
    // LibBase MUST initialize before LibA and LibB, and MainApp MUST initialize last!
    TEST_CHECK(init_modules[0] == "libbase.dylib", "LibBase initialized first (bottom of DAG)");
    TEST_CHECK(init_modules.back() == "MainApp", "MainApp initialized last (root of DAG)");

    TEST_CHECK(init_funcs.size() == 4, "4 initializer functions scheduled");
    TEST_CHECK(init_funcs[0] == 0x1000ULL, "LibBase initializer ran first");
    TEST_CHECK(init_funcs.back() == 0x4000ULL, "MainApp initializer ran last");

    std::cout << "  -> Bottom-up topological initialization verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Reverse Topological Termination Order (LIFO)
// -----------------------------------------------------------------------------
void test_topological_termination_order() {
    std::cout << "[Stage 204.10 Test 2] Reverse Topological Termination Order...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Register modules
    DyldLoadedModule lib_dep;
    lib_dep.name = "libdep.dylib";
    lib_dep.initializers = {0x1000ULL};
    lib_dep.terminators  = {0x1080ULL};
    engine.register_module(std::move(lib_dep));

    DyldLoadedModule app;
    app.name = "App";
    app.dependencies = {"libdep.dylib"};
    app.initializers = {0x2000ULL};
    app.terminators  = {0x2080ULL};
    engine.register_module(std::move(app));

    // Initialize
    std::vector<std::string> inits;
    (void)engine.compute_topological_initializers("App", inits);

    // Compute terminators
    std::vector<std::string> term_modules;
    auto term_funcs = engine.compute_topological_terminators(term_modules);

    TEST_CHECK(term_modules.size() == 2, "All initialized modules scheduled for termination");
    // LIFO order: App must terminate before libdep!
    TEST_CHECK(term_modules[0] == "App", "App terminated first");
    TEST_CHECK(term_modules[1] == "libdep.dylib", "Dependency terminated after dependent");

    TEST_CHECK(term_funcs.size() == 2, "2 terminator functions scheduled");
    TEST_CHECK(term_funcs[0] == 0x2080ULL, "App terminator executed first");
    TEST_CHECK(term_funcs[1] == 0x1080ULL, "Dependency terminator executed last");

    std::cout << "  -> Reverse topological termination verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Run-Once Guarantees
// -----------------------------------------------------------------------------
void test_run_once_guarantees() {
    std::cout << "[Stage 204.10 Test 3] Run-Once Guarantees...\n";

    DyldEngine engine;
    engine.clear_modules();

    DyldLoadedModule mod;
    mod.name = "libonce.dylib";
    mod.initializers = {0x5000ULL};
    mod.terminators  = {0x5080ULL};
    engine.register_module(std::move(mod));

    std::vector<std::string> init1;
    auto f1 = engine.compute_topological_initializers("libonce.dylib", init1);
    TEST_CHECK(f1.size() == 1, "First init invocation schedules initializer");

    std::vector<std::string> init2;
    auto f2 = engine.compute_topological_initializers("libonce.dylib", init2);
    TEST_CHECK(f2.empty(), "Second init invocation does NOT run again (run-once guarantee)");

    std::vector<std::string> term1;
    auto t1 = engine.compute_topological_terminators(term1);
    TEST_CHECK(t1.size() == 1, "First termination invocation runs terminator");

    std::vector<std::string> term2;
    auto t2 = engine.compute_topological_terminators(term2);
    TEST_CHECK(t2.empty(), "Second termination invocation does NOT run again");

    std::cout << "  -> Run-once guarantees verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Circular Dependency Cycle Breaking
// -----------------------------------------------------------------------------
void test_circular_dependency_breaking() {
    std::cout << "[Stage 204.10 Test 4] Circular Dependency Cycle Breaking...\n";

    DyldEngine engine;
    engine.clear_modules();

    DyldLoadedModule mod_x;
    mod_x.name = "libX.dylib";
    mod_x.dependencies = {"libY.dylib"};
    mod_x.initializers = {0x7000ULL};
    engine.register_module(std::move(mod_x));

    DyldLoadedModule mod_y;
    mod_y.name = "libY.dylib";
    mod_y.dependencies = {"libX.dylib"};
    mod_y.initializers = {0x8000ULL};
    engine.register_module(std::move(mod_y));

    std::vector<std::string> inits;
    auto funcs = engine.compute_topological_initializers("libX.dylib", inits);

    TEST_CHECK(inits.size() == 2, "Both cyclic modules resolved without deadlock");
    TEST_CHECK(funcs.size() == 2, "Both initializers scheduled");

    std::cout << "  -> Circular dependency cycle breaking verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Initializer Pointer Executability Validation
// -----------------------------------------------------------------------------
void test_initializer_executability_validation() {
    std::cout << "[Stage 204.10 Test 5] Initializer Executability Validation...\n";

    DyldEngine engine;

    MachOSegmentInfo text_seg;
    text_seg.name = "__TEXT";
    text_seg.vmaddr = 0x100000000ULL;
    text_seg.vmsize = 0x4000ULL;
    text_seg.initprot = kVmProtExecute | kVmProtRead;
    text_seg.maxprot = kVmProtExecute | kVmProtRead;

    MachOSegmentInfo data_seg;
    data_seg.name = "__DATA";
    data_seg.vmaddr = 0x100004000ULL;
    data_seg.vmsize = 0x4000ULL;
    data_seg.initprot = kVmProtRead | kVmProtWrite;
    data_seg.maxprot = kVmProtRead | kVmProtWrite;

    std::vector<MachOSegmentInfo> segs = {text_seg, data_seg};

    // 1. Valid pointer in __TEXT (executable)
    DyldLoadedModule valid_mod;
    valid_mod.name = "valid.dylib";
    valid_mod.initializers = {0x100000100ULL};
    std::string err;
    TEST_CHECK(engine.validate_initializers(valid_mod, segs, err), "Valid executable initializer accepted");

    // 2. Invalid pointer in __DATA (non-executable)
    DyldLoadedModule data_mod;
    data_mod.name = "data_rogue.dylib";
    data_mod.initializers = {0x100004100ULL};
    TEST_CHECK(!engine.validate_initializers(data_mod, segs, err), "Initializer in __DATA rejected");

    // 3. Null pointer
    DyldLoadedModule null_mod;
    null_mod.name = "null_rogue.dylib";
    null_mod.initializers = {0ULL};
    TEST_CHECK(!engine.validate_initializers(null_mod, segs, err), "Null initializer pointer rejected");

    std::cout << "  -> Initializer executability validation verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.10 Runtime Initialization & Termination Tests ===\n";
    test_topological_initialization_order();
    test_topological_termination_order();
    test_run_once_guarantees();
    test_circular_dependency_breaking();
    test_initializer_executability_validation();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.10 Runtime Initialization & Termination passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.10 Runtime Initialization & Termination failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}
