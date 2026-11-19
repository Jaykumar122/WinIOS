#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"

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

using namespace winios::kernel;
using namespace winios::security;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static ApplicationIdentity make_ident(const std::string& bundle_id) {
    ApplicationIdentity app{};
    app.bundle_id = bundle_id;
    app.display_name = bundle_id;
    return app;
}

// -----------------------------------------------------------------------------
// Test 1: Concurrent Duplicate Sequence Number Injection
// -----------------------------------------------------------------------------
static void test_concurrent_duplicate_sequence_injection() {
    std::cout << "[Stage 234.17] Test 1: Testing Concurrent Duplicate Sequence Numbers...\n";

    auto ident_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(ident_mgr, cap_mgr);

    auto h_sender = ident_mgr->allocate_process(make_ident("sender_proc"));
    auto h_receiver = ident_mgr->allocate_process(make_ident("receiver_proc"));

    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true /* strict sequence */);
    TEST_ASSERT(token.token_id != 0, "Token granted");

    // Launch 4 concurrent threads all attempting to submit the same sequence number (e.g. seq = 42)
    const uint64_t duplicate_seq = 42;
    std::atomic<int> success_count{0};
    std::atomic<int> replay_count{0};

    auto worker = [&]() {
        const char msg[] = "CONCURRENT_DUPLICATE_PAYLOAD";
        auto status = broker.send_message(token.token_id, h_sender, msg, sizeof(msg), 1, duplicate_seq);
        if (status == IpcStatus::Success) {
            success_count++;
        } else if (status == IpcStatus::ReplayDetected) {
            replay_count++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Strictly 1 thread must succeed; remaining 3 must detect replay
    TEST_ASSERT(success_count == 1, "Exactly one thread succeeded with sequence 42");
    TEST_ASSERT(replay_count == 3, "Exactly 3 threads caught as ReplayDetected");

    // Any attempt to submit sequence < 42 or == 42 must now fail
    const char msg[] = "STALE_PAYLOAD";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, msg, sizeof(msg), 1, 41) == IpcStatus::ReplayDetected,
                "Lower sequence rejected");
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, msg, sizeof(msg), 1, 42) == IpcStatus::ReplayDetected,
                "Equal sequence rejected");

    // Higher sequence (43) succeeds
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, msg, sizeof(msg), 1, 43) == IpcStatus::Success,
                "Higher sequence accepted");

    std::cout << "  -> Concurrent duplicate sequence defense verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Process Termination and Stale Generation Protection
// -----------------------------------------------------------------------------
static void test_process_restart_stale_generation() {
    std::cout << "[Stage 234.17] Test 2: Testing Process Restart & Stale Generation Protection...\n";

    auto ident_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(ident_mgr, cap_mgr);

    auto h_sender = ident_mgr->allocate_process(make_ident("sender_app"));
    auto h_receiver = ident_mgr->allocate_process(make_ident("receiver_app"));

    uint32_t receiver_pid = h_receiver.pid;
    uint32_t orig_receiver_gen = h_receiver.generation;

    // Grant communication capability from sender to receiver
    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, false);
    TEST_ASSERT(token.token_id != 0, "Token granted");

    // Message succeeds initially
    const char payload[] = "INITIAL_MSG";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload, sizeof(payload)) == IpcStatus::Success,
                "Message delivered to original receiver");

    // Terminate receiver process
    bool term_ok = ident_mgr->terminate_process(h_receiver);
    TEST_ASSERT(term_ok, "Receiver process terminated");

    // Reallocate in the same slot (simulating PID recycling)
    ident_mgr->set_slot_generation(receiver_pid, orig_receiver_gen); // Reset slot generation state
    auto h_new_receiver = ident_mgr->allocate_process(make_ident("new_recycled_app"));
    // Ensure it was assigned the recycled slot with a higher generation
    if (h_new_receiver.pid != receiver_pid) {
        // Force slot generation increment test
        ident_mgr->terminate_process(h_new_receiver);
    }

    // Now, sending via the old token MUST fail with StaleProcessHandle because receiver gen is dead
    const char recycled_payload[] = "SPOOF_ATTEMPT";
    auto stale_status = broker.send_message(token.token_id, h_sender, recycled_payload, sizeof(recycled_payload));
    TEST_ASSERT(stale_status == IpcStatus::StaleProcessHandle, "Stale receiver handle rejected");

    std::cout << "  -> Stale generation protection verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Generational Wraparound Protection (Slot Retirement)
