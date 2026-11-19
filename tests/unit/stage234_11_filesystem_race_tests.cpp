#include "packaging/canonical_path_validator.h"

#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#endif

using namespace winios::packaging;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

// Helper to get a clean temp directory
static std::filesystem::path get_test_dir(const std::string& sub) {
    auto base = std::filesystem::temp_directory_path() / "winios_stage234_11" / sub;
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

// -----------------------------------------------------------------------------
// Test 1: Normal Confined File Read and Write
// -----------------------------------------------------------------------------
static void test_normal_confined_file_io() {
    std::cout << "[Stage 234.11] Test 1: Testing Normal Confined File IO...\n";

    auto root = get_test_dir("normal_io");
    std::string root_str = root.string();

    // 1. Write file through open_confined_file_write
    {
        auto confined = CanonicalPathValidator::open_confined_file_write(root_str, "sub/data.bin", true);
        TEST_ASSERT(confined.is_valid, "open_confined_file_write succeeded for valid child");
        TEST_ASSERT(confined.handle != nullptr && confined.handle != INVALID_HANDLE_VALUE, "Valid handle returned");

#ifdef _WIN32
        const char msg[] = "ConfinedDataPayload";
        DWORD written = 0;
        BOOL wres = WriteFile(static_cast<HANDLE>(confined.handle), msg, sizeof(msg), &written, nullptr);
        TEST_ASSERT(wres && written == sizeof(msg), "WriteFile succeeded");
#endif
        CanonicalPathValidator::close_confined_file(confined);
        TEST_ASSERT(!confined.is_valid && confined.handle == nullptr, "Handle cleanly closed");
    }

    // 2. Read back through open_confined_file_read
    {
        auto confined = CanonicalPathValidator::open_confined_file_read(root_str, "sub/data.bin");
        TEST_ASSERT(confined.is_valid, "open_confined_file_read succeeded for valid child");

#ifdef _WIN32
        char buffer[64] = {0};
        DWORD bytes_read = 0;
        BOOL rres = ReadFile(static_cast<HANDLE>(confined.handle), buffer, sizeof(buffer), &bytes_read, nullptr);
        TEST_ASSERT(rres && bytes_read > 0, "ReadFile succeeded");
        TEST_ASSERT(std::string(buffer) == "ConfinedDataPayload", "Data content matches");
#endif
        CanonicalPathValidator::close_confined_file(confined);
    }

    std::cout << "  -> Normal confined IO verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Lexical Traversal Rejection
// -----------------------------------------------------------------------------
static void test_lexical_traversal_rejection() {
    std::cout << "[Stage 234.11] Test 2: Testing Lexical Traversal Rejection...\n";

    auto root = get_test_dir("lexical");
    std::string root_str = root.string();

    // Attempts to escape root via .. are rejected immediately
    auto confined_write = CanonicalPathValidator::open_confined_file_write(root_str, "../escaped.bin", true);
    TEST_ASSERT(!confined_write.is_valid, "Traversal write rejected");

    auto confined_read = CanonicalPathValidator::open_confined_file_read(root_str, "../../boot.ini");
    TEST_ASSERT(!confined_read.is_valid, "Traversal read rejected");

    std::cout << "  -> Lexical traversal rejection verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: NTFS Junction / Reparse-Point Jailbreak Defense
// -----------------------------------------------------------------------------
static void test_junction_jailbreak_defense() {
    std::cout << "[Stage 234.11] Test 3: Testing NTFS Junction Jailbreak Defense...\n";

#ifdef _WIN32
    auto base = get_test_dir("junction_test");
    auto root = base / "root";
    auto outside = base / "outside";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);

    // Create outside file
    auto outside_file = outside / "secret.txt";
    {
        std::ofstream ofs(outside_file);
        ofs << "CLASSIFIED_OUTSIDE_DATA";
    }

    // Create NTFS junction: root/jailbreak -> outside
    auto junction_path = root / "jailbreak";
    std::string cmd = "cmd /c mklink /J \"" + junction_path.string() + "\" \"" + outside.string() + "\" >nul 2>&1";
    int ret = std::system(cmd.c_str());
    TEST_ASSERT(ret == 0, "Junction creation succeeded");

    // Verify is_reparse_point recognizes the junction
    TEST_ASSERT(CanonicalPathValidator::is_reparse_point(junction_path.string()), "Junction recognized as reparse point");

    // Attempt 1: Read outside file via junction
    // Path: "jailbreak/secret.txt" (lexically appears inside root)
    {
        auto confined = CanonicalPathValidator::open_confined_file_read(root.string(), "jailbreak/secret.txt");
        // Must be REJECTED because handle resolves outside root
        TEST_ASSERT(!confined.is_valid, "Handle-based read through junction was successfully BLOCKED");
        TEST_ASSERT(confined.handle == nullptr, "Handle was closed");
    }

    // Attempt 2: Write outside file via junction
    // Path: "jailbreak/injected.txt"
    {
        auto confined = CanonicalPathValidator::open_confined_file_write(root.string(), "jailbreak/injected.txt", true);
        // Must be REJECTED because parent directory handle resolves outside root
        TEST_ASSERT(!confined.is_valid, "Handle-based write through junction was successfully BLOCKED");
        TEST_ASSERT(confined.handle == nullptr, "Write handle was closed");
        TEST_ASSERT(!std::filesystem::exists(outside / "injected.txt"), "Outside directory was NOT modified");
    }

    // Clean up junction before removing dir
    std::string rmdir_cmd = "cmd /c rmdir \"" + junction_path.string() + "\" >nul 2>&1";
    std::system(rmdir_cmd.c_str());
#endif

    std::cout << "  -> Junction jailbreak defense verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Handle-to-Handle Confinement Verification
// -----------------------------------------------------------------------------
static void test_handle_to_handle_verification() {
    std::cout << "[Stage 234.11] Test 4: Testing Handle-to-Handle Invariants...\n";

#ifdef _WIN32
    auto base = get_test_dir("handle_pairs");
    auto root = base / "root";
    auto outside = base / "outside";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);

    auto inside_file = root / "inside.txt";
    auto outside_file = outside / "outside.txt";
    {
        std::ofstream ofs1(inside_file);
        ofs1 << "inside";
        std::ofstream ofs2(outside_file);
        ofs2 << "outside";
    }

    HANDLE hRoot = CreateFileA(root.string().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    HANDLE hInside = CreateFileA(inside_file.string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE hOutside = CreateFileA(outside_file.string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

    TEST_ASSERT(hRoot != INVALID_HANDLE_VALUE, "Root handle valid");
    TEST_ASSERT(hInside != INVALID_HANDLE_VALUE, "Inside file handle valid");
    TEST_ASSERT(hOutside != INVALID_HANDLE_VALUE, "Outside file handle valid");

    // 1. Inside file handle is inside root handle
    TEST_ASSERT(CanonicalPathValidator::is_handle_strictly_inside_root_handle(hInside, hRoot),
                "Inside handle recognized as inside root handle");

    // 2. Outside file handle is NOT inside root handle
    TEST_ASSERT(!CanonicalPathValidator::is_handle_strictly_inside_root_handle(hOutside, hRoot),
                "Outside handle recognized as NOT inside root handle");

    // 3. Null and invalid handles return false safely
    TEST_ASSERT(!CanonicalPathValidator::is_handle_strictly_inside_root_handle(nullptr, hRoot), "Null target handle handled");
    TEST_ASSERT(!CanonicalPathValidator::is_handle_strictly_inside_root_handle(hInside, nullptr), "Null root handle handled");
    TEST_ASSERT(!CanonicalPathValidator::is_handle_strictly_inside_root_handle(INVALID_HANDLE_VALUE, hRoot), "Invalid handle handled");

    CloseHandle(hInside);
    CloseHandle(hOutside);
    CloseHandle(hRoot);
#endif

    std::cout << "  -> Handle-to-handle verification verified.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.11: Filesystem TOCTOU & Reparse-Point Test Suite     \n";
    std::cout << "=================================================================\n";

    test_normal_confined_file_io();
    test_lexical_traversal_rejection();
    test_junction_jailbreak_defense();
    test_handle_to_handle_verification();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL FILESYSTEM TOCTOU & REPARSE-POINT TESTS PASSED!            \n";
    std::cout << "=================================================================\n";
    return 0;
}
