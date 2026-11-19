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
#endif

using namespace winios::packaging;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static std::filesystem::path get_test_dir(const std::string& sub) {
    auto base = std::filesystem::temp_directory_path() / "winios_stage235_9" / sub;
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

static void test_confined_write_and_read() {
    std::cout << "[Test 1] Confined write and read with volume verification...\n";
    auto root = get_test_dir("confined_io");
    std::string root_str = root.string();

    auto confined = CanonicalPathValidator::open_confined_file_write(root_str, "app/payload.bin", true);
    TEST_ASSERT(confined.is_valid, "open_confined_file_write valid");
    TEST_ASSERT(confined.handle != nullptr && confined.handle != INVALID_HANDLE_VALUE, "Valid handle");

#ifdef _WIN32
    const char data[] = "WinIOS235ConfinedPayloadData";
    DWORD written = 0;
    BOOL ok = WriteFile(static_cast<HANDLE>(confined.handle), data, sizeof(data), &written, nullptr);
    TEST_ASSERT(ok && written == sizeof(data), "WriteFile succeeded");

    // Test check_volume_identity with root handle
    std::wstring wroot = root.wstring();
    HANDLE hRoot = CreateFileW(
        wroot.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    TEST_ASSERT(hRoot != INVALID_HANDLE_VALUE, "Opened root directory handle");
    TEST_ASSERT(CanonicalPathValidator::check_volume_identity(confined.handle, hRoot),
                "File and root directory must share volume identity");
    CloseHandle(hRoot);
#endif

    CanonicalPathValidator::close_confined_file(confined);
    TEST_ASSERT(!confined.is_valid && confined.handle == nullptr, "Handle cleanly reset");

    // Read back
    auto read_confined = CanonicalPathValidator::open_confined_file_read(root_str, "app/payload.bin");
    TEST_ASSERT(read_confined.is_valid, "open_confined_file_read valid");

#ifdef _WIN32
    char buf[64] = {0};
    DWORD read_bytes = 0;
    BOOL rok = ReadFile(static_cast<HANDLE>(read_confined.handle), buf, sizeof(buf), &read_bytes, nullptr);
    TEST_ASSERT(rok && read_bytes == sizeof(data), "ReadFile succeeded");
    TEST_ASSERT(std::string(buf) == data, "Data matches exactly");
#endif
    CanonicalPathValidator::close_confined_file(read_confined);
    std::cout << "  Passed!\n";
}

static void test_escaping_handle_rejection() {
    std::cout << "[Test 2] Escaping handle rejection...\n";
    auto root = get_test_dir("sandbox_root");
    auto outside = get_test_dir("outside_root");

    std::string root_str = root.string();
    std::string outside_str = outside.string();

    // Create outside target file
    std::filesystem::path outside_file = outside / "secret.txt";
    std::ofstream ofs(outside_file);
    ofs << "TopSecretOutsidePayload";
    ofs.close();

#ifdef _WIN32
    std::wstring woutside = outside_file.wstring();
    HANDLE hOutside = CreateFileW(
        woutside.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    TEST_ASSERT(hOutside != INVALID_HANDLE_VALUE, "Opened outside file");

    // is_handle_strictly_inside_root must reject handle pointing outside root
    bool inside = CanonicalPathValidator::is_handle_strictly_inside_root(hOutside, root_str);
    TEST_ASSERT(!inside, "Handle pointing outside root must be rejected");

    CloseHandle(hOutside);
#endif

    // Attempting to open traversal via relative path fails at validation
    auto res = CanonicalPathValidator::open_confined_file_write(root_str, "../secret.txt", false);
    TEST_ASSERT(!res.is_valid, "Relative escaping path rejected");
    std::cout << "  Passed!\n";
}

static void test_volume_identity_edge_cases() {
    std::cout << "[Test 3] Volume identity edge cases...\n";
    TEST_ASSERT(!CanonicalPathValidator::check_volume_identity(nullptr, nullptr), "Null handles rejected");
#ifdef _WIN32
    TEST_ASSERT(!CanonicalPathValidator::check_volume_identity(INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE),
                "Invalid handles rejected");
#endif
    std::cout << "  Passed!\n";
}

static void test_confined_file_lifecycle() {
    std::cout << "[Test 4] Confined file handle lifecycle and cleanup...\n";
    auto root = get_test_dir("lifecycle");
    std::string root_str = root.string();

    CanonicalPathValidator::ConfinedFileHandle h =
        CanonicalPathValidator::open_confined_file_write(root_str, "file.txt", true);
    TEST_ASSERT(h.is_valid, "Initial open valid");
    TEST_ASSERT(!h.canonical_path.empty(), "Path populated");

    CanonicalPathValidator::close_confined_file(h);
    TEST_ASSERT(!h.is_valid, "is_valid false after close");
    TEST_ASSERT(h.handle == nullptr, "handle null after close");
    TEST_ASSERT(h.canonical_path.empty(), "canonical_path empty after close");

    // Calling close again on already closed handle is safe (idempotent)
    CanonicalPathValidator::close_confined_file(h);
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.9 Filesystem Race & Reparse-Point Tests ===" << std::endl;
    test_confined_write_and_read();
    test_escaping_handle_rejection();
    test_volume_identity_edge_cases();
    test_confined_file_lifecycle();
    std::cout << "=== All Stage 235.9 tests PASSED successfully ===" << std::endl;
    return 0;
}