// -----------------------------------------------------------------------------
static void test_generation_exhaustion_retirement() {
    std::cout << "[Stage 234.17] Test 3: Testing Generation Exhaustion Slot Retirement...\n";

    auto ident_mgr = std::make_shared<ProcessIdentityManager>();

    auto h = ident_mgr->allocate_process(make_ident("test_exhaustion"));
    uint32_t pid = h.pid;
    ident_mgr->terminate_process(h);

    // Set slot generation to kMaxGeneration (exhausted)
    ident_mgr->set_slot_generation(pid, ProcessIdentityManager::kMaxGeneration);

    // Allocating on an exhausted generation slot must NOT wrap around to 1
    // The handle must return pid=0, generation=0 (permanently retired)
    ProcessHandle exhausted_handle = ident_mgr->allocate_process(make_ident("after_exhaustion"));
    if (exhausted_handle.pid == pid) {
        TEST_ASSERT(exhausted_handle.generation == 0, "Exhausted generation did NOT wrap around");
    } else {
        // Allocator skipped the exhausted slot
        TEST_ASSERT(ident_mgr->get_active_generation(pid) == ProcessIdentityManager::kMaxGeneration,
                    "Exhausted slot remained retired");
    }

    std::cout << "  -> Generation exhaustion retirement verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Concurrent Token Revocation
// -----------------------------------------------------------------------------
static void test_concurrent_token_revocation() {
    std::cout << "[Stage 234.17] Test 4: Testing Concurrent Token Revocation...\n";

    auto ident_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(ident_mgr, cap_mgr);

    auto h_sender = ident_mgr->allocate_process(make_ident("sender_c"));
    auto h_receiver = ident_mgr->allocate_process(make_ident("receiver_c"));
    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, false);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<int> denied_count{0};
    std::atomic<int> success_count{0};

    auto sender_worker = [&]() {
        while (!start_flag.load()) { /* spin */ }
        const char msg[] = "PAYLOAD";
        while (!stop_flag.load()) {
            auto s = broker.send_message(token.token_id, h_sender, msg, sizeof(msg));
            if (s == IpcStatus::Success) {
                success_count++;
            } else if (s == IpcStatus::PermissionDenied) {
                denied_count++;
            }
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> senders;
    for (int i = 0; i < 3; ++i) {
        senders.emplace_back(sender_worker);
    }

    std::thread revoker([&]() {
        while (!start_flag.load()) { /* spin */ }
        while (success_count.load() < 10) {
            std::this_thread::yield();
        }
        broker.revoke_token(token.token_id);
        while (denied_count.load() < 5) {
            std::this_thread::yield();
        }
        stop_flag.store(true);
    });

    start_flag.store(true);

    for (auto& s : senders) s.join();
    revoker.join();

    // After revoker joined, token must be revoked and all future messages denied
    const char final_msg[] = "POST_REVOCATION";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, final_msg, sizeof(final_msg)) == IpcStatus::PermissionDenied,
                "Token permanently denied after revocation");
    TEST_ASSERT(denied_count >= 5, "Denied messages recorded during revocation race");

    std::cout << "  -> Concurrent token revocation verified.\n";
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
    std::cout << "  Stage 234.17: IPC Capability & Anti-Replay Concurrency Tests   \n";
    std::cout << "=================================================================\n";

    test_concurrent_duplicate_sequence_injection();
    test_process_restart_stale_generation();
    test_generation_exhaustion_retirement();
    test_concurrent_token_revocation();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL IPC CAPABILITY & ANTI-REPLAY CONCURRENCY TESTS PASSED!     \n";
    std::cout << "=================================================================\n";
    return 0;
}
