#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>

#include "kernel/process_identity.h"
#include "kernel/ipc_capability_broker.h"
#include "security/capability_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::security;

static void test_anti_replay_strict_sequence_zero_rejected() {
    std::cout << "[Stage 232.16] Testing strict anti-replay rejection on sequence_num == 0 (SEC-232-04)...\n";

    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.test.sender", "1.0.0", "Sender", 0x02030000};
    ApplicationIdentity app2{"com.test.receiver", "1.0.0", "Receiver", 0x02030000};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);
    TEST_ASSERT(h1.is_valid() && h2.is_valid());

    // Issue token with require_strict_sequence = true
    auto token = broker.grant_communication_right(h1, h2, 0x1 /* Send right */, true /* require_strict_sequence */);
    TEST_ASSERT(token.token_id != 0);

    const std::string payload = "SECURE_IPC_PAYLOAD";

    // 1. Initial send with sequence_num = 0 MUST be strictly rejected with ReplayDetected
    auto st0 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 100, 0);
    TEST_ASSERT(st0 == IpcStatus::ReplayDetected);

    // 2. Initial send with sequence_num = 1 MUST succeed
    auto st1 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 100, 1);
    TEST_ASSERT(st1 == IpcStatus::Success);

    // 3. Replay with sequence_num = 1 MUST be rejected with ReplayDetected
    auto st1_replay = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 100, 1);
    TEST_ASSERT(st1_replay == IpcStatus::ReplayDetected);

    // 4. Sequence_num = 0 attempt after sequence has advanced MUST be rejected
    auto st0_late = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 100, 0);
    TEST_ASSERT(st0_late == IpcStatus::ReplayDetected);

    // 5. Sequence_num = 2 MUST succeed
    auto st2 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 100, 2);
    TEST_ASSERT(st2 == IpcStatus::Success);
}

static void test_anti_replay_non_strict_zero_bypass_mitigation() {
    std::cout << "[Stage 232.16] Testing non-strict token zero-sequence bypass mitigation...\n";

    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.test.sender", "1.0.0", "Sender", 0x02030000};
    ApplicationIdentity app2{"com.test.receiver", "1.0.0", "Receiver", 0x02030000};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    // Standard token (require_strict_sequence = false)
    auto token = broker.grant_communication_right(h1, h2, 0x1, false);

    const std::string payload = "DATA";

    // Advance sequence number to 42
    auto st42 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 1, 42);
    TEST_ASSERT(st42 == IpcStatus::Success);

    // Attacker tries to bypass anti-replay check by sending sequence_num = 0 (SEC-232-04 bug)
    // Must be rejected as ReplayDetected!
    auto st0_bypass = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 2, 0);
    TEST_ASSERT(st0_bypass == IpcStatus::ReplayDetected);

    // Stale sequence number (20 < 42) must also be rejected
    auto st20 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 3, 20);
    TEST_ASSERT(st20 == IpcStatus::ReplayDetected);

    // Monotonic advance (43 > 42) must succeed
    auto st43 = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 4, 43);
    TEST_ASSERT(st43 == IpcStatus::Success);
}

