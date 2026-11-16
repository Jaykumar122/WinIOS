// ==============================================================================
// WinIOS Campaign 202 Stage 15: Synthetic Mach IPC Subsystem Tests
// ==============================================================================
// Verifies:
// 1. Generation-safe handle allocator and rollover/stale handle rejection
// 2. Reference counting (mach_port_mod_refs, mach_port_get_refs) and dead name transitions
// 3. Send-once right semantics (one-shot message delivery and immediate invalidation)
// 4. Bounded queue capacity (64 message limit and kMachSendTimedOut behavior)
// 5. mach_msg_overwrite_trap separate receive buffer writes and buffer bounds
// 6. Cross-task IPC rendezvous and reply port rights translation
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_mach_ipc.h"

using namespace winios;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Generation-Safe Handle Allocator and Rollover/Stale Handle Validation
// -----------------------------------------------------------------------------
void test_generation_safe_handle_allocator() {
    std::cout << "[Test 1] Generation-Safe Handle Allocator & Stale Handle Rejection...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTask = 100;
    auto space = ipc.create_task_port_space(kTask);
    TEST_CHECK(space != nullptr, "Port space must be created");

    auto obj1 = std::make_shared<MachPortObject>(101, kTask);
    mach_port_t h1 = space->allocate_port(obj1, kMachPortRightReceive);
    TEST_CHECK(h1 != kMachPortNull, "Port handle must not be null");

    std::uint16_t idx1 = MachPortSpace::handle_index(h1);
    std::uint16_t gen1 = MachPortSpace::handle_generation(h1);
    TEST_CHECK(idx1 > 0, "Index must be non-zero");
    TEST_CHECK(gen1 == 1, "Initial generation must be 1");
    TEST_CHECK(space->lookup_port(h1) == obj1, "Lookup must return obj1");

    // Deallocate port -> slot is returned to free list
    TEST_CHECK(space->deallocate_port(h1), "Deallocate must succeed");
    TEST_CHECK(space->lookup_port(h1) == nullptr, "Deallocated handle must return null");

    // Allocate new port -> should reuse slot with incremented generation
    auto obj2 = std::make_shared<MachPortObject>(102, kTask);
    mach_port_t h2 = space->allocate_port(obj2, kMachPortRightReceive);
    TEST_CHECK(h2 != kMachPortNull, "New port handle must not be null");

    std::uint16_t idx2 = MachPortSpace::handle_index(h2);
    std::uint16_t gen2 = MachPortSpace::handle_generation(h2);
    TEST_CHECK(idx2 == idx1, "Reallocated port should reuse index");
    TEST_CHECK(gen2 == 2, "Generation should be incremented to 2");

    // Stale handle check: h1 has same index but generation 1
    TEST_CHECK(space->lookup_port(h1) == nullptr, "Stale handle h1 must NOT resolve to obj2");
    TEST_CHECK(space->lookup_port(h2) == obj2, "Valid handle h2 must resolve to obj2");

    std::cout << "  -> Generation-safe handle allocator passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Reference Counting & Dead Name Transition
// -----------------------------------------------------------------------------
void test_reference_counting_and_dead_name() {
    std::cout << "[Test 2] Reference Counting & Dead Name Transition...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTaskServer = 201;
    constexpr std::uint32_t kTaskClient = 202;

    auto srv_space = ipc.create_task_port_space(kTaskServer);
    auto cli_space = ipc.create_task_port_space(kTaskClient);

    // Server creates a port with receive right
    auto port_obj = std::make_shared<MachPortObject>(2001, kTaskServer);
    mach_port_t srv_port = srv_space->allocate_port(port_obj, kMachPortRightReceive);

    // Client gains send right to the same port
    mach_port_t cli_port = cli_space->allocate_port(port_obj, kMachPortRightSend);
    TEST_CHECK(cli_space->has_right(cli_port, kMachPortRightSend), "Client must have send right");
    TEST_CHECK(cli_space->get_refs(cli_port, kMachPortRightSend) == 1, "Initial urefs must be 1");

    // Increment send urefs by +3
    TEST_CHECK(cli_space->mod_refs(cli_port, kMachPortRightSend, 3), "mod_refs +3 must succeed");
    TEST_CHECK(cli_space->get_refs(cli_port, kMachPortRightSend) == 4, "urefs must be 4");

    // Server destroys receive right -> port object dies
    TEST_CHECK(srv_space->deallocate_port(srv_port), "Server deallocate receive right must succeed");
    TEST_CHECK(port_obj->is_dead(), "Port object must be marked dead");

    // Client inspects port: send right must automatically transition to dead name
    TEST_CHECK(cli_space->has_right(cli_port, kMachPortRightDeadName), "Client right must transition to DeadName");
    TEST_CHECK(!cli_space->has_right(cli_port, kMachPortRightSend), "Client must no longer have Send right");
    TEST_CHECK(cli_space->get_refs(cli_port, kMachPortRightDeadName) == 4, "Dead name must retain urefs count");

    // Memory message send to dead name must fail with kMachSendInvalidDest
    Memory mem(0x10000ULL);
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(hdr);
    hdr.msgh_remote_port = cli_port;
    std::memcpy(mem.raw_data() + 0x1000, &hdr, sizeof(hdr));

    auto status = ipc.mach_msg(kTaskClient, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_CHECK(status == kMachSendInvalidDest, "Sending to dead name must fail with kMachSendInvalidDest");

    // Decrement dead name refs by -4
    TEST_CHECK(cli_space->mod_refs(cli_port, kMachPortRightDeadName, -4), "mod_refs -4 must succeed");
    TEST_CHECK(!cli_space->has_right(cli_port, kMachPortRightDeadName), "Dead name must be destroyed");

    std::cout << "  -> Reference counting & dead name transition passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Send-Once Right Semantics
// -----------------------------------------------------------------------------
void test_send_once_right_semantics() {
    std::cout << "[Test 3] Send-Once Right Semantics...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTaskServer = 301;
    constexpr std::uint32_t kTaskClient = 302;

    auto srv_space = ipc.create_task_port_space(kTaskServer);
    auto cli_space = ipc.create_task_port_space(kTaskClient);

    auto port_obj = std::make_shared<MachPortObject>(3001, kTaskServer);
    mach_port_t srv_recv = srv_space->allocate_port(port_obj, kMachPortRightReceive);
    mach_port_t cli_so   = cli_space->allocate_port(port_obj, kMachPortRightSendOnce);

    TEST_CHECK(cli_space->has_right(cli_so, kMachPortRightSendOnce), "Client must have send-once right");

    Memory mem(0x10000ULL);
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(hdr);
    hdr.msgh_remote_port = cli_so;
    hdr.msgh_id = 42;
    std::memcpy(mem.raw_data() + 0x2000, &hdr, sizeof(hdr));

    // First send: should succeed and consume send-once right
    auto st1 = ipc.mach_msg(kTaskClient, 0x2000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_CHECK(st1 == kMachMsgSuccess, "First send must succeed: " + std::to_string(st1));
    TEST_CHECK(port_obj->queue_size() == 1, "Server queue must contain 1 message");
    TEST_CHECK(!cli_space->has_right(cli_so, kMachPortRightSendOnce), "Send-once right must be consumed");

    // Second send: should fail because right is consumed
    auto st2 = ipc.mach_msg(kTaskClient, 0x2000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_CHECK(st2 == kMachSendInvalidDest, "Second send on send-once right must fail with kMachSendInvalidDest");

    (void)srv_recv;
    std::cout << "  -> Send-once right semantics passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Bounded Queue Capacity (64 Message Limit)
// -----------------------------------------------------------------------------
void test_bounded_queue_capacity() {
    std::cout << "[Test 4] Bounded Queue Capacity (64 Messages Limit)...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTask = 400;
    auto space = ipc.create_task_port_space(kTask);

    auto port_obj = std::make_shared<MachPortObject>(4001, kTask);
    mach_port_t p = space->allocate_port(port_obj, kMachPortRightSend);

    Memory mem(0x10000ULL);
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(hdr);
    hdr.msgh_remote_port = p;
    std::memcpy(mem.raw_data() + 0x1000, &hdr, sizeof(hdr));

    // Enqueue exactly 64 messages
    for (std::size_t i = 0; i < 64; ++i) {
        auto st = ipc.mach_msg(kTask, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
        TEST_CHECK(st == kMachMsgSuccess, "Send within queue limit must succeed");
    }
    TEST_CHECK(port_obj->queue_size() == 64, "Queue size must be exactly 64");

    // Attempt 65th send -> must return kMachSendTimedOut
    auto st65 = ipc.mach_msg(kTask, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_CHECK(st65 == kMachSendTimedOut, "65th message must be rejected with kMachSendTimedOut");

    // Dequeue 1 message and retry
    auto popped = port_obj->dequeue_message();
    TEST_CHECK(popped.has_value(), "Dequeue must succeed");
    TEST_CHECK(port_obj->queue_size() == 63, "Queue size must now be 63");

    auto st66 = ipc.mach_msg(kTask, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_CHECK(st66 == kMachMsgSuccess, "Send after queue drain must succeed");
    TEST_CHECK(port_obj->queue_size() == 64, "Queue size must be 64 again");

    std::cout << "  -> Bounded queue capacity passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: mach_msg_overwrite_trap with Separate Receive Buffer
// -----------------------------------------------------------------------------
void test_mach_msg_overwrite_separate_buffers() {
    std::cout << "[Test 5] mach_msg_overwrite with Separate Buffers...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTask = 500;
    auto space = ipc.create_task_port_space(kTask);

    auto port_obj = std::make_shared<MachPortObject>(5001, kTask);
    mach_port_t recv_port = space->allocate_port(port_obj, kMachPortRightReceive);
    mach_port_t send_port = space->allocate_port(port_obj, kMachPortRightSend);

    Memory mem(0x100000ULL);
    const std::uint64_t send_vaddr = 0x20000ULL;
    const std::uint64_t rcv_vaddr  = 0x40000ULL;

    struct TestPayloadMsg {
        MachMsgHeaderRaw hdr;
        std::uint64_t    data1;
        std::uint64_t    data2;
    };

    TestPayloadMsg send_msg{};
    send_msg.hdr.msgh_size = sizeof(TestPayloadMsg);
    send_msg.hdr.msgh_remote_port = send_port;
    send_msg.hdr.msgh_id = 0x1234;
    send_msg.data1 = 0xCAFEBABE11223344ULL;
    send_msg.data2 = 0xDEADBEEF55667788ULL;

    std::memcpy(mem.raw_data() + send_vaddr, &send_msg, sizeof(send_msg));

    // Clear receive buffer
    std::memset(mem.raw_data() + rcv_vaddr, 0xAA, sizeof(TestPayloadMsg));

    // Perform mach_msg_overwrite: send from 0x20000, receive into 0x40000
    auto st = ipc.mach_msg_overwrite(
        kTask,
        send_vaddr,
        kMachSendMsg | kMachRcvMsg,
        sizeof(TestPayloadMsg),
        sizeof(TestPayloadMsg),
        recv_port,
        0,
        mem,
        rcv_vaddr,
        sizeof(TestPayloadMsg));

    TEST_CHECK(st == kMachMsgSuccess, "mach_msg_overwrite must succeed: " + std::to_string(st));

    // Verify send buffer is unchanged
    TestPayloadMsg check_send{};
    std::memcpy(&check_send, mem.raw_data() + send_vaddr, sizeof(check_send));
    TEST_CHECK(check_send.data1 == 0xCAFEBABE11223344ULL, "Send buffer data1 must remain intact");

    // Verify receive buffer received the message
    TestPayloadMsg check_rcv{};
    std::memcpy(&check_rcv, mem.raw_data() + rcv_vaddr, sizeof(check_rcv));
    TEST_CHECK(check_rcv.hdr.msgh_id == 0x1234, "Received msgh_id must match");
    TEST_CHECK(check_rcv.data1 == 0xCAFEBABE11223344ULL, "Received data1 must match payload");
    TEST_CHECK(check_rcv.data2 == 0xDEADBEEF55667788ULL, "Received data2 must match payload");

    // Test buffer too small check (kMachRcvTooLarge)
    // Enqueue another message
    auto st_enq = ipc.mach_msg(kTask, send_vaddr, kMachSendMsg, sizeof(TestPayloadMsg), 0, 0, 0, mem);
    TEST_CHECK(st_enq == kMachMsgSuccess, "Enqueue for undersized receive test must succeed");
    auto st_small = ipc.mach_msg_overwrite(
        kTask,
        0,
        kMachRcvMsg,
        0,
        sizeof(MachMsgHeaderRaw) /* too small for payload */,
        recv_port,
        0,
        mem,
        rcv_vaddr,
        sizeof(MachMsgHeaderRaw));
    TEST_CHECK(st_small == kMachRcvTooLarge, "Undersized receive buffer must return kMachRcvTooLarge");

    std::cout << "  -> mach_msg_overwrite separate buffers passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Cross-Task Rendezvous & Reply Port Rights Translation
// -----------------------------------------------------------------------------
void test_cross_task_rendezvous_and_reply_port() {
    std::cout << "[Test 6] Cross-Task Rendezvous & Reply Port Rights Translation...\n";
    MachIpcSubsystem ipc;
    constexpr std::uint32_t kTaskServer = 601;
    constexpr std::uint32_t kTaskClient = 602;

    auto srv_space = ipc.create_task_port_space(kTaskServer);
    auto cli_space = ipc.create_task_port_space(kTaskClient);

    // 1. Server creates service port and registers in bootstrap catalog
    auto srv_obj = std::make_shared<MachPortObject>(6001, kTaskServer);
    mach_port_t srv_recv = srv_space->allocate_port(srv_obj, kMachPortRightReceive);
    TEST_CHECK(ipc.bootstrap_register(kTaskServer, "com.apple.test.calc", srv_recv), "Bootstrap register must succeed");

    // 2. Client looks up service port
    mach_port_t cli_srv = ipc.bootstrap_look_up(kTaskClient, "com.apple.test.calc");
    TEST_CHECK(cli_srv != kMachPortNull, "Client bootstrap lookup must return valid port");

    // 3. Client creates reply port
    mach_port_t cli_reply = ipc.mach_reply_port(kTaskClient);
    TEST_CHECK(cli_reply != kMachPortNull, "Reply port must not be null");

    Memory mem(0x100000ULL);
    const std::uint64_t client_req_vaddr = 0x10000ULL;
    const std::uint64_t server_rcv_vaddr = 0x20000ULL;

    struct RpcRequestMsg {
        MachMsgHeaderRaw hdr;
        std::uint32_t    opcode;
        std::uint32_t    arg1;
        std::uint32_t    arg2;
    };

    RpcRequestMsg req{};
    req.hdr.msgh_size = sizeof(RpcRequestMsg);
    req.hdr.msgh_remote_port = cli_srv;
    req.hdr.msgh_local_port = cli_reply; // Reply port
    req.hdr.msgh_id = 100;
    req.opcode = 1; // ADD
    req.arg1 = 25;
    req.arg2 = 17;

    std::memcpy(mem.raw_data() + client_req_vaddr, &req, sizeof(req));

    // Client sends request
    auto st_send = ipc.mach_msg(kTaskClient, client_req_vaddr, kMachSendMsg, sizeof(req), 0, 0, 0, mem);
    TEST_CHECK(st_send == kMachMsgSuccess, "Client send must succeed");

    // Server receives request
    auto st_rcv = ipc.mach_msg(kTaskServer, server_rcv_vaddr, kMachRcvMsg, 0, sizeof(RpcRequestMsg), srv_recv, 0, mem);
    TEST_CHECK(st_rcv == kMachMsgSuccess, "Server receive must succeed");

    RpcRequestMsg srv_received_msg{};
    std::memcpy(&srv_received_msg, mem.raw_data() + server_rcv_vaddr, sizeof(srv_received_msg));
    TEST_CHECK(srv_received_msg.hdr.msgh_id == 100, "Server received id 100");
    TEST_CHECK(srv_received_msg.opcode == 1, "Server received opcode 1");

    // Remote port in received message must be translated into a send right in server's space
    mach_port_t srv_reply_port = srv_received_msg.hdr.msgh_remote_port;
    TEST_CHECK(srv_reply_port != kMachPortNull, "Reply port in server space must be allocated");
    TEST_CHECK(srv_space->has_right(srv_reply_port, kMachPortRightSend), "Server must have send right to client's reply port");

    // Server sends response back using translated reply port
    struct RpcResponseMsg {
        MachMsgHeaderRaw hdr;
        std::uint32_t    result;
    };

    const std::uint64_t server_resp_vaddr = 0x30000ULL;
    const std::uint64_t client_resp_vaddr = 0x40000ULL;

    RpcResponseMsg resp{};
    resp.hdr.msgh_size = sizeof(RpcResponseMsg);
    resp.hdr.msgh_remote_port = srv_reply_port;
    resp.hdr.msgh_id = 101;
    resp.result = srv_received_msg.arg1 + srv_received_msg.arg2; // 25 + 17 = 42

    std::memcpy(mem.raw_data() + server_resp_vaddr, &resp, sizeof(resp));
    auto st_resp_send = ipc.mach_msg(kTaskServer, server_resp_vaddr, kMachSendMsg, sizeof(resp), 0, 0, 0, mem);
    TEST_CHECK(st_resp_send == kMachMsgSuccess, "Server reply send must succeed");

    // Client receives response on reply port
    auto st_resp_rcv = ipc.mach_msg(kTaskClient, client_resp_vaddr, kMachRcvMsg, 0, sizeof(RpcResponseMsg), cli_reply, 0, mem);
    TEST_CHECK(st_resp_rcv == kMachMsgSuccess, "Client reply receive must succeed");

    RpcResponseMsg cli_received_resp{};
    std::memcpy(&cli_received_resp, mem.raw_data() + client_resp_vaddr, sizeof(cli_received_resp));
    TEST_CHECK(cli_received_resp.hdr.msgh_id == 101, "Client received response id 101");
    TEST_CHECK(cli_received_resp.result == 42, "Client received calculation result 42");

    std::cout << "  -> Cross-task rendezvous & reply port translation passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 202.15 Synthetic Mach IPC Subsystem Tests ===\n";
    test_generation_safe_handle_allocator();
    test_reference_counting_and_dead_name();
    test_send_once_right_semantics();
    test_bounded_queue_capacity();
    test_mach_msg_overwrite_separate_buffers();
    test_cross_task_rendezvous_and_reply_port();

    if (g_failures != 0) {
        std::cerr << "Stage 202.15 tests FAILED with " << g_failures << " error(s).\n";
        return 1;
    }
    std::cout << "All Stage 202.15 tests passed successfully!\n";
    return 0;
}
