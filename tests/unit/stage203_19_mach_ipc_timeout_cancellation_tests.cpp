// ==============================================================================
// WinIOS Campaign 203 Stage 203.19: Mach IPC Timeout and Cancellation Tests
// ==============================================================================
// Independent verification of:
// 1. Receive timeout (kMachRcvTimedOut = 0x10004003) on empty port/set
// 2. Send timeout (kMachSendTimedOut = 0x10000004) when queue limit (64) is reached
// 3. Send cancellation on dead destination (kMachSendInvalidDest = 0x10000003)
// 4. Combined Send-and-Receive (pseudo-receive timeout preserving sent message)
// 5. Invalid receive port name & header size error reporting
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/darwin_mach_ipc.h"
#include "memory/memory.h"

using namespace winios::runtime;
using namespace winios;

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
// Test 1: Receive Timeout on Empty Port
// -----------------------------------------------------------------------------
void test_receive_timeout_on_empty_port() {
    std::cout << "[Stage 203.19 Test 1] Receive Timeout on Empty Port...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 801;
    auto space = ipc.create_task_port_space(task_id);

    mach_port_t rcv_port = ipc.mach_port_allocate(task_id, kMachPortRightReceive);
    TEST_CHECK(rcv_port != kMachPortNull, "Receive port allocated");

    Memory mem(0x10000);
    const std::uint64_t rcv_addr = 0x1000;

    int r = ipc.mach_msg(
        task_id, rcv_addr, kMachRcvMsg | kMachRcvTimeout, 0, 1024, rcv_port, 50, mem);

    TEST_CHECK(r == kMachRcvTimedOut, "Receiving on empty port returns kMachRcvTimedOut (0x10004003)");

    std::cout << "  -> Receive timeout verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Send Timeout on Full Queue (Queue Limit = 64)
// -----------------------------------------------------------------------------
void test_send_timeout_on_full_queue() {
    std::cout << "[Stage 203.19 Test 2] Send Timeout on Full Queue (64 msgs)...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 802;
    const std::uint32_t receiver_task = 803;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t rcv_port = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto rcv_obj = rcv_space->lookup_port(rcv_port);
    mach_port_t snd_port = snd_space->allocate_port(rcv_obj, kMachPortRightSend);

    Memory mem(0x10000);
    const std::uint64_t msg_addr = 0x2000;

    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    hdr.msgh_remote_port = snd_port;
    hdr.msgh_local_port = kMachPortNull;
    hdr.msgh_id = 555;
    std::memcpy(mem.raw_data() + msg_addr, &hdr, sizeof(hdr));

    // Fill queue up to kMaxQueueLimit (64)
    for (std::size_t i = 0; i < MachPortObject::kMaxQueueLimit; ++i) {
        int sr = ipc.mach_msg(sender_task, msg_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
        TEST_CHECK(sr == kMachMsgSuccess, "Enqueueing within queue limit must succeed");
    }

    TEST_CHECK(rcv_obj->queue_size() == MachPortObject::kMaxQueueLimit, "Queue is completely full at 64");

    // 65th message must time out
    int sr_overflow = ipc.mach_msg(
        sender_task, msg_addr, kMachSendMsg | kMachSendTimeout, hdr.msgh_size, 0, 0, 10, mem);

    TEST_CHECK(sr_overflow == kMachSendTimedOut, "Sending to full queue returns kMachSendTimedOut (0x10000004)");
    TEST_CHECK(rcv_obj->queue_size() == MachPortObject::kMaxQueueLimit, "Queue size remains 64");

    std::cout << "  -> Send timeout verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Send Cancellation on Dead Destination
// -----------------------------------------------------------------------------
void test_send_cancellation_on_dead_destination() {
    std::cout << "[Stage 203.19 Test 3] Send Cancellation on Dead Destination...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 804;
    const std::uint32_t receiver_task = 805;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t rcv_port = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    auto rcv_obj = rcv_space->lookup_port(rcv_port);
    mach_port_t snd_port = snd_space->allocate_port(rcv_obj, kMachPortRightSend);

    // Receiver destroys/deallocates port
    ipc.mach_port_deallocate(receiver_task, rcv_port);
    TEST_CHECK(rcv_obj->is_dead(), "Destination object is dead");

    Memory mem(0x10000);
    const std::uint64_t msg_addr = 0x1000;
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    hdr.msgh_remote_port = snd_port;
    std::memcpy(mem.raw_data() + msg_addr, &hdr, sizeof(hdr));

    int sr = ipc.mach_msg(sender_task, msg_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr == kMachSendInvalidDest, "Sending to dead port returns kMachSendInvalidDest (0x10000003)");

    std::cout << "  -> Send cancellation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Combined Send-and-Receive (Pseudo-Receive Timeout)
// -----------------------------------------------------------------------------
void test_combined_send_and_receive_pseudo_receive_timeout() {
    std::cout << "[Stage 203.19 Test 4] Combined Send-and-Receive Timeout...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t client_task = 806;
    const std::uint32_t server_task = 807;

    auto cli_space = ipc.create_task_port_space(client_task);
    auto srv_space = ipc.create_task_port_space(server_task);

    mach_port_t srv_port = ipc.mach_port_allocate(server_task, kMachPortRightReceive);
    auto srv_obj = srv_space->lookup_port(srv_port);
    mach_port_t cli_send_srv = cli_space->allocate_port(srv_obj, kMachPortRightSend);

    mach_port_t reply_port = ipc.mach_reply_port(client_task);

    Memory mem(0x10000);
    const std::uint64_t buf_addr = 0x2000;

    MachMsgHeaderRaw snd_hdr{};
    snd_hdr.msgh_bits = 0;
    snd_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    snd_hdr.msgh_remote_port = cli_send_srv;
    snd_hdr.msgh_local_port = reply_port;
    snd_hdr.msgh_id = 999;

    std::memcpy(mem.raw_data() + buf_addr, &snd_hdr, sizeof(snd_hdr));
    std::uint32_t payload = 0x12345678;
    std::memcpy(mem.raw_data() + buf_addr + sizeof(snd_hdr), &payload, sizeof(payload));

    // Client executes atomic Send-and-Receive:
    // It sends to server, and awaits reply on reply_port
    int r = ipc.mach_msg(
        client_task, buf_addr, kMachSendMsg | kMachRcvMsg | kMachRcvTimeout,
        snd_hdr.msgh_size, 1024, reply_port, 25, mem);

    // Reply port has no reply yet, so pseudo-receive returns kMachRcvTimedOut
    TEST_CHECK(r == kMachRcvTimedOut, "Combined send-and-receive returns kMachRcvTimedOut on reply timeout");

    // CRITICAL: The sent message MUST be safely queued on the server's port!
    TEST_CHECK(srv_obj->has_messages(), "Server received message despite client receive timeout");

    const std::uint64_t srv_rcv_addr = 0x4000;
    int srv_r = ipc.mach_msg(server_task, srv_rcv_addr, kMachRcvMsg, 0, 1024, srv_port, 0, mem);
    TEST_CHECK(srv_r == kMachMsgSuccess, "Server dequeues client message");

    MachMsgHeaderRaw srv_hdr{};
    std::memcpy(&srv_hdr, mem.raw_data() + srv_rcv_addr, sizeof(srv_hdr));
    TEST_CHECK(srv_hdr.msgh_id == 999, "Server message ID matches sent message ID");

    std::cout << "  -> Combined send-and-receive verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Invalid Port Name & Header Size Errors
// -----------------------------------------------------------------------------
void test_invalid_name_and_header_errors() {
    std::cout << "[Stage 203.19 Test 5] Invalid Name & Header Size Errors...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 808;
    ipc.create_task_port_space(task_id);

    Memory mem(0x10000);

    // Receive on invalid/non-existent port handle
    int r1 = ipc.mach_msg(
        task_id, 0x1000, kMachRcvMsg, 0, 1024, 0x1234, 0, mem);
    TEST_CHECK(r1 == kMachRcvInvalidName, "Receiving on invalid port returns kMachRcvInvalidName (0x10004002)");

    // Receive with buffer size smaller than header
    mach_port_t rcv_port = ipc.mach_port_allocate(task_id, kMachPortRightReceive);
    int r2 = ipc.mach_msg(
        task_id, 0x1000, kMachRcvMsg, 0, sizeof(MachMsgHeaderRaw) - 1, rcv_port, 0, mem);
    TEST_CHECK(r2 == kMachRcvHeaderError, "Receiving with tiny buffer returns kMachRcvHeaderError (0x1000400B)");

    std::cout << "  -> Invalid name and header errors verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.19: Mach IPC Timeout and Cancellation Tests\n";
    std::cout << "==================================================================\n";

    test_receive_timeout_on_empty_port();
    test_send_timeout_on_full_queue();
    test_send_cancellation_on_dead_destination();
    test_combined_send_and_receive_pseudo_receive_timeout();
    test_invalid_name_and_header_errors();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.19 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
