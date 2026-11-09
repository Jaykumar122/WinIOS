#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_mach_ipc.h"

using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

bool test_task_port_space_creation() {
    std::cout << "[Test 1] Task Port Space Initialization...\n";

    MachIpcSubsystem ipc;
    auto space1 = ipc.get_task_port_space(1); // launchd
    TEST_ASSERT(space1 != nullptr, "Task 1 (launchd) space must exist by default");
    TEST_ASSERT(space1->task_self() != 0, "Task self port must be non-zero");
    TEST_ASSERT(space1->host_self() != 0, "Host self port must be non-zero");

    auto space2 = ipc.create_task_port_space(100); // SpringBoard
    TEST_ASSERT(space2 != nullptr, "Task 100 space created");
    TEST_ASSERT(space2->task_id() == 100, "Task id match");

    std::cout << "  Passed (Task 1 and Task 100 port spaces initialized)\n";
    return true;
}

bool test_port_allocation_and_rights() {
    std::cout << "[Test 2] Mach Port Allocation & Right Insertion...\n";

    MachIpcSubsystem ipc;
    auto space = ipc.get_task_port_space(1);
    TEST_ASSERT(space != nullptr, "Task 1 space must exist");

    auto obj = std::make_shared<MachPortObject>(1001, 1);
    mach_port_t port = space->allocate_port(obj, kMachPortRightReceive);
    TEST_ASSERT(port != 0, "Allocated port must be non-zero");
    TEST_ASSERT(space->has_right(port, kMachPortRightReceive), "Must have receive right");

    bool ok_insert = space->insert_right(port, obj, kMachPortRightSend);
    TEST_ASSERT(ok_insert, "insert_right must succeed");
    TEST_ASSERT(space->has_right(port, kMachPortRightSend), "Must now have send right");

    std::cout << "  Passed (Port 0x" << std::hex << port << std::dec << " allocated with rights)\n";
    return true;
}

bool test_port_deallocation() {
    std::cout << "[Test 3] Mach Port Deallocation...\n";

    MachIpcSubsystem ipc;
    auto space = ipc.get_task_port_space(1);
    auto obj = std::make_shared<MachPortObject>(1002, 1);
    mach_port_t port = space->allocate_port(obj, kMachPortRightReceive);

    TEST_ASSERT(space->lookup_port(port) != nullptr, "Port lookup before deallocation");
    bool ok_dealloc = space->deallocate_port(port);
    TEST_ASSERT(ok_dealloc, "deallocate_port must succeed");
    TEST_ASSERT(space->lookup_port(port) == nullptr, "Port lookup after deallocation must be null");

    std::cout << "  Passed (Port deallocated)\n";
    return true;
}

bool test_bootstrap_service_registration() {
    std::cout << "[Test 4] Bootstrap Service Registration...\n";

    MachIpcSubsystem ipc;
    auto space = ipc.get_task_port_space(1);
    auto obj = std::make_shared<MachPortObject>(2001, 1);
    mach_port_t ws_port = space->allocate_port(obj, kMachPortRightReceive);

    bool ok_reg = ipc.bootstrap_register(1, "com.apple.windowserver", ws_port);
    TEST_ASSERT(ok_reg, "bootstrap_register must succeed for WindowServer");

    std::cout << "  Passed (Registered 'com.apple.windowserver')\n";
    return true;
}

