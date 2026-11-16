// ==============================================================================
// WinIOS Campaign 203 Stage 203.17: Mach IPC Port Sets & Notifications Tests
// ==============================================================================
// Independent verification of:
// 1. Port set allocation (kMachPortRightPortSet) & member insertion/extraction
// 2. Message multiplexing: receiving on a port set dequeues from member ports
// 3. Header local port attribution: msgh_local_port reflects the receiving member
// 4. Dead-name notifications (MACH_NOTIFY_DEAD_NAME = 72) upon port death
// 5. Port set lifecycle & automatic membership cleanup upon deallocation
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
// Test 1: Port Set Allocation & Membership Management
// -----------------------------------------------------------------------------
void test_port_set_allocation_and_membership() {
    std::cout << "[Stage 203.17 Test 1] Port Set Allocation & Membership Management...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 601;
    auto space = ipc.create_task_port_space(task_id);
    TEST_CHECK(space != nullptr, "Task port space created");

    // Allocate two ports with receive rights
    mach_port_t p1 = ipc.mach_port_allocate(task_id, kMachPortRightReceive);
    mach_port_t p2 = ipc.mach_port_allocate(task_id, kMachPortRightReceive);
    TEST_CHECK(p1 != kMachPortNull && p2 != kMachPortNull, "Member ports allocated");

    // Allocate port set
    mach_port_t pset = ipc.mach_port_allocate(task_id, kMachPortRightPortSet);
    TEST_CHECK(pset != kMachPortNull, "Port set allocated");
    TEST_CHECK(space->is_port_set(pset), "Port set identified correctly");

    // Insert members
    TEST_CHECK(ipc.mach_port_insert_member(task_id, p1, pset), "Insert p1 into pset");
    TEST_CHECK(ipc.mach_port_insert_member(task_id, p2, pset), "Insert p2 into pset");

    auto members = space->get_set_members(pset);
    TEST_CHECK(members.size() == 2, "Port set contains 2 members");

    // Extract p1
    TEST_CHECK(ipc.mach_port_extract_member(task_id, p1, pset), "Extract p1 from pset");
    members = space->get_set_members(pset);
    TEST_CHECK(members.size() == 1, "Port set contains 1 member");
    TEST_CHECK(members[0] == p2, "Remaining member is p2");

    std::cout << "  -> Port set allocation and membership verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Port Set Message Multiplexing & msgh_local_port Attribution
// -----------------------------------------------------------------------------
void test_port_set_message_receipt_multiplexing() {
    std::cout << "[Stage 203.17 Test 2] Port Set Message Receipt Multiplexing...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 602;
    const std::uint32_t receiver_task = 603;

    auto rcv_space = ipc.create_task_port_space(receiver_task);
    auto snd_space = ipc.create_task_port_space(sender_task);

    // Receiver creates two receive ports and a port set
    mach_port_t p1 = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    mach_port_t p2 = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    mach_port_t pset = ipc.mach_port_allocate(receiver_task, kMachPortRightPortSet);

    ipc.mach_port_insert_member(receiver_task, p1, pset);
    ipc.mach_port_insert_member(receiver_task, p2, pset);

    // Give sender send rights to p1 and p2
    auto obj1 = rcv_space->lookup_port(p1);
    auto obj2 = rcv_space->lookup_port(p2);
    mach_port_t snd_p1 = snd_space->allocate_port(obj1, kMachPortRightSend);
    mach_port_t snd_p2 = snd_space->allocate_port(obj2, kMachPortRightSend);

    Memory mem(0x100000); // 1 MB guest memory

    // Sender sends M1 to p1
    const std::uint64_t msg1_addr = 0x1000;
    MachMsgHeaderRaw hdr1{};
    hdr1.msgh_bits = 0;
    hdr1.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    hdr1.msgh_remote_port = snd_p1;
    hdr1.msgh_local_port = kMachPortNull;
    hdr1.msgh_id = 1001;

    std::memcpy(mem.raw_data() + msg1_addr, &hdr1, sizeof(hdr1));
    std::uint32_t payload1 = 0xDEADBEEF;
    std::memcpy(mem.raw_data() + msg1_addr + sizeof(hdr1), &payload1, sizeof(payload1));

    int sr1 = ipc.mach_msg(
        sender_task, msg1_addr, kMachSendMsg, hdr1.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr1 == kMachMsgSuccess, "Send M1 to p1 succeeded");

    // Sender sends M2 to p2
    const std::uint64_t msg2_addr = 0x2000;
    MachMsgHeaderRaw hdr2{};
    hdr2.msgh_bits = 0;
    hdr2.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    hdr2.msgh_remote_port = snd_p2;
    hdr2.msgh_local_port = kMachPortNull;
    hdr2.msgh_id = 1002;

    std::memcpy(mem.raw_data() + msg2_addr, &hdr2, sizeof(hdr2));
    std::uint32_t payload2 = 0xCAFEBABE;
    std::memcpy(mem.raw_data() + msg2_addr + sizeof(hdr2), &payload2, sizeof(payload2));

    int sr2 = ipc.mach_msg(
        sender_task, msg2_addr, kMachSendMsg, hdr2.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(sr2 == kMachMsgSuccess, "Send M2 to p2 succeeded");

    // Receiver receives on pset! (First should dequeue M1 from p1)
    const std::uint64_t rcv_addr = 0x4000;
    int rr1 = ipc.mach_msg(
        receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, pset, 0, mem);
    TEST_CHECK(rr1 == kMachMsgSuccess, "Receive from pset succeeded (M1)");

    MachMsgHeaderRaw rcv_hdr1{};
    std::memcpy(&rcv_hdr1, mem.raw_data() + rcv_addr, sizeof(rcv_hdr1));
    TEST_CHECK(rcv_hdr1.msgh_id == 1001, "First received message ID is 1001");
    TEST_CHECK(rcv_hdr1.msgh_local_port == p1, "msgh_local_port accurately attributes receiving member p1");

    // Receiver receives on pset again! (Should dequeue M2 from p2)
    int rr2 = ipc.mach_msg(
        receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, pset, 0, mem);
    TEST_CHECK(rr2 == kMachMsgSuccess, "Receive from pset succeeded (M2)");

    MachMsgHeaderRaw rcv_hdr2{};
    std::memcpy(&rcv_hdr2, mem.raw_data() + rcv_addr, sizeof(rcv_hdr2));
    TEST_CHECK(rcv_hdr2.msgh_id == 1002, "Second received message ID is 1002");
    TEST_CHECK(rcv_hdr2.msgh_local_port == p2, "msgh_local_port accurately attributes receiving member p2");

    // Third receive on empty pset times out
    int rr3 = ipc.mach_msg(
        receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, pset, 0, mem);
    TEST_CHECK(rr3 == kMachRcvTimedOut, "Receive on empty pset returns kMachRcvTimedOut");

    std::cout << "  -> Port set multiplexing verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Dead-Name Notifications (MACH_NOTIFY_DEAD_NAME)
// -----------------------------------------------------------------------------
void test_dead_name_notification() {
    std::cout << "[Stage 203.17 Test 3] Dead-Name Notifications (MACH_NOTIFY_DEAD_NAME)...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t server_task = 604;
    const std::uint32_t client_task = 605;

    auto srv_space = ipc.create_task_port_space(server_task);
    auto cli_space = ipc.create_task_port_space(client_task);

    // Server allocates service port
    mach_port_t srv_port = ipc.mach_port_allocate(server_task, kMachPortRightReceive);
    auto srv_obj = srv_space->lookup_port(srv_port);

    // Client obtains send right
    mach_port_t cli_send_port = cli_space->allocate_port(srv_obj, kMachPortRightSend);

    // Client allocates notification port (receive right)
    mach_port_t notify_port = ipc.mach_port_allocate(client_task, kMachPortRightReceive);

    // Client requests dead name notification
    bool req = ipc.mach_port_request_notification(
        client_task, cli_send_port, MACH_NOTIFY_DEAD_NAME, notify_port);
    TEST_CHECK(req, "Dead name notification successfully requested");

    // Server destroys/deallocates service port
    TEST_CHECK(ipc.mach_port_deallocate(server_task, srv_port), "Server deallocates receive right");
    TEST_CHECK(srv_obj->is_dead(), "Port object marked dead");

    // Client checks notification port
    Memory mem(0x10000);
    const std::uint64_t rcv_addr = 0x2000;
    int rr = ipc.mach_msg(
        client_task, rcv_addr, kMachRcvMsg, 0, 1024, notify_port, 0, mem);
    TEST_CHECK(rr == kMachMsgSuccess, "Notification received on notify_port");

    MachMsgHeaderRaw notif_hdr{};
    std::memcpy(&notif_hdr, mem.raw_data() + rcv_addr, sizeof(notif_hdr));
    TEST_CHECK(notif_hdr.msgh_id == static_cast<std::int32_t>(MACH_NOTIFY_DEAD_NAME), "Message ID is MACH_NOTIFY_DEAD_NAME (72)");

    mach_port_t dead_name = 0;
    std::memcpy(&dead_name, mem.raw_data() + rcv_addr + sizeof(notif_hdr), sizeof(dead_name));
    TEST_CHECK(dead_name == cli_send_port, "Notification payload identifies dead port name");

    // Client's port right has converted to dead name
    TEST_CHECK(cli_space->has_right(cli_send_port, kMachPortRightDeadName), "Client port right is now DeadName");

    std::cout << "  -> Dead-name notifications verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Port Set Deallocation & Membership Cleanup
// -----------------------------------------------------------------------------
void test_port_set_cleanup_on_deallocate() {
    std::cout << "[Stage 203.17 Test 4] Port Set Deallocation & Cleanup...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 606;
    auto space = ipc.create_task_port_space(task_id);

    mach_port_t p1 = ipc.mach_port_allocate(task_id, kMachPortRightReceive);
    mach_port_t pset = ipc.mach_port_allocate(task_id, kMachPortRightPortSet);

    ipc.mach_port_insert_member(task_id, p1, pset);
    TEST_CHECK(space->get_set_members(pset).size() == 1, "Member inserted");

    // Deallocate port set
    TEST_CHECK(ipc.mach_port_deallocate(task_id, pset), "Deallocate port set");
    TEST_CHECK(!space->is_port_set(pset), "Port set no longer exists");

    // Member p1 still retains its receive right
    TEST_CHECK(space->has_right(p1, kMachPortRightReceive), "Member retains receive right after set destruction");

    std::cout << "  -> Port set cleanup verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.17: Mach IPC Port Sets & Notifications Tests\n";
    std::cout << "==================================================================\n";

    test_port_set_allocation_and_membership();
    test_port_set_message_receipt_multiplexing();
    test_dead_name_notification();
    test_port_set_cleanup_on_deallocate();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.17 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
