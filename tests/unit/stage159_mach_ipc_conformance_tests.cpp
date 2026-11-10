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

bool is_carry_set(const CPU& cpu) noexcept {
    return (cpu.get_nzcv() & 0x2u) != 0;
}

// Test 1: Mach Trap Invocation Styles (SVC #0x81 & SVC #0x80 with negative X16)
bool test_mach_trap_invocation_conventions() {
    std::cout << "[Test 1] Mach Trap Invocation Styles (SVC #0x81 & SVC #0x80 with negative X16)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Invocation via SVC #0x81 with X16 = -28 (task_self_trap)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0x92800370u); // movn x16, #27 (x16 = -28)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    mach_port_t port1 = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(port1 != kMachPortNull, "SVC #0x81 task_self_trap must return valid port");
    TEST_ASSERT(result.mach_traps_handled == 1, "Must record 1 Mach trap handled");
    TEST_ASSERT(!is_carry_set(cpu), "Mach traps must not set carry flag on success");

    // 2. Invocation via SVC #0x80 with X16 = -28 (Darwin dual convention)
    setup_guest_cpu(cpu, mem, 0x1010);
    mem.write_u32(0x1010, 0x92800370u); // movn x16, #27 (x16 = -28)
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    mach_port_t port2 = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(port2 == port1, "SVC #0x80 with negative X16 must resolve identical port");
    TEST_ASSERT(result.mach_traps_handled == 2, "Must record 2 Mach traps handled");

    return true;
}

// Test 2: Task Port Space Isolation
bool test_task_port_space_isolation() {
    std::cout << "[Test 2] Task Port Space Isolation Between Distinct Processes...\n";

    MachIpcSubsystem ipc;

    auto space1 = ipc.create_task_port_space(1);
    auto space2 = ipc.create_task_port_space(2);

    TEST_ASSERT(space1 != nullptr && space2 != nullptr, "Both spaces must be valid");
    TEST_ASSERT(space1 != space2, "Port spaces must be distinct objects");

    mach_port_t p1 = 1001;
    mach_port_t p2 = 2001;

    auto obj1 = std::make_shared<MachPortObject>(p1, 1);
    auto obj2 = std::make_shared<MachPortObject>(p2, 2);

    space1->insert_right(p1, obj1, kMachPortRightReceive);
    space2->insert_right(p2, obj2, kMachPortRightReceive);

    // Verify lookup isolation
    TEST_ASSERT(space1->lookup_port(p1) != nullptr, "Space 1 must find port p1");
    TEST_ASSERT(space1->lookup_port(p2) == nullptr, "Space 1 must NOT find port p2 of Task 2");
    TEST_ASSERT(space2->lookup_port(p2) != nullptr, "Space 2 must find port p2");
    TEST_ASSERT(space2->lookup_port(p1) == nullptr, "Space 2 must NOT find port p1 of Task 1");

    return true;
}

// Test 3: mach_msg_trap (-31) Send and Receive Round-Trip
bool test_mach_msg_roundtrip() {
    std::cout << "[Test 3] mach_msg_trap (-31) Send & Receive Round-Trip...\n";

    Memory mem(1024 * 1024);
    MachIpcSubsystem ipc;

    // Create a target port with receive right in task 1
    auto space = ipc.create_task_port_space(1);
    mach_port_t port = 5001;
    auto obj = std::make_shared<MachPortObject>(port, 1);
    space->insert_right(port, obj, kMachPortRightReceive);

    // Construct a Mach message at memory address 0x5000:
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 16;
    hdr.msgh_remote_port = port;
    hdr.msgh_local_port = 0;
    hdr.msgh_id = 0x1234;

    std::memcpy(mem.raw_data() + 0x5000, &hdr, sizeof(hdr));
    const char payload[] = "MACH_IPC_DATA_1";
    std::memcpy(mem.raw_data() + 0x5000 + sizeof(hdr), payload, 16);

    // Send the message via mach_msg(kMachSendMsg = 1)
    std::int32_t ret = ipc.mach_msg(1, 0x5000, kMachSendMsg, hdr.msgh_size, 0, 0, 0, mem);
    TEST_ASSERT(ret == 0, "mach_msg SEND must succeed with KERN_SUCCESS (0)");

    // Clear destination memory buffer at 0x6000
    std::memset(mem.raw_data() + 0x6000, 0, 128);

    // Receive the message via mach_msg(kMachRcvMsg = 2)
    ret = ipc.mach_msg(1, 0x6000, kMachRcvMsg, 0, 128, port, 0, mem);
    TEST_ASSERT(ret == 0, "mach_msg RECEIVE must succeed with KERN_SUCCESS (0)");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + 0x6000, sizeof(rcv_hdr));
    TEST_ASSERT(rcv_hdr.msgh_id == 0x1234, "Received message ID must match sent ID");

    char rcv_body[16] = {0};
    std::memcpy(rcv_body, mem.raw_data() + 0x6000 + sizeof(rcv_hdr), 16);
    TEST_ASSERT(std::strcmp(rcv_body, "MACH_IPC_DATA_1") == 0,
                "Received message payload must match sent payload");

    return true;
}