bool test_bootstrap_service_lookup() {
    std::cout << "[Test 5] Bootstrap Service Lookup Across Tasks...\n";

    MachIpcSubsystem ipc;
    // Task 1 registers WindowServer
    auto space1 = ipc.get_task_port_space(1);
    auto ws_obj = std::make_shared<MachPortObject>(2002, 1);
    mach_port_t ws_server_port = space1->allocate_port(ws_obj, kMachPortRightReceive);
    ipc.bootstrap_register(1, "com.apple.windowserver", ws_server_port);

    // Task 100 (SpringBoard) looks up WindowServer
    auto space100 = ipc.create_task_port_space(100);
    mach_port_t client_port = ipc.bootstrap_look_up(100, "com.apple.windowserver");
    TEST_ASSERT(client_port != kMachPortNull, "bootstrap_look_up must find WindowServer");

    // Verify client port in task 100 points to the same underlying kernel MachPortObject
    auto resolved_obj = space100->lookup_port(client_port);
    TEST_ASSERT(resolved_obj != nullptr, "Client port must resolve to object");
    TEST_ASSERT(resolved_obj->id() == ws_obj->id(), "Must resolve to identical kernel object");
    TEST_ASSERT(space100->has_right(client_port, kMachPortRightSend), "Client must have send right");

    std::cout << "  Passed (SpringBoard connected to WindowServer via bootstrap)\n";
    return true;
}

bool test_mach_msg_send_and_receive() {
    std::cout << "[Test 6] Mach Message Send & Receive Roundtrip...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x10000); // 64 KB guest RAM

    // Setup server port (Task 1)
    auto space1 = ipc.get_task_port_space(1);
    auto srv_obj = std::make_shared<MachPortObject>(3001, 1);
    mach_port_t srv_port = space1->allocate_port(srv_obj, kMachPortRightReceive);
    space1->insert_right(srv_port, srv_obj, kMachPortRightSend);

    // Prepare message in memory at 0x1000
    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_bits = 0;
    send_hdr.msgh_remote_port = srv_port;
    send_hdr.msgh_local_port = 0;
    send_hdr.msgh_id = 1001; // Request ID

    const char payload[] = "CREATE_SURFACE_REQUEST";
    std::size_t payload_len = sizeof(payload);
    std::size_t total_send_size = sizeof(MachMsgHeaderRaw) + payload_len;
    send_hdr.msgh_size = static_cast<std::uint32_t>(total_send_size);

    std::memcpy(mem.raw_data() + 0x1000, &send_hdr, sizeof(send_hdr));
    std::memcpy(mem.raw_data() + 0x1000 + sizeof(send_hdr), payload, payload_len);

    // Send message via mach_msg
    std::int32_t send_rc = ipc.mach_msg(
        1, 0x1000, kMachSendMsg, static_cast<std::uint32_t>(total_send_size), 0, 0, 0, mem);
    TEST_ASSERT(send_rc == kMachMsgSuccess, "mach_msg SEND must succeed");
    TEST_ASSERT(srv_obj->has_messages(), "Server port must have queued message");

    // Receive message into memory at 0x2000
    std::int32_t rcv_rc = ipc.mach_msg(
        1, 0x2000, kMachRcvMsg, 0, 1024, srv_port, 0, mem);
    TEST_ASSERT(rcv_rc == kMachMsgSuccess, "mach_msg RCV must succeed");

    // Inspect received message
    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x2000, sizeof(rcv_hdr));
    TEST_ASSERT(rcv_hdr.msgh_id == 1001, "Received message ID must match");
    TEST_ASSERT(rcv_hdr.msgh_size == total_send_size, "Received size must match");

    const char* rcv_payload = reinterpret_cast<const char*>(mem.raw_data() + 0x2000 + sizeof(MachMsgHeaderRaw));
    TEST_ASSERT(std::strcmp(rcv_payload, payload) == 0, "Received payload payload match");

    std::cout << "  Passed (mach_msg delivered " << total_send_size << " bytes)\n";
    return true;
}

