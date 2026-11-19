#include "security/security_utils.h"
#include "security/publisher_trust_store.h"
#include "security/capability_manager.h"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "packaging/canonical_path_validator.h"
#include "loader/app_launcher.h"
#include "loader/app_inspector.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "documents/document_registry.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#if defined(_WIN32)
#include <cstdlib>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

using namespace winios;
using namespace winios::security;
using namespace winios::packaging;
using namespace winios::kernel;
using namespace winios::platform;
using namespace winios::documents;

static void test_package_tampering_and_revocation() {
    std::cout << "[Test 1] Package signature tampering, manifest substitution, and key revocation...\n";

    PublisherTrustStore trust_store;
    PublisherKey dev_key{};
    dev_key.key_id = "adv_dev_key";
    dev_key.publisher_id = "com.winios.adversary";
    dev_key.role = PublisherRole::Developer;
    dev_key.key_bytes = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                         0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                         0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                         0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
    dev_key.valid_from = 0;
    dev_key.valid_until = 0xFFFFFFFF;
    dev_key.is_revoked = false;
    assert(trust_store.register_key(dev_key));

    // Create valid package
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.tampertest";
    pkg.manifest.name = "TamperApp";
    pkg.manifest.display_name = "Tamper Test";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.add_file("bin/main", {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01});

    auto valid_packed = pkg.pack_v3();
    assert(PublisherTrustStore::sign_package(valid_packed, dev_key));

    // Verify valid package passes
    auto v_res = trust_store.verify_package_signature(
        valid_packed.data(), valid_packed.size(), TrustPolicy::Development);
    assert(v_res.status == SignatureStatus::DeveloperSigned);
    assert(v_res.is_trusted);

    // 1. Bit-flip tampering in signature trailer (within the 32-byte signature block)
    auto tampered_sig = valid_packed;
    tampered_sig[tampered_sig.size() - 10] ^= 0xFF;
    auto t1_res = trust_store.verify_package_signature(
        tampered_sig.data(), tampered_sig.size(), TrustPolicy::Development);
    assert(t1_res.status == SignatureStatus::CorruptSignature);
    assert(!t1_res.is_trusted);

    // 2. Manifest payload substitution (modify bundle ID in payload without re-signing)
    auto tampered_payload = valid_packed;
    // Find manifest start and modify a byte
    for (size_t i = 16; i < tampered_payload.size() - 64; ++i) {
        if (tampered_payload[i] == 't' && tampered_payload[i + 1] == 'a' && tampered_payload[i + 2] == 'm') {
            tampered_payload[i] = 'X';
            break;
        }
    }
    auto t2_res = trust_store.verify_package_signature(
        tampered_payload.data(), tampered_payload.size(), TrustPolicy::Development);
    assert(t2_res.status == SignatureStatus::CorruptSignature || !t2_res.is_trusted);

    // 3. Key revocation: revoke publisher key
    assert(trust_store.revoke_key("adv_dev_key"));
    auto t3_res = trust_store.verify_package_signature(
        valid_packed.data(), valid_packed.size(), TrustPolicy::Development);
    assert(t3_res.status == SignatureStatus::RevokedPublisher);
    assert(!t3_res.is_trusted);

    std::cout << "  -> Passed.\n";
}

