// ============================================================================
// Stage 220.16 Unit Tests: Message Trap, Mach IPC Ports & Access Control
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>
#include <cstring>

#include "cpu/cpu.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/user_syscall_dispatcher.h"
#include "runtime/guest_ipc.h"
#include "winios_sdk/winios_sdk.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::memory;

static void test_port_lifecycle_and_access_rights() {
    std::cout << "[Test 1] Port creation, access control, and ownership isolation...\n";
    GuestIpcRouter& router = GuestIpcRouter::Get();

    // Process 101 creates a port
    uint32_t port_a = router.CreatePort(101, "com.apple.service.a");
    TEST_ASSERT(port_a != 0);

    // Process 102 creates another port
    uint32_t port_b = router.CreatePort(102, "com.apple.service.b");
    TEST_ASSERT(port_b != 0 && port_b != port_a);

    // Process 102 sends message to port_a
    const char payload[] = "MachMsg Payload 1234";
    TEST_ASSERT(router.Send(102, port_a, payload, sizeof(payload), 0x55AA));

    // Process 102 tries to receive from port_a -> Must FAIL (only owner 101 can receive)
    IpcMessage msg{};
    TEST_ASSERT(!router.Receive(102, port_a, &msg));

    // Process 101 receives from port_a -> Succeeded!
    TEST_ASSERT(router.Receive(101, port_a, &msg));
    TEST_ASSERT(msg.sender_pid == 102);
    TEST_ASSERT(msg.target_port == port_a);
    TEST_ASSERT(msg.message_id == 0x55AA);
    TEST_ASSERT(msg.data.size() == sizeof(payload));
    TEST_ASSERT(std::memcmp(msg.data.data(), payload, sizeof(payload)) == 0);

    // Process 102 tries to destroy port_a -> Must FAIL (not owner)
    TEST_ASSERT(!router.DestroyPort(port_a, 102));

    // Process 101 destroys port_a -> Succeeded
    TEST_ASSERT(router.DestroyPort(port_a, 101));

    // Cleanup process 102 ports
    router.CleanupProcess(102);
    TEST_ASSERT(!router.DestroyPort(port_b, 102)); // Already cleaned up

    std::cout << "  Passed port lifecycle and access control tests.\n";
}

static void test_message_size_and_queue_depth_limits() {
    std::cout << "[Test 2] Message size limit (4 KB) and queue depth limit (32)...\n";
    GuestIpcRouter& router = GuestIpcRouter::Get();

    uint32_t port = router.CreatePort(201, "queue.test");
    TEST_ASSERT(port != 0);

    // 1. Oversized message (> 4096 bytes) -> rejected
    std::vector<uint8_t> giant(4097, 0xEE);
    TEST_ASSERT(!router.Send(202, port, giant.data(), giant.size(), 1));

    // 2. Exactly 4096 bytes -> succeeds
    std::vector<uint8_t> max_msg(4096, 0xDD);
    TEST_ASSERT(router.Send(202, port, max_msg.data(), max_msg.size(), 2));

    // Drain that message
    IpcMessage m{};
    TEST_ASSERT(router.Receive(201, port, &m));

    // 3. Fill queue up to kMaxIpcPortQueueDepth (32)
    uint8_t byte = 0x01;
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT(router.Send(202, port, &byte, 1, static_cast<uint32_t>(i)));
    }

    // 33rd message -> queue full, rejected
    TEST_ASSERT(!router.Send(202, port, &byte, 1, 999));

    // Drain one message
    TEST_ASSERT(router.Receive(201, port, &m));
    TEST_ASSERT(m.message_id == 0);

    // Now send succeeds again
    TEST_ASSERT(router.Send(202, port, &byte, 1, 1000));

    router.CleanupProcess(201);
    std::cout << "  Passed message size and queue depth tests.\n";
}

static void test_msg_trap_syscall_dispatch() {
    std::cout << "[Test 3] Syscall trap dispatch (SYS_mach_msg / SYS_msg_trap)...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(301);

    const uint64_t msg_va = 0x20000ULL;
    const uint64_t page_msg = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(msg_va, page_msg, PagePermissions::UserReadWrite));

    BsdProcess proc(301, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    GuestIpcRouter& router = GuestIpcRouter::Get();
    uint32_t dest_port = router.CreatePort(302, "dest.service");
    TEST_ASSERT(dest_port != 0);

    // Construct winios_msg_header_t + payload in user memory
    const char text[] = "Hello Mach Msg Trap";
    winios_msg_header_t hdr{};
    hdr.msg_size = static_cast<uint32_t>(sizeof(hdr) + sizeof(text));
    hdr.flags = 0;
    hdr.msg_id = 0x1234;
    hdr.src_port = 0;
    hdr.dst_port = dest_port;

    TEST_ASSERT(uas.copyin(msg_va, &hdr, sizeof(hdr)));
    TEST_ASSERT(uas.copyin(msg_va + sizeof(hdr), text, sizeof(text)));

    // 1. Valid SYS 527 dispatch
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, msg_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // KERN_SUCCESS

    // Verify destination received payload
    IpcMessage rcv{};
    TEST_ASSERT(router.Receive(302, dest_port, &rcv));
    TEST_ASSERT(rcv.sender_pid == 301);
    TEST_ASSERT(rcv.message_id == 0x1234);
    TEST_ASSERT(rcv.data.size() == sizeof(text));
    TEST_ASSERT(std::memcmp(rcv.data.data(), text, sizeof(text)) == 0);

    // 2. Misaligned msg_va -> EFAULT (14)
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, 0x20001ULL);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);

    // 3. Invalid destination port -> ENOENT (2)
    hdr.dst_port = 99999;
    TEST_ASSERT(uas.copyin(msg_va, &hdr, sizeof(hdr)));
    cpu.write_register(16, kSysWiniosMsgTrap);
    cpu.write_register(0, msg_va);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoENOENT);

    router.CleanupProcess(302);
    std::cout << "  Passed message trap syscall dispatch tests.\n";
}

int main() {
    std::cout << "=== Stage 220.16 Message Trap and Port Lifecycle Tests ===\n";
    test_port_lifecycle_and_access_rights();
    test_message_size_and_queue_depth_limits();
    test_msg_trap_syscall_dispatch();
    std::cout << "All Stage 220.16 tests passed successfully.\n";
    return 0;
}
