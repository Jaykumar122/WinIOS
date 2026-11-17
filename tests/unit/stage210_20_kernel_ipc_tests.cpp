#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>

#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_mach_ipc_port_send_and_recv() {
    std::cout << "[Test 1] Stage 210.20: Darwin Mach IPC Port Allocation, Message Queue & Capacity...\n";

    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize();

    IpcManager ipc;

    // 1. Create port owned by TID 1
    auto* port = ipc.create_port(1, 4); // Capacity: 4 messages
    TEST_CHECK(port != nullptr, "Create IPC port");
    TEST_CHECK(port->port_id() == 1, "Port ID is 1");
    TEST_CHECK(port->owner_tid() == 1, "Port owner TID is 1");
    TEST_CHECK(port->capacity() == 4, "Port capacity is 4");
    TEST_CHECK(port->is_empty(), "Port is initially empty");

    // 2. Send messages
    KernelMessage msg1{};
    msg1.sender_tid = 2;
    msg1.receiver_tid = 1;
    msg1.msg_id = 0x1001;
    msg1.payload_size = 8;
    std::memcpy(msg1.payload, "HELLO_1", 8);

    TEST_CHECK(port->try_send(msg1, sched), "try_send msg1");
    TEST_CHECK(port->message_count() == 1, "Message count is 1");
    TEST_CHECK(!port->is_empty(), "Port is no longer empty");

    KernelMessage msg2{};
    msg2.sender_tid = 3;
    msg2.receiver_tid = 1;
    msg2.msg_id = 0x1002;
    msg2.payload_size = 8;
    std::memcpy(msg2.payload, "HELLO_2", 8);

    TEST_CHECK(port->try_send(msg2, sched), "try_send msg2");
    TEST_CHECK(port->message_count() == 2, "Message count is 2");

    // 3. Receive messages (FIFO)
    KernelMessage recv_msg{};
    TEST_CHECK(port->try_recv(recv_msg, sched), "try_recv first message");
    TEST_CHECK(recv_msg.msg_id == 0x1001, "Received msg1 first");
    TEST_CHECK(std::memcmp(recv_msg.payload, "HELLO_1", 8) == 0, "Payload matches 'HELLO_1'");

    TEST_CHECK(port->try_recv(recv_msg, sched), "try_recv second message");
    TEST_CHECK(recv_msg.msg_id == 0x1002, "Received msg2 second");

    // Port is now empty, receive should return false
    TEST_CHECK(!port->try_recv(recv_msg, sched), "try_recv on empty port returns false");

    // 4. Port destruction
    TEST_CHECK(ipc.destroy_port(1, sched), "Destroy port 1");
    TEST_CHECK(ipc.get_port(1) == nullptr, "Port 1 no longer exists");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.20 Kernel IPC Tests ===\n";
    test_mach_ipc_port_send_and_recv();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.20: All kernel IPC tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.20: " << g_failures << " tests failed.\n";
        return 1;
    }
}
