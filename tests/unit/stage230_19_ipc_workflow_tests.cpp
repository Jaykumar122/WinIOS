#include "kernel/ipc_capability_broker.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::kernel;

static void test_ipc_workflow_and_stale_rejection() {
    std::cout << "[Test 1] IpcCapabilityBroker communication & bounds...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 101, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    // Allocate 2 processes
    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);
    assert(h1.is_valid() && h2.is_valid());
    assert(h1.pid == 100);
    assert(h2.pid == 101);

    // 1. Grant capability token from h1 to h2
    auto token = broker.grant_communication_right(h1, h2);
    assert(token.token_id > 0);
    assert(!token.is_revoked);

    // 2. Send valid message
    std::string text = "Hello from App1 to App2!";
    auto st1 = broker.send_message(token.token_id, h1, text.data(), text.size(), 42);
    assert(st1 == IpcStatus::Success);
    (void)st1;

    // Receive message
    runtime::IpcMessage rcv{};
    auto rc1 = broker.receive_message(h2, rcv);
    assert(rc1 == IpcStatus::Success);
    (void)rc1;
    assert(rcv.sender_pid == h1.pid);
    assert(rcv.message_id == 42);
    assert(std::string(reinterpret_cast<const char*>(rcv.data.data()), rcv.data.size()) == text);

    // 3. Payload size check
    std::vector<uint8_t> huge_buf(5000, 0xAA);
    auto st_large = broker.send_message(token.token_id, h1, huge_buf.data(), huge_buf.size(), 99);
    assert(st_large == IpcStatus::PayloadTooLarge);
    (void)st_large;

    // 4. Bounded queue check (capacity 32)
    for (size_t i = 0; i < 32; ++i) {
        auto st = broker.send_message(token.token_id, h1, "ping", 4, static_cast<uint32_t>(i));
        assert(st == IpcStatus::Success);
        (void)st;
    }
    // 33rd message should fail with QueueFull
    auto st_full = broker.send_message(token.token_id, h1, "overflow", 8, 999);
    assert(st_full == IpcStatus::QueueFull);
    (void)st_full;

    // Drain the queue
    for (size_t i = 0; i < 32; ++i) {
        runtime::IpcMessage drain_msg{};
        auto dr = broker.receive_message(h2, drain_msg);
        assert(dr == IpcStatus::Success);
        (void)dr;
    }

    std::cout << "  -> Queue bounds passed.\n";

    // 5. Stale Generation Rejection Test
    std::cout << "[Test 2] Stale process handle / generation rollover rejection...\n";
    // Terminate h2
    id_mgr->terminate_process(h2);

    // Old token should now be rejected because h2 is invalid
    auto st_dead = broker.send_message(token.token_id, h1, "dead test", 9, 100);
    assert(st_dead == IpcStatus::StaleProcessHandle);
    (void)st_dead;

    // Now re-allocate PID 101 with a new app (generation will increment!)
    ApplicationIdentity app3{"com.winios.app3", "App3", "1.0"};
    ProcessHandle h2_new = id_mgr->allocate_process(app3);
    assert(h2_new.is_valid());
    assert(h2_new.pid == h2.pid);
    assert(h2_new.generation > h2.generation); // Generation incremented!

    // Sending to old token must still be rejected with StaleProcessHandle!
    auto st_stale = broker.send_message(token.token_id, h1, "stale attack", 12, 101);
    assert(st_stale == IpcStatus::StaleProcessHandle);
    (void)st_stale;

    // 6. Token Revocation Test
    std::cout << "[Test 3] Token revocation...\n";
    auto token2 = broker.grant_communication_right(h1, h2_new);
    assert(token2.token_id > 0);

    broker.revoke_token(token2.token_id);
    auto st_rev = broker.send_message(token2.token_id, h1, "revoked", 7, 102);
    assert(st_rev == IpcStatus::PermissionDenied);
    (void)st_rev;

    std::cout << "  -> Stale generation rejection & revocation passed.\n";
}

int main() {
    std::cout << "=== Stage 230.19: Inter-Application Communication (IPC) Workflow Tests ===\n";
    test_ipc_workflow_and_stale_rejection();
    std::cout << "=== All Stage 230.19 Tests PASSED ===\n";
    return 0;
}
