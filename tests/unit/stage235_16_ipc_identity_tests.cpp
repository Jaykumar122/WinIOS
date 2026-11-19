#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "security/capability_manager.h"
#include "runtime/guest_ipc.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

using namespace winios::kernel;
using namespace winios::security;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static void test_generational_process_allocation() {
    std::cout << "[Test 1] Generational process allocation & quarantine...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 2);

    ApplicationIdentity app{"com.winios.app1", "1.0.0", "App1"};
    ProcessHandle h1 = id_mgr->allocate_process(app);
    TEST_ASSERT(h1.is_valid(), "Process 1 allocated");
    TEST_ASSERT(h1.pid >= 100, "Valid PID");
    TEST_ASSERT(h1.generation == 1, "Initial generation is 1");

    // Valid handle check
    TEST_ASSERT(id_mgr->is_valid_handle(h1), "Handle active before termination");

    // Terminate process
    bool term_ok = id_mgr->terminate_process(h1);
    TEST_ASSERT(term_ok, "Process terminated");

    // Terminated handle check must be false
    TEST_ASSERT(!id_mgr->is_valid_handle(h1), "Terminated handle must be invalid");
    std::cout << "  Passed!\n";
}

static void test_ipc_token_grant_and_send() {
    std::cout << "[Test 2] IPC capability token grant & message delivery...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.sender", "1.0.0", "Sender"};
    ApplicationIdentity app2{"com.winios.receiver", "1.0.0", "Receiver"};

    ProcessHandle sender = id_mgr->allocate_process(app1);
    ProcessHandle receiver = id_mgr->allocate_process(app2);

    // Grant communication right
    IpcCapabilityToken token = broker.grant_communication_right(sender, receiver, 0x1, true);
    TEST_ASSERT(!token.token_hex.empty(), "Token issued");
    TEST_ASSERT(token.sender == sender, "Sender matches");
    TEST_ASSERT(token.receiver == receiver, "Receiver matches");

    // Send valid message
    const char msg[] = "HELLO";
    IpcStatus send_st = broker.send_message(token.token_id, sender, msg, sizeof(msg), 101, 1 /* seq */);
    TEST_ASSERT(send_st == IpcStatus::Success, "Message send succeeded");

    // Receive message
    IpcMessage recv_msg{};
    IpcStatus recv_st = broker.receive_message(receiver, recv_msg);
    TEST_ASSERT(recv_st == IpcStatus::Success, "Message receive succeeded");
    TEST_ASSERT(recv_msg.data_size == sizeof(msg), "Payload size matches");
    TEST_ASSERT(std::memcmp(recv_msg.data.data(), msg, sizeof(msg)) == 0, "Payload matches exactly");
    std::cout << "  Passed!\n";
}

static void test_anti_replay_enforcement() {
    std::cout << "[Test 3] Anti-replay sequence number enforcement...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ProcessHandle sender = id_mgr->allocate_process({"com.winios.s", "1.0.0", "S"});
    ProcessHandle receiver = id_mgr->allocate_process({"com.winios.r", "1.0.0", "R"});

    IpcCapabilityToken token = broker.grant_communication_right(sender, receiver, 0x1, true);

    const char msg[] = "ABC";
    // Message 1 with sequence 10
    TEST_ASSERT(broker.send_message(token.token_id, sender, msg, sizeof(msg), 1, 10) == IpcStatus::Success,
                "Seq 10 accepted");

    // Message 2 replaying sequence 10 must be rejected
    TEST_ASSERT(broker.send_message(token.token_id, sender, msg, sizeof(msg), 2, 10) == IpcStatus::ReplayDetected,
                "Replayed seq 10 rejected");

    // Message 3 with lower sequence 5 must be rejected
    TEST_ASSERT(broker.send_message(token.token_id, sender, msg, sizeof(msg), 3, 5) == IpcStatus::ReplayDetected,
                "Decreasing seq 5 rejected");

    // Message 4 with higher sequence 11 must succeed
    TEST_ASSERT(broker.send_message(token.token_id, sender, msg, sizeof(msg), 4, 11) == IpcStatus::Success,
                "Increasing seq 11 accepted");
    std::cout << "  Passed!\n";
}

static void test_stale_process_handle_rejection() {
    std::cout << "[Test 4] Stale process handle rejection after termination...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ProcessHandle sender = id_mgr->allocate_process({"com.winios.s", "1.0.0", "S"});
    ProcessHandle receiver = id_mgr->allocate_process({"com.winios.r", "1.0.0", "R"});

    IpcCapabilityToken token = broker.grant_communication_right(sender, receiver, 0x1, true);

    // Terminate sender
    id_mgr->terminate_process(sender);

    // Attempt to send using token with terminated sender
    const char msg[] = "TEST";
    IpcStatus st = broker.send_message(token.token_id, sender, msg, sizeof(msg), 1, 1);
    TEST_ASSERT(st == IpcStatus::SenderInvalid || st == IpcStatus::StaleProcessHandle,
                "Stale sender must be rejected");
    std::cout << "  Passed!\n";
}

int main() {
    std::cout << "=== Stage 235.16 IPC & Process Identity Security Tests ===" << std::endl;
    test_generational_process_allocation();
    test_ipc_token_grant_and_send();
    test_anti_replay_enforcement();
    test_stale_process_handle_rejection();
    std::cout << "=== All Stage 235.16 tests PASSED successfully ===" << std::endl;
    return 0;
}