bool test_mach_msg_send_invalid_destination() {
    std::cout << "[Test 7] mach_msg Invalid Destination Port Rejection...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    MachMsgHeaderRaw hdr{};
    hdr.msgh_remote_port = 0x9999; // Non-existent port
    hdr.msgh_size = sizeof(hdr);
    std::memcpy(mem.raw_data() + 0x1000, &hdr, sizeof(hdr));

    std::int32_t rc = ipc.mach_msg(
        1, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_ASSERT(rc == kMachSendInvalidDest, "Must fail with kMachSendInvalidDest");

    std::cout << "  Passed (Invalid port rejected)\n";
    return true;
}

bool test_mach_msg_rcv_too_small_buffer() {
    std::cout << "[Test 8] mach_msg Receive Buffer Too Small Rejection...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    auto space = ipc.get_task_port_space(1);
    auto obj = std::make_shared<MachPortObject>(4001, 1);
    mach_port_t port = space->allocate_port(obj, kMachPortRightReceive);
    space->insert_right(port, obj, kMachPortRightSend);

    // Queue 100-byte message
    MachMessage msg;
    msg.header.msgh_remote_port = port;
    msg.payload.assign(100, 0xAA);
    msg.header.msgh_size = static_cast<std::uint32_t>(sizeof(MachMsgHeaderRaw) + 100);
    obj->enqueue_message(std::move(msg));

    // Try receiving with buffer size of only 30 bytes
    std::int32_t rc = ipc.mach_msg(
        1, 0x1000, kMachRcvMsg, 0, 30, port, 0, mem);
    TEST_ASSERT(rc == kMachRcvTooLarge, "Must fail with kMachRcvTooLarge");

    std::cout << "  Passed (Buffer overrun avoided)\n";
    return true;
}

bool test_mach_reply_port_trap() {
    std::cout << "[Test 9] mach_reply_port Trap Dispatch...\n";

    MachIpcSubsystem ipc;
    mach_port_t reply_port = ipc.mach_reply_port(1);
    TEST_ASSERT(reply_port != kMachPortNull, "Reply port must be allocated");

    auto space = ipc.get_task_port_space(1);
    TEST_ASSERT(space->has_right(reply_port, kMachPortRightReceive), "Reply port must have receive right");

    std::cout << "  Passed (Reply port 0x" << std::hex << reply_port << std::dec << " allocated)\n";
    return true;
}

bool test_mach_trap_dispatcher_integration() {
    std::cout << "[Test 10] Mach Trap Dispatcher System Calls...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    // Trap -28: task_self
    std::uint64_t ret_task = 0;
    std::int32_t status_task = ipc.dispatch_mach_trap(1, -28, {}, mem, ret_task);
    TEST_ASSERT(status_task == 0, "Trap -28 must succeed");
    TEST_ASSERT(ret_task == 0x101, "Task self must be 0x101");

    // Trap -29: host_self
    std::uint64_t ret_host = 0;
    std::int32_t status_host = ipc.dispatch_mach_trap(1, -29, {}, mem, ret_host);
    TEST_ASSERT(status_host == 0, "Trap -29 must succeed");
    TEST_ASSERT(ret_host == 0x102, "Host self must be 0x102");

    // Trap -61: mach_port_allocate
    std::uint64_t ret_port = 0;
    std::int32_t status_alloc = ipc.dispatch_mach_trap(1, -61, {0, kMachPortRightReceive}, mem, ret_port);
    TEST_ASSERT(status_alloc == 0, "Trap -61 must succeed");
    TEST_ASSERT(ret_port != 0, "Allocated port must be non-zero");

    std::cout << "  Passed (Mach trap dispatcher verified)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 124: DARWIN MACH SYSTEM CALLS & IPC TESTS         \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: Darwin Mach Traps & Message Queue IPC Subsystem        \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_task_port_space_creation()) passed++;
    if (test_port_allocation_and_rights()) passed++;
    if (test_port_deallocation()) passed++;
    if (test_bootstrap_service_registration()) passed++;
    if (test_bootstrap_service_lookup()) passed++;
    if (test_mach_msg_send_and_receive()) passed++;
    if (test_mach_msg_send_invalid_destination()) passed++;
    if (test_mach_msg_rcv_too_small_buffer()) passed++;
    if (test_mach_reply_port_trap()) passed++;
    if (test_mach_trap_dispatcher_integration()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 124 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
