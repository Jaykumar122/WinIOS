#include "documents/recent_document_manager.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

int main() {
    using namespace winios;
    using namespace winios::documents;

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    vfs->mkdir("/Documents");

    RecentDocumentManager mgr(vfs);

    // 1. Add recents
    mgr.add_recent("com.winios.editor", 0x100000001ULL, "doc1.txt", "/Documents/doc1.txt");
    mgr.add_recent("com.winios.editor", 0x100000002ULL, "doc2.txt", "/Documents/doc2.txt");

    auto list = mgr.get_recent("com.winios.editor");
    TEST_ASSERT(list.size() == 2);
    TEST_ASSERT(list[0].canonical_path == "/Documents/doc2.txt"); // Most recent first
    TEST_ASSERT(list[1].canonical_path == "/Documents/doc1.txt");

    // 2. Re-adding existing moves to front
    mgr.add_recent("com.winios.editor", 0x100000001ULL, "doc1.txt", "/Documents/doc1.txt");
    list = mgr.get_recent("com.winios.editor");
    TEST_ASSERT(list.size() == 2);
    TEST_ASSERT(list[0].canonical_path == "/Documents/doc1.txt"); // Moved to front!

    // 3. Bound to 16 entries
    for (int i = 3; i <= 25; ++i) {
        mgr.add_recent("com.winios.editor", static_cast<uint64_t>(i), "doc" + std::to_string(i) + ".txt",
                       "/Documents/doc" + std::to_string(i) + ".txt");
    }
    list = mgr.get_recent("com.winios.editor");
    TEST_ASSERT(list.size() == 16); // Strictly capped at 16!

    // 4. Missing file pruning against VFS
    const std::string dummy = "vfs file\n";
    vfs->write_file("/Documents/doc25.txt", dummy.data(), dummy.size());
    // Only doc25.txt exists in VFS, others do not
    mgr.prune_missing();
    list = mgr.get_recent("com.winios.editor");
    TEST_ASSERT(list.size() == 1);
    TEST_ASSERT(list[0].canonical_path == "/Documents/doc25.txt");

    // 5. Serialization and Deserialization roundtrip
    mgr.add_recent("com.winios.editor", 0x100000099ULL, "doc99.txt", "/Documents/doc99.txt");
    std::string serialized = mgr.serialize();

    RecentDocumentManager mgr2(vfs);
    TEST_ASSERT(mgr2.deserialize(serialized));
    auto list2 = mgr2.get_recent("com.winios.editor");
    TEST_ASSERT(list2.size() == 2);
    TEST_ASSERT(list2[0].canonical_path == "/Documents/doc99.txt");

    std::cout << "stage227_11_recent_document_tests: ALL PASSED" << std::endl;
    return 0;
}
