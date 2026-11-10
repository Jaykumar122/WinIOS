// =============================================================================
// stage181_mach_ipc_guest_integration_tests.cpp
//
// Stage 181: Mach IPC and Trap Guest Integration
// Verifies Mach message buffer parsing from guest memory, port rights
// translations, bootstrap service lookup, client-server IPC roundtrips,
// complex message descriptors, and native guest ARM64 Mach trap execution.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Mach Message Header Parsing and Staging
// -----------------------------------------------------------------------------
bool test_mach_msg_header_parsing_and_staging() {
    std::cout << "[Test 1] Mach Message Header Parsing and Guest Staging...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);

    std::uint32_t task_id = 10;
    auto space = ipc.create_task_port_space(task_id);
    auto dest_obj = std::make_shared<MachPortObject>(1001, task_id);
    mach_port_t dest_port = space->allocate_port(dest_obj, kMachPortRightReceive);

    // Stage Mach message in guest memory at 0x4000
    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_bits = 0x15;
    send_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 16;
    send_hdr.msgh_remote_port = dest_port;
    send_hdr.msgh_local_port = kMachPortNull;
    send_hdr.msgh_id = 42;

    std::memcpy(mem.raw_data() + 0x4000, &send_hdr, sizeof(send_hdr));
    std::memcpy(mem.raw_data() + 0x4000 + sizeof(send_hdr), "TestDataPayload!", 16);

    // Send
    std::int32_t ret = ipc.mach_msg(task_id, 0x4000, kMachSendMsg, send_hdr.msgh_size, 0, 0, 0, mem);
    TEST_ASSERT(ret == kMachMsgSuccess, "mach_msg send must succeed");
    TEST_ASSERT(dest_obj->has_messages(), "dest_obj must have queued message");

    // Receive into 0x8000
    ret = ipc.mach_msg(task_id, 0x8000, kMachRcvMsg, 0, 0x1000, dest_port, 0, mem);
    TEST_ASSERT(ret == kMachMsgSuccess, "mach_msg receive must succeed");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x8000, sizeof(rcv_hdr));
    TEST_ASSERT(rcv_hdr.msgh_id == 42, "msgh_id mismatch");
    TEST_ASSERT(rcv_hdr.msgh_size == send_hdr.msgh_size, "msgh_size mismatch");

    char payload[17]{0};
    std::memcpy(payload, mem.raw_data() + 0x8000 + sizeof(rcv_hdr), 16);
    TEST_ASSERT(std::string(payload) == "TestDataPayload!", "Payload mismatch");

    std::cout << "  Passed (Validated header serialization, queuing, and retrieval)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Mach Port Rights Translation
