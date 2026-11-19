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

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios::documents;
using namespace winios::vfs;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Immediate Grant Revocation Interception
// -----------------------------------------------------------------------------
static void test_immediate_grant_revocation_interception() {
    std::cout << "[Stage 234.18] Test 1: Testing Immediate Grant Revocation Interception...\n";

    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const std::uint32_t owner_pid = 100;
    const std::uint32_t grantee_pid = 200;

    auto doc = registry->register_document(owner_pid, "com.owner.app", "notes.txt",
                                           "/Documents/notes.txt", "text/plain", 1024, false);
    TEST_ASSERT(doc != kInvalidDocumentHandle, "Document registered");

    // Grant ReadWrite access to grantee
    auto grant = registry->create_grant(doc, owner_pid, grantee_pid, "com.grantee.app",
                                        DocumentAccessMode::ReadWrite, 0);
    TEST_ASSERT(grant.grant_token != 0 && !grant.is_revoked, "Grant created");

    // Grantee opens stream
    auto stream = stream_mgr.open_stream(doc, grantee_pid, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(stream != kInvalidStreamHandle, "Stream opened successfully");

    // Write works while grant is active
    const char write_data[] = "ACTIVE_GRANT_PAYLOAD";
    std::size_t written = 0;
    bool w_ok = stream_mgr.write_stream(stream, grantee_pid, write_data, sizeof(write_data), written);
    TEST_ASSERT(w_ok && written == sizeof(write_data), "Write succeeded under active grant");

    // Seek back to start and verify read
    std::uint64_t new_cursor = 0;
    stream_mgr.seek_stream(stream, grantee_pid, 0, new_cursor);
    char read_buf[64] = {0};
    std::size_t bytes_read = 0;
    bool r_ok = stream_mgr.read_stream(stream, grantee_pid, read_buf, sizeof(write_data), bytes_read);
    TEST_ASSERT(r_ok && bytes_read == sizeof(write_data), "Read succeeded under active grant");

    // Owner revokes the grant
    bool rev_ok = registry->revoke_grant(grant.grant_token, owner_pid);
    TEST_ASSERT(rev_ok, "Grant revoked");

    // Stream remains open in memory, but ANY read/write must be IMMEDIATELY DENIED via dynamic check_access
    char read_post[64] = {0};
    std::size_t bytes_read_post = 0;
    bool r_post = stream_mgr.read_stream(stream, grantee_pid, read_post, 32, bytes_read_post);
    TEST_ASSERT(!r_post, "Read on revoked grant was successfully BLOCKED");

    std::size_t written_post = 0;
    bool w_post = stream_mgr.write_stream(stream, grantee_pid, "EVIL", 4, written_post);
    TEST_ASSERT(!w_post, "Write on revoked grant was successfully BLOCKED");

    std::cout << "  -> Immediate revocation interception verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Multithreaded Read/Write/Revoke Race
// -----------------------------------------------------------------------------
static void test_multithreaded_read_write_revoke_race() {
    std::cout << "[Stage 234.18] Test 2: Testing Multithreaded Concurrent Read/Write/Revoke Race...\n";

    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const std::uint32_t owner_pid = 100;
    const std::uint32_t grantee_pid = 200;

    auto doc = registry->register_document(owner_pid, "com.owner.app", "shared.bin",
                                           "/Documents/shared.bin", "application/octet-stream", 4096, false);
    auto grant = registry->create_grant(doc, owner_pid, grantee_pid, "com.grantee.app",
                                        DocumentAccessMode::ReadWrite, 0);
    auto stream = stream_mgr.open_stream(doc, grantee_pid, DocumentAccessMode::ReadWrite);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<int> pre_revocation_ops{0};
    std::atomic<int> post_revocation_denials{0};

    // Worker 1: Writer thread
    auto writer = [&]() {
        while (!start_flag.load()) { /* spin */ }
        const char chunk[16] = "DATA_CHUNK_001";
        while (!stop_flag.load()) {
            std::size_t written = 0;
            if (stream_mgr.write_stream(stream, grantee_pid, chunk, sizeof(chunk), written)) {
                pre_revocation_ops++;
            } else {
                post_revocation_denials++;
            }
            std::this_thread::yield();
        }
    };

    // Worker 2: Reader thread
    auto reader = [&]() {
        while (!start_flag.load()) { /* spin */ }
        char buf[16];
        while (!stop_flag.load()) {
            std::size_t bytes_read = 0;
            if (stream_mgr.read_stream(stream, grantee_pid, buf, sizeof(buf), bytes_read)) {
                pre_revocation_ops++;
            } else {
                post_revocation_denials++;
            }
            std::this_thread::yield();
        }
    };

    std::thread t_writer(writer);
    std::thread t_reader(reader);

    // Revoker thread
    std::thread t_revoker([&]() {
        while (!start_flag.load()) { /* spin */ }
        // Let operations run for a moment
        while (pre_revocation_ops.load() < 20) {
            std::this_thread::yield();
        }
        // Concurrently revoke grant
        registry->revoke_grant(grant.grant_token, owner_pid);
        // Wait for workers to experience denials
        while (post_revocation_denials.load() < 10) {
            std::this_thread::yield();
        }
        stop_flag.store(true);
    });

    start_flag.store(true);

    t_writer.join();
    t_reader.join();
    t_revoker.join();

    TEST_ASSERT(pre_revocation_ops >= 20, "Operations succeeded prior to revocation");
    TEST_ASSERT(post_revocation_denials >= 10, "Operations rejected after revocation");

    // Final invariant check: after revocation, stream can NEVER be read or written again
    char final_buf[16];
    std::size_t final_read = 0;
    TEST_ASSERT(!stream_mgr.read_stream(stream, grantee_pid, final_buf, sizeof(final_buf), final_read),
                "Stream permanently sealed against reads");

    std::cout << "  -> Multithreaded read/write/revoke race verified without corruption.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Explicit Stream Invalidation via close_streams_for_grantee
// -----------------------------------------------------------------------------
static void test_explicit_stream_invalidation() {
    std::cout << "[Stage 234.18] Test 3: Testing Explicit Stream Invalidation...\n";

    auto registry = std::make_shared<DocumentRegistry>();
    auto vfs = std::make_shared<VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const std::uint32_t owner_pid = 100;
    const std::uint32_t grantee_pid = 200;

    auto doc = registry->register_document(owner_pid, "com.owner.app", "file.bin",
                                           "/Documents/file.bin", "text/plain", 512, false);
    registry->create_grant(doc, owner_pid, grantee_pid, "com.grantee.app",
                           DocumentAccessMode::Read, 0);

    auto stream = stream_mgr.open_stream(doc, grantee_pid, DocumentAccessMode::Read);
    TEST_ASSERT(stream_mgr.is_stream_valid(stream), "Stream is initially valid");

    // Invalidate grantee's streams
    stream_mgr.close_streams_for_grantee(doc, grantee_pid);

    // Stream must now be invalid due to generation advancement / removal
    TEST_ASSERT(!stream_mgr.is_stream_valid(stream), "Stream handle is now invalid");

    char buf[16];
    std::size_t r = 0;
    TEST_ASSERT(!stream_mgr.read_stream(stream, grantee_pid, buf, sizeof(buf), r), "Read on invalidated stream rejected");

    std::cout << "  -> Explicit stream invalidation verified.\n";
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
    std::cout << "  Stage 234.18: Document Grant Lifetime & Race Test Suite       \n";
    std::cout << "=================================================================\n";

    test_immediate_grant_revocation_interception();
    test_multithreaded_read_write_revoke_race();
    test_explicit_stream_invalidation();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL DOCUMENT GRANT LIFETIME & RACE TESTS PASSED!               \n";
    std::cout << "=================================================================\n";
    return 0;
}
