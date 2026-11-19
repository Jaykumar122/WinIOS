#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "IPC Anti-Replay Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;

static void test_strict_monotonic_sequence_enforcement() {
    std::cout << "[Stage 233.16] Testing strict monotonic sequence enforcement and anti-replay...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.sender", "Sender", "1.0"};
    ApplicationIdentity app2{"com.winios.receiver", "Receiver", "1.0"};

    ProcessHandle h_sender = id_mgr->allocate_process(app1);
    ProcessHandle h_receiver = id_mgr->allocate_process(app2);
    TEST_ASSERT(h_sender.is_valid() && h_receiver.is_valid());

    // Grant token with strict sequence enforcement
    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true /* require_strict_sequence */);
    TEST_ASSERT(token.token_id != 0);
    TEST_ASSERT(token.require_strict_sequence);

    const std::string payload = "PAYLOAD_SEQ";

    // 1. Sequence 0 must be rejected
    auto status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 0);
    TEST_ASSERT(status == IpcStatus::ReplayDetected);

    // 2. Sequence 1 must succeed
    status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 1);
    TEST_ASSERT(status == IpcStatus::Success);

    // 3. Exact replay of sequence 1 must be rejected
    status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 1);
    TEST_ASSERT(status == IpcStatus::ReplayDetected);

    // 4. Stale/regressive sequence (e.g. sequence 0) must be rejected
    status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 0);
    TEST_ASSERT(status == IpcStatus::ReplayDetected);

    // 5. Strictly increasing sequence jumps (2 -> 5 -> 100) must succeed
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 2, 2) == IpcStatus::Success);
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 3, 5) == IpcStatus::Success);
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 4, 100) == IpcStatus::Success);

    // 6. Any sequence <= 100 must now be rejected
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 5, 100) == IpcStatus::ReplayDetected);
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 6, 99) == IpcStatus::ReplayDetected);
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 7, 50) == IpcStatus::ReplayDetected);
}

static void test_post_validation_sequence_advancement() {
    std::cout << "[Stage 233.16] Testing post-validation sequence advancement...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.s1", "S1", "1.0"};
    ApplicationIdentity app2{"com.winios.r1", "R1", "1.0"};

    ProcessHandle h_sender = id_mgr->allocate_process(app1);
    ProcessHandle h_receiver = id_mgr->allocate_process(app2);

    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true);

    // Send sequence 1
    std::string payload = "VALID_MSG";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 1) == IpcStatus::Success);

    // Attempt to send sequence 2 with payload > 4096 bytes (PayloadTooLarge)
    std::vector<std::uint8_t> large_payload(IpcCapabilityBroker::kMaxMessageBytes + 10, 0xEE);
    auto status = broker.send_message(token.token_id, h_sender, large_payload.data(), large_payload.size(), 2, 2);
    TEST_ASSERT(status == IpcStatus::PayloadTooLarge);

    // Verify token sequence was NOT advanced to 2
    auto tok_info = broker.get_token_by_id(token.token_id);
    TEST_ASSERT(tok_info.has_value());
    TEST_ASSERT(tok_info->last_sequence_num == 1);

    // Valid message with sequence 2 must now succeed!
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 2, 2) == IpcStatus::Success);
    tok_info = broker.get_token_by_id(token.token_id);
    TEST_ASSERT(tok_info->last_sequence_num == 2);

    // Now test queue capacity boundary: fill up to 32 messages
    // Messages 1 and 2 are currently in queue. Add 30 more messages to reach 32.
    for (uint64_t seq = 3; seq <= 32; ++seq) {
        TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), static_cast<uint32_t>(seq), seq) == IpcStatus::Success);
    }
    TEST_ASSERT(broker.queue_depth(h_receiver.pid) == IpcCapabilityBroker::kMaxPortCapacity);

    // 33rd message should fail with QueueFull
    status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 33, 33);
    TEST_ASSERT(status == IpcStatus::QueueFull);

    // Verify sequence 33 was NOT consumed
    tok_info = broker.get_token_by_id(token.token_id);
    TEST_ASSERT(tok_info->last_sequence_num == 32);

    // Receive one message from receiver
    runtime::IpcMessage rcv{};
    TEST_ASSERT(broker.receive_message(h_receiver, rcv) == IpcStatus::Success);
    TEST_ASSERT(broker.queue_depth(h_receiver.pid) == IpcCapabilityBroker::kMaxPortCapacity - 1);

    // Now send sequence 33 -> must succeed!
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 33, 33) == IpcStatus::Success);
    tok_info = broker.get_token_by_id(token.token_id);
    TEST_ASSERT(tok_info->last_sequence_num == 33);
}

