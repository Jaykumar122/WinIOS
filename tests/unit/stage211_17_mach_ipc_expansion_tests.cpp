#include <iostream>
#include <vector>
#include <cstdint>

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

void test_process_ipc_space_rights() {
    std::cout << "[Test 1] Stage 211.17: Mach Port Rights (Receive, Send, SendOnce)...\n";

    ProcessIpcSpace space;
    TEST_CHECK(space.rights_count() == 0, "Initial IPC space has 0 rights");

    // Grant Send right to Port 10
    TEST_CHECK(space.grant_right(10, MachPortRight::Send), "Grant Send right to Port 10");
    TEST_CHECK(space.has_send_right(10), "Has send right to Port 10");
    TEST_CHECK(!space.has_recv_right(10), "Does not have receive right to Port 10");

    // Grant Receive right to Port 20
    TEST_CHECK(space.grant_right(20, MachPortRight::Receive), "Grant Receive right to Port 20");
    TEST_CHECK(space.has_recv_right(20), "Has receive right to Port 20");
    TEST_CHECK(!space.has_send_right(20), "Does not have send right to Port 20");

    // Grant SendOnce right to Port 30
    TEST_CHECK(space.grant_right(30, MachPortRight::SendOnce), "Grant SendOnce right to Port 30");
    TEST_CHECK(space.has_send_right(30), "Has send right to Port 30 initially");

    // Consume SendOnce right
    TEST_CHECK(space.consume_send_once(30), "SendOnce right consumed on use");
    TEST_CHECK(!space.has_send_right(30), "Send right gone after SendOnce consumption");

    // Revoke right
    TEST_CHECK(space.revoke_right(10), "Revoke right to Port 10");
    TEST_CHECK(!space.has_send_right(10), "No right after revocation");
}

void test_bounded_port_queue_and_communication() {
    std::cout << "[Test 2] Stage 211.17: Bounded Mach Message Queuing & Inter-Process Transfer...\n";

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    IpcManager ipc_mgr;
    auto* port = ipc_mgr.create_port(1, 4); // Capacity 4
    TEST_CHECK(port != nullptr, "Port created with capacity 4");
    TEST_CHECK(port->capacity() == 4, "Capacity is 4");

    // Send 4 messages
    for (std::uint32_t i = 1; i <= 4; ++i) {
        KernelMessage msg{};
        msg.sender_tid = 10;
        msg.receiver_tid = 20;
        msg.msg_id = 100 + i;
        TEST_CHECK(port->try_send(msg, sched), "Send message " + std::to_string(i));
    }

    TEST_CHECK(port->is_full(), "Port queue is now full");

    // 5th message must fail (Queue exhaustion defensive protection)
    KernelMessage overflow_msg{};
    overflow_msg.msg_id = 999;
    TEST_CHECK(!port->try_send(overflow_msg, sched), "Overflow message rejected");

    // Receive messages in FIFO order
    for (std::uint32_t i = 1; i <= 4; ++i) {
        KernelMessage recv_msg{};
        TEST_CHECK(port->try_recv(recv_msg, sched), "Receive message " + std::to_string(i));
        TEST_CHECK(recv_msg.msg_id == 100 + i, "FIFO message ID matches " + std::to_string(100 + i));
    }

    TEST_CHECK(port->is_empty(), "Port queue is empty after receiving all messages");
    KernelMessage empty_msg{};
    TEST_CHECK(!port->try_recv(empty_msg, sched), "Receiving from empty port returns false");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.17 Mach IPC Expansion Tests ===\n";
    test_process_ipc_space_rights();
    test_bounded_port_queue_and_communication();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.17: All Mach IPC expansion tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.17: " << g_failures << " tests failed.\n";
        return 1;
    }
}