static void test_generational_process_handle_isolation() {
    std::cout << "[Stage 232.16] Testing generational process handle stale rejection...\n";

    auto id_mgr = std::make_shared<ProcessIdentityManager>();
    auto cap_mgr = std::make_shared<CapabilityManager>();
    IpcCapabilityBroker broker(id_mgr, cap_mgr);

    ApplicationIdentity app1{"com.test.victim", "1.0.0", "Victim", 0x02030000};
    ApplicationIdentity app2{"com.test.receiver", "1.0.0", "Receiver", 0x02030000};

    // Allocate process with fixed PID 200
    ProcessHandle h1 = id_mgr->allocate_fixed_pid(200, app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);
    TEST_ASSERT(h1.pid == 200 && h1.generation == 1);

    auto token = broker.grant_communication_right(h1, h2, 0x1);
    TEST_ASSERT(token.token_id != 0);

    // Terminate victim process 200
    TEST_ASSERT(id_mgr->terminate_process(h1));
    broker.cleanup_process(h1);

    // Reallocate slot 200 for a new process (generation becomes 2)
    ApplicationIdentity app_attacker{"com.test.attacker", "1.0.0", "Attacker", 0x02030000};
    ProcessHandle h1_new = id_mgr->allocate_fixed_pid(200, app_attacker);
    TEST_ASSERT(h1_new.pid == 200 && h1_new.generation == 2);

    // Attacker attempts to use victim's previous token with new handle (gen 2 vs token sender gen 1)
    const std::string payload = "EXPLOIT_PAYLOAD";
    auto st_attack = broker.send_message(token.token_id, h1_new, payload.data(), payload.size(), 1, 1);
    TEST_ASSERT(st_attack == IpcStatus::SenderInvalid || st_attack == IpcStatus::PermissionDenied);

    // Stale handle with dead generation 1 attempts to use the token
    auto st_stale = broker.send_message(token.token_id, h1, payload.data(), payload.size(), 1, 1);
    TEST_ASSERT(st_stale == IpcStatus::SenderInvalid || st_stale == IpcStatus::PermissionDenied);
}

static void test_capability_propagation_least_privilege() {
    std::cout << "[Stage 232.16] Testing capability least-privilege propagation and revocation...\n";

    CapabilityManager cap_mgr;
    const uint32_t parent_pid = 500;
    const uint32_t child_pid = 501;

    // Grant elevated capabilities to parent
    cap_mgr.grant(parent_pid, Capability::TaskManage);
    cap_mgr.grant(parent_pid, Capability::SettingsWrite);
    cap_mgr.grant(parent_pid, Capability::ProcessSpawn);
    cap_mgr.grant(parent_pid, Capability::DisplayWindow);

    TEST_ASSERT(cap_mgr.check(parent_pid, Capability::TaskManage));
    TEST_ASSERT(cap_mgr.check(parent_pid, Capability::SettingsWrite));
    TEST_ASSERT(cap_mgr.check(parent_pid, Capability::ProcessSpawn));

    // Child is given only baseline capabilities
    cap_mgr.grant_mask(child_pid, Capability::StoragePrivate | Capability::DisplayWindow | Capability::LoggingAccess);

    // Verify child does NOT possess parent's elevated capabilities
    TEST_ASSERT(!cap_mgr.check(child_pid, Capability::TaskManage));
    TEST_ASSERT(!cap_mgr.check(child_pid, Capability::SettingsWrite));
    TEST_ASSERT(!cap_mgr.check(child_pid, Capability::ProcessSpawn));

    // Child possesses baseline capabilities
    TEST_ASSERT(cap_mgr.check(child_pid, Capability::StoragePrivate));
    TEST_ASSERT(cap_mgr.check(child_pid, Capability::DisplayWindow));
    TEST_ASSERT(cap_mgr.check(child_pid, Capability::LoggingAccess));

    // On process termination, revoke_all must clear all capabilities
    cap_mgr.revoke_all(child_pid);
    TEST_ASSERT(cap_mgr.get_mask(child_pid) == 0);
    TEST_ASSERT(!cap_mgr.check(child_pid, Capability::StoragePrivate));
    TEST_ASSERT(!cap_mgr.check(child_pid, Capability::DisplayWindow));
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "STAGE 232.16: Process Identity and Capability Lifecycle Tests\n";
    std::cout << "========================================================\n";

    test_anti_replay_strict_sequence_zero_rejected();
    test_anti_replay_non_strict_zero_bypass_mitigation();
    test_generational_process_handle_isolation();
    test_capability_propagation_least_privilege();

    std::cout << "All Stage 232.16 tests passed successfully!\n";
    return 0;
}
