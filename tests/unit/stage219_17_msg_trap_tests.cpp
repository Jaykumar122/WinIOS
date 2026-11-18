#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "winios_sdk/winios_sdk.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_ipc.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

void test_msg_trap_valid_message_delivery() {
    std::cout << "[Test 1] Stage 219.16: Darwin-Inspired Message Trap (SYS 527 / 31) Delivery...\n";

    Memory mem(0x400000); // 4 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "msg_trap_vm");

    uint64_t data_page = ppm.allocate_page();
    const uint64_t data_va = 0x10004000ULL;
    TEST_ASSERT(uas.map_user_page(data_va, data_page, PagePermissions::UserReadWrite));

    BsdProcess proc(50, 1, "sender_proc");
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // Create target IPC port for Receiver (PID 60)
    uint32_t dest_port = GuestIpcRouter::Get().CreatePort(60, "service_port");
    TEST_ASSERT(dest_port != 0);

    // Prepare message with header + payload at data_va
    const char payload[] = "Mach Message Payload Data 2026";
    const uint32_t payload_len = static_cast<uint32_t>(sizeof(payload));
    const uint32_t total_size = static_cast<uint32_t>(sizeof(winios_msg_header_t) + payload_len);

    winios_msg_header_t hdr{};
    hdr.msg_size = total_size;
    hdr.msg_id = 0x4321;
    hdr.src_port = 1;
    hdr.dst_port = dest_port;
    hdr.flags = 0;

    TEST_ASSERT(uas.copyin(data_va, &hdr, sizeof(hdr)));
    TEST_ASSERT(uas.copyin(data_va + sizeof(hdr), payload, payload_len));

    // Invoke SYS_winios_msg_trap (SYS 527)
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, data_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // KERN_SUCCESS

    // Verify receiver receives message through router
    IpcMessage received{};
    TEST_ASSERT(GuestIpcRouter::Get().Receive(60, dest_port, &received));
    TEST_ASSERT(received.sender_pid == 50);
    TEST_ASSERT(received.message_id == 0x4321);
    TEST_ASSERT(received.data.size() == payload_len);
    TEST_ASSERT(std::memcmp(received.data.data(), payload, payload_len) == 0);

    std::cout << "  -> Message trap header parsing, boundary check, and router delivery verified.\n";
}

void test_msg_trap_boundary_and_fault_rejection() {
    std::cout << "[Test 2] Stage 219.17: Message Trap Size Limits & Fault Rejection...\n";

    Memory mem(0x400000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x3C0000ULL, 0x28020ULL);
    TEST_ASSERT(ppm.initialize(false));

    uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "msg_fault_vm");

    uint64_t data_page = ppm.allocate_page();
    const uint64_t data_va = 0x10004000ULL;
    TEST_ASSERT(uas.map_user_page(data_va, data_page, PagePermissions::UserReadWrite));

    BsdProcess proc(50, 1, "sender_proc");
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);
    CPU cpu;
    cpu.reset();

    // 1. Oversized message (> 4096 bytes) must be rejected with EINVAL (22)
    winios_msg_header_t oversized_hdr{};
    oversized_hdr.msg_size = 8192; // Exceeds 4096 limit
    oversized_hdr.msg_id = 1;
    oversized_hdr.dst_port = 1;
    TEST_ASSERT(uas.copyin(data_va, &oversized_hdr, sizeof(oversized_hdr)));

    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, data_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 22); // EINVAL

    // 2. Undersized message (< sizeof(hdr)) must be rejected with EINVAL (22)
    winios_msg_header_t undersized_hdr{};
    undersized_hdr.msg_size = 8;
    undersized_hdr.msg_id = 1;
    TEST_ASSERT(uas.copyin(data_va, &undersized_hdr, sizeof(undersized_hdr)));

    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, data_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 22); // EINVAL

    // 3. Unmapped pointer must be rejected with EFAULT (14)
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, 0x30000000ULL); // unmapped VA
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 14); // EFAULT

    std::cout << "  -> Oversized, undersized, and unmapped message trap invocations rejected cleanly.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.17: Darwin-Inspired Message Trap Tests\n";
    std::cout << "=================================================================\n";

    test_msg_trap_valid_message_delivery();
    test_msg_trap_boundary_and_fault_rejection();

    std::cout << "\n[PASS] All Stage 219.17 tests passed successfully!\n";
    return 0;
}
