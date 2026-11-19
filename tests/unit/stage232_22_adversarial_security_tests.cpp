#include "packaging/app_package.h"
#include "packaging/canonical_path_validator.h"
#include "packaging/package_installer.h"
#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "security/capability_manager.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "vfs/vfs.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <thread>
#include <atomic>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::security;
using namespace winios::kernel;

// Simple deterministic PRNG (Linear Congruential Generator) for reproducible fuzzing
class DeterministicFuzzer {
public:
    explicit DeterministicFuzzer(uint64_t seed = 0x191E232BULL) : state_(seed) {}

    uint32_t next_u32() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state_ >> 32);
    }

    uint32_t range(uint32_t min_val, uint32_t max_val) {
        if (min_val >= max_val) return min_val;
        return min_val + (next_u32() % (max_val - min_val + 1));
    }

    void mutate_buffer(std::vector<uint8_t>& buf, size_t mutation_count) {
        if (buf.empty()) return;
        for (size_t i = 0; i < mutation_count; ++i) {
            uint32_t op = range(0, 3);
            size_t idx = range(0, static_cast<uint32_t>(buf.size() - 1));
            switch (op) {
            case 0: // Bit flip
                buf[idx] ^= static_cast<uint8_t>(1u << range(0, 7));
                break;
            case 1: // Byte overwrite
                buf[idx] = static_cast<uint8_t>(range(0, 255));
                break;
            case 2: // Swap adjacent
                if (idx + 1 < buf.size()) {
                    std::swap(buf[idx], buf[idx + 1]);
                }
                break;
            case 3: // Zero byte
                buf[idx] = 0;
                break;
            }
        }
    }

private:
    uint64_t state_;
};

// 1. Adversarial Package & Corrupt Trailer Fuzzing
static void test_fuzz_package_and_trailer_parser() {
    std::cout << "[Stage 232.22] Testing deterministic package & signature trailer fuzzing (500 iterations)...\n";

    AppPackage valid_pkg;
    valid_pkg.manifest.bundle_id = "com.winios.fuzztest";
    valid_pkg.manifest.name = "FuzzTest";
    valid_pkg.manifest.version = "1.0.0";
    valid_pkg.manifest.executable_name = "bin/main";
    valid_pkg.add_file("bin/main", {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01});

    auto unsigned_bytes = valid_pkg.pack_v3();
    auto identity = DeveloperKeyStore::generate_identity("com.fuzz.dev", "Fuzz Labs", "KEY-FUZZ-01");
    auto signed_bytes = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_bytes, identity, 0, 1000, 2000));

    PublisherTrustStore store;
    store.register_key(DeveloperKeyStore::export_public_key(identity));

    DeterministicFuzzer fuzzer(0xA5A55A5AULL);

    for (int iter = 0; iter < 500; ++iter) {
        auto mutated = signed_bytes;
        size_t mutations = fuzzer.range(1, 10);
        fuzzer.mutate_buffer(mutated, mutations);

        // Neither validate_package_data nor verify_package_signature should ever crash or UB
        AppPackage out_pkg;
        auto val_res = AppPackage::validate_package_data(mutated.data(), mutated.size(), &out_pkg);
        (void)val_res;

        auto sig_res = store.verify_package_signature(mutated.data(), mutated.size(), TrustPolicy::Development, 1500);
        // If mutated was valid, it must match; otherwise corrupt/rejected
        if (sig_res.status == SignatureStatus::DeveloperSigned && sig_res.is_trusted) {
            TEST_ASSERT(sig_res.publisher_id == "com.fuzz.dev");
        }
    }

    std::cout << "  -> 500 package mutations processed safely with zero crashes.\n";
}

// 2. Canonical Path & Windows Namespace Attack Vectors
static void test_adversarial_paths_and_windows_namespaces() {
    std::cout << "[Stage 232.22] Testing adversarial path traversals & Windows namespace vectors...\n";

    const std::vector<std::string> malicious_paths = {
        // Directory traversals
        "../etc/passwd",
        "..\\..\\Windows\\System32\\cmd.exe",
        "bin/../../secret.txt",
        "bin/./../../secret.txt",
        "....//....//escape",
        "a/b/../../../c",
        // NT device namespaces
        "\\\\?\\C:\\Windows\\System32\\cmd.exe",
        "\\\\.\\Global\\GlobalEvent",
        "\\\\.\\PhysicalDrive0",
        "\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\",
        // Absolute paths and drive letters
        "/bin/sh",
        "\\bin\\sh",
        "C:/app/bin",
        "D:\\app\\bin",
        // Windows reserved DOS device names
        "CON",
        "PRN",
        "AUX",
        "NUL",
        "COM1", "COM9",
        "LPT1", "LPT9",
        "con.txt",
        "nul.tar.gz",
        "res/COM2/data",
        // Alternate Data Streams
        "icon.png:hidden_stream",
        "manifest.ini::$DATA",
        "bin/main:zone.identifier",
        // Short name 8.3 alias attempts (SEC-232-02)
        "res/PROGRA~1/bin",
        "res/APPDIR~1",
        "res/DOCUME~1.TXT",
        // Control characters
        "bad\x01name",
        "file\x1fname",
        // Trailing dots and spaces
        "folder. ",
        "file...",
        "space "
    };

    std::string canonical_out;
    for (const auto& path : malicious_paths) {
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(path, canonical_out);
        TEST_ASSERT(status != PathValidationStatus::Valid);
    }

    std::cout << "  -> All 32 adversarial path injection vectors rejected.\n";
}