static void test_fuzz_and_path_traversal_attacks() {
    std::cout << "[Test 2] Mutation parser fuzzing and path traversal attacks...\n";

    // 1. Parser rejects invalid magic
    std::vector<uint8_t> bad_magic = {'B', 'A', 'D', 'M', 'A', 'G', 'I', 'C', 0, 0, 0, 0, 0, 0, 0, 0};
    AppPackage p1{};
    assert(AppPackage::validate_package_data(bad_magic.data(), bad_magic.size(), &p1) == PackageValidationResult::InvalidMagic);

    // 2. Parser rejects truncated header
    std::vector<uint8_t> trunc = {'W', 'I', 'O', 'S', 'P', 'K', 'G', '3'};
    AppPackage p2{};
    assert(AppPackage::validate_package_data(trunc.data(), trunc.size(), &p2) == PackageValidationResult::PayloadTruncated);

    // Corrupted header version != 3
    std::vector<uint8_t> corrupt_hdr(64, 0);
    std::memcpy(corrupt_hdr.data(), "WIOSPKG3", 8);
    uint32_t bad_ver = 99;
    std::memcpy(corrupt_hdr.data() + 8, &bad_ver, 4);
    assert(AppPackage::validate_package_data(corrupt_hdr.data(), corrupt_hdr.size(), &p2) == PackageValidationResult::CorruptedHeader);

    // 3. Canonical path validation rejects traversal and reserved names
    std::string norm;
    // Traversal
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("../etc/passwd", norm) == PathValidationStatus::DirectoryTraversal);
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/../../secret", norm) == PathValidationStatus::DirectoryTraversal);
    // Absolute root
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("/bin/app", norm) == PathValidationStatus::AbsoluteRootForbidden);
    // Drive letter
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("C:/Windows/System32", norm) == PathValidationStatus::DriveLetterForbidden);
    // Windows device names
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("res/CON", norm) == PathValidationStatus::WindowsReservedDevice);
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("res/NUL.txt", norm) == PathValidationStatus::WindowsReservedDevice);
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("res/COM1", norm) == PathValidationStatus::WindowsReservedDevice);
    // Alternate Data Streams
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("res/icon.png:hidden", norm) == PathValidationStatus::AlternateDataStreamForbidden);
    // Control characters
    assert(CanonicalPathValidator::validate_and_normalize_bundle_path("res/bad\x01file", norm) == PathValidationStatus::ControlCharactersForbidden);

    std::cout << "  -> Passed.\n";
}

static void test_process_recycling_and_token_anti_replay() {
    std::cout << "[Test 3] Process handle recycling, token forgery, and anti-replay defense...\n";

    auto identity_mgr = std::make_shared<ProcessIdentityManager>(100, 200, 0);
    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto ipc_broker = std::make_shared<IpcCapabilityBroker>(identity_mgr, cap_mgr);
    auto doc_reg = std::make_shared<DocumentRegistry>(cap_mgr, identity_mgr);

    ApplicationIdentity sys_app{"com.winios.sys", "1.0.0", "Sys", 0x00020400};
    ApplicationIdentity app{"com.winios.app", "1.0.0", "App", 0x00020400};

    // Allocate fixed PID 1 for sys
    ProcessHandle h_sys = identity_mgr->allocate_fixed_pid(1, sys_app);
    assert(h_sys.pid == 1 && h_sys.generation == 1);

    // Allocate fixed PID 101 gen 1
    ProcessHandle h1 = identity_mgr->allocate_fixed_pid(101, app);
    assert(h1.pid == 101 && h1.generation == 1);

    // Grant IPC token
    auto token = ipc_broker->grant_communication_right(h1, h_sys, 0x1);
    assert(!token.token_hex.empty());

    // Send sequence 1
    std::vector<uint8_t> payload = {1, 2, 3};
    assert(ipc_broker->send_message_by_hex(token.token_hex, h1, payload.data(), payload.size(), 1, 1) == IpcStatus::Success);

    // Replay sequence 1 -> REJECTED
    assert(ipc_broker->send_message_by_hex(token.token_hex, h1, payload.data(), payload.size(), 1, 1) == IpcStatus::ReplayDetected);

    // Forged token hex -> REJECTED
    std::string forged_token = "0123456789abcdef0123456789abcdef";
    assert(ipc_broker->send_message_by_hex(forged_token, h1, payload.data(), payload.size(), 1, 2) == IpcStatus::PermissionDenied);

    // Grant document access to h1
    cap_mgr->grant(1, Capability::DocumentCreate);
    cap_mgr->grant(1, Capability::DocumentShare);
    cap_mgr->grant(101, Capability::DocumentRead);
    auto doc_h = doc_reg->register_document(1, "com.winios.sys", "Doc1", "/Documents/doc1.txt", "text/plain", 100, false);
    assert(doc_h != kInvalidDocumentHandle);
    doc_reg->create_grant(doc_h, h_sys, h1, "com.winios.app", DocumentAccessMode::Read);
    assert(doc_reg->check_access(doc_h, h1, DocumentAccessMode::Read));

    // Terminate PID 101 gen 1
    assert(identity_mgr->terminate_process(h1));
    assert(!identity_mgr->is_valid_handle(h1));

    // Allocate PID 101 again -> Must receive generation 2
    ProcessHandle h2 = identity_mgr->allocate_fixed_pid(101, app);
    assert(h2.pid == 101 && h2.generation == 2);

    // Stale handle h1 fails IPC send
    assert(ipc_broker->send_message_by_hex(token.token_hex, h1, payload.data(), payload.size(), 1, 3) == IpcStatus::SenderInvalid);

    // Stale handle h1 cannot access document
    assert(!doc_reg->check_access(doc_h, h1, DocumentAccessMode::Read));

    // New generation h2 has NOT been granted access -> access denied! (No privilege leakage)
    assert(!doc_reg->check_access(doc_h, h2, DocumentAccessMode::Read));

    identity_mgr->terminate_process(h_sys);
    identity_mgr->terminate_process(h2);
    std::cout << "  -> Passed.\n";
}

