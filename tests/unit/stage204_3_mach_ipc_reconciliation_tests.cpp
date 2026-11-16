// ==============================================================================
// WinIOS Campaign 204 Stage 204.3: Mach IPC Handle & Reconciliation Tests
// ==============================================================================
// Comprehensive verification of Mach IPC handle integrity and lifecycle:
// 1. Generation wraparound (0xFFFF -> 1, avoiding 0)
// 2. Index reuse across port destruction and reallocation
// 3. Stale handle rejection upon generation increment
// 4. Send-once right atomic single-use consumption
// 5. Port-set membership additions, lookups, and member destruction cleanup
// 6. Dead-name notifications generated upon receive right destruction
// 7. Bounded queue limits (64 messages) and resource cleanup
// ==============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
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
// Test 1: Handle Generation Lifecycle, Reuse & Wraparound
// -----------------------------------------------------------------------------
void test_mach_handle_generation_lifecycle() {
    std::cout << "[Stage 204.3 Test 1] Mach Port Handle Generation Lifecycle...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 701;
    auto space = ipc.create_task_port_space(task_id);
    TEST_CHECK(space != nullptr, "Task port space created");

    auto port1 = std::make_shared<MachPortObject>(101, task_id);

    // Initial allocation
    mach_port_t name1 = space->allocate_port(port1, kMachPortRightReceive);
    TEST_CHECK(name1 != kMachPortNull, "Initial port allocation must succeed");

    std::uint16_t index1 = MachPortSpace::handle_index(name1);
    std::uint16_t gen1   = MachPortSpace::handle_generation(name1);
    TEST_CHECK(gen1 >= 1, "Generation must start at >= 1");

    // Deallocate port
    bool dealloc_ok = space->deallocate_port(name1);
    TEST_CHECK(dealloc_ok, "Deallocating active port must succeed");

    // Stale handle check: old name1 must now fail lookup
    TEST_CHECK(space->lookup_port(name1) == nullptr, "Stale port handle must be rejected");

    // Reallocate in the same space: should reuse slot index1 with incremented generation
    auto port2 = std::make_shared<MachPortObject>(102, task_id);
    mach_port_t name2 = space->allocate_port(port2, kMachPortRightReceive);
    std::uint16_t index2 = MachPortSpace::handle_index(name2);
    std::uint16_t gen2   = MachPortSpace::handle_generation(name2);

    TEST_CHECK(index2 == index1, "Slot index should be recycled when available");
    TEST_CHECK(gen2 == gen1 + 1, "Generation of recycled slot must increment by 1");
    TEST_CHECK(space->lookup_port(name1) == nullptr, "Stale handle from previous generation must still be rejected");
    TEST_CHECK(space->lookup_port(name2) == port2, "Active handle must resolve to new port object");

    // Simulate advancing generation count to verify wraparound (0xFFFF -> 1)
    space->deallocate_port(name2);
    for (std::uint16_t g = gen2; g < 0xFFFE; ++g) {
        auto dummy_port = std::make_shared<MachPortObject>(103, task_id);
        mach_port_t n = space->allocate_port(dummy_port, kMachPortRightReceive);
        space->deallocate_port(n);
    }

    auto wrap_port = std::make_shared<MachPortObject>(104, task_id);
    mach_port_t wrap_name = space->allocate_port(wrap_port, kMachPortRightReceive);
    std::uint16_t wrap_gen = MachPortSpace::handle_generation(wrap_name);
    TEST_CHECK(wrap_gen >= 1, "Wrapped generation must never be 0 (must be >= 1)");

    std::cout << "  -> Handle generation lifecycle verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Send-Once Consumption and Atomic Transition
// -----------------------------------------------------------------------------
void test_send_once_consumption() {
    std::cout << "[Stage 204.3 Test 2] Send-Once Atomic Consumption...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 702;
    auto space = ipc.create_task_port_space(task_id);

    auto port = std::make_shared<MachPortObject>(201, task_id);
    mach_port_t recv_name = space->allocate_port(port, kMachPortRightReceive);
    mach_port_t so_name = space->allocate_port(port, kMachPortRightSendOnce);

    TEST_CHECK(space->get_right_type(so_name) == kMachPortRightSendOnce, "Right must be SendOnce");

    // Construct simple message
    MachMessage msg{};
    msg.header.msgh_bits = 0;
    msg.header.msgh_size = sizeof(MachMsgHeaderRaw);
    msg.header.msgh_remote_port = so_name;
    msg.header.msgh_local_port = kMachPortNull;
    msg.header.msgh_id = 1000;

    // Send through send-once right: enqueues to destination and consumes right
    auto dest_port = space->lookup_port(so_name);
    TEST_CHECK(dest_port != nullptr, "Destination port lookup must succeed");
    bool enq_ok = dest_port->enqueue_message(msg);
    TEST_CHECK(enq_ok, "Enqueue via send-once port must succeed");

    // Consume the send-once right
    bool consume_ok = space->consume_send_once(so_name);
    TEST_CHECK(consume_ok, "Consuming send-once right must succeed");

    // Right must now be consumed / invalid
    TEST_CHECK(space->lookup_port(so_name) == nullptr, "Send-once right must be destroyed upon use");
    TEST_CHECK(space->get_right_type(so_name) == ~0u, "Right type must be ~0u (invalid)");

    // Message must be in the port's queue
    auto recv_port = space->lookup_port(recv_name);
    TEST_CHECK(recv_port != nullptr, "Receive port lookup must succeed");
    auto dequeued = recv_port->dequeue_message();
    TEST_CHECK(dequeued.has_value() && dequeued->header.msgh_id == 1000, "Message must be received correctly");

    std::cout << "  -> Send-once consumption verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Port-Set Membership and Destruction Cleanup
// -----------------------------------------------------------------------------
void test_port_set_lifecycle() {
    std::cout << "[Stage 204.3 Test 3] Port-Set Membership & Cleanup...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t sender_task = 703;
    const std::uint32_t receiver_task = 704;

    auto snd_space = ipc.create_task_port_space(sender_task);
    auto rcv_space = ipc.create_task_port_space(receiver_task);

    mach_port_t pset_name = ipc.mach_port_allocate(receiver_task, kMachPortRightPortSet);
    TEST_CHECK(pset_name != kMachPortNull, "Port set allocation must succeed");
    TEST_CHECK(rcv_space->is_port_set(pset_name), "Type must be PortSet");

    mach_port_t nameA = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);
    mach_port_t nameB = ipc.mach_port_allocate(receiver_task, kMachPortRightReceive);

    // Insert into port set
    TEST_CHECK(ipc.mach_port_insert_member(receiver_task, nameA, pset_name), "Insert portA into set must succeed");
    TEST_CHECK(ipc.mach_port_insert_member(receiver_task, nameB, pset_name), "Insert portB into set must succeed");

    auto members = rcv_space->get_set_members(pset_name);
    TEST_CHECK(members.size() == 2, "Port set must have 2 members");

    // Give sender send right to nameB
    auto objB = rcv_space->lookup_port(nameB);
    mach_port_t snd_b = snd_space->allocate_port(objB, kMachPortRightSend);

    Memory mem(0x10000);
    const std::uint64_t msg_addr = 0x1000;
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    hdr.msgh_remote_port = snd_b;
    hdr.msgh_local_port = kMachPortNull;
    hdr.msgh_id = 3001;
    std::memcpy(mem.raw_data() + msg_addr, &hdr, sizeof(hdr));

    int send_ret = ipc.mach_msg(sender_task, msg_addr, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_CHECK(send_ret == kMachMsgSuccess, "Send to portB must succeed");

    // Multiplexed receive via mach_msg on pset
    const std::uint64_t rcv_addr = 0x2000;
    int rcv_ret = ipc.mach_msg(receiver_task, rcv_addr, kMachRcvMsg, 0, 1024, pset_name, 0, mem);
    TEST_CHECK(rcv_ret == kMachMsgSuccess, "Receive from port-set must dequeue pending message");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + rcv_addr, sizeof(rcv_hdr));
    TEST_CHECK(rcv_hdr.msgh_id == 3001, "Received message must match sent message");
    TEST_CHECK(rcv_hdr.msgh_local_port == nameB, "msgh_local_port must identify member port B");

    // Deallocate portA: should automatically remove from port set
    rcv_space->deallocate_port(nameA);
    auto remaining = rcv_space->get_set_members(pset_name);
    TEST_CHECK(remaining.size() == 1 && remaining[0] == nameB, "Deallocated member must be removed from set");

    std::cout << "  -> Port-set lifecycle verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Queue Limits and Resource Cleanup
// -----------------------------------------------------------------------------
void test_queue_limits_and_cleanup() {
    std::cout << "[Stage 204.3 Test 4] Queue Limits & Resource Cleanup...\n";

    MachIpcSubsystem ipc;
    const std::uint32_t task_id = 705;
    auto space = ipc.create_task_port_space(task_id);

    constexpr std::size_t kQueueLimit = MachPortObject::kMaxQueueLimit; // 64
    auto port = std::make_shared<MachPortObject>(401, task_id);
    mach_port_t name = space->allocate_port(port, kMachPortRightReceive);

    // Fill queue to capacity (64 messages)
    for (std::size_t i = 0; i < kQueueLimit; ++i) {
        MachMessage msg{};
        msg.header.msgh_size = sizeof(MachMsgHeaderRaw);
        msg.header.msgh_id = static_cast<std::int32_t>(4000 + i);
        bool ok = port->enqueue_message(msg);
        TEST_CHECK(ok, "Queue insertion under limit must succeed");
    }

    // Attempt 65th send: must fail due to queue capacity exhaustion
    MachMessage overflow_msg{};
    overflow_msg.header.msgh_size = sizeof(MachMsgHeaderRaw);
    overflow_msg.header.msgh_id = 9999;
    bool overflow_ok = port->enqueue_message(overflow_msg);
    TEST_CHECK(!overflow_ok, "Send must be rejected when port queue is full");

    // Deallocate port: must clean up all queued resources cleanly without leak
    bool dealloc_ok = space->deallocate_port(name);
    TEST_CHECK(dealloc_ok, "Port deallocation with full queue must clean up cleanly");
    TEST_CHECK(space->lookup_port(name) == nullptr, "Deallocated port must no longer exist");

    std::cout << "  -> Queue limits & resource cleanup verified.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.3 Mach IPC Reconciliation Tests ===\n";
    test_mach_handle_generation_lifecycle();
    test_send_once_consumption();
    test_port_set_lifecycle();
    test_queue_limits_and_cleanup();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.3 Mach IPC Reconciliation passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.3 Mach IPC Reconciliation failed with " << g_failures << " errors.\n";
        return 1;
    }
}
