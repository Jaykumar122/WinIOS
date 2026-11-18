#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "vfs/vfs.h"
#include "documents/document_types.h"
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "documents/recent_document_manager.h"
#include "documents/desktop_session_manager.h"
#include "security/capability_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::vfs;
using namespace winios::documents;
using namespace winios::security;

int main() {
    std::cout << "[Test Stage 227.23] Adversarial Document & Session Security Tests...\n" << std::flush;

    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->mkdir("/Documents");
    vfs->mkdir("/Shared");
    vfs->mkdir("/tmp");

    std::vector<uint8_t> dummy_bytes = {'T', 'E', 'S', 'T'};
    vfs->write_file("/Documents/file1.txt", dummy_bytes.data(), dummy_bytes.size());
    vfs->write_file("/Documents/file2.txt", dummy_bytes.data(), dummy_bytes.size());

    auto caps = std::make_shared<CapabilityManager>();
    // Grant Alice (PID 1001) all document permissions
    caps->grant_mask(1001, Capability::DocumentRead | Capability::DocumentWrite | Capability::DocumentCreate | Capability::DocumentShare | Capability::DocumentDelete);
    // Eve (PID 1337) starts with NO permissions
    // Bob (PID 100)
    caps->grant_mask(100, Capability::DocumentRead | Capability::DocumentWrite | Capability::DocumentCreate | Capability::DocumentShare | Capability::DocumentDelete);

    auto doc_reg = std::make_shared<DocumentRegistry>(caps);
    auto stream_mgr = std::make_shared<DocumentStreamManager>(doc_reg, vfs);

    // 1. Forged Document Handles
    const uint64_t forged_doc_handles[] = {
        0ULL,
        ~0ULL,
        0x0000000100000000ULL, // Generation 1, ID 0
        0x0000000000000001ULL, // Generation 0, ID 1
        0xDEADBEEF00000042ULL, // Forged generation
        0x00000001FFFFFFFFULL  // Out-of-bounds ID
    };

    for (uint64_t raw_h : forged_doc_handles) {
        DocumentHandle h{raw_h};
        TEST_ASSERT(!doc_reg->is_handle_valid(h));
        auto meta = doc_reg->get_metadata(h);
        TEST_ASSERT(!meta.has_value());
        TEST_ASSERT(!doc_reg->check_access(h, 100, DocumentAccessMode::Read));
        TEST_ASSERT(!doc_reg->check_access(h, 100, DocumentAccessMode::Write));
        auto grant = doc_reg->create_grant(h, 100, 200, "com.winios.grantee", DocumentAccessMode::Read);
        TEST_ASSERT(grant.grant_token == 0);
        TEST_ASSERT(!doc_reg->revoke_grant(12345, 100));

        StreamHandle sh = stream_mgr->open_stream(h, 100, DocumentAccessMode::Read);
        TEST_ASSERT(sh == kInvalidStreamHandle);
    }

    // 2. Stale Document Handle after Unregister
    DocumentHandle valid_h = doc_reg->register_document(100, "com.winios.app", "file1.txt", "/Documents/file1.txt", "text/plain", dummy_bytes.size());
    TEST_ASSERT(valid_h != kInvalidDocumentHandle);
    TEST_ASSERT(doc_reg->is_handle_valid(valid_h));
    TEST_ASSERT(doc_reg->unregister_document(valid_h, 100));
    TEST_ASSERT(!doc_reg->is_handle_valid(valid_h));

    // Stale handle operations must now fail
    auto stale_meta = doc_reg->get_metadata(valid_h);
    TEST_ASSERT(!stale_meta.has_value());

    // 3. Forged and Stale Stream Handles
    const uint64_t forged_stream_handles[] = {
        0ULL,
        ~0ULL,
        0x0000000100000000ULL,
        0x0000000000000001ULL,
        0xBADC0FFEE0000007ULL
    };

    for (uint64_t raw_sh : forged_stream_handles) {
        StreamHandle sh{raw_sh};
        TEST_ASSERT(!stream_mgr->is_stream_valid(sh));
        std::vector<uint8_t> buf(16);
        size_t bytes_io = 0;
        TEST_ASSERT(!stream_mgr->read_stream(sh, 100, buf.data(), buf.size(), bytes_io));
        TEST_ASSERT(!stream_mgr->write_stream(sh, 100, buf.data(), buf.size(), bytes_io));
        uint64_t cursor = 0;
        TEST_ASSERT(!stream_mgr->seek_stream(sh, 100, 0, cursor));
        TEST_ASSERT(!stream_mgr->close_stream(sh, 100));
    }

    // 4. Cross-Process Tampering Denial
    DocumentHandle alice_h = doc_reg->register_document(1001, "com.winios.alice", "file2.txt", "/Documents/file2.txt", "text/plain", dummy_bytes.size());
    TEST_ASSERT(alice_h != kInvalidDocumentHandle);

    // Eve (PID 1337) attempts to access Alice's document without grant or capability
    StreamHandle eve_sh = stream_mgr->open_stream(alice_h, 1337, DocumentAccessMode::Read);
    TEST_ASSERT(eve_sh == kInvalidStreamHandle);
    eve_sh = stream_mgr->open_stream(alice_h, 1337, DocumentAccessMode::Write);
    TEST_ASSERT(eve_sh == kInvalidStreamHandle);
    auto eve_grant = doc_reg->create_grant(alice_h, 1337, 2000, "com.winios.eve", DocumentAccessMode::Read);
    TEST_ASSERT(eve_grant.grant_token == 0);
    TEST_ASSERT(!doc_reg->revoke_grant(123, 1337));
    TEST_ASSERT(!doc_reg->unregister_document(alice_h, 1337));

    // Alice grants Read access to Eve (give Eve DocumentRead capability so she can use the grant)
    caps->grant(1337, Capability::DocumentRead);
    auto alice_grant = doc_reg->create_grant(alice_h, 1001, 1337, "com.winios.eve", DocumentAccessMode::Read);
    TEST_ASSERT(alice_grant.grant_token != 0);

    eve_sh = stream_mgr->open_stream(alice_h, 1337, DocumentAccessMode::Read);
    TEST_ASSERT(eve_sh != kInvalidStreamHandle);
    TEST_ASSERT(stream_mgr->close_stream(eve_sh, 1337));

    // Eve tries to open with Write - must be denied
    eve_sh = stream_mgr->open_stream(alice_h, 1337, DocumentAccessMode::Write);
    TEST_ASSERT(eve_sh == kInvalidStreamHandle);

    // Alice revokes grant from Eve
    TEST_ASSERT(doc_reg->revoke_grant(alice_grant.grant_token, 1001));
    eve_sh = stream_mgr->open_stream(alice_h, 1337, DocumentAccessMode::Read);
    TEST_ASSERT(eve_sh == kInvalidStreamHandle);

    // 5. Path Traversal & Root Sandbox Violation Denial
    const std::string malicious_paths[] = {
        "/Documents/../../etc/passwd",
        "/tmp/../System/Library/passwords",
        "relative/path/document.txt",
        "C:\\Windows\\System32\\cmd.exe",
        "/UnauthorizedRoot/data.txt",
        "/Documents/./../Shared/../../leak.txt"
    };

    for (const auto& path : malicious_paths) {
        std::string clean;
        TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path(path, clean));
        DocumentHandle bad_h = doc_reg->register_document(1001, "com.winios.alice", "bad", path, "text/plain", 0);
        TEST_ASSERT(bad_h == kInvalidDocumentHandle);
    }

    // 6. Bounded Streams (10 MB Bound) & Overflow Protection
    DocumentHandle test_doc = doc_reg->register_document(1001, "com.winios.alice", "large.bin", "/Shared/large.bin", "application/octet-stream", 0);
    TEST_ASSERT(test_doc != kInvalidDocumentHandle);
    StreamHandle large_sh = stream_mgr->open_stream(test_doc, 1001, DocumentAccessMode::Write);
    TEST_ASSERT(large_sh != kInvalidStreamHandle);

    // Attempting seek beyond 10MB limit must be rejected
    uint64_t cursor = 0;
    TEST_ASSERT(!stream_mgr->seek_stream(large_sh, 1001, 0xFFFFFFFFFFFFFFFFULL, cursor));
    TEST_ASSERT(!stream_mgr->seek_stream(large_sh, 1001, kMaxDocumentSizeBytes + 1024, cursor));

    // Attempting write exceeding 10MB limit must be rejected
    std::vector<uint8_t> overflow_byte = {0xAA};
    size_t written = 0;
    TEST_ASSERT(!stream_mgr->write_stream(large_sh, 1001, overflow_byte.data(), kMaxDocumentSizeBytes + 1, written));
    TEST_ASSERT(stream_mgr->close_stream(large_sh, 1001));

    // 7. Recent Documents Flooding / DoS Protection
    RecentDocumentManager recent_mgr(vfs);
    for (int i = 0; i < 300; ++i) {
        recent_mgr.add_recent("com.winios.app", DocumentHandle{static_cast<uint64_t>(100 + i)}, "doc_" + std::to_string(i) + ".txt", "/Documents/doc_" + std::to_string(i) + ".txt");
    }
    auto recents = recent_mgr.get_recent("com.winios.app");
    TEST_ASSERT(recents.size() <= 16);

    // 8. Desktop Session Manager Corrupt JSON Resilience
    DesktopSessionManager session_mgr(nullptr, nullptr, vfs);

    const std::string bad_jsons[] = {
        "",
        "{",
        "{\"session_version\": 2, \"windows\": [",
        "{\"windows\": null, \"documents\": 12345}",
        "not json at all"
    };

    for (const auto& bad_str : bad_jsons) {
        DesktopSessionData loaded{};
        // Must return false cleanly without crashing
        TEST_ASSERT(!session_mgr.deserialize(bad_str, loaded));
    }

    std::cout << "[Test Stage 227.23] PASSED successfully.\n" << std::flush;
    return 0;
}
