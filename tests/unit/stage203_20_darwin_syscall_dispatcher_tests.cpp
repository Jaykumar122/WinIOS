// ==============================================================================
// WinIOS Campaign 203 Stage 203.20: Synthetic Darwin Syscall Dispatcher Tests
// ==============================================================================
// Independent verification of:
// 1. BSD POSIX system calls: getpid (20), getuid (24), write (4), exit (1)
// 2. Bound DarwinVmSpace integration: mmap (197), mprotect (74), munmap (73)
// 3. Bound MachIpcSubsystem integration: mach_reply_port (-26), mach_msg (-31)
// 4. SVC 0x80 unified dispatching for positive (BSD) and negative (Mach) X16
// 5. Error handling and unknown trap reporting (ENOSYS 78, KERN_FAILURE 5)
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "runtime/darwin_syscall_dispatcher.h"
#include "memory/darwin_vm_object.h"
#include "runtime/darwin_mach_ipc.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios::runtime;
using namespace winios::memory;
using namespace winios;

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
// Test 1: BSD Basic Syscalls (getpid, getuid, write, exit)
// -----------------------------------------------------------------------------
void test_bsd_basic_syscalls() {
    std::cout << "[Stage 203.20 Test 1] BSD Basic Syscalls (getpid, write, exit)...\n";

    DarwinSyscallDispatcher dispatcher;
    Memory mem(0x10000);
    CPU cpu;

    // 1. SYS_getpid (20)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid));
    std::int64_t r_pid = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_pid == 1, "getpid returns 1");
    TEST_CHECK(cpu.read_register(0) == 1, "X0 holds PID 1");

    // 2. SYS_getuid (24)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetUid));
    std::int64_t r_uid = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_uid == 0, "getuid returns 0");
    TEST_CHECK(cpu.read_register(0) == 0, "X0 holds UID 0");

    // 3. SYS_write (4)
    const char text[] = "Darwin Synthetic Kernel Online\n";
    const std::uint64_t text_addr = 0x2000;
    std::memcpy(mem.raw_data() + text_addr, text, sizeof(text) - 1);

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Write));
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, text_addr);
    cpu.write_register(2, sizeof(text) - 1);

    std::int64_t r_write = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_write == sizeof(text) - 1, "write returns byte count written");
    TEST_CHECK(dispatcher.stdout_buffer() == text, "Stdout buffer captured exact text");

    // 4. SYS_exit (1)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Exit));
    cpu.write_register(0, 42); // exit code

    dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(dispatcher.has_exited(), "has_exited is true after SYS_exit");
    TEST_CHECK(dispatcher.exit_code() == 42, "exit_code is 42");

    std::cout << "  -> BSD basic syscalls verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Bound DarwinVmSpace Integration (mmap, mprotect, munmap)
// -----------------------------------------------------------------------------
void test_vm_syscalls_with_bound_vm_space() {
    std::cout << "[Stage 203.20 Test 2] Bound DarwinVmSpace Integration...\n";

    DarwinSyscallDispatcher dispatcher;
    auto vm = std::make_shared<DarwinVmSpace>(901, 0x100000000ULL);
    dispatcher.bind_vm_space(vm);

    Memory mem(0x10000);
    CPU cpu;

    // 1. SYS_mmap (197)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap));
    cpu.write_register(0, 0); // addr = 0 (auto-placement)
    cpu.write_register(1, 0x8000); // len = 32KB
    cpu.write_register(2, kDarwinVmProtRead | kDarwinVmProtWrite);
    cpu.write_register(3, kDarwinMapAnonymous | kDarwinMapPrivate);
    cpu.write_register(4, static_cast<std::uint64_t>(-1));
    cpu.write_register(5, 0);

    std::int64_t map_va = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(map_va != 0, "mmap returns non-zero virtual address");
    TEST_CHECK(is_page_aligned(static_cast<std::uint64_t>(map_va)), "Allocated address is 16KB aligned");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(map_va), "X0 holds allocated address");
    TEST_CHECK(vm->region_count() == 1, "VmSpace contains 1 region");

    // 2. SYS_mprotect (74)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mprotect));
    cpu.write_register(0, static_cast<std::uint64_t>(map_va));
    cpu.write_register(1, 0x8000);
    cpu.write_register(2, kDarwinVmProtRead); // RO

    std::int64_t r_prot = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_prot == kKernSuccess, "mprotect returns kKernSuccess (0)");
    TEST_CHECK(vm->check_access(static_cast<std::uint64_t>(map_va), 0x8000, kDarwinVmProtRead), "Region is readable");
    TEST_CHECK(!vm->check_access(static_cast<std::uint64_t>(map_va), 0x8000, kDarwinVmProtWrite), "Region is not writable");

    // 3. SYS_munmap (73)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Munmap));
    cpu.write_register(0, static_cast<std::uint64_t>(map_va));
    cpu.write_register(1, 0x8000);

    std::int64_t r_unmap = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_unmap == kKernSuccess, "munmap returns kKernSuccess (0)");
    TEST_CHECK(vm->region_count() == 0, "VmSpace is empty after munmap");

    std::cout << "  -> Bound VM space syscalls verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Bound MachIpcSubsystem Integration (mach_reply_port, mach_msg)
