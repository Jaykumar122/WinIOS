// =============================================================================
// WinIOS — Stage 204.8: Runtime Search Paths and Dependency Identity Unit Tests
// =============================================================================

#include "loader/dyld_engine.h"
#include <iostream>
#include <vector>
#include <string>
#include <set>

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
// Test 1: Lexical Virtual Path Normalization & Dot-Dot Resolution
// -----------------------------------------------------------------------------
void test_virtual_path_normalization() {
    std::cout << "[Stage 204.8 Test 1] Virtual Path Normalization...\n";

    TEST_CHECK(DyldEngine::normalize_virtual_path("/usr/lib/../lib/libSystem.B.dylib") == "/usr/lib/libSystem.B.dylib",
               "Single .. resolved");
    TEST_CHECK(DyldEngine::normalize_virtual_path("/System//Library///Frameworks/./UIKit.framework") == "/System/Library/Frameworks/UIKit.framework",
               "Duplicate slashes and . collapsed");
    TEST_CHECK(DyldEngine::normalize_virtual_path("/var/root/../../etc/shadow") == "/etc/shadow",
               "Root-clamped traversal normalizes cleanly");
    TEST_CHECK(DyldEngine::normalize_virtual_path("Frameworks/../Frameworks/MyLib.dylib") == "Frameworks/MyLib.dylib",
               "Relative path normalization preserves relative semantics");
    TEST_CHECK(DyldEngine::normalize_virtual_path("C:\\App\\Frameworks\\Sub\\..\\Lib.dylib") == "C:/App/Frameworks/Lib.dylib",
               "Backslashes converted to forward slashes");

    std::cout << "  -> Path normalization verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Token Expansion (@executable_path, @loader_path)
// -----------------------------------------------------------------------------
void test_token_expansion() {
    std::cout << "[Stage 204.8 Test 2] Token Expansion...\n";

    std::string exec_dir = "/Applications/DemoApp.app";
    std::string loader_dir = "/Applications/DemoApp.app/Frameworks/Shared.framework";

    std::string s1 = DyldEngine::expand_path_tokens("@executable_path/Frameworks/libfoo.dylib", exec_dir, loader_dir);
    TEST_CHECK(s1 == "/Applications/DemoApp.app/Frameworks/libfoo.dylib", "Executable path token expanded");

    std::string s2 = DyldEngine::expand_path_tokens("@loader_path/../PlugIns/helper.dylib", exec_dir, loader_dir);
    TEST_CHECK(s2 == "/Applications/DemoApp.app/Frameworks/Shared.framework/../PlugIns/helper.dylib", "Loader path token expanded");

    std::string s3 = DyldEngine::expand_path_tokens("/usr/lib/libm.dylib", exec_dir, loader_dir);
    TEST_CHECK(s3 == "/usr/lib/libm.dylib", "Absolute path unchanged");

    std::cout << "  -> Token expansion verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Sandbox Boundary Enforcement & Traversal Rejection
// -----------------------------------------------------------------------------
void test_sandbox_boundary_enforcement() {
    std::cout << "[Stage 204.8 Test 3] Sandbox Boundary Enforcement...\n";

    std::string sandbox = "/private/var/containers/Bundle/Application/AppID";

    // Valid inside sandbox
    TEST_CHECK(DyldEngine::is_path_sandboxed("/private/var/containers/Bundle/Application/AppID/App.app/App", sandbox),
               "Valid path inside sandbox accepted");
    TEST_CHECK(DyldEngine::is_path_sandboxed("/private/var/containers/Bundle/Application/AppID/Frameworks/lib.dylib", sandbox),
               "Framework inside sandbox accepted");

    // Traversal attempts
    TEST_CHECK(!DyldEngine::is_path_sandboxed("/private/var/containers/Bundle/Application/AppID/../../etc/master.passwd", sandbox),
               "Path traversing outside sandbox rejected");
    TEST_CHECK(!DyldEngine::is_path_sandboxed("/usr/lib/libSystem.B.dylib", sandbox),
               "Foreign root path outside sandbox rejected");

    std::cout << "  -> Sandbox boundary enforcement verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: @rpath Priority Resolution with Simulated File Registry
// -----------------------------------------------------------------------------
void test_rpath_priority_resolution() {
    std::cout << "[Stage 204.8 Test 4] @rpath Priority Resolution...\n";

    // Simulated virtual filesystem file set
    std::set<std::string> vfs_files = {
        "/App/RPATH2/libconflict.dylib",
        "/App/RPATH1/libconflict.dylib", // Higher priority
        "/App/RPATH2/libunique2.dylib",
        "/usr/lib/libfallback.dylib"
    };

    DyldPathResolveOptions opts;
    opts.executable_dir = "/App";
    opts.loader_dir = "/App/Frameworks";
    opts.rpaths = {
        "@executable_path/RPATH1",
        "@executable_path/RPATH2"
    };
    opts.fallback_paths = {"/usr/lib", "/System/Library/Frameworks"};
    opts.sandbox_root = ""; // Unrestricted for this test
    opts.file_exists_predicate = [&](std::string_view p) {
        return vfs_files.find(std::string(p)) != vfs_files.end();
    };

    // 1. Priority order: libconflict exists in both RPATH1 and RPATH2; RPATH1 MUST win
    std::string r1 = DyldEngine::resolve_dependency_path("@rpath/libconflict.dylib", opts);
    TEST_CHECK(r1 == "/App/RPATH1/libconflict.dylib", "Higher priority RPATH1 candidate selected");

    // 2. Secondary RPATH: libunique2 exists only in RPATH2
    std::string r2 = DyldEngine::resolve_dependency_path("@rpath/libunique2.dylib", opts);
    TEST_CHECK(r2 == "/App/RPATH2/libunique2.dylib", "Secondary RPATH candidate selected");

    // 3. Fallback path: libfallback exists in /usr/lib (fallback)
    std::string r3 = DyldEngine::resolve_dependency_path("@rpath/libfallback.dylib", opts);
    TEST_CHECK(r3 == "/usr/lib/libfallback.dylib", "Fallback directory candidate selected when not in RPATHs");

    // 4. Nonexistent library: fails cleanly
    std::string r4 = DyldEngine::resolve_dependency_path("@rpath/libghost.dylib", opts);
    TEST_CHECK(r4.empty(), "Nonexistent library returns empty string");

    std::cout << "  -> @rpath priority and fallback resolution verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Sandbox Boundary Enforcement during @rpath Resolution
// -----------------------------------------------------------------------------
void test_rpath_sandboxed_resolution() {
    std::cout << "[Stage 204.8 Test 5] Sandboxed @rpath Resolution...\n";

    std::set<std::string> vfs_files = {
        "/Sandbox/App/Frameworks/Safe.dylib",
        "/System/Library/Frameworks/Escape.dylib"
    };

    DyldPathResolveOptions opts;
    opts.executable_dir = "/Sandbox/App";
    opts.loader_dir = "/Sandbox/App/Frameworks";
    opts.rpaths = {
        "@executable_path/Frameworks",
        "@executable_path/../../System/Library/Frameworks" // Malicious RPATH attempting sandbox escape!
    };
    opts.fallback_paths = {};
    opts.sandbox_root = "/Sandbox";
    opts.file_exists_predicate = [&](std::string_view p) {
        return vfs_files.find(std::string(p)) != vfs_files.end();
    };

    // Safe library inside /Sandbox
    std::string r1 = DyldEngine::resolve_dependency_path("@rpath/Safe.dylib", opts);
    TEST_CHECK(r1 == "/Sandbox/App/Frameworks/Safe.dylib", "Safe sandboxed library resolves");

    // Library attempting sandbox escape via ..
    std::string r2 = DyldEngine::resolve_dependency_path("@rpath/Escape.dylib", opts);
    TEST_CHECK(r2.empty(), "Sandbox escape attempt rejected even if file exists");

    std::cout << "  -> Sandboxed @rpath resolution verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.8 Runtime Search Paths & Dependency Identity Tests ===\n";
    test_virtual_path_normalization();
    test_token_expansion();
    test_sandbox_boundary_enforcement();
    test_rpath_priority_resolution();
    test_rpath_sandboxed_resolution();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.8 Runtime Search Paths & Dependency Identity passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.8 Runtime Search Paths & Dependency Identity failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}
