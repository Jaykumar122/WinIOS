#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "documents/document_types.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <cstring>

using namespace winios::documents;
using namespace winios::vfs;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_concurrent_reader_streams() {
    std::cout << "[Test 1] Concurrent reader streams on shared document...\n";
    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const std::uint32_t owner_pid = 100;
    auto doc = registry->register_document(owner_pid, "com.owner.app", "shared.txt",
                                           "/Documents/shared.txt", "text/plain", 1024, false);
    TEST_ASSERT(doc != kInvalidDocumentHandle, "Document registered");

    // Write initial content to VFS
    const char content[] = "CONCURRENT_SHARED_DOCUMENT_CONTENT_235";
    vfs->mkdir("/Documents");
    vfs->create_file("/Documents/shared.txt");
    vfs->write_file("/Documents/shared.txt", content, sizeof(content));

    // Spawn 4 concurrent readers
    constexpr int kNumReaders = 4;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < kNumReaders; ++i) {
        std::uint32_t reader_pid = 200 + static_cast<std::uint32_t>(i);
        auto grant = registry->create_grant(doc, owner_pid, reader_pid, "com.reader.app",
                                            DocumentAccessMode::Read, 0);
        TEST_ASSERT(grant.grant_token != 0, "Grant created");

        threads.emplace_back([&, reader_pid]() {
            auto stream = stream_mgr.open_stream(doc, reader_pid, DocumentAccessMode::Read);
            if (stream != kInvalidStreamHandle) {
                char buf[64] = {0};
                std::size_t read_bytes = 0;
                if (stream_mgr.read_stream(stream, reader_pid, buf, sizeof(buf), read_bytes)) {
                    if (read_bytes == sizeof(content) && std::strcmp(buf, content) == 0) {
                        success_count++;
                    }
                }
                stream_mgr.close_stream(stream, reader_pid);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    TEST_ASSERT(success_count == kNumReaders, "All concurrent readers read successfully");
    std::cout << "  Passed!\n";
}

static void test_grant_revocation_closes_streams() {
    std::cout << "[Test 2] Grant revocation invalidates active streams...\n";
    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const std::uint32_t owner_pid = 100;
    const std::uint32_t grantee_pid = 300;

    auto doc = registry->register_document(owner_pid, "com.owner.app", "secret.txt",
                                           "/Documents/secret.txt", "text/plain", 1024, false);
    auto grant = registry->create_grant(doc, owner_pid, grantee_pid, "com.grantee.app",
                                        DocumentAccessMode::ReadWrite, 0);

    auto stream = stream_mgr.open_stream(doc, grantee_pid, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(stream != kInvalidStreamHandle, "Stream opened under active grant");

    // Revoke grant
    bool rev_ok = registry->revoke_grant(grant.grant_token, owner_pid);
    TEST_ASSERT(rev_ok, "Grant revoked by owner");

    // Subsequent write must fail
    const char write_data[] = "FAIL_PAYLOAD";
    std::size_t written = 0;
    bool w_ok = stream_mgr.write_stream(stream, grantee_pid, write_data, sizeof(write_data), written);
    TEST_ASSERT(!w_ok, "Write must fail after grant revocation");
    std::cout << "  Passed!\n";
}

static void test_unauthorized_pid_access_denied() {
    std::cout << "[Test 3] Unauthorized PID access rejection...\n";
    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    auto doc = registry->register_document(100, "com.owner.app", "private.txt",
                                           "/Documents/private.txt", "text/plain", 1024, false);

    // PID 999 was never granted access
    auto stream = stream_mgr.open_stream(doc, 999, DocumentAccessMode::Read);
    TEST_ASSERT(stream == kInvalidStreamHandle, "Unauthorized PID must receive invalid stream handle");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.17 Document Concurrency & Grant Tests ===" << std::endl;
    test_concurrent_reader_streams();
    test_grant_revocation_closes_streams();
    test_unauthorized_pid_access_denied();
    std::cout << "=== All Stage 235.17 tests PASSED successfully ===" << std::endl;
    return 0;
}