// -----------------------------------------------------------------------------
bool test_mach_port_rights_translation() {
    std::cout << "[Test 2] Cross-Task Port Rights Translation...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);

    std::uint32_t client_task = 100;
    std::uint32_t server_task = 200;

    auto client_space = ipc.create_task_port_space(client_task);
    auto server_space = ipc.create_task_port_space(server_task);

    // Server creates service port
    auto service_obj = std::make_shared<MachPortObject>(5001, server_task);
    mach_port_t server_service_port = server_space->allocate_port(service_obj, kMachPortRightReceive);

    // Client gets send right to server service port
    mach_port_t client_dest_port = client_space->allocate_port(service_obj, kMachPortRightSend);

    // Client allocates a local port first to ensure namespace divergence
    auto dummy_obj = std::make_shared<MachPortObject>(9999, client_task);
    (void)client_space->allocate_port(dummy_obj, kMachPortRightSend);

    // Client creates a reply port
    auto reply_obj = std::make_shared<MachPortObject>(5002, client_task);
    mach_port_t client_reply_port = client_space->allocate_port(reply_obj, kMachPortRightReceive);

    // Client sends message specifying client_reply_port as msgh_local_port
    MachMsgHeaderRaw send_hdr{};
    send_hdr.msgh_bits = 0x15;
    send_hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    send_hdr.msgh_remote_port = client_dest_port;
    send_hdr.msgh_local_port = client_reply_port;
    send_hdr.msgh_id = 1001;

    std::memcpy(mem.raw_data() + 0x1000, &send_hdr, sizeof(send_hdr));
    TEST_ASSERT(ipc.mach_msg(client_task, 0x1000, kMachSendMsg, sizeof(send_hdr), 0, 0, 0, mem) == kMachMsgSuccess,
                "Send must succeed");

    // Server receives message
    TEST_ASSERT(ipc.mach_msg(server_task, 0x2000, kMachRcvMsg, 0, 0x1000, server_service_port, 0, mem) == kMachMsgSuccess,
                "Receive must succeed");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x2000, sizeof(rcv_hdr));

    // In server space: remote_port must be translated to a valid send right pointing to reply_obj
    TEST_ASSERT(rcv_hdr.msgh_remote_port != kMachPortNull, "Receiver remote port must not be null");
    TEST_ASSERT(rcv_hdr.msgh_remote_port != client_reply_port, "Port number must be localized to server space");

    auto translated_obj = server_space->lookup_port(rcv_hdr.msgh_remote_port);
    TEST_ASSERT(translated_obj != nullptr && translated_obj->id() == reply_obj->id(),
                "Translated port must reference the original reply_obj");

    std::cout << "  Passed (Translated client reply port right into server port space)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Bootstrap Server Registration and Lookup
