#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_kernel_message_layout() {
    std::cout << "Running test_kernel_message_layout...\n";
    EXPECT_EQ(sizeof(KernelMessage), 64u);
}

void test_port_creation_and_capacity() {
    std::cout << "Running test_port_creation_and_capacity...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(100, 3);
    EXPECT_TRUE(port != nullptr);
    EXPECT_EQ(port->owner_tid(), 100u);
    EXPECT_EQ(port->capacity(), 3u);
    EXPECT_TRUE(port->is_empty());
    EXPECT_TRUE(!port->is_full());

    KernelMessage msg{};
    msg.sender_tid = 100;
    msg.msg_id = 1;

    EXPECT_TRUE(port->try_send(msg, sched));
    msg.msg_id = 2;
    EXPECT_TRUE(port->try_send(msg, sched));
    msg.msg_id = 3;
    EXPECT_TRUE(port->try_send(msg, sched));

    EXPECT_TRUE(port->is_full());
    EXPECT_EQ(port->message_count(), 3u);

    // 4th send must fail
    msg.msg_id = 4;
    EXPECT_TRUE(!port->try_send(msg, sched));
}

void test_nonblocking_send_and_receive() {
    std::cout << "Running test_nonblocking_send_and_receive...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(101, 10);

    for (std::uint32_t i = 1; i <= 5; ++i) {
        KernelMessage msg{};
        msg.sender_tid = 200;
        msg.receiver_tid = 101;
        msg.msg_id = i;
        msg.payload_size = 4;
        std::memcpy(msg.payload, &i, 4);
        EXPECT_TRUE(port->try_send(msg, sched));
    }

    EXPECT_EQ(port->message_count(), 5u);

    for (std::uint32_t i = 1; i <= 5; ++i) {
        KernelMessage out{};
        EXPECT_TRUE(port->try_recv(out, sched));
        EXPECT_EQ(out.msg_id, i);
        std::uint32_t val = 0;
        std::memcpy(&val, out.payload, 4);
        EXPECT_EQ(val, i);
    }

    EXPECT_TRUE(port->is_empty());
    KernelMessage extra{};
    EXPECT_TRUE(!port->try_recv(extra, sched));
}

void test_blocking_send_unblocked_by_recv() {
    std::cout << "Running test_blocking_send_unblocked_by_recv...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    CPU cpu;
    KernelThread* sender = mgr.create_thread("Sender", 0x3000, 0, 0x40000, 0x4000, 16);
    sched.enqueue_thread(sender);
    sched.tick(cpu);
    EXPECT_EQ(sched.current_thread(), sender);

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(500, 1); // Capacity 1

    KernelMessage m1{};
    m1.msg_id = 1;
    EXPECT_TRUE(port->try_send(m1, sched));
    EXPECT_TRUE(port->is_full());

    // Sender blocks on full queue
    KernelMessage m2{};
    m2.msg_id = 2;
    port->send_blocking(sender, m2, sched, cpu);

    EXPECT_EQ(static_cast<std::uint32_t>(sender->state()), static_cast<std::uint32_t>(ThreadState::Blocked));
    EXPECT_EQ(port->send_waiters_count(), 1u);

    // Receiver drains message -> sender must unblock
    KernelMessage out{};
    EXPECT_TRUE(port->try_recv(out, sched));
    EXPECT_EQ(out.msg_id, 1u);

    EXPECT_EQ(port->send_waiters_count(), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(sender->state()), static_cast<std::uint32_t>(ThreadState::Ready));
}

void test_blocking_recv_unblocked_by_send() {
    std::cout << "Running test_blocking_recv_unblocked_by_send...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    CPU cpu;
    KernelThread* receiver = mgr.create_thread("Receiver", 0x3000, 0, 0x40000, 0x4000, 16);
    sched.enqueue_thread(receiver);
    sched.tick(cpu);
    EXPECT_EQ(sched.current_thread(), receiver);

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(600, 5);

    EXPECT_TRUE(port->is_empty());
    KernelMessage out{};
    port->recv_blocking(receiver, out, sched, cpu);

    EXPECT_EQ(static_cast<std::uint32_t>(receiver->state()), static_cast<std::uint32_t>(ThreadState::Blocked));
    EXPECT_EQ(port->recv_waiters_count(), 1u);

    // Sender sends message -> receiver unblocks
    KernelMessage in_msg{};
    in_msg.msg_id = 999;
    EXPECT_TRUE(port->try_send(in_msg, sched));

    EXPECT_EQ(port->recv_waiters_count(), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(receiver->state()), static_cast<std::uint32_t>(ThreadState::Ready));
}

void test_guest_memory_ipc_transfer() {
    std::cout << "Running test_guest_memory_ipc_transfer...\n";
    Memory mem(0x100000);
    KernelThreadManager mgr;
    PriorityScheduler sched(mgr, mem);
    sched.initialize(0x90000, 0x4000, 0x2000);

    IpcManager ipc_mgr;
    IpcPort* port = ipc_mgr.create_port(777, 8);

    const std::uint64_t sender_buf = 0x2000ULL;
    const std::uint64_t receiver_buf = 0x3000ULL;

    // Sender writes message structure into guest memory at 0x2000
    KernelMessage send_msg{};
    send_msg.sender_tid = 10;
    send_msg.receiver_tid = 20;
    send_msg.msg_id = 42;
    send_msg.payload_size = 12;
    std::memcpy(send_msg.payload, "Hello WinIOS", 12);

    for (std::size_t i = 0; i < sizeof(KernelMessage); i += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, reinterpret_cast<const std::uint8_t*>(&send_msg) + i, 4);
        mem.write_u32(static_cast<std::size_t>(sender_buf + i), word);
    }

    // Send from guest memory
    bool ok_send = ipc_mgr.send_from_guest(port->port_id(), 10, sender_buf, mem, sched);
    EXPECT_TRUE(ok_send);
    EXPECT_EQ(port->message_count(), 1u);

    // Receive to guest memory at 0x3000
    bool ok_recv = ipc_mgr.recv_to_guest(port->port_id(), 20, receiver_buf, mem, sched);
    EXPECT_TRUE(ok_recv);
    EXPECT_TRUE(port->is_empty());

    // Read back received message from guest memory
    KernelMessage recv_msg{};
    for (std::size_t i = 0; i < sizeof(KernelMessage); i += 4) {
        const std::uint32_t word = mem.read_u32(static_cast<std::size_t>(receiver_buf + i));
        std::memcpy(reinterpret_cast<std::uint8_t*>(&recv_msg) + i, &word, 4);
    }

    EXPECT_EQ(recv_msg.sender_tid, 10u);
    EXPECT_EQ(recv_msg.receiver_tid, 20u);
    EXPECT_EQ(recv_msg.msg_id, 42u);
    EXPECT_EQ(recv_msg.payload_size, 12u);
    EXPECT_EQ(std::memcmp(recv_msg.payload, "Hello WinIOS", 12), 0);
}

int main() {
    std::cout << "=== WINIOS STAGE 194.14: GUEST KERNEL IPC & MESSAGE QUEUES TESTS ===\n";

    test_kernel_message_layout();
    test_port_creation_and_capacity();
    test_nonblocking_send_and_receive();
    test_blocking_send_unblocked_by_recv();
    test_blocking_recv_unblocked_by_send();
    test_guest_memory_ipc_transfer();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
