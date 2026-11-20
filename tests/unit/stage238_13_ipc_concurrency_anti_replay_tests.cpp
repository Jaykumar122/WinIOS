// ============================================================================
// Stage 238.13: IPC Concurrency and Anti-Replay Validation Suite
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

#include "tests/support/test_assert.h"
#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "security/capability_manager.h"

using namespace winios;
using namespace winios::kernel;

namespace {

void test_authoritative_validation_sequence() {
    std::cout << "[Test 1] Authoritative IPC Capability Validation Sequence...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(200, 201, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity appA{"com.winios.appA", "AppA", "1.0"};
    ApplicationIdentity appB{"com.winios.appB", "AppB", "1.0"};

    ProcessHandle pA = id_mgr->allocate_process(appA);
    ProcessHandle pB = id_mgr->allocate_process(appB);
    TEST_ASSERT(pA.is_valid() && pB.is_valid());

    auto token = broker.grant_communication_right(pA, pB, 0x1, true);
    TEST_ASSERT(token.token_id > 0);

    // 1. Invalid sender handle (wrong generation or pid)
    ProcessHandle fake_sender{pA.pid, pA.generation + 1};
    auto st_bad_sender = broker.send_message(token.token_id, fake_sender, "test", 4, 1, 1);
    TEST_ASSERT(st_bad_sender == IpcStatus::SenderInvalid, "Invalid sender must return SenderInvalid");

    // 2. Successful legitimate transmission with sequence #1
    auto st_ok = broker.send_message(token.token_id, pA, "msg 1", 5, 1, 1);
    TEST_ASSERT(st_ok == IpcStatus::Success, "Legitimate send must succeed");

    // 3. Replay attack: resending sequence #1
    auto st_replay = broker.send_message(token.token_id, pA, "replay msg", 10, 2, 1);
    TEST_ASSERT(st_replay == IpcStatus::ReplayDetected, "Replayed sequence number must return ReplayDetected");

    // 4. Stale recipient generation detection
    id_mgr->terminate_process(pB);
    // Send to terminated recipient -> StaleProcessHandle
    auto st_dead = broker.send_message(token.token_id, pA, "to dead", 7, 3, 2);
    TEST_ASSERT(st_dead == IpcStatus::StaleProcessHandle, "Terminated recipient must return StaleProcessHandle");

    // Slot recycling: Allocate new app in same PID
    ApplicationIdentity appB2{"com.winios.appB2", "AppB2", "1.0"};
    ProcessHandle pB2 = id_mgr->allocate_process(appB2);
    TEST_ASSERT(pB2.pid == pB.pid);
    TEST_ASSERT(pB2.generation > pB.generation);

    // Old token has stale generation -> must still return StaleProcessHandle!
    auto st_recycled = broker.send_message(token.token_id, pA, "to recycled", 11, 4, 3);
    TEST_ASSERT(st_recycled == IpcStatus::StaleProcessHandle,
                "Old capability to recycled PID must return StaleProcessHandle");

    // 5. Explicitly revoked token on alive recipient -> PermissionDenied
    auto token2 = broker.grant_communication_right(pA, pB2, 0x1, false);
    broker.revoke_token(token2.token_id);
    auto st_rev = broker.send_message(token2.token_id, pA, "revoked test", 12, 5, 0);
    TEST_ASSERT(st_rev == IpcStatus::PermissionDenied, "Explicitly revoked token must return PermissionDenied");

    std::cout << "  -> Passed authoritative validation sequence\n";
}

void test_concurrent_ipc_racing() {
    std::cout << "[Test 2] Concurrent IPC Message Dispatch and Anti-Replay...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(300, 301, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity appX{"com.winios.appX", "AppX", "1.0"};
    ApplicationIdentity appY{"com.winios.appY", "AppY", "1.0"};

    ProcessHandle pX = id_mgr->allocate_process(appX);
    ProcessHandle pY = id_mgr->allocate_process(appY);

    auto token = broker.grant_communication_right(pX, pY, 0x1, true);

    std::atomic<int> success_count{0};
    std::atomic<int> replay_count{0};
    constexpr int kThreads = 8;
    std::vector<std::thread> workers;

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < 50; ++i) {
                uint64_t seq = static_cast<uint64_t>(t * 100 + i + 1);
                auto st = broker.send_message(token.token_id, pX, "concurrent data", 15, 10, seq);
                if (st == IpcStatus::Success) {
                    ++success_count;
                } else if (st == IpcStatus::ReplayDetected) {
                    ++replay_count;
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(success_count > 0, "At least some strictly ordered messages must succeed");
    std::cout << "  -> Concurrency test completed: " << success_count << " succeeded, " << replay_count << " replayed/out-of-order\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.13: IPC Concurrency & Anti-Replay Suite     \n";
    std::cout << "========================================================\n";

    test_authoritative_validation_sequence();
    test_concurrent_ipc_racing();

    std::cout << "\nALL STAGE 238.13 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
