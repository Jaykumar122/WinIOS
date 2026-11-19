#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::documents;
using namespace winios::kernel;
using namespace winios::security;

static void test_grant_revocation_invalidates_active_streams() {
    std::cout << "[Stage 232.17] Testing grant revocation invalidation of active streams...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const uint32_t owner_pid = 100;
    const uint32_t grantee_pid = 200;

    // Setup identities and capabilities
    ApplicationIdentity app_owner{"com.sample.owner", "1.0.0", "Owner", 0x02030000};
    ApplicationIdentity app_grantee{"com.sample.grantee", "1.0.0", "Grantee", 0x02030000};
    ProcessHandle h_owner = id_mgr->allocate_fixed_pid(owner_pid, app_owner);
    ProcessHandle h_grantee = id_mgr->allocate_fixed_pid(grantee_pid, app_grantee);
    TEST_ASSERT(h_owner.is_valid() && h_grantee.is_valid());

    cap_mgr->grant(owner_pid, Capability::DocumentRead);
    cap_mgr->grant(owner_pid, Capability::DocumentWrite);
    cap_mgr->grant(owner_pid, Capability::DocumentShare);
    cap_mgr->grant(owner_pid, Capability::DocumentCreate);

    cap_mgr->grant(grantee_pid, Capability::DocumentRead);
    cap_mgr->grant(grantee_pid, Capability::DocumentWrite);

    // Create file in VFS
    vfs->mkdir("/Documents");
    const std::string file_content = "CONFIDENTIAL_FINANCIAL_RECORDS_2026";
    vfs->create_file("/Documents/records.txt");
    vfs->write_file("/Documents/records.txt", file_content.data(), file_content.size());

    // Register document
    DocumentHandle doc_handle = registry->register_document(
        owner_pid, "com.sample.owner", "records.txt", "/Documents/records.txt", "text/plain", file_content.size(), false);
    TEST_ASSERT(doc_handle != kInvalidDocumentHandle);

    // Create grant: owner grants Read access to grantee
    auto grant = registry->create_grant(doc_handle, owner_pid, grantee_pid, "com.sample.grantee", DocumentAccessMode::Read, 0);
    TEST_ASSERT(grant.grant_token != 0);

    // Grantee opens stream
    StreamHandle sh = stream_mgr.open_stream(doc_handle, grantee_pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh != kInvalidStreamHandle);

    // Grantee reads 12 bytes
    char read_buf[64]{};
    size_t nread = 0;
    TEST_ASSERT(stream_mgr.read_stream(sh, grantee_pid, read_buf, 12, nread));
    TEST_ASSERT(nread == 12);
    TEST_ASSERT(std::string(read_buf, nread) == "CONFIDENTIAL");

    // Owner revokes grant
    TEST_ASSERT(registry->revoke_grant(grant.grant_token, owner_pid));

    // Dynamic verification: subsequent read on open stream MUST fail!
    char fail_buf[64]{};
    size_t fail_read = 0;
    TEST_ASSERT(!stream_mgr.read_stream(sh, grantee_pid, fail_buf, 12, fail_read));

    // Attempting to open a new stream MUST also fail
    StreamHandle new_sh = stream_mgr.open_stream(doc_handle, grantee_pid, DocumentAccessMode::Read);
    TEST_ASSERT(new_sh == kInvalidStreamHandle);
}

static void test_time_bounded_grant_expiration() {
    std::cout << "[Stage 232.17] Testing time-bounded document grant expiration...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const uint32_t owner_pid = 300;
    const uint32_t grantee_pid = 301;

    ApplicationIdentity app_o{"com.test.o", "1.0.0", "O", 0x02030000};
    ApplicationIdentity app_g{"com.test.g", "1.0.0", "G", 0x02030000};
    ProcessHandle ho = id_mgr->allocate_fixed_pid(owner_pid, app_o);
    ProcessHandle hg = id_mgr->allocate_fixed_pid(grantee_pid, app_g);
    TEST_ASSERT(ho.is_valid() && hg.is_valid());

    cap_mgr->grant(owner_pid, Capability::DocumentRead);
    cap_mgr->grant(owner_pid, Capability::DocumentShare);
    cap_mgr->grant(owner_pid, Capability::DocumentCreate);
    cap_mgr->grant(grantee_pid, Capability::DocumentRead);

    DocumentHandle doc = registry->register_document(
        owner_pid, "com.test.o", "timed.txt", "/Documents/timed.txt", "text/plain", 100, false);
    TEST_ASSERT(doc != kInvalidDocumentHandle);

    // Create grant with 1,000 microseconds (1 ms) expiry
    auto grant = registry->create_grant(doc, owner_pid, grantee_pid, "com.test.g", DocumentAccessMode::Read, 1000);
    TEST_ASSERT(grant.grant_token != 0);

    // Immediate check access should succeed
    TEST_ASSERT(registry->check_access(doc, grantee_pid, DocumentAccessMode::Read));

    // Wait 5 milliseconds for grant window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // After window elapsed, check_access must return false (expired)
    TEST_ASSERT(!registry->check_access(doc, grantee_pid, DocumentAccessMode::Read));

    // Opening stream after expiry must return kInvalidStreamHandle
    StreamHandle sh = stream_mgr.open_stream(doc, grantee_pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh == kInvalidStreamHandle);
}