static void test_macho_adversarial_validation() {
    std::cout << "[Test 4] Adversarial Mach-O binary rejection...\n";

    // 1. Non-Mach-O magic (ELF)
    std::vector<uint8_t> elf_bin = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
    elf_bin.resize(64, 0);
    auto rep1 = loader::AppInspector::inspect_binary(elf_bin.data(), elf_bin.size());
    assert(!rep1.is_valid);
    assert(rep1.magic != loader::AppInspector::kMachO64Magic);

    // 2. Non-ARM64 cputype (x86_64: 0x01000007)
    std::vector<uint8_t> x86_bin = {0xCF, 0xFA, 0xED, 0xFE, 0x07, 0x00, 0x00, 0x01};
    x86_bin.resize(64, 0);
    auto rep2 = loader::AppInspector::inspect_binary(x86_bin.data(), x86_bin.size());
    assert(!rep2.is_valid);
    assert(rep2.cputype != loader::AppInspector::kCpuTypeArm64);

    // 3. Genuine ARM64 binary passes
    auto genuine = loader::get_embedded_campaign230_app_startup();
    auto rep3 = loader::AppInspector::inspect_binary(genuine.data(), genuine.size());
    assert(rep3.is_valid);
    assert(rep3.page_aligned_16k);
    assert(rep3.wx_clean);

    std::cout << "  -> Passed.\n";
}

static void test_concurrent_ipc_and_resource_safety() {
    std::cout << "[Test 5] Concurrent IPC transactions and resource budget safety...\n";

    auto identity_mgr = std::make_shared<ProcessIdentityManager>(100, 300, 8);
    auto cap_mgr = std::make_shared<CapabilityManager>();
    auto ipc_broker = std::make_shared<IpcCapabilityBroker>(identity_mgr, cap_mgr);
    ResourceBudgetManager budget_mgr;

    ApplicationIdentity app_send{"com.winios.csender", "1.0.0", "CSender", 0x00020400};
    ApplicationIdentity app_recv{"com.winios.crecv", "1.0.0", "CRecv", 0x00020400};

    ProcessHandle h_snd = identity_mgr->allocate_process(app_send);
    ProcessHandle h_rcv = identity_mgr->allocate_process(app_recv);

    auto token = ipc_broker->grant_communication_right(h_snd, h_rcv, 0x1);
    assert(!token.token_hex.empty());

    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> total_sent{0};

    // Thread 1: Concurrent IPC sender
    std::thread sender_thread([&]() {
        uint64_t seq = 1;
        std::vector<uint8_t> data = {'T', 'E', 'S', 'T'};
        while (!stop_flag.load()) {
            auto st = ipc_broker->send_message_by_hex(
                token.token_hex, h_snd, data.data(), data.size(), 1, seq++);
            if (st == IpcStatus::Success) {
                total_sent.fetch_add(1);
            }
            std::this_thread::yield();
        }
    });

    // Thread 2: Concurrent resource allocation and release
    std::thread budget_thread([&]() {
        for (int i = 0; i < 1000; ++i) {
            budget_mgr.allocate_memory(h_snd.pid, 4096);
            budget_mgr.free_memory(h_snd.pid, 4096);
        }
    });

    // Run for a brief window
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_flag.store(true);

    sender_thread.join();
    budget_thread.join();

    assert(total_sent.load() > 0);
    assert(budget_mgr.get_task_memory(h_snd.pid) == 0);

    // Clean termination
    identity_mgr->terminate_process(h_snd);
    identity_mgr->terminate_process(h_rcv);

    std::cout << "  -> Passed (" << total_sent.load() << " concurrent messages sent safely).\n";
}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
    std::cout << "=== Stage 231.22: Adversarial, Mutation, and Concurrency Testing ===\n";
    test_package_tampering_and_revocation();
    test_fuzz_and_path_traversal_attacks();
    test_process_recycling_and_token_anti_replay();
    test_macho_adversarial_validation();
    test_concurrent_ipc_and_resource_safety();
    std::cout << "=== All Stage 231.22 Adversarial Tests PASSED ===\n";
    return 0;
}
