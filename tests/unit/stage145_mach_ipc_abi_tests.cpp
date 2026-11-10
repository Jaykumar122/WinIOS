#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
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

void setup_guest_cpu(CPU& cpu, Memory& mem, std::uint64_t code_addr = 0x1000, std::uint64_t vbar_el1 = 0x10000) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar_el1);
    mem.write_u32(vbar_el1 + 0x400, 0xD503201Fu); // NOP at lower EL sync vector
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x40000);
    cpu.set_sp_el1(0x50000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(code_addr);
}

// Test 1: task_self_trap (-28) and mach_reply_port_trap (-26)
bool test_task_self_and_reply_port_traps() {
    std::cout << "[Test 1] task_self_trap (-28) and mach_reply_port_trap (-26)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. task_self_trap (-28)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0x92800370u); // movn x16, #27 (x16 = -28)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    mach_port_t self_port = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(self_port != kMachPortNull, "task_self_trap must return valid port");
    TEST_ASSERT(result.mach_traps_handled == 1, "Must handle 1 Mach trap");

    // 2. mach_reply_port_trap (-26)
    setup_guest_cpu(cpu, mem, 0x1010);
    mem.write_u32(0x1010, 0x92800330u); // movn x16, #25 (x16 = -26)
    mem.write_u32(0x1014, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    mach_port_t reply_port = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(reply_port != kMachPortNull, "mach_reply_port_trap must return valid port");
    TEST_ASSERT(reply_port != self_port, "Reply port must differ from task_self port");
    TEST_ASSERT(result.mach_traps_handled == 2, "Must handle 2 Mach traps");

    return true;
}

// Test 2: Guest mach_msg_trap (-31) Send and In-Memory Queue Verification
bool test_mach_msg_send_and_queue() {
    std::cout << "[Test 2] mach_msg_trap (-31) Send & In-Memory Queue Verification...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Initialize task 1 port space and register a target service port
    auto space = ipc.create_task_port_space(1);
    auto service_obj = std::make_shared<MachPortObject>(777, 1);
    mach_port_t dest_port = space->allocate_port(service_obj, kMachPortRightReceive);

    // Construct Mach message in guest memory at 0x3000
    // Header (24 bytes) + Payload ("PING", 4 bytes) = 28 bytes
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0x15;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    hdr.msgh_remote_port = dest_port;
    hdr.msgh_local_port = 0;
    hdr.msgh_id = 1001;
    std::memcpy(mem.raw_data() + 0x3000, &hdr, sizeof(hdr));
    std::memcpy(mem.raw_data() + 0x3000 + sizeof(hdr), "PING", 4);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Invoke mach_msg_trap (-31)
    // Args: x0=0x3000, x1=kMachSendMsg (1), x2=28 (send_size), x3=0, x4=0, x5=0
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, 0x3000);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, 28);
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);
    mem.write_u32(0x1000, 0x928003D0u); // movn x16, #30 (x16 = -31)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int32_t ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachMsgSuccess, "mach_msg send must return MACH_MSG_SUCCESS (0)");

    // Verify queue in destination port object
    TEST_ASSERT(service_obj->has_messages(), "Destination port object must have queued message");
    TEST_ASSERT(service_obj->queue_size() == 1, "Queue size must be 1");

    return true;
}

