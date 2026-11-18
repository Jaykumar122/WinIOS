#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::storage;
using namespace winios::vfs;

void test_path_sanitization() {
    std::cout << "[Test 1] Path Traversal and Sanitization Defense...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    IsolatedStorage storage(vfs, "com.winios.app", 1024 * 1024);

    TEST_ASSERT(storage.is_path_safe("notes.txt"));
    TEST_ASSERT(storage.is_path_safe("docs/sub/notes.txt"));

    // Traversal attempts
    TEST_ASSERT(!storage.is_path_safe("../notes.txt"));
    TEST_ASSERT(!storage.is_path_safe("/notes.txt"));
    TEST_ASSERT(!storage.is_path_safe("\\notes.txt"));
    TEST_ASSERT(!storage.is_path_safe("docs/../../notes.txt"));
    TEST_ASSERT(!storage.is_path_safe("C:notes.txt"));

    std::cout << "[PASS] Path sanitization verified.\n";
}

void test_recursive_quota_and_overwrite() {
    std::cout << "[Test 2] Recursive Storage Quota and Overwrite Math...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    // Set 1000 byte quota
    IsolatedStorage storage(vfs, "com.winios.quota", 1000);

    // 1. Write file in subdirectory (300 bytes)
    std::vector<uint8_t> data300(300, 0xAA);
    auto res = storage.atomic_write("nested/dir/data.bin", data300.data(), data300.size());
    TEST_ASSERT(res == StorageResult::Success);

    // Verify recursive usage is 300 bytes!
    TEST_ASSERT(storage.current_usage_bytes() == 300);

    // 2. Write another file (400 bytes) -> total 700 bytes
    std::vector<uint8_t> data400(400, 0xBB);
    res = storage.atomic_write("top.bin", data400.data(), data400.size());
    TEST_ASSERT(res == StorageResult::Success);
    TEST_ASSERT(storage.current_usage_bytes() == 700);

    // 3. Overwrite top.bin with 400 bytes (700 - 400 + 400 = 700 <= 1000)
    // In buggy implementations this would fail because 700 + 400 = 1100 > 1000!
    res = storage.atomic_write("top.bin", data400.data(), data400.size());
    TEST_ASSERT(res == StorageResult::Success);

    // 4. Exceed quota: write 400 bytes to new file (700 + 400 = 1100 > 1000)
    res = storage.atomic_write("overflow.bin", data400.data(), data400.size());
    TEST_ASSERT(res == StorageResult::QuotaExceeded);

    std::cout << "[PASS] Recursive quota and overwrite math verified.\n";
}

void test_recursive_transaction_recovery() {
    std::cout << "[Test 3] Recursive Crash Transaction Recovery...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    IsolatedStorage storage(vfs, "com.winios.crash", 10000);

    // Create a nested directory with an orphaned .tmp file
    vfs->mkdir("/data/com.winios.crash/nested");
    vfs->create_file("/data/com.winios.crash/nested/doc.txt.tmp.123");
    vfs->create_file("/data/com.winios.crash/root.txt.tmp.124");
    vfs->create_file("/data/com.winios.crash/real_doc.txt");

    // Recover transactions
    std::size_t recovered = storage.recover_transactions();
    TEST_ASSERT(recovered == 2);
    TEST_ASSERT(!vfs->exists("/data/com.winios.crash/nested/doc.txt.tmp.123"));
    TEST_ASSERT(!vfs->exists("/data/com.winios.crash/root.txt.tmp.124"));
    TEST_ASSERT(vfs->exists("/data/com.winios.crash/real_doc.txt")); // Real doc preserved!

    std::cout << "[PASS] Recursive transaction recovery verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.11: STORAGE QUOTA & RECOVERY TESTS\n";
    std::cout << "========================================\n";

    test_path_sanitization();
    test_recursive_quota_and_overwrite();
    test_recursive_transaction_recovery();

    std::cout << "\nALL STAGE 224.11 TESTS PASSED (100%).\n";
    return 0;
}
