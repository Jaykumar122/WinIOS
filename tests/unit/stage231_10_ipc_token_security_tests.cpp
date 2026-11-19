#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <set>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::kernel;

// 1. Test 128-bit CSPRNG token generation and uniqueness
static void test_csprng_token_generation() {
    std::cout << "[Test 1] 128-bit CSPRNG token generation and uniqueness...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);
    TEST_ASSERT(h1.is_valid() && h2.is_valid());

    std::set<std::string> hex_tokens;
    std::set<uint64_t> id_tokens;

    for (int i = 0; i < 50; ++i) {
        auto tok = broker.grant_communication_right(h1, h2, 0x1);
        TEST_ASSERT(tok.token_id != 0);
        TEST_ASSERT(tok.token_hex.length() == 32); // 16 bytes = 32 hex chars
        for (char c : tok.token_hex) {
            TEST_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        TEST_ASSERT(hex_tokens.insert(tok.token_hex).second); // Must be globally unique
        TEST_ASSERT(id_tokens.insert(tok.token_id).second);
    }
    std::cout << "  -> 50 CSPRNG 128-bit tokens verified unique.\n";
}

// 2. Test send and receive by token hex and secure sender verification
static void test_send_by_hex_and_secure_token() {
    std::cout << "[Test 2] Send by hex and secure token verification...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    auto tok = broker.grant_communication_right(h1, h2, 0x1);
    TEST_ASSERT(tok.token_id != 0);

    std::string token1 = id_mgr->get_process_token(h1);
    TEST_ASSERT(!token1.empty());

    // Send using send_message_secure with valid token
    std::string msg_data = "Secure message payload";
    auto st = broker.send_message_secure(tok.token_hex, h1, token1, msg_data.data(), msg_data.size(), 777);
    TEST_ASSERT(st == IpcStatus::Success);

    // Receive message
    runtime::IpcMessage rcv{};
    TEST_ASSERT(broker.receive_message(h2, rcv) == IpcStatus::Success);
    TEST_ASSERT(rcv.sender_pid == h1.pid);
    TEST_ASSERT(rcv.message_id == 777);
    TEST_ASSERT(std::string(rcv.data.begin(), rcv.data.end()) == msg_data);

    // Send with invalid process security token
    auto st_bad_proc = broker.send_message_secure(tok.token_hex, h1, "bad_process_token_123456789012",
                                                  msg_data.data(), msg_data.size(), 778);
    TEST_ASSERT(st_bad_proc == IpcStatus::SenderInvalid);

    // Send with non-existent capability hex
    auto st_bad_cap = broker.send_message_secure("00001111222233334444555566667777", h1, token1,
                                                 msg_data.data(), msg_data.size(), 779);
    TEST_ASSERT(st_bad_cap == IpcStatus::PermissionDenied);

    std::cout << "  -> Secure token verification passed.\n";
}

// 3. Test anti-replay protection with sequence numbers
static void test_anti_replay_protection() {
    std::cout << "[Test 3] Anti-replay sequence protection...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    auto tok = broker.grant_communication_right(h1, h2, 0x1);

    // Send with seq = 10 -> OK
    TEST_ASSERT(broker.send_message(tok.token_id, h1, "seq10", 5, 1, 10) == IpcStatus::Success);

    // Replay attack: send again with seq = 10 -> ReplayDetected!
    TEST_ASSERT(broker.send_message(tok.token_id, h1, "replay10", 8, 2, 10) == IpcStatus::ReplayDetected);

    // Stale sequence: send with seq = 5 -> ReplayDetected!
    TEST_ASSERT(broker.send_message(tok.token_id, h1, "stale5", 6, 3, 5) == IpcStatus::ReplayDetected);

    // Monotonic increase: send with seq = 15 -> OK
    TEST_ASSERT(broker.send_message(tok.token_id, h1, "seq15", 5, 4, 15) == IpcStatus::Success);

    std::cout << "  -> Anti-replay enforcement passed.\n";
}

// 4. Test handle generational binding and auto-revocation on stale receiver
static void test_generation_binding_and_auto_revocation() {
    std::cout << "[Test 4] Handle generational binding and auto-revocation...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 101, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    auto tok = broker.grant_communication_right(h1, h2, 0x1);
    TEST_ASSERT(tok.token_id != 0);

    // Terminate receiver h2
    id_mgr->terminate_process(h2);

    // Attempt send to dead receiver -> StaleProcessHandle and auto-revoke
    auto st_dead = broker.send_message(tok.token_id, h1, "ping", 4, 1);
    TEST_ASSERT(st_dead == IpcStatus::StaleProcessHandle);

    // Re-allocate slot 101 with new app3 (new generation)
    ApplicationIdentity app3{"com.winios.app3", "App3", "1.0"};
    ProcessHandle h2_new = id_mgr->allocate_process(app3);
    TEST_ASSERT(h2_new.pid == h2.pid);
    TEST_ASSERT(h2_new.generation > h2.generation);

    // Attempt send using old token -> Token was auto-revoked, so PermissionDenied!
    auto st_after_realloc = broker.send_message(tok.token_id, h1, "attack", 6, 2);
    TEST_ASSERT(st_after_realloc == IpcStatus::PermissionDenied);

    // Test sender generation mismatch
    ProcessHandle fake_sender = h1;
    fake_sender.generation += 1;
    auto tok2 = broker.grant_communication_right(h1, h2_new, 0x1);
    auto st_bad_sender = broker.send_message(tok2.token_id, fake_sender, "hi", 2, 3);
    TEST_ASSERT(st_bad_sender == IpcStatus::SenderInvalid);

    std::cout << "  -> Generational binding and auto-revocation passed.\n";
}

// 5. Test cleanup on process termination
static void test_process_cleanup() {
    std::cout << "[Test 5] Process cleanup and token revocation...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    auto tok = broker.grant_communication_right(h1, h2, 0x1);

    // Enqueue 2 messages
    broker.send_message(tok.token_id, h1, "msg1", 4, 1);
    broker.send_message(tok.token_id, h1, "msg2", 4, 2);
    TEST_ASSERT(broker.queue_depth(h2.pid) == 2);

    // Cleanup h2
    broker.cleanup_process(h2);
    TEST_ASSERT(broker.queue_depth(h2.pid) == 0);

    // Token should now be revoked
    auto tok_lookup = broker.get_token_by_id(tok.token_id);
    TEST_ASSERT(tok_lookup.has_value());
    TEST_ASSERT(tok_lookup->is_revoked);

    TEST_ASSERT(broker.send_message(tok.token_id, h1, "msg3", 4, 3) == IpcStatus::PermissionDenied);

    std::cout << "  -> Process cleanup verified.\n";
}

int main() {
    std::cout << "=== Stage 231.10: IPC Capability Token Security Tests ===\n";
    test_csprng_token_generation();
    test_send_by_hex_and_secure_token();
    test_anti_replay_protection();
    test_generation_binding_and_auto_revocation();
    test_process_cleanup();
    std::cout << "=== All Stage 231.10 Tests PASSED ===\n";
    return 0;
}
