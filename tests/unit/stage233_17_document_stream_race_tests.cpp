#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Document Stream Race Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::documents;
using namespace winios::kernel;
using namespace winios::security;

static void test_concurrent_read_write_stream_safety() {
    std::cout << "[Stage 233.17] Testing concurrent multi-threaded stream access...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const uint32_t pid = 100;
    ApplicationIdentity app{"com.concurrent.app", "1.0.0", "ConcurrentApp", 0x02030000};
    ProcessHandle h = id_mgr->allocate_fixed_pid(pid, app);
    TEST_ASSERT(h.is_valid());

    cap_mgr->grant(pid, Capability::DocumentRead);
    cap_mgr->grant(pid, Capability::DocumentWrite);
    cap_mgr->grant(pid, Capability::DocumentCreate);

    DocumentHandle doc = registry->register_document(
        pid, "com.concurrent.app", "shared.bin", "/Documents/shared.bin", "application/octet-stream", 0, false);
    TEST_ASSERT(doc != kInvalidDocumentHandle);

    StreamHandle sh = stream_mgr.open_stream(doc, pid, DocumentAccessMode::ReadWrite);
    TEST_ASSERT(sh != kInvalidStreamHandle);

    std::atomic<bool> start_signal{false};
    std::atomic<int> write_successes{0};
    std::atomic<int> read_successes{0};

    // Thread 1: Writer
    std::thread writer([&]() {
        while (!start_signal.load()) std::this_thread::yield();
        for (int i = 0; i < 50; ++i) {
            uint8_t byte = static_cast<uint8_t>(i & 0xFF);
            size_t written = 0;
            if (stream_mgr.write_stream(sh, pid, &byte, 1, written)) {
                write_successes++;
            }
        }
    });

    // Thread 2: Reader
    std::thread reader([&]() {
        while (!start_signal.load()) std::this_thread::yield();
        for (int i = 0; i < 50; ++i) {
            uint8_t buf[1]{};
            size_t nread = 0;
            if (stream_mgr.read_stream(sh, pid, buf, 1, nread)) {
                read_successes++;
            }
        }
    });

    start_signal.store(true);
    writer.join();
    reader.join();

    TEST_ASSERT(write_successes.load() == 50);
    TEST_ASSERT(read_successes.load() == 50);
    TEST_ASSERT(stream_mgr.close_stream(sh, pid));
}

static void test_process_termination_stream_cleanup_and_stale_handles() {
    std::cout << "[Stage 233.17] Testing process termination stream cleanup and handle generation...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    DocumentStreamManager stream_mgr(registry, vfs);

    const uint32_t owner_pid = 200;
    const uint32_t client_pid = 201;

    ApplicationIdentity app_o{"com.owner", "1.0.0", "Owner", 0x02030000};
    ApplicationIdentity app_c{"com.client", "1.0.0", "Client", 0x02030000};
    TEST_ASSERT(id_mgr->allocate_fixed_pid(owner_pid, app_o).is_valid());
    TEST_ASSERT(id_mgr->allocate_fixed_pid(client_pid, app_c).is_valid());

    cap_mgr->grant(owner_pid, Capability::DocumentRead);
    cap_mgr->grant(owner_pid, Capability::DocumentShare);
    cap_mgr->grant(owner_pid, Capability::DocumentCreate);
    cap_mgr->grant(client_pid, Capability::DocumentRead);

    DocumentHandle doc = registry->register_document(
        owner_pid, "com.owner", "doc1.txt", "/Documents/doc1.txt", "text/plain", 100, false);
    auto grant = registry->create_grant(doc, owner_pid, client_pid, "com.client", DocumentAccessMode::Read, 0);
    TEST_ASSERT(grant.grant_token != 0);

    StreamHandle sh = stream_mgr.open_stream(doc, client_pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh != kInvalidStreamHandle);
    TEST_ASSERT(stream_mgr.is_stream_valid(sh));

    // Client process terminates -> supervisor calls close_all_for_process
    stream_mgr.close_all_for_process(client_pid);

    // Stream must no longer be valid
    TEST_ASSERT(!stream_mgr.is_stream_valid(sh));

    // Attempting to read using the terminated client's old handle must fail
    char buf[16]{};
    size_t nread = 0;
    TEST_ASSERT(!stream_mgr.read_stream(sh, client_pid, buf, sizeof(buf), nread));

    // Another process attempting to use that stream handle must also fail
    TEST_ASSERT(!stream_mgr.read_stream(sh, owner_pid, buf, sizeof(buf), nread));
}

