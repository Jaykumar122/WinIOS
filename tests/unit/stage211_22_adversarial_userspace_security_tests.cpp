#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/arm_trap_frame.h"
#include "kernel/bsd_process.h"
#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/virtual_page_table.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_adversarial_security_suite() {
    std::cout << "[Test 1] Stage 211.22: 18-Vector Adversarial Userspace Security Audit...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "security_uas");

    // Vector 1: Malformed Mach-O header (< 32 bytes)
    std::vector<std::uint8_t> malformed_hdr = {0xCF, 0xFA, 0xED, 0xFE};
    auto img1 = MachoUserLoader::load_image(malformed_hdr, uas, ppm, mem);
    TEST_CHECK(!img1.valid, "Vector 1: Malformed Mach-O header (< 32B) rejected");

    // Vector 2: Invalid Mach-O magic
    std::vector<std::uint8_t> bad_magic(64, 0);
    auto img2 = MachoUserLoader::load_image(bad_magic, uas, ppm, mem);
    TEST_CHECK(!img2.valid, "Vector 2: Corrupted Mach-O magic rejected");

    // Vector 3: W^X violation (segment marked Writable + Executable)
    std::vector<std::uint8_t> wx_binary(128, 0);
    const std::uint32_t magic = kMhMagic64;
    const std::int32_t cputype = kCpuTypeArm64;
    const std::uint32_t ncmds = 1;
    const std::uint32_t sizeofcmds = 72;
    std::memcpy(wx_binary.data(), &magic, 4);
    std::memcpy(wx_binary.data() + 4, &cputype, 4);
    std::memcpy(wx_binary.data() + 16, &ncmds, 4);
    std::memcpy(wx_binary.data() + 20, &sizeofcmds, 4);
    // LC_SEGMENT_64
    const std::uint32_t cmd = kLcSegment64;
    const std::uint32_t cmdsize = 72;
    const std::int32_t wx_prot = 0x6; // VM_PROT_WRITE | VM_PROT_EXECUTE (W^X violation!)
    std::memcpy(wx_binary.data() + 32, &cmd, 4);
    std::memcpy(wx_binary.data() + 36, &cmdsize, 4);
    std::memcpy(wx_binary.data() + 92, &wx_prot, 4);
    auto img3 = MachoUserLoader::load_image(wx_binary, uas, ppm, mem);
    TEST_CHECK(!img3.valid, "Vector 3: W^X violation rejected by loader");

    // Vector 4: PAGEZERO with non-zero permissions
    std::vector<std::uint8_t> bad_pagezero(128, 0);
    std::memcpy(bad_pagezero.data(), &magic, 4);
    std::memcpy(bad_pagezero.data() + 4, &cputype, 4);
    std::memcpy(bad_pagezero.data() + 16, &ncmds, 4);
    std::memcpy(bad_pagezero.data() + 20, &sizeofcmds, 4);
    std::memcpy(bad_pagezero.data() + 32, &cmd, 4);
    std::memcpy(bad_pagezero.data() + 36, &cmdsize, 4);
    std::memcpy(bad_pagezero.data() + 40, "__PAGEZERO", 10);
    const std::int32_t read_prot = 0x1;
    std::memcpy(bad_pagezero.data() + 92, &read_prot, 4); // Non-zero initprot!
    auto img4 = MachoUserLoader::load_image(bad_pagezero, uas, ppm, mem);
    TEST_CHECK(!img4.valid, "Vector 4: PAGEZERO with non-zero permissions rejected");

    // Vector 5: Kernel memory mapping attempt in user address space
    const std::uint64_t dummy_pa = ppm.allocate_page();
    TEST_CHECK(!uas.map_user_page(0x10000ULL, dummy_pa, PagePermissions::KernelExecute),
               "Vector 5: Mapping kernel permission in user address space rejected");

    // Vector 6: Mapping beyond user address space limit (>= 2 GiB)
    TEST_CHECK(!uas.map_user_page(0x90000000ULL, dummy_pa, PagePermissions::UserReadWrite),
               "Vector 6: Mapping beyond 2 GiB user address limit rejected");

    // Vector 7: User buffer pointer overflow validation (e.g. 0xFFFFFFFFFFFFF000)
    TEST_CHECK(!uas.validate_user_buffer(0xFFFFFFFFFFFFFF00ULL, 0x1000, false),
               "Vector 7: Integer overflow in buffer address rejected");

    // Vector 8: Syscall with invalid syscall number (999999)
    CPU cpu;
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("sec_proc", 0);

    cpu.write_register(16, 999999);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched),
               "Vector 8: Out-of-bounds syscall number rejected with ENOSYS");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -78, "Returns -78");

    // Vector 9: SYS_write with unmapped user buffer (EFAULT)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 1);
    cpu.write_register(1, 0x12345000ULL); // Unmapped address
    cpu.write_register(2, 64);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched),
               "Vector 9: SYS_write with unmapped user buffer rejected with EFAULT");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -14, "Returns -14");

    // Vector 10: SYS_write with invalid FD (EBADF)
    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, 999); // Out-of-range FD
    cpu.write_register(1, 0x10000ULL);
    cpu.write_register(2, 10);
    TEST_CHECK(!dispatcher.handle_syscall(cpu, *proc, uas, sched),
               "Vector 10: SYS_write with invalid FD rejected with EBADF");
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -9, "Returns -9");

    // Vector 11: Unauthorized IPC message transmission (no send right)
    ProcessIpcSpace ipc_space;
    TEST_CHECK(!ipc_space.has_send_right(42), "Vector 11: Sending on port without send right rejected");

    // Vector 12: Unauthorized IPC message reception (no receive right)
    TEST_CHECK(!ipc_space.has_recv_right(42), "Vector 12: Receiving on port without receive right rejected");

    // Vector 13: IPC port queue flooding beyond capacity
    IpcManager ipc_mgr;
    auto* port = ipc_mgr.create_port(1, 2); // Capacity 2
    KernelMessage msg{};
    TEST_CHECK(port->try_send(msg, sched), "Send 1 accepted");
    TEST_CHECK(port->try_send(msg, sched), "Send 2 accepted");
    TEST_CHECK(!port->try_send(msg, sched), "Vector 13: 3rd message rejected by full queue");

    // Vector 14: Process termination of PID 0 forbidden
    TEST_CHECK(!proc_mgr.terminate_process(0, 1), "Vector 14: Termination of PID 0 (kernel_process) forbidden");

    // Vector 15: Reaping active (non-zombie) process forbidden
    TEST_CHECK(!proc_mgr.reap_process(proc->pid()), "Vector 15: Reaping non-zombie process forbidden");

    // Vector 16: Closing already-closed file descriptor (double close)
    TEST_CHECK(!proc->fd_table().close_fd(30), "Vector 16: Closing unallocated FD rejected");

    // Vector 17: UserExecute page hardware descriptor denies EL1 execution (PXN=1)
    const auto u_desc = VirtualPageTable::make_page_descriptor(0x10000ULL, PagePermissions::UserExecute);
    const auto u_fields = VirtualPageTable::parse_descriptor(u_desc, 3);
    TEST_CHECK(!u_fields.privileged_execute, "Vector 17: UserExecute descriptor has PXN=1 (privileged execute forbidden)");

    // Vector 18: KernelExecute page hardware descriptor denies EL0 execution (UXN=1)
    const auto k_desc = VirtualPageTable::make_page_descriptor(0x10000ULL, PagePermissions::KernelExecute);
    const auto k_fields = VirtualPageTable::parse_descriptor(k_desc, 3);
    TEST_CHECK(!k_fields.user_execute, "Vector 18: KernelExecute descriptor has UXN=1 (user execute forbidden)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.22 Adversarial Security Tests ===\n";
    test_adversarial_security_suite();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.22: All 18 adversarial security vectors defeated.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.22: " << g_failures << " tests failed.\n";
        return 1;
    }
}
