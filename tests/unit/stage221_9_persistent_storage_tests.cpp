#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

void test_isolated_storage_basic_rw_and_persistence() {
    std::cout << "[Test 1] Isolated Storage Read/Write & Persistence...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/data");

    // Session 1: App "com.winios.notes" writes a file
    {
        IsolatedStorage storage(vfs, "com.winios.notes");
        const std::string content = "Persistent note data from session 1";
        auto res = storage.atomic_write("note1.txt", content.data(), content.size());
        TEST_ASSERT(res == StorageResult::Success);
        TEST_ASSERT(storage.exists("note1.txt"));
    }

    // Session 2: App re-opens storage and reads back persisted note
    {
        IsolatedStorage storage(vfs, "com.winios.notes");
        TEST_ASSERT(storage.exists("note1.txt"));
        std::vector<uint8_t> data;
        auto res = storage.read("note1.txt", data);
        TEST_ASSERT(res == StorageResult::Success);
        std::string read_str(data.begin(), data.end());
        TEST_ASSERT(read_str == "Persistent note data from session 1");
    }

    std::cout << "[PASS] Basic read/write and persistence verified.\n";
}

void test_cross_app_storage_isolation() {
    std::cout << "[Test 2] Cross-Application Storage Isolation...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/data");

    IsolatedStorage appA(vfs, "com.winios.appA");
    IsolatedStorage appB(vfs, "com.winios.appB");

    const std::string secret = "SuperSecretKey123";
    TEST_ASSERT(appA.atomic_write("secret.key", secret.data(), secret.size()) == StorageResult::Success);

    // AppB must not see AppA's file in its sandbox
    TEST_ASSERT(!appB.exists("secret.key"));
    std::vector<uint8_t> out;
    TEST_ASSERT(appB.read("secret.key", out) == StorageResult::FileNotFound);

    // AppB cannot traverse to AppA's storage
    TEST_ASSERT(!appB.is_path_safe("../com.winios.appA/secret.key"));
    TEST_ASSERT(appB.read("../com.winios.appA/secret.key", out) == StorageResult::InvalidPath);

    std::cout << "[PASS] Cross-application isolation verified.\n";
}

void test_storage_quota_enforcement() {
    std::cout << "[Test 3] Storage Quota Enforcement...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/data");

    // Set small quota of 1 KB
    const size_t kSmallQuota = 1024;
    IsolatedStorage storage(vfs, "com.winios.quotatest", kSmallQuota);

    std::vector<uint8_t> small_chunk(512, 0x42);
    TEST_ASSERT(storage.atomic_write("small.bin", small_chunk.data(), small_chunk.size()) == StorageResult::Success);

    // Second write exceeding quota
    std::vector<uint8_t> big_chunk(1024, 0x99);
    auto res = storage.atomic_write("big.bin", big_chunk.data(), big_chunk.size());
    TEST_ASSERT(res == StorageResult::QuotaExceeded);

    std::cout << "[PASS] Quota enforcement verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.9: Persistent Per-Application Storage Tests ===\n";
    test_isolated_storage_basic_rw_and_persistence();
    test_cross_app_storage_isolation();
    test_storage_quota_enforcement();
    std::cout << "[ALL PASS] Stage 221.9 tests passed successfully.\n";
    return 0;
}