// 3. Tampered Write-Ahead Installation Journal Fuzzing
static void test_tampered_installation_journals() {
    std::cout << "[Stage 232.22] Testing tampered write-ahead installation journal recovery...\n";

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/var");
    vfs->mkdir("/var/log");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    packaging::PackageInstaller installer(vfs, registry);

    // Create journal file with tampered checksum
    std::string journal_path = "/var/log/install_journal_tampered.log";
    std::string valid_journal_line = "STATE=5;TX=101;BUNDLE=com.evil.tamper;DIR=/Applications/com.evil.tamper;HASH=1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef\n";
    std::vector<uint8_t> journal_data(valid_journal_line.begin(), valid_journal_line.end());

    // Corrupt hash
    journal_data[journal_data.size() - 5] ^= 0x55;
    vfs->write_file(journal_path, journal_data.data(), journal_data.size());

    // Recovery must detect corruption and abort cleanly without panic
    auto rep = installer.reconcile_and_recover();
    (void)rep;

    // Verify no unverified app registered
    TEST_ASSERT(!registry->is_installed("com.evil.tamper"));

    std::cout << "  -> Tampered journal safely rejected during recovery.\n";
}

// 4. IPC Anti-Replay & Token Forgery Fuzzing
static void test_ipc_token_and_handle_forgery() {
    std::cout << "[Stage 232.22] Testing IPC token and handle forgery attack vectors...\n";

    auto id_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    kernel::IpcCapabilityBroker broker(id_mgr, cap_mgr);

    kernel::ApplicationIdentity app1{"com.fuzz.sender", "1.0.0", "Sender", 0x00020400};
    kernel::ApplicationIdentity app2{"com.fuzz.receiver", "1.0.0", "Receiver", 0x00020400};

    auto h1 = id_mgr->allocate_process(app1);
    auto h2 = id_mgr->allocate_process(app2);
    TEST_ASSERT(h1.is_valid() && h2.is_valid());

    auto token = broker.grant_communication_right(h1, h2, 0x1, true /* require_strict_sequence */);
    TEST_ASSERT(token.token_id != 0);

    const std::string payload = "FUZZ_PAYLOAD";

    // 1. Sequence number 0 rejection (SEC-232-04)
    TEST_ASSERT(broker.send_message(token.token_id, h1, payload.data(), payload.size(), 1, 0) == kernel::IpcStatus::ReplayDetected);

    // 2. Forged token ID
    uint64_t forged_token_id = 0xDEADBEEFCAFEULL;
    TEST_ASSERT(broker.send_message(forged_token_id, h1, payload.data(), payload.size(), 1, 1) == kernel::IpcStatus::PermissionDenied);

    // 3. Forged token hex string
    std::string forged_hex = "ffffffffffffffffffffffffffffffff";
    TEST_ASSERT(broker.send_message_by_hex(forged_hex, h1, payload.data(), payload.size(), 1, 1) == kernel::IpcStatus::PermissionDenied);

    // 4. Stale sender generation
    kernel::ProcessHandle stale_h1{h1.pid, h1.generation + 99};
    TEST_ASSERT(broker.send_message(token.token_id, stale_h1, payload.data(), payload.size(), 1, 1) == kernel::IpcStatus::SenderInvalid);

    // 5. Send with legitimate handle
    TEST_ASSERT(broker.send_message(token.token_id, h1, payload.data(), payload.size(), 1, 1) == kernel::IpcStatus::Success);

    // 6. Replay attack with same sequence number
    TEST_ASSERT(broker.send_message(token.token_id, h1, payload.data(), payload.size(), 1, 1) == kernel::IpcStatus::ReplayDetected);

    std::cout << "  -> Token forgery, sequence zero bypass, and handle tampering rejected.\n";
}

// 5. Concurrent Multi-Threaded Stress & Race Condition Testing
static void test_concurrent_adversarial_stress() {
    std::cout << "[Stage 232.22] Testing concurrent stress and race condition resilience...\n";

    auto id_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 300, 8);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto broker = std::make_shared<kernel::IpcCapabilityBroker>(id_mgr, cap_mgr);

    kernel::ApplicationIdentity app_a{"com.winios.stress.a", "1.0.0", "StressA", 0x00020400};
    kernel::ApplicationIdentity app_b{"com.winios.stress.b", "1.0.0", "StressB", 0x00020400};

    auto h_a = id_mgr->allocate_process(app_a);
    auto h_b = id_mgr->allocate_process(app_b);

    auto token = broker->grant_communication_right(h_a, h_b, 0x1, false);

    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> successes{0};
    std::atomic<uint64_t> collisions{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t]() {
            DeterministicFuzzer fuzzer(0xBEEF0000ULL + t);
            std::string msg = "THREAD_PAYLOAD_" + std::to_string(t);
            while (!stop_flag.load()) {
                uint64_t seq = fuzzer.range(1, 100);
                auto st = broker->send_message(token.token_id, h_a, msg.data(), msg.size(), 10, seq);
                if (st == kernel::IpcStatus::Success) {
                    successes.fetch_add(1);
                } else if (st == kernel::IpcStatus::ReplayDetected) {
                    collisions.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_flag.store(true);

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(successes.load() > 0);
    id_mgr->terminate_process(h_a);
    id_mgr->terminate_process(h_b);

    std::cout << "  -> Multi-threaded stress passed (" << successes.load() << " delivered, "
              << collisions.load() << " replay collisions safely handled).\n";
}

int main() {
    std::cout << "=== Stage 232.22: Adversarial Security & Fuzz Testing Suite ===\n";
    test_fuzz_package_and_trailer_parser();
    test_adversarial_paths_and_windows_namespaces();
    test_tampered_installation_journals();
    test_ipc_token_and_handle_forgery();
    test_concurrent_adversarial_stress();
    std::cout << "=== All Stage 232.22 Adversarial Tests PASSED ===\n";
    return 0;
}
