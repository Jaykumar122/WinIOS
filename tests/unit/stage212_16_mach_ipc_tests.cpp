#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bootstrap_registry.h"
#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

void test_port_rights_and_send_once() {
    std::cout << "[Test 1] Mach Port Rights and Send-Once Consumption..." << std::endl;

    ProcessIpcSpace server_space;
    ProcessIpcSpace client_space;

    const std::uint32_t port_id = 100;

    // Server gets Receive right
    server_space.grant_right(port_id, MachPortRight::Receive);
    TEST_CHECK(server_space.has_recv_right(port_id), "Server has receive right");
    TEST_CHECK(!server_space.has_send_right(port_id), "Server does not have send right");

    // Client gets Send right
    client_space.grant_right(port_id, MachPortRight::Send);
    TEST_CHECK(client_space.has_send_right(port_id), "Client has send right");
    TEST_CHECK(!client_space.has_recv_right(port_id), "Client does not have receive right");

    // Client gets SendOnce right on reply port 101
    const std::uint32_t reply_port = 101;
    client_space.grant_right(reply_port, MachPortRight::SendOnce);
    TEST_CHECK(client_space.has_send_right(reply_port), "Client has send right via SendOnce");

    // Consume SendOnce right
    TEST_CHECK(client_space.consume_send_once(reply_port), "Consumed SendOnce right");
    TEST_CHECK(!client_space.has_send_right(reply_port), "SendOnce right no longer present after consumption");
    TEST_CHECK(!client_space.consume_send_once(reply_port), "Second consumption fails");
}

void test_ipc_message_delivery() {
    std::cout << "[Test 2] Mach IPC Message Transmission and Queue Order..." << std::endl;

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(1, 8);
    TEST_CHECK(port != nullptr, "Created IPC port with capacity 8");

    // Send 3 distinct messages
    for (std::uint32_t i = 1; i <= 3; ++i) {
        KernelMessage msg{};
        msg.sender_tid = 100 + i;
        msg.receiver_tid = 1;
        msg.msg_id = 0x1000 + i;
        msg.payload_size = 16;
        std::snprintf(reinterpret_cast<char*>(msg.payload), kMaxIpcPayloadSize, "Payload %u", i);

        TEST_CHECK(port->try_send(msg, sched), "Sent message " + std::to_string(i));
    }

    TEST_CHECK(port->message_count() == 3, "Port has 3 messages queued");

    // Receive and verify in FIFO order
    for (std::uint32_t i = 1; i <= 3; ++i) {
        KernelMessage out_msg{};
        TEST_CHECK(port->try_recv(out_msg, sched), "Received message " + std::to_string(i));
        TEST_CHECK(out_msg.sender_tid == 100 + i, "Sender TID matches");
        TEST_CHECK(out_msg.msg_id == 0x1000 + i, "Message ID matches");
        char expected[kMaxIpcPayloadSize];
        std::snprintf(expected, sizeof(expected), "Payload %u", i);
        TEST_CHECK(std::strcmp(reinterpret_cast<char*>(out_msg.payload), expected) == 0, "Payload matches");
    }

    TEST_CHECK(port->is_empty(), "Port queue empty after draining");
}

void test_capacity_overflow_rejection() {
    std::cout << "[Test 3] Port Queue Bounded Capacity and Overflow Rejection..." << std::endl;

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    IpcManager ipc_mgr;
    const std::size_t cap = 4;
    IpcPort* port = ipc_mgr.create_port(1, cap);

    KernelMessage dummy{};
    dummy.sender_tid = 10;
    dummy.receiver_tid = 1;
    dummy.msg_id = 99;

    // Fill port to capacity
    for (std::size_t i = 0; i < cap; ++i) {
        TEST_CHECK(port->try_send(dummy, sched), "Pushed message to queue");
    }
    TEST_CHECK(port->is_full(), "Port is full");

    // Attempt 5th send -> must be rejected
    TEST_CHECK(!port->try_send(dummy, sched), "Overflow message rejected");

    // Pop 1 message
    KernelMessage out{};
    TEST_CHECK(port->try_recv(out, sched), "Popped 1 message");
    TEST_CHECK(!port->is_full(), "Port no longer full");

    // Now send succeeds
    TEST_CHECK(port->try_send(dummy, sched), "Message accepted after space freed");
}

void test_port_destruction() {
    std::cout << "[Test 4] Port Destruction and Dead Cleanup..." << std::endl;

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(1, 4);
    const std::uint32_t pid = port->port_id();

    TEST_CHECK(ipc_mgr.get_port(pid) != nullptr, "Port registered in IpcManager");
    TEST_CHECK(ipc_mgr.destroy_port(pid, sched), "Destroyed port");
    TEST_CHECK(ipc_mgr.get_port(pid) == nullptr, "Port lookup returns nullptr after destruction");
}

void test_bootstrap_ipc_service_binding() {
    std::cout << "[Test 5] Bootstrap Service Registry + Mach IPC Integration..." << std::endl;

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    IpcManager ipc_mgr;
    BootstrapServiceRegistry registry;

    // 1. Service daemon creates Mach port and registers with Bootstrap Registry
    IpcPort* service_port = ipc_mgr.create_port(10 /* daemon tid */, 16);
    const std::string service_name = "com.apple.notificationcenter";
    TEST_CHECK(registry.check_in(service_name, service_port->port_id(), 5 /* daemon pid */) == kBootstrapSuccess,
               "Daemon registered 'com.apple.notificationcenter'");

    // 2. Client process looks up service port
    std::uint32_t resolved_port_id = 0;
    TEST_CHECK(registry.look_up(service_name, resolved_port_id, 20 /* client pid */) == kBootstrapSuccess,
               "Client looked up service port");
    TEST_CHECK(resolved_port_id == service_port->port_id(), "Resolved port matches daemon's port");

    // 3. Client transmits request message
    ProcessIpcSpace client_space;
    client_space.grant_right(resolved_port_id, MachPortRight::Send);

    IpcPort* target_port = ipc_mgr.get_port(resolved_port_id);
    TEST_CHECK(target_port != nullptr, "Acquired target IpcPort from ID");

    KernelMessage req{};
    req.sender_tid = 50;
    req.receiver_tid = 10;
    req.msg_id = 0x42;
    std::memcpy(req.payload, "PING", 5);
    TEST_CHECK(target_port->try_send(req, sched), "Client sent PING request");

    // 4. Daemon receives request
    KernelMessage daemon_recv{};
    TEST_CHECK(service_port->try_recv(daemon_recv, sched), "Daemon received request");
    TEST_CHECK(std::strcmp(reinterpret_cast<char*>(daemon_recv.payload), "PING") == 0, "Payload is PING");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.16 Mach IPC Reliability & Port Space Tests ===" << std::endl;
    test_port_rights_and_send_once();
    test_ipc_message_delivery();
    test_capacity_overflow_rejection();
    test_port_destruction();
    test_bootstrap_ipc_service_binding();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.16: All Mach IPC reliability tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.16: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