static void test_generational_stale_handle_defense() {
    std::cout << "[Stage 233.16] Testing generational handle validation and stale process defense...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.s2", "S2", "1.0"};
    ApplicationIdentity app2{"com.winios.r2", "R2", "1.0"};

    ProcessHandle h_sender = id_mgr->allocate_process(app1);
    ProcessHandle h_receiver = id_mgr->allocate_process(app2);

    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true);

    std::string payload = "DATA";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 1) == IpcStatus::Success);

    // Receiver process terminates
    id_mgr->terminate_process(h_receiver);

    // Sender attempts to send to terminated receiver -> StaleProcessHandle
    auto status = broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 2, 2);
    TEST_ASSERT(status == IpcStatus::StaleProcessHandle);

    // Sender terminates as well
    id_mgr->terminate_process(h_sender);

    // New process spawns with reused PID but bumped generation
    ProcessHandle h_new_sender = id_mgr->allocate_process(app1);
    // Attempting to send using old token with new process handle -> SenderInvalid
    status = broker.send_message(token.token_id, h_new_sender, payload.data(), payload.size(), 3, 3);
    TEST_ASSERT(status == IpcStatus::SenderInvalid);
}

static void test_token_revocation_and_process_cleanup() {
    std::cout << "[Stage 233.16] Testing token revocation and lifecycle cleanup...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.s3", "S3", "1.0"};
    ApplicationIdentity app2{"com.winios.r3", "R3", "1.0"};

    ProcessHandle h_sender = id_mgr->allocate_process(app1);
    ProcessHandle h_receiver = id_mgr->allocate_process(app2);

    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true);

    std::string payload = "PING";
    TEST_ASSERT(broker.send_message(token.token_id, h_sender, payload.data(), payload.size(), 1, 1) == IpcStatus::Success);

    // Explicit revocation by token hex
    broker.revoke_token_by_hex(token.token_hex);
    auto status = broker.send_message_by_hex(token.token_hex, h_sender, payload.data(), payload.size(), 2, 2);
    TEST_ASSERT(status == IpcStatus::PermissionDenied);

    // Re-grant and test cleanup_process
    auto token2 = broker.grant_communication_right(h_sender, h_receiver, 0x1, true);
    TEST_ASSERT(broker.send_message(token2.token_id, h_sender, payload.data(), payload.size(), 1, 1) == IpcStatus::Success);
    TEST_ASSERT(broker.queue_depth(h_receiver.pid) == 2); // 1 ping from earlier + 1 ping from token2

    // Cleanup process h_receiver
    broker.cleanup_process(h_receiver);
    // Queue is purged
    TEST_ASSERT(broker.queue_depth(h_receiver.pid) == 0);
    // Token2 was revoked
    status = broker.send_message(token2.token_id, h_sender, payload.data(), payload.size(), 2, 2);
    TEST_ASSERT(status == IpcStatus::PermissionDenied);
}

static void test_cross_process_spoofing_defense() {
    std::cout << "[Stage 233.16] Testing cross-process spoofing defense...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.winios.s4", "S4", "1.0"};
    ApplicationIdentity app2{"com.winios.r4", "R4", "1.0"};
    ApplicationIdentity app_attacker{"com.winios.bad", "Attacker", "1.0"};

    ProcessHandle h_sender = id_mgr->allocate_process(app1);
    ProcessHandle h_receiver = id_mgr->allocate_process(app2);
    ProcessHandle h_attacker = id_mgr->allocate_process(app_attacker);

    auto token = broker.grant_communication_right(h_sender, h_receiver, 0x1, true);

    std::string payload = "SPOOF_ATTEMPT";
    // Attacker tries to send using legitimate token
    auto status = broker.send_message(token.token_id, h_attacker, payload.data(), payload.size(), 1, 1);
    TEST_ASSERT(status == IpcStatus::SenderInvalid);

    // Attacker tries to steal messages from receiver queue
    runtime::IpcMessage stolen_msg{};
    status = broker.receive_message(h_attacker, stolen_msg);
    TEST_ASSERT(status == IpcStatus::PortNotFound);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.16: IPC Capability & Anti-Replay Tests  \n";
    std::cout << "====================================================\n";

    test_strict_monotonic_sequence_enforcement();
    test_post_validation_sequence_advancement();
    test_generational_stale_handle_defense();
    test_token_revocation_and_process_cleanup();
    test_cross_process_spoofing_defense();

    std::cout << "\n[Stage 233.16] ALL IPC ANTI-REPLAY TESTS PASSED.\n";
    return 0;
}
