#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "vfs/vfs.h"

#include <cassert>
#include <cstring>
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

    auto reg = std::make_shared<DocumentRegistry>();
    DocumentHandle doc_h = reg->register_document(50, "com.winios.editor", "stream_test.txt", "/Documents/stream_test.txt");
    TEST_ASSERT(doc_h != kInvalidDocumentHandle);

    DocumentStreamManager stream_mgr(reg, vfs);

    // 1. Open stream for ReadWrite
    StreamHandle st_h = stream_mgr.open_stream(doc_h, 50, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(st_h != kInvalidStreamHandle);
    TEST_ASSERT(stream_mgr.is_stream_valid(st_h));

    // 2. Write to stream
    const std::string test_data = "ARM64 WinIOS Document Stream Platform\nLine 2 content.\n";
    std::size_t written = 0;
    TEST_ASSERT(stream_mgr.write_stream(st_h, 50, test_data.data(), test_data.size(), written));
    TEST_ASSERT(written == test_data.size());
    TEST_ASSERT(stream_mgr.get_stream_size(st_h) == test_data.size());

    // 3. Seek to beginning
    std::uint64_t new_cursor = 999;
    TEST_ASSERT(stream_mgr.seek_stream(st_h, 50, 0, new_cursor));
    TEST_ASSERT(new_cursor == 0);

    // 4. Read back from stream
    std::vector<char> read_buf(test_data.size() + 1, 0);
    std::size_t read_bytes = 0;
    TEST_ASSERT(stream_mgr.read_stream(st_h, 50, read_buf.data(), test_data.size(), read_bytes));
    TEST_ASSERT(read_bytes == test_data.size());
    TEST_ASSERT(std::string(read_buf.data()) == test_data);

    // 5. Flush stream (stages to .tmp and promotes to canonical path)
    TEST_ASSERT(stream_mgr.flush_stream(st_h, 50));
    TEST_ASSERT(vfs->exists("/Documents/stream_test.txt"));
    TEST_ASSERT(!vfs->exists("/Documents/stream_test.txt.tmp")); // Cleaned up!

    // Verify VFS file content matches
    std::vector<std::uint8_t> vfs_data;
    TEST_ASSERT(vfs->read_file("/Documents/stream_test.txt", vfs_data));
    TEST_ASSERT(vfs_data.size() == test_data.size());

    // 6. Close stream
    TEST_ASSERT(stream_mgr.close_stream(st_h, 50));
    TEST_ASSERT(!stream_mgr.is_stream_valid(st_h));

    // 7. Re-open stream loads existing content from VFS
    StreamHandle st_h2 = stream_mgr.open_stream(doc_h, 50, DocumentAccessMode::Read);
    TEST_ASSERT(st_h2 != kInvalidStreamHandle);
    TEST_ASSERT(stream_mgr.get_stream_size(st_h2) == test_data.size());

    std::vector<char> read_buf2(test_data.size() + 1, 0);
    std::size_t read_bytes2 = 0;
    TEST_ASSERT(stream_mgr.read_stream(st_h2, 50, read_buf2.data(), test_data.size(), read_bytes2));
    TEST_ASSERT(std::string(read_buf2.data()) == test_data);
    TEST_ASSERT(stream_mgr.close_stream(st_h2, 50));

    // 8. Atomic save directly
    const std::string direct_data = "Replaced atomically.\n";
    TEST_ASSERT(stream_mgr.atomic_save_document(doc_h, 50, direct_data.data(), direct_data.size()));
    vfs_data.clear();
    TEST_ASSERT(vfs->read_file("/Documents/stream_test.txt", vfs_data));
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(vfs_data.data()), vfs_data.size()) == direct_data);

    std::cout << "stage227_8_document_stream_tests: ALL PASSED" << std::endl;
    return 0;
}