// Test 3: Guest mach_msg_trap Receive and Memory Writing
bool test_mach_msg_receive_into_guest_memory() {
    std::cout << "[Test 3] mach_msg_trap (-31) Receive and Memory Writing...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto space = ipc.create_task_port_space(1);
    auto service_obj = std::make_shared<MachPortObject>(888, 1);
    mach_port_t rcv_port = space->allocate_port(service_obj, kMachPortRightReceive);

    // Pre-enqueue a message into the port: header + "PONG_DATA"
    MachMessage msg{};
    msg.header.msgh_bits = 0x12;
    msg.header.msgh_size = sizeof(MachMsgHeaderRaw) + 9;
    msg.header.msgh_remote_port = 0;
    msg.header.msgh_local_port = rcv_port;
    msg.header.msgh_id = 2002;
    const char payload[] = "PONG_DATA";
    msg.payload.assign(payload, payload + 9);
    service_obj->enqueue_message(std::move(msg));

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Invoke mach_msg_trap with MACH_RCV_MSG (2)
    // Destination buffer in guest memory at 0x6000, rcv_size = 128
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, 0x6000);
    cpu.write_register(1, kMachRcvMsg);
    cpu.write_register(2, 0);   // send_size
    cpu.write_register(3, 128); // rcv_size
    cpu.write_register(4, rcv_port);
    cpu.write_register(5, 0);
    mem.write_u32(0x1000, 0x928003D0u); // movn x16, #30 (-31)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int32_t ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachMsgSuccess, "mach_msg receive must return MACH_MSG_SUCCESS (0)");

    // Inspect guest memory at 0x6000
    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x6000, sizeof(rcv_hdr));
    TEST_ASSERT(rcv_hdr.msgh_id == 2002, "Received header msgh_id must be 2002");
    TEST_ASSERT(rcv_hdr.msgh_size == sizeof(MachMsgHeaderRaw) + 9, "Received msgh_size must be 33");

    char rcv_payload[10] = {0};
    std::memcpy(rcv_payload, mem.raw_data() + 0x6000 + sizeof(rcv_hdr), 9);
    TEST_ASSERT(std::string(rcv_payload) == "PONG_DATA", "Received payload must equal PONG_DATA");

    return true;
}

// Test 4: FIFO Message Queue Ordering
bool test_mach_msg_fifo_queue_ordering() {
    std::cout << "[Test 4] FIFO Message Queue Ordering...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto space = ipc.create_task_port_space(1);
    auto queue_obj = std::make_shared<MachPortObject>(999, 1);
    mach_port_t port = space->allocate_port(queue_obj, kMachPortRightReceive);

    // Enqueue 3 messages with IDs 101, 102, 103
    for (int id = 101; id <= 103; ++id) {
        MachMessage m{};
        m.header.msgh_id = id;
        m.header.msgh_size = sizeof(MachMsgHeaderRaw);
        queue_obj->enqueue_message(std::move(m));
    }

    TEST_ASSERT(queue_obj->queue_size() == 3, "Queue size must be 3");

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Dequeue sequentially through mach_msg_trap
    for (int expected_id = 101; expected_id <= 103; ++expected_id) {
        setup_guest_cpu(cpu, mem, 0x1000);
        cpu.write_register(0, 0x7000);
        cpu.write_register(1, kMachRcvMsg);
        cpu.write_register(2, 0);
        cpu.write_register(3, 128);
        cpu.write_register(4, port);
        cpu.write_register(5, 0);
        mem.write_u32(0x1000, 0x928003D0u); // movn x16, #30 (-31)
        mem.write_u32(0x1004, 0xD4001021u);

        engine.step_one(result, config);
        engine.step_one(result, config);

        TEST_ASSERT(cpu.read_register(0) == 0, "Receive must succeed");
        MachMsgHeaderRaw h{};
        std::memcpy(&h, mem.raw_data() + 0x7000, sizeof(h));
        TEST_ASSERT(h.msgh_id == expected_id, "Messages must dequeue in strict FIFO order");
    }

    TEST_ASSERT(queue_obj->queue_size() == 0, "Queue must be empty after 3 receives");

    return true;
}