// -----------------------------------------------------------------------------
void test_mach_traps_with_bound_ipc() {
    std::cout << "[Stage 203.20 Test 3] Bound MachIpcSubsystem Integration...\n";

    DarwinSyscallDispatcher dispatcher;
    auto ipc = std::make_shared<MachIpcSubsystem>();
    const std::uint32_t task_id = 902;
    dispatcher.bind_mach_ipc(ipc, task_id);

    Memory mem(0x100000);
    CPU cpu;

    // 1. mach_reply_port (-26)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::MachReplyPort));
    std::int64_t reply_port = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(reply_port != kMachPortNull, "mach_reply_port returns valid handle");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(reply_port), "X0 receives reply port");

    // 2. mach_task_self (-28)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::TaskSelf));
    std::int64_t task_self = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(task_self != kMachPortNull, "mach_task_self returns valid handle");

    // 3. mach_host_self (-29)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::HostSelf));
    std::int64_t host_self = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(host_self != kMachPortNull, "mach_host_self returns valid handle");

    // 4. Send message to reply port and receive via mach_msg (-31)
    auto space = ipc->get_task_port_space(task_id);
    auto port_obj = space->lookup_port(static_cast<mach_port_t>(reply_port));
    mach_port_t snd_port = space->allocate_port(port_obj, kMachPortRightSend);

    const std::uint64_t snd_addr = 0x2000;
    MachMsgHeaderRaw snd_hdr{};
    snd_hdr.msgh_bits = 0;
    snd_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    snd_hdr.msgh_remote_port = snd_port;
    snd_hdr.msgh_local_port = kMachPortNull;
    snd_hdr.msgh_id = 3003;

    std::memcpy(mem.raw_data() + snd_addr, &snd_hdr, sizeof(snd_hdr));
    std::uint32_t val = 0x778899AA;
    std::memcpy(mem.raw_data() + snd_addr + sizeof(snd_hdr), &val, sizeof(val));

    // Send via dispatch
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::MachMsg));
    cpu.write_register(0, snd_addr);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, snd_hdr.msgh_size);
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);

    std::int64_t r_send = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_send == kMachMsgSuccess, "mach_msg send dispatch succeeded");

    // Receive via dispatch
    const std::uint64_t rcv_addr = 0x4000;
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::MachMsg));
    cpu.write_register(0, rcv_addr);
    cpu.write_register(1, kMachRcvMsg);
    cpu.write_register(2, 0);
    cpu.write_register(3, 1024);
    cpu.write_register(4, static_cast<mach_port_t>(reply_port));
    cpu.write_register(5, 0);

    std::int64_t r_rcv = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_rcv == kMachMsgSuccess, "mach_msg receive dispatch succeeded");

    MachMsgHeaderRaw rcv_hdr{};
    std::memcpy(&rcv_hdr, mem.raw_data() + rcv_addr, sizeof(rcv_hdr));
    TEST_CHECK(rcv_hdr.msgh_id == 3003, "Received message ID matches 3003");

    std::cout << "  -> Bound Mach IPC traps verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: SVC 0x80 Unified Routing for Negative X16
// -----------------------------------------------------------------------------
void test_negative_x16_routing() {
    std::cout << "[Stage 203.20 Test 4] SVC 0x80 Unified Routing for Negative X16...\n";

    DarwinSyscallDispatcher dispatcher;
    Memory mem(0x10000);
    CPU cpu;

    // Call MachReplyPort (-26) using SVC 0x80 (BSD vector)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::MachReplyPort));
    std::int64_t res = dispatcher.dispatch(cpu, mem, 0x80);

    // Default mock handler returns 0x203
    TEST_CHECK(res == 0x203, "Negative X16 routed to Mach trap handler on SVC 0x80");

    std::cout << "  -> Unified routing verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Error Handling for Unimplemented Syscalls
// -----------------------------------------------------------------------------
void test_unimplemented_syscall_error_reporting() {
    std::cout << "[Stage 203.20 Test 5] Unimplemented Syscall Error Reporting...\n";

    DarwinSyscallDispatcher dispatcher;
    Memory mem(0x10000);
    CPU cpu;

    // Unregistered BSD syscall 9999
    cpu.write_register(16, 9999);
    std::int64_t r_bsd = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_bsd == -78, "Unimplemented BSD syscall returns -ENOSYS (-78)");
    TEST_CHECK(cpu.read_register(0) == 78, "X0 holds ENOSYS (78)");

    // Unregistered Mach trap -9999
    cpu.write_register(16, static_cast<std::uint64_t>(-9999));
    std::int64_t r_mach = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_mach == 5, "Unimplemented Mach trap returns KERN_FAILURE (5)");
    TEST_CHECK(cpu.read_register(0) == 5, "X0 holds KERN_FAILURE (5)");

    std::cout << "  -> Error reporting verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.20: Synthetic Darwin Syscall Dispatcher Tests\n";
    std::cout << "==================================================================\n";

    test_bsd_basic_syscalls();
    test_vm_syscalls_with_bound_vm_space();
    test_mach_traps_with_bound_ipc();
    test_negative_x16_routing();
    test_unimplemented_syscall_error_reporting();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.20 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