static void test_generational_process_handle_stale_grant_rejection() {
    std::cout << "[Stage 232.17] Testing generational process handle stale grant rejection...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);

    const uint32_t owner_pid = 400;
    const uint32_t target_pid = 401;

    ApplicationIdentity app_o{"com.test.o", "1.0.0", "O", 0x02030000};
    ApplicationIdentity app_victim{"com.test.v", "1.0.0", "Victim", 0x02030000};

    ProcessHandle ho = id_mgr->allocate_fixed_pid(owner_pid, app_o);
    TEST_ASSERT(ho.is_valid());
    ProcessHandle h_victim = id_mgr->allocate_fixed_pid(target_pid, app_victim);
    TEST_ASSERT(h_victim.pid == target_pid && h_victim.generation == 1);

    cap_mgr->grant(owner_pid, Capability::DocumentRead);
    cap_mgr->grant(owner_pid, Capability::DocumentShare);
    cap_mgr->grant(owner_pid, Capability::DocumentCreate);
    cap_mgr->grant(target_pid, Capability::DocumentRead);

    DocumentHandle doc = registry->register_document(
        owner_pid, "com.test.o", "shared.txt", "/Documents/shared.txt", "text/plain", 100, false);

    // Grant access to victim (PID 401, Generation 1)
    auto grant = registry->create_grant(doc, owner_pid, target_pid, "com.test.v", DocumentAccessMode::Read, 0);
    TEST_ASSERT(grant.grantee_generation == 1);

    // Terminate victim process
    TEST_ASSERT(id_mgr->terminate_process(h_victim));

    // Slot 401 is reallocated to an unrelated new process (Generation 2)
    ApplicationIdentity app_attacker{"com.test.attacker", "1.0.0", "Attacker", 0x02030000};
    ProcessHandle h_attacker = id_mgr->allocate_fixed_pid(target_pid, app_attacker);
    TEST_ASSERT(h_attacker.pid == target_pid && h_attacker.generation == 2);

    // Attacker process running on recycled PID 401 attempts to access doc:
    // check_access detects generation mismatch (active generation 2 vs grant generation 1)
    TEST_ASSERT(!registry->check_access(doc, target_pid, DocumentAccessMode::Read));
}

static void test_stream_read_write_offset_and_size_confinement() {
    std::cout << "[Stage 232.17] Testing stream read/write offset and size boundary confinement...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, nullptr);
    DocumentStreamManager stream_mgr(registry, nullptr);

    const uint32_t pid = 600;
    cap_mgr->grant(pid, Capability::DocumentRead);
    cap_mgr->grant(pid, Capability::DocumentWrite);
    cap_mgr->grant(pid, Capability::DocumentCreate);

    DocumentHandle doc = registry->register_document(
        pid, "com.test.rw", "buffer.bin", "/Documents/buffer.bin", "application/octet-stream", 0, false);

    StreamHandle sh = stream_mgr.open_stream(doc, pid, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(sh != kInvalidStreamHandle);

    // Write 100 bytes of data
    std::vector<uint8_t> test_data(100, 0x42);
    size_t written = 0;
    TEST_ASSERT(stream_mgr.write_stream(sh, pid, test_data.data(), test_data.size(), written));
    TEST_ASSERT(written == 100);

    // Seek to position 80
    uint64_t new_cursor = 0;
    TEST_ASSERT(stream_mgr.seek_stream(sh, pid, 80, new_cursor));
    TEST_ASSERT(new_cursor == 80);

    // Read 50 bytes: must be confined to remaining 20 bytes (100 - 80)
    std::vector<uint8_t> read_buf(50, 0);
    size_t nread = 0;
    TEST_ASSERT(stream_mgr.read_stream(sh, pid, read_buf.data(), read_buf.size(), nread));
    TEST_ASSERT(nread == 20); // Confined to end of buffer!

    // Next read is at EOF (cursor == 100, buffer.size() == 100)
    size_t eof_read = 999;
    TEST_ASSERT(stream_mgr.read_stream(sh, pid, read_buf.data(), read_buf.size(), eof_read));
    TEST_ASSERT(eof_read == 0);

    // Seek to offset > 10 MB limit or writing > 10 MB must be rejected
    std::vector<uint8_t> excessive_data(11 * 1024 * 1024, 0); // 11 MB
    size_t exc_written = 0;
    TEST_ASSERT(!stream_mgr.write_stream(sh, pid, excessive_data.data(), excessive_data.size(), exc_written));

    TEST_ASSERT(stream_mgr.close_stream(sh, pid));
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "STAGE 232.17: Document Grants and File Stream Security Tests\n";
    std::cout << "========================================================\n";

    test_grant_revocation_invalidates_active_streams();
    test_time_bounded_grant_expiration();
    test_generational_process_handle_stale_grant_rejection();
    test_stream_read_write_offset_and_size_confinement();

    std::cout << "All Stage 232.17 tests passed successfully!\n";
    return 0;
}
