#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

using namespace winios;
using namespace winios::storage;
using namespace winios::vfs;

void test_path_safety() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();

    IsolatedStorage storage(vfs, "com.winios.notes");

    // Safe paths
    assert(storage.is_path_safe("notes.txt"));
    assert(storage.is_path_safe("subfolder/doc.json"));
    assert(storage.is_path_safe("a/b/c/data.bin"));

    // Traversal attacks rejected
    assert(!storage.is_path_safe("../escaped.txt"));
    assert(!storage.is_path_safe("sub/../../escaped.txt"));
    assert(!storage.is_path_safe("..\\escaped.txt"));
    assert(!storage.is_path_safe("/etc/passwd"));
    assert(!storage.is_path_safe("\\windows\\system32"));
    assert(!storage.is_path_safe(""));

    std::cout << "[PASS] test_path_safety passed\n";
}

void test_atomic_write_and_recovery() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();

    IsolatedStorage storage(vfs, "com.winios.counter");

    const std::string test_data = "Count: 42\nState: Verified";
    auto res_write = storage.atomic_write("state.txt", test_data.data(), test_data.size());
    assert(res_write == StorageResult::Success);

    // Read back
    std::vector<std::uint8_t> read_buf;
    auto res_read = storage.read("state.txt", read_buf);
    assert(res_read == StorageResult::Success);
    assert(read_buf.size() == test_data.size());
    assert(std::memcmp(read_buf.data(), test_data.data(), test_data.size()) == 0);

    // Simulate crash leaving orphan .tmp file
    const std::string orphan_tmp = "/data/com.winios.counter/crash.txt.tmp.999";
    assert(vfs->create_file(orphan_tmp));
    assert(vfs->exists(orphan_tmp));

    // Run recovery
    std::size_t recovered = storage.recover_transactions();
    assert(recovered == 1);
    assert(!vfs->exists(orphan_tmp));
    // Valid file untouched
    assert(storage.exists("state.txt"));

    std::cout << "[PASS] test_atomic_write_and_recovery passed\n";
}

void test_storage_isolation_and_quota() {
    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();

    IsolatedStorage storageA(vfs, "com.winios.appA", 1024 /* 1 KB quota */);
    IsolatedStorage storageB(vfs, "com.winios.appB", 1024);

    const std::string secretA = "Secret Data A";
    assert(storageA.atomic_write("secret.txt", secretA.data(), secretA.size()) == StorageResult::Success);

    // AppB cannot see or read AppA's file through its isolated storage
    std::vector<std::uint8_t> bufB;
    assert(storageB.read("secret.txt", bufB) == StorageResult::FileNotFound);
    assert(!storageB.exists("secret.txt"));

    // Attempting path escape into AppA
    assert(!storageB.is_path_safe("../com.winios.appA/secret.txt"));
    assert(storageB.read("../com.winios.appA/secret.txt", bufB) == StorageResult::InvalidPath);

    // Quota test: AppA has 1024 byte quota; attempting to write 2000 bytes fails
    std::vector<std::uint8_t> big_data(2000, 0xFF);
    auto res_quota = storageA.atomic_write("big.bin", big_data.data(), big_data.size());
    assert(res_quota == StorageResult::QuotaExceeded);

    std::cout << "[PASS] test_storage_isolation_and_quota passed\n";
}

int main() {
    std::cout << "Starting Stage 215.12 Isolated Storage Tests...\n";
    test_path_safety();
    test_atomic_write_and_recovery();
    test_storage_isolation_and_quota();
    std::cout << "All Stage 215.12 Isolated Storage tests PASSED!\n";
    return 0;
}