static void test_stream_mode_confinement() {
    std::cout << "[Stage 233.17] Testing stream mode confinement (read-only stream rejecting write)...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    DocumentStreamManager stream_mgr(registry, nullptr);

    const uint32_t pid = 300;
    ApplicationIdentity app{"com.mode.app", "1.0.0", "ModeApp", 0x02030000};
    TEST_ASSERT(id_mgr->allocate_fixed_pid(pid, app).is_valid());

    // Process has BOTH Read and Write capabilities
    cap_mgr->grant(pid, Capability::DocumentRead);
    cap_mgr->grant(pid, Capability::DocumentWrite);
    cap_mgr->grant(pid, Capability::DocumentCreate);

    DocumentHandle doc = registry->register_document(
        pid, "com.mode.app", "mode_test.txt", "/Documents/mode_test.txt", "text/plain", 10, false);

    // Open stream in READ mode only
    StreamHandle sh_ro = stream_mgr.open_stream(doc, pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh_ro != kInvalidStreamHandle);

    // Attempting to write to read-only stream must be strictly rejected
    const char write_data[] = "FORBIDDEN_WRITE";
    size_t nwritten = 0;
    TEST_ASSERT(!stream_mgr.write_stream(sh_ro, pid, write_data, sizeof(write_data), nwritten));

    // Reading from it must succeed
    char read_buf[16]{};
    size_t nread = 0;
    TEST_ASSERT(stream_mgr.read_stream(sh_ro, pid, read_buf, sizeof(read_buf), nread));

    TEST_ASSERT(stream_mgr.close_stream(sh_ro, pid));
}

static void test_stream_handle_generation_recycling() {
    std::cout << "[Stage 233.17] Testing stream handle generation recycling...\n";

    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto registry = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);
    DocumentStreamManager stream_mgr(registry, nullptr);

    const uint32_t pid = 400;
    ApplicationIdentity app{"com.recycle.app", "1.0.0", "RecycleApp", 0x02030000};
    TEST_ASSERT(id_mgr->allocate_fixed_pid(pid, app).is_valid());

    cap_mgr->grant(pid, Capability::DocumentRead);
    cap_mgr->grant(pid, Capability::DocumentCreate);

    DocumentHandle doc = registry->register_document(
        pid, "com.recycle.app", "r.txt", "/Documents/r.txt", "text/plain", 10, false);

    // Open stream 1
    StreamHandle sh1 = stream_mgr.open_stream(doc, pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh1 != kInvalidStreamHandle);

    // Close stream 1
    TEST_ASSERT(stream_mgr.close_stream(sh1, pid));

    // Old handle sh1 is invalid
    TEST_ASSERT(!stream_mgr.is_stream_valid(sh1));

    // Open stream 2
    StreamHandle sh2 = stream_mgr.open_stream(doc, pid, DocumentAccessMode::Read);
    TEST_ASSERT(sh2 != kInvalidStreamHandle);
    TEST_ASSERT(sh1 != sh2); // Generations or IDs differ

    // Calling read on sh1 must still fail
    char buf[16]{};
    size_t nread = 0;
    TEST_ASSERT(!stream_mgr.read_stream(sh1, pid, buf, sizeof(buf), nread));

    // Calling read on sh2 succeeds
    TEST_ASSERT(stream_mgr.read_stream(sh2, pid, buf, sizeof(buf), nread));

    TEST_ASSERT(stream_mgr.close_stream(sh2, pid));
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.17: Document Stream Race & Concurrency  \n";
    std::cout << "====================================================\n";

    test_concurrent_read_write_stream_safety();
    test_process_termination_stream_cleanup_and_stale_handles();
    test_stream_mode_confinement();
    test_stream_handle_generation_recycling();

    std::cout << "\n[Stage 233.17] ALL DOCUMENT STREAM RACE TESTS PASSED.\n";
    return 0;
}