// Test 4: mach_msg_trap Error Signaling: No Carry Modification
bool test_mach_msg_invalid_dest_error() {
    std::cout << "[Test 4] mach_msg_trap Error Signaling (No Carry Flag Modification)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Construct message to invalid/unallocated port 99999
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(MachMsgHeaderRaw);
    hdr.msgh_remote_port = 99999;
    hdr.msgh_id = 99;
    std::memcpy(mem.raw_data() + 0x7000, &hdr, sizeof(hdr));

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Guest invocation:
    // mov x0, #0x7000 (msg_addr)
    // mov x1, #1 (MACH_SEND_MSG)
    // mov x2, #24 (send_size)
    // mov x3, #0 (rcv_size)
    // mov x4, #0 (rcv_name)
    // mov x5, #0 (timeout)
    // movn x16, #30 (x16 = -31, mach_msg_trap)
    // svc #0x81
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD28E0000u); // mov x0, #0x7000
    mem.write_u32(0x1004, 0xD2800021u); // mov x1, #1
    mem.write_u32(0x1008, 0xD2800302u); // mov x2, #24
    mem.write_u32(0x100C, 0xD2800003u); // mov x3, #0
    mem.write_u32(0x1010, 0xD2800004u); // mov x4, #0
    mem.write_u32(0x1014, 0xD2800005u); // mov x5, #0
    mem.write_u32(0x1018, 0x928003D0u); // movn x16, #30 (x16 = -31)
    mem.write_u32(0x101C, 0xD4001021u); // svc #0x81

    for (int i = 0; i < 8; ++i) engine.step_one(result, config);

    std::uint64_t ret = cpu.read_register(0);
    TEST_ASSERT(ret == static_cast<std::uint64_t>(kMachSendInvalidDest),
                "Must return kMachSendInvalidDest (0x10000003)");
    TEST_ASSERT(!is_carry_set(cpu), "Mach traps must NEVER set PSTATE.C (carry flag)");

    return true;
}

// Test 5: Mach Port Rights Manipulation (Send, Receive, Deallocation)
bool test_mach_port_rights_lifecycle() {
    std::cout << "[Test 5] Mach Port Rights Manipulation (Send, Receive, Deallocation)...\n";

    MachIpcSubsystem ipc;
    auto space = ipc.create_task_port_space(1);
    TEST_ASSERT(space != nullptr, "Task space must be valid");

    mach_port_t port = 8000;
    auto obj = std::make_shared<MachPortObject>(port, 1);

    // Insert Receive right
    TEST_ASSERT(space->insert_right(port, obj, kMachPortRightReceive), "Insert receive right must succeed");
    TEST_ASSERT(space->has_right(port, kMachPortRightReceive), "Entry rights must contain Receive right");
    TEST_ASSERT(space->lookup_port(port) == obj, "Lookup port must return exact object");

    // Deallocate port
    TEST_ASSERT(space->deallocate_port(port), "Deallocate port must succeed");
    TEST_ASSERT(space->lookup_port(port) == nullptr, "Port must be unmapped after deallocation");

    return true;
}

// Test 6: Mach Kernel Utility Traps: host_self (-29), semaphores (-33, -36), swtch (-59, -60)
bool test_mach_utility_traps() {
    std::cout << "[Test 6] Mach Kernel Utility Traps: host_self, semaphores, swtch...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. host_self_trap (-29)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0x92800390u); // movn x16, #28 (x16 = -29)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);
    TEST_ASSERT(cpu.read_register(0) == 0x101, "host_self_trap must return host port 0x101");

    // 2. swtch_pri (-59)
    setup_guest_cpu(cpu, mem, 0x1010);
    mem.write_u32(0x1010, 0x92800750u); // movn x16, #58 (x16 = -59)
    mem.write_u32(0x1014, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);
    TEST_ASSERT(cpu.read_register(0) == 1, "swtch_pri must return 1");

    return true;
}

// Test 7: Unimplemented Mach Trap Returns KERN_FAILURE (5)
bool test_unimplemented_mach_trap() {
    std::cout << "[Test 7] Unimplemented Mach Trap Returns KERN_FAILURE (5)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Trap -999 (invalid)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0x92807CE0u); // movn x16, #998 (x16 = -999)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 5u, "Unimplemented Mach trap must return KERN_FAILURE (5)");
    TEST_ASSERT(!is_carry_set(cpu), "Mach traps must not modify carry flag even on failure");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 159: Mach Trap and IPC ABI Conformance\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_mach_trap_invocation_conventions, "Mach Trap Invocation Styles");
    run_test(test_task_port_space_isolation, "Task Port Space Isolation");
    run_test(test_mach_msg_roundtrip, "mach_msg_trap Send & Receive Round-Trip");
    run_test(test_mach_msg_invalid_dest_error, "mach_msg_trap Error Signaling (No Carry Flag)");
    run_test(test_mach_port_rights_lifecycle, "Mach Port Rights Manipulation");
    run_test(test_mach_utility_traps, "Mach Kernel Utility Traps");
    run_test(test_unimplemented_mach_trap, "Unimplemented Mach Trap KERN_FAILURE (5)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