// Test 5: Error Propagation (Invalid Dest, Invalid Data, Timed Out, Too Large)
bool test_mach_msg_error_propagation() {
    std::cout << "[Test 5] mach_msg Error Propagation (Invalid Dest, Size, Timeout, Too Large)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto space = ipc.create_task_port_space(1);
    auto port_obj = std::make_shared<MachPortObject>(1234, 1);
    mach_port_t port = space->allocate_port(port_obj, kMachPortRightReceive);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Send with invalid destination port (port 99999 does not exist)
    setup_guest_cpu(cpu, mem, 0x1000);
    MachMsgHeaderRaw bad_dest_hdr{};
    bad_dest_hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    bad_dest_hdr.msgh_remote_port = 99999;
    std::memcpy(mem.raw_data() + 0x3000, &bad_dest_hdr, sizeof(bad_dest_hdr));

    cpu.write_register(0, 0x3000);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, sizeof(MachMsgHeaderRaw));
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);
    mem.write_u32(0x1000, 0x928003D0u);
    mem.write_u32(0x1004, 0xD4001021u);

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int32_t ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachSendInvalidDest, "Send to nonexistent port must return MACH_SEND_INVALID_DEST");

    // 2. Send with send_size < sizeof(MachMsgHeaderRaw) (10 bytes)
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, 0x3000);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, 10); // truncated
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);
    mem.write_u32(0x1010, 0x928003D0u);
    mem.write_u32(0x1014, 0xD4001021u);

    engine.step_one(result, config);
    engine.step_one(result, config);

    ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachSendInvalidData, "Send with truncated size must return MACH_SEND_INVALID_DATA");

    // 3. Receive on empty queue -> returns MACH_RCV_TIMED_OUT
    setup_guest_cpu(cpu, mem, 0x1020);
    cpu.write_register(0, 0x4000);
    cpu.write_register(1, kMachRcvMsg);
    cpu.write_register(2, 0);
    cpu.write_register(3, 128);
    cpu.write_register(4, port); // empty queue
    cpu.write_register(5, 0);
    mem.write_u32(0x1020, 0x928003D0u);
    mem.write_u32(0x1024, 0xD4001021u);

    engine.step_one(result, config);
    engine.step_one(result, config);

    ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachRcvTimedOut, "Receive on empty queue must return MACH_RCV_TIMED_OUT");

    // 4. Receive with buffer too small (message size = 40, rcv_size = 28)
    MachMessage large_msg{};
    large_msg.header.msgh_size = sizeof(MachMsgHeaderRaw) + 16;
    large_msg.payload.resize(16, 0xAA);
    port_obj->enqueue_message(std::move(large_msg));

    setup_guest_cpu(cpu, mem, 0x1030);
    cpu.write_register(0, 0x4000);
    cpu.write_register(1, kMachRcvMsg);
    cpu.write_register(2, 0);
    cpu.write_register(3, sizeof(MachMsgHeaderRaw)); // only room for header, not 16-byte payload
    cpu.write_register(4, port);
    cpu.write_register(5, 0);
    mem.write_u32(0x1030, 0x928003D0u);
    mem.write_u32(0x1034, 0xD4001021u);

    engine.step_one(result, config);
    engine.step_one(result, config);

    ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachRcvTooLarge, "Receive with undersized buffer must return MACH_RCV_TOO_LARGE");

    return true;
}

// Test 6: Task Port Space Isolation
bool test_task_port_space_isolation() {
    std::cout << "[Test 6] Task Port Space Isolation between Tasks...\n";

    MachIpcSubsystem ipc;
    auto space1 = ipc.create_task_port_space(1);
    auto space2 = ipc.create_task_port_space(2);

    auto obj1 = std::make_shared<MachPortObject>(100, 1);
    mach_port_t name1 = space1->allocate_port(obj1, kMachPortRightReceive);

    // Verify task 2 cannot look up task 1's local port name
    TEST_ASSERT(space2->lookup_port(name1) == nullptr, "Task 2 must NOT resolve Task 1 local port name");
    TEST_ASSERT(space1->lookup_port(name1) == obj1, "Task 1 must resolve its own port");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 145: MACH TRAP & IPC ABI VERIFICATION SUITE       \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_task_self_and_reply_port_traps, "task_self_trap and mach_reply_port_trap");
    run_test(test_mach_msg_send_and_queue, "mach_msg_trap Send and In-Memory Queue");
    run_test(test_mach_msg_receive_into_guest_memory, "mach_msg_trap Receive into Guest Memory");
    run_test(test_mach_msg_fifo_queue_ordering, "FIFO Message Queue Ordering");
    run_test(test_mach_msg_error_propagation, "mach_msg Error Propagation");
    run_test(test_task_port_space_isolation, "Task Port Space Isolation");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 145 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
