// =============================================================================
// stage_192_10_mach_ipc_tests.cpp
//
// Stage 192.10: Darwin Mach IPC Right and Message Validation Tests
// Verifies:
//   - Port rights allocation, inspection, and deallocation (send, receive)
//   - Standard Mach traps (task_self, thread_self, mach_reply_port, host_self)
//   - mach_msg message delivery (MACH_SEND_MSG and MACH_RCV_MSG)
//   - Error codes: invalid dest (0x10000003), invalid data (0x10000002),
//                  invalid name (0x10004002), rcv timeout (0x10004003),
//                  too large (0x10004004)
//   - Bidirectional Request/Reply Mach IPC pattern with port translation
//   - Direct guest trap dispatch via handle_mach_trap (X16 negative trap numbers)
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Port Rights Allocation, Ownership, and Deallocation
// -----------------------------------------------------------------------------
bool test_port_rights_lifecycle() {
    std::cout << "[Test 1] Port Rights Allocation, Ownership, and Deallocation...\n";

    MachIpcSubsystem ipc;
    constexpr uint32_t kTaskId = 1001;
    auto space = ipc.create_task_port_space(kTaskId);
    CHECK(space != nullptr, "create_task_port_space failed");

    // Allocate a new port with receive right
    auto obj = std::make_shared<MachPortObject>(5001, kTaskId);
    mach_port_t port_name = space->allocate_port(obj, kMachPortRightReceive);
    CHECK(port_name != kMachPortNull, "allocate_port failed");
    CHECK(space->has_right(port_name, kMachPortRightReceive), "must have receive right");

    // Look up port object
    auto lookup = space->lookup_port(port_name);
    CHECK(lookup == obj, "lookup_port object pointer mismatch");
    CHECK(lookup->receiver_task() == kTaskId, "receiver task mismatch");

    // Insert send right
    CHECK(space->insert_right(port_name, obj, kMachPortRightSend), "insert_right send failed");
    CHECK(space->has_right(port_name, kMachPortRightSend), "must have send right");

    // Deallocate port (urefs decremented to 0)
    CHECK(space->deallocate_port(port_name), "deallocate_port 1 failed");
    CHECK(space->deallocate_port(port_name), "deallocate_port 2 failed");
    CHECK(space->lookup_port(port_name) == nullptr, "port must be null after deallocation");
    CHECK(!space->has_right(port_name, kMachPortRightReceive), "must not have rights after deallocation");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Standard Mach Trap Dispatch via handle_mach_trap
// -----------------------------------------------------------------------------
bool test_standard_mach_traps() {
    std::cout << "[Test 2] Standard Mach Trap Dispatch (task_self, thread_self, reply_port)...\n";

    CPU cpu;
    Memory memory(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);
    engine.set_current_pid(1002);

    DarwinPipelineResult res;

    // 1. task_self_trap (X16 = -28)
    cpu.write_register(16, static_cast<uint64_t>(-28));
    engine.handle_mach_trap(res);
    mach_port_t task_port = static_cast<mach_port_t>(cpu.read_register(0));
    CHECK(task_port != kMachPortNull, "task_self_trap returned null port");

    // 2. mach_reply_port (X16 = -26)
    cpu.write_register(16, static_cast<uint64_t>(-26));
    engine.handle_mach_trap(res);
    mach_port_t reply_port = static_cast<mach_port_t>(cpu.read_register(0));
    CHECK(reply_port != kMachPortNull && reply_port != task_port, "mach_reply_port invalid");

    // 3. thread_self_trap (X16 = -27)
    cpu.write_register(16, static_cast<uint64_t>(-27));
    engine.handle_mach_trap(res);
    mach_port_t thread_port = static_cast<mach_port_t>(cpu.read_register(0));
    CHECK(thread_port != kMachPortNull, "thread_self_trap returned null port");

    // 4. host_self_trap (X16 = -29)
    cpu.write_register(16, static_cast<uint64_t>(-29));
    engine.handle_mach_trap(res);
    mach_port_t host_port = static_cast<mach_port_t>(cpu.read_register(0));
    CHECK(host_port != kMachPortNull, "host_self_trap returned null port");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: mach_msg Send, Receive, and Payload Verification
// -----------------------------------------------------------------------------
bool test_mach_msg_send_receive() {
    std::cout << "[Test 3] mach_msg Send, Receive, and Payload Verification...\n";

    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);
    constexpr uint32_t kSenderTask = 1003;
    constexpr uint32_t kReceiverTask = 1004;

    auto sender_space = ipc.create_task_port_space(kSenderTask);
    auto receiver_space = ipc.create_task_port_space(kReceiverTask);

    // Receiver creates a destination port
    auto dest_obj = std::make_shared<MachPortObject>(6001, kReceiverTask);
    mach_port_t rcv_port = receiver_space->allocate_port(dest_obj, kMachPortRightReceive);

    // Sender gets a send right to the destination port
    mach_port_t send_port = sender_space->allocate_port(dest_obj, kMachPortRightSend);

    // Prepare message in memory
    uint64_t msg_va = 0x20000;
    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_bits = 0;
    send_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 16;
    send_hdr.msgh_remote_port = send_port;
    send_hdr.msgh_local_port = kMachPortNull;
    send_hdr.msgh_id = 4242;

    const char payload[] = "Mach Payload OK";
    std::memcpy(memory.raw_data() + msg_va, &send_hdr, sizeof(send_hdr));
    std::memcpy(memory.raw_data() + msg_va + sizeof(send_hdr), payload, 16);

    // Send message (option = kMachSendMsg 0x1)
    int32_t send_ret = ipc.mach_msg(
        kSenderTask, msg_va, kMachSendMsg,
        sizeof(MachMsgHeaderRaw) + 16, 0, 0, 0, memory);
    CHECK(send_ret == kMachMsgSuccess, "mach_msg send failed");

    // Receive message (option = kMachRcvMsg 0x2)
    uint64_t rcv_va = 0x30000;
    int32_t rcv_ret = ipc.mach_msg(
        kReceiverTask, rcv_va, kMachRcvMsg,
        0, 1024, rcv_port, 0, memory);
    CHECK(rcv_ret == kMachMsgSuccess, "mach_msg receive failed");

    // Verify received header and payload
    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, memory.raw_data() + rcv_va, sizeof(rcv_hdr));
    CHECK(rcv_hdr.msgh_id == 4242, "Received msgh_id mismatch");

    char rcv_payload[16];
    std::memcpy(rcv_payload, memory.raw_data() + rcv_va + sizeof(rcv_hdr), 16);
    CHECK(std::strcmp(rcv_payload, payload) == 0, "Received payload mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: mach_msg Error Matrix & Boundary Validation
// -----------------------------------------------------------------------------
bool test_mach_msg_error_matrix() {
    std::cout << "[Test 4] mach_msg Error Matrix & Boundary Validation...\n";

    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);
    constexpr uint32_t kTask = 1005;
    auto space = ipc.create_task_port_space(kTask);

    uint64_t va = 0x40000;

    // 1. send_size < sizeof(MachMsgHeaderRaw) -> kMachSendInvalidData (0x10000002)
    int32_t err1 = ipc.mach_msg(kTask, va, kMachSendMsg, 10, 0, 0, 0, memory);
    CHECK(err1 == kMachSendInvalidData, "Expected kMachSendInvalidData");

    // 2. msgh_remote_port does not exist -> kMachSendInvalidDest (0x10000003)
    MachMsgHeaderRaw bad_dest_hdr{};
    bad_dest_hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    bad_dest_hdr.msgh_remote_port = 9999; // non-existent
    std::memcpy(memory.raw_data() + va, &bad_dest_hdr, sizeof(bad_dest_hdr));

    int32_t err2 = ipc.mach_msg(kTask, va, kMachSendMsg, sizeof(MachMsgHeaderRaw), 0, 0, 0, memory);
    CHECK(err2 == kMachSendInvalidDest, "Expected kMachSendInvalidDest");

    // 3. rcv_name does not exist -> kMachRcvInvalidName (0x10004002)
    int32_t err3 = ipc.mach_msg(kTask, va, kMachRcvMsg, 0, 512, 9999, 0, memory);
    CHECK(err3 == kMachRcvInvalidName, "Expected kMachRcvInvalidName");

    // 4. Receive from empty port -> kMachRcvTimedOut (0x10004003)
    auto empty_obj = std::make_shared<MachPortObject>(7001, kTask);
    mach_port_t empty_port = space->allocate_port(empty_obj, kMachPortRightReceive);

    int32_t err4 = ipc.mach_msg(kTask, va, kMachRcvMsg, 0, 512, empty_port, 0, memory);
    CHECK(err4 == kMachRcvTimedOut, "Expected kMachRcvTimedOut");

    // 5. rcv_size too small for enqueued message -> kMachRcvTooLarge (0x10004004)
    mach_port_t send_port = space->allocate_port(empty_obj, kMachPortRightSend);
    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 64; // 88 bytes total
    send_hdr.msgh_remote_port = send_port;
    std::memcpy(memory.raw_data() + va, &send_hdr, sizeof(send_hdr));

    CHECK(ipc.mach_msg(kTask, va, kMachSendMsg, sizeof(MachMsgHeaderRaw) + 64, 0, 0, 0, memory) == kMachMsgSuccess,
          "Send before overflow receive failed");

    // Try to receive with only 32 bytes buffer
    int32_t err5 = ipc.mach_msg(kTask, va + 0x1000, kMachRcvMsg, 0, 32, empty_port, 0, memory);
    CHECK(err5 == kMachRcvTooLarge, "Expected kMachRcvTooLarge");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Bidirectional Request-Reply Pattern with Port Rights Translation
// -----------------------------------------------------------------------------
bool test_bidirectional_request_reply() {
    std::cout << "[Test 5] Bidirectional Request-Reply Pattern with Port Translation...\n";

    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);
    constexpr uint32_t kClientTask = 1006;
    constexpr uint32_t kServerTask = 1007;

    auto client_space = ipc.create_task_port_space(kClientTask);
    auto server_space = ipc.create_task_port_space(kServerTask);

    // Server service port
    auto service_obj = std::make_shared<MachPortObject>(8001, kServerTask);
    mach_port_t server_service_port = server_space->allocate_port(service_obj, kMachPortRightReceive);

    // Client allocates a local reply port
    auto reply_obj = std::make_shared<MachPortObject>(8002, kClientTask);
    mach_port_t client_reply_port = client_space->allocate_port(reply_obj, kMachPortRightReceive);

    // Client gets send right to server service
    mach_port_t client_server_ref = client_space->allocate_port(service_obj, kMachPortRightSend);

    // Step 1: Client sends request to Server with msgh_local_port = client_reply_port
    uint64_t client_req_va = 0x50000;
    MachMsgHeaderRaw req_hdr{};
    req_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 8;
    req_hdr.msgh_remote_port = client_server_ref;
    req_hdr.msgh_local_port = client_reply_port; // reply port!
    req_hdr.msgh_id = 100;

    std::memcpy(memory.raw_data() + client_req_va, &req_hdr, sizeof(req_hdr));
    std::memcpy(memory.raw_data() + client_req_va + sizeof(req_hdr), "REQUEST", 8);

    CHECK(ipc.mach_msg(kClientTask, client_req_va, kMachSendMsg,
                       sizeof(MachMsgHeaderRaw) + 8, 0, 0, 0, memory) == kMachMsgSuccess,
          "Client send request failed");

    // Step 2: Server receives request. Note: reply port translated into server's port space!
    uint64_t server_req_va = 0x60000;
    CHECK(ipc.mach_msg(kServerTask, server_req_va, kMachRcvMsg,
                       0, 512, server_service_port, 0, memory) == kMachMsgSuccess,
          "Server receive request failed");

    MachMsgHeaderRaw srv_rcv_hdr{};
    std::memcpy(&srv_rcv_hdr, memory.raw_data() + server_req_va, sizeof(srv_rcv_hdr));
    CHECK(srv_rcv_hdr.msgh_id == 100, "Server received id mismatch");
    mach_port_t server_reply_dest = srv_rcv_hdr.msgh_remote_port;
    CHECK(server_reply_dest != kMachPortNull, "Translated reply port must be non-null");

    // Step 3: Server sends reply back through translated reply port
    uint64_t server_resp_va = 0x70000;
    MachMsgHeaderRaw resp_hdr{};
    resp_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 6;
    resp_hdr.msgh_remote_port = server_reply_dest;
    resp_hdr.msgh_local_port = kMachPortNull;
    resp_hdr.msgh_id = 101;

    std::memcpy(memory.raw_data() + server_resp_va, &resp_hdr, sizeof(resp_hdr));
    std::memcpy(memory.raw_data() + server_resp_va + sizeof(resp_hdr), "REPLY", 6);

    CHECK(ipc.mach_msg(kServerTask, server_resp_va, kMachSendMsg,
                       sizeof(MachMsgHeaderRaw) + 6, 0, 0, 0, memory) == kMachMsgSuccess,
          "Server send reply failed");

    // Step 4: Client receives reply on client_reply_port
    uint64_t client_resp_va = 0x80000;
    CHECK(ipc.mach_msg(kClientTask, client_resp_va, kMachRcvMsg,
                       0, 512, client_reply_port, 0, memory) == kMachMsgSuccess,
          "Client receive reply failed");

    MachMsgHeaderRaw client_rcv_hdr{};
    std::memcpy(&client_rcv_hdr, memory.raw_data() + client_resp_va, sizeof(client_rcv_hdr));
    CHECK(client_rcv_hdr.msgh_id == 101, "Client received reply id mismatch");

    char reply_payload[6];
    std::memcpy(reply_payload, memory.raw_data() + client_resp_va + sizeof(client_rcv_hdr), 6);
    CHECK(std::strcmp(reply_payload, "REPLY") == 0, "Client received reply content mismatch");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.10: Darwin Mach IPC Conformance Tests\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_port_rights_lifecycle, "Port Rights Allocation, Ownership, and Deallocation");
    run_test(test_standard_mach_traps, "Standard Mach Trap Dispatch (task_self, thread_self, reply_port)");
    run_test(test_mach_msg_send_receive, "mach_msg Send, Receive, and Payload Verification");
    run_test(test_mach_msg_error_matrix, "mach_msg Error Matrix & Boundary Validation");
    run_test(test_bidirectional_request_reply, "Bidirectional Request-Reply Pattern with Port Translation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