// -----------------------------------------------------------------------------
bool test_bootstrap_server_registration_and_lookup() {
    std::cout << "[Test 3] Bootstrap Service Registration and Lookup...\n";

    MachIpcSubsystem ipc;

    std::uint32_t daemon_task = 50;
    std::uint32_t client_task = 60;

    auto daemon_space = ipc.create_task_port_space(daemon_task);
    auto daemon_obj = std::make_shared<MachPortObject>(9001, daemon_task);
    mach_port_t daemon_port = daemon_space->allocate_port(daemon_obj, kMachPortRightReceive);

    // Register with bootstrap server
    bool reg_ok = ipc.bootstrap_register(daemon_task, "com.apple.windowserver", daemon_port);
    TEST_ASSERT(reg_ok, "bootstrap_register must succeed");

    // Client looks up service
    mach_port_t looked_up = ipc.bootstrap_look_up(client_task, "com.apple.windowserver");
    TEST_ASSERT(looked_up != kMachPortNull, "bootstrap_look_up must find registered service");

    auto client_space = ipc.get_task_port_space(client_task);
    TEST_ASSERT(client_space != nullptr, "Client space must exist");
    auto resolved_obj = client_space->lookup_port(looked_up);
    TEST_ASSERT(resolved_obj != nullptr && resolved_obj->id() == daemon_obj->id(),
                "Looked up port must point to daemon object");

    // Lookup of nonexistent service returns kMachPortNull
    mach_port_t missing = ipc.bootstrap_look_up(client_task, "com.apple.nonexistent");
    TEST_ASSERT(missing == kMachPortNull, "Missing service must return kMachPortNull");

    std::cout << "  Passed (Registered and resolved com.apple.windowserver port)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Bidirectional Client-Server IPC Roundtrip
// -----------------------------------------------------------------------------
bool test_bidirectional_client_server_ipc_roundtrip() {
    std::cout << "[Test 4] Bidirectional Client-Server IPC Roundtrip...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);

    std::uint32_t client_pid = 10;
    std::uint32_t server_pid = 20;

    auto client_space = ipc.create_task_port_space(client_pid);
    auto server_space = ipc.create_task_port_space(server_pid);

    auto srv_obj = std::make_shared<MachPortObject>(8001, server_pid);
    mach_port_t srv_port = server_space->allocate_port(srv_obj, kMachPortRightReceive);
    ipc.bootstrap_register(server_pid, "com.apple.echo_service", srv_port);

    // 1. Client connects to service and sends request
    mach_port_t client_to_srv = ipc.bootstrap_look_up(client_pid, "com.apple.echo_service");
    mach_port_t client_reply = ipc.mach_reply_port(client_pid);

    MachMsgHeaderRaw req{};
    req.msgh_bits = 0x15;
    req.msgh_size = sizeof(MachMsgHeaderRaw) + 8;
    req.msgh_remote_port = client_to_srv;
    req.msgh_local_port = client_reply;
    req.msgh_id = 0x100;

    std::memcpy(mem.raw_data() + 0x1000, &req, sizeof(req));
    std::memcpy(mem.raw_data() + 0x1000 + sizeof(req), "PING1234", 8);

    TEST_ASSERT(ipc.mach_msg(client_pid, 0x1000, kMachSendMsg, req.msgh_size, 0, 0, 0, mem) == kMachMsgSuccess,
                "Client send failed");

    // 2. Server receives request
    TEST_ASSERT(ipc.mach_msg(server_pid, 0x2000, kMachRcvMsg, 0, 0x1000, srv_port, 0, mem) == kMachMsgSuccess,
                "Server receive failed");

    MachMsgHeaderRaw srv_rcv{};
    std::memcpy(&srv_rcv, mem.raw_data() + 0x2000, sizeof(srv_rcv));
    TEST_ASSERT(srv_rcv.msgh_id == 0x100, "Server received wrong msgh_id");

    // 3. Server sends reply back to translated srv_rcv.msgh_remote_port
    MachMsgHeaderRaw resp{};
    resp.msgh_bits = 0x15;
    resp.msgh_size = sizeof(MachMsgHeaderRaw) + 8;
    resp.msgh_remote_port = srv_rcv.msgh_remote_port; // reply port
    resp.msgh_local_port = kMachPortNull;
    resp.msgh_id = 0x200;

    std::memcpy(mem.raw_data() + 0x3000, &resp, sizeof(resp));
    std::memcpy(mem.raw_data() + 0x3000 + sizeof(resp), "PONG1234", 8);

    TEST_ASSERT(ipc.mach_msg(server_pid, 0x3000, kMachSendMsg, resp.msgh_size, 0, 0, 0, mem) == kMachMsgSuccess,
                "Server reply failed");

    // 4. Client receives reply
    TEST_ASSERT(ipc.mach_msg(client_pid, 0x4000, kMachRcvMsg, 0, 0x1000, client_reply, 0, mem) == kMachMsgSuccess,
                "Client receive reply failed");

    MachMsgHeaderRaw client_rcv{};
    std::memcpy(&client_rcv, mem.raw_data() + 0x4000, sizeof(client_rcv));
    TEST_ASSERT(client_rcv.msgh_id == 0x200, "Client received wrong reply id");

    char reply_buf[9]{0};
    std::memcpy(reply_buf, mem.raw_data() + 0x4000 + sizeof(client_rcv), 8);
    TEST_ASSERT(std::string(reply_buf) == "PONG1234", "Reply payload mismatch");

    std::cout << "  Passed (Full bidirectional PING/PONG roundtrip completed)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: mach_msg Timeout and Error Handling
// -----------------------------------------------------------------------------
bool test_mach_msg_timeout_and_error_handling() {
    std::cout << "[Test 5] mach_msg Error Conditions and Timeouts...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);

    std::uint32_t task_id = 30;
    auto space = ipc.create_task_port_space(task_id);
    auto obj = std::make_shared<MachPortObject>(3001, task_id);
    mach_port_t port = space->allocate_port(obj, kMachPortRightReceive);

    // 1. Receive from empty port -> kMachRcvTimedOut
    std::int32_t ret = ipc.mach_msg(task_id, 0x1000, kMachRcvMsg, 0, 0x100, port, 0, mem);
    TEST_ASSERT(ret == kMachRcvTimedOut, "Empty receive must report kMachRcvTimedOut");

    // 2. Send to nonexistent port -> kMachSendInvalidDest
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(hdr);
    hdr.msgh_remote_port = 99999;
    std::memcpy(mem.raw_data() + 0x1000, &hdr, sizeof(hdr));

    ret = ipc.mach_msg(task_id, 0x1000, kMachSendMsg, sizeof(hdr), 0, 0, 0, mem);
    TEST_ASSERT(ret == kMachSendInvalidDest, "Bad dest must report kMachSendInvalidDest");

    // 3. Receive with too small buffer -> kMachRcvTooLarge
    hdr.msgh_remote_port = port;
    hdr.msgh_size = sizeof(hdr) + 64;
    std::memcpy(mem.raw_data() + 0x1000, &hdr, sizeof(hdr));
    (void)ipc.mach_msg(task_id, 0x1000, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);

    // Try receive with size = sizeof(hdr) (smaller than total size)
    ret = ipc.mach_msg(task_id, 0x2000, kMachRcvMsg, 0, sizeof(hdr), port, 0, mem);
    TEST_ASSERT(ret == kMachRcvTooLarge, "Undersized receive buffer must report kMachRcvTooLarge");

    std::cout << "  Passed (Enforced Mach IPC error codes and timeout states)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Complex Message Descriptors
// -----------------------------------------------------------------------------
bool test_mach_complex_message_descriptors() {
    std::cout << "[Test 6] Complex Message Descriptors (Port & OOL)...\n";

    TEST_ASSERT(sizeof(MachMsgPortDescriptorRaw) == 12, "MachMsgPortDescriptorRaw size mismatch");
    TEST_ASSERT(sizeof(MachMsgOolDescriptorRaw) == 16, "MachMsgOolDescriptorRaw size mismatch");
    TEST_ASSERT(MACH_MSGH_BITS_COMPLEX == 0x80000000u, "Complex bit flag mismatch");

    std::cout << "  Passed (Validated Darwin Mach complex descriptor structures)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Native Guest Instruction Mach Trap Execution
// -----------------------------------------------------------------------------
bool test_guest_instruction_mach_trap_dispatch() {
    std::cout << "[Test 7] Guest Instruction Mach Trap Execution (SVC #0x81)...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Execute mach_reply_port_trap (-26) via guest instructions:
    // movn x16, #25 (~(-26) = 25)
    // svc #0x81
    // ret
    std::vector<std::uint32_t> code = {
        0x92800330u, // movn x16, #25
        0xD4001021u, // svc #0x81
        0xD65F03C0u  // ret
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x1000 + (i * 4), code[i]);
    }

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.write_register(30, 0x6000); // LR

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 1: movn x16, #25
    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(static_cast<std::int64_t>(cpu.read_register(16)) == -26, "X16 must be -26");

    // Step 2: svc #0x81 -> handles mach_reply_port_trap
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(result.mach_traps_handled == 1, "Must handle 1 mach trap");
    mach_port_t reply_port = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(reply_port != kMachPortNull, "Must return valid reply port");

    // Step 3: ret
    TEST_ASSERT(engine.step_one(result, cfg), "Step 3 failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x6000, "PC must be LR after ret");

    std::cout << "  Passed (Executed native Mach trap via SVC #0x81 instruction)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 181: MACH IPC AND TRAP GUEST INTEGRATION SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_mach_msg_header_parsing_and_staging()) passed++; else failed++;
    if (test_mach_port_rights_translation()) passed++; else failed++;
    if (test_bootstrap_server_registration_and_lookup()) passed++; else failed++;
    if (test_bidirectional_client_server_ipc_roundtrip()) passed++; else failed++;
    if (test_mach_msg_timeout_and_error_handling()) passed++; else failed++;
    if (test_mach_complex_message_descriptors()) passed++; else failed++;
    if (test_guest_instruction_mach_trap_dispatch()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 181 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
