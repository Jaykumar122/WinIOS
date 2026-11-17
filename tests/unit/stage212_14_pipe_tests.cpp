#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/pipe.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "vfs/vfs.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::vfs;

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

struct PipeTestContext {
    Memory mem{0x800000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr{};
    PriorityScheduler sched{thread_mgr, mem};
    VirtualConsole console{};
    UserSyscallDispatcher dispatcher{console};
    VirtualFileSystem vfs{};
    std::uint64_t root_l2_pa{0};
    std::unique_ptr<UserAddressSpace> uas;
    BsdProcess proc{100, 1, "pipe_proc"};
    CPU cpu;

    PipeTestContext() {
        ppm.initialize(false);
        sched.initialize();
        root_l2_pa = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "pipe_uas");
        dispatcher.set_vfs(&vfs);
        dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

        // Map two user data pages at 0x10000 and 0x14000
        const std::uint64_t p1 = ppm.allocate_page();
        const std::uint64_t p2 = ppm.allocate_page();
        uas->map_user_page(0x10000ULL, p1, PagePermissions::UserReadWrite);
        uas->map_user_page(0x14000ULL, p2, PagePermissions::UserReadWrite);

        cpu.reset();
        cpu.set_current_el(0);
    }

    std::uint64_t invoke_syscall(std::uint64_t num, std::uint64_t a0 = 0, std::uint64_t a1 = 0, std::uint64_t a2 = 0) {
        cpu.write_register(16, num);
        cpu.write_register(0, a0);
        cpu.write_register(1, a1);
        cpu.write_register(2, a2);
        dispatcher.handle_syscall(cpu, proc, *uas, sched);
        return cpu.read_register(0);
    }

    bool last_call_succeeded() const {
        return (cpu.get_spsr_el1() & (1u << 29)) == 0;
    }
};

void test_pipe_creation_and_descriptor_pairing() {
    std::cout << "[Test 1] SYS_pipe Creation and Register/Memory Pairing..." << std::endl;

    PipeTestContext ctx;

    // 1. Invoke pipe with pointer to int[2] at 0x10000
    const std::uint64_t ret = ctx.invoke_syscall(kSysPipe, 0x10000ULL);
    TEST_CHECK(ctx.last_call_succeeded(), "SYS_pipe succeeded");

    const std::uint64_t read_fd = ret;
    const std::uint64_t write_fd = ctx.cpu.read_register(1);
    TEST_CHECK(read_fd >= 3, "Read fd >= 3");
    TEST_CHECK(write_fd >= 3, "Write fd >= 3");
    TEST_CHECK(read_fd != write_fd, "Read and write fds are distinct");

    // Check user memory buffer at 0x10000
    int mem_fds[2]{-1, -1};
    ctx.uas->copyout(reinterpret_cast<const void*>(0x10000ULL), mem_fds, sizeof(mem_fds));
    TEST_CHECK(mem_fds[0] == static_cast<int>(read_fd), "Memory fds[0] matches read_fd");
    TEST_CHECK(mem_fds[1] == static_cast<int>(write_fd), "Memory fds[1] matches write_fd");

    // Check readable / writable permissions on fds
    const auto* r_desc = ctx.proc.fd_table().get_fd(static_cast<int>(read_fd));
    const auto* w_desc = ctx.proc.fd_table().get_fd(static_cast<int>(write_fd));
    TEST_CHECK(r_desc && r_desc->readable && !r_desc->writable, "Read fd is readable and not writable");
    TEST_CHECK(w_desc && !w_desc->readable && w_desc->writable, "Write fd is not readable and writable");

    // Clean up
    ctx.invoke_syscall(kSysClose, read_fd);
    ctx.invoke_syscall(kSysClose, write_fd);
}

void test_pipe_data_streaming() {
    std::cout << "[Test 2] Pipe Unidirectional Data Streaming..." << std::endl;

    PipeTestContext ctx;

    ctx.invoke_syscall(kSysPipe, 0x10000ULL);
    const int rfd = static_cast<int>(ctx.cpu.read_register(0));
    const int wfd = static_cast<int>(ctx.cpu.read_register(1));

    // Write "Message 1; Message 2;" into pipe
    const std::string msg1 = "Message 1; ";
    const std::string msg2 = "Message 2;";
    const std::string full_msg = msg1 + msg2;

    ctx.uas->copyin(0x10100ULL, msg1.c_str(), msg1.size());
    const std::uint64_t w1 = ctx.invoke_syscall(kSysWrite, wfd, 0x10100ULL, msg1.size());
    TEST_CHECK(ctx.last_call_succeeded() && w1 == msg1.size(), "Wrote msg1 into pipe");

    ctx.uas->copyin(0x10100ULL, msg2.c_str(), msg2.size());
    const std::uint64_t w2 = ctx.invoke_syscall(kSysWrite, wfd, 0x10100ULL, msg2.size());
    TEST_CHECK(ctx.last_call_succeeded() && w2 == msg2.size(), "Wrote msg2 into pipe");

    // Read back combined stream from rfd into 0x14000
    const std::uint64_t bytes_read = ctx.invoke_syscall(kSysRead, rfd, 0x14000ULL, 128);
    TEST_CHECK(ctx.last_call_succeeded() && bytes_read == full_msg.size(), "Read combined stream");

    std::vector<char> read_buf(full_msg.size() + 1, 0);
    ctx.uas->copyout(reinterpret_cast<const void*>(0x14000ULL), read_buf.data(), full_msg.size());
    TEST_CHECK(std::string(read_buf.data()) == full_msg, "Pipe content matches exactly");

    ctx.invoke_syscall(kSysClose, rfd);
    ctx.invoke_syscall(kSysClose, wfd);
}

void test_pipe_eof_and_epipe_semantics() {
    std::cout << "[Test 3] Pipe EOF and Broken Pipe (EPIPE) Semantics..." << std::endl;

    PipeTestContext ctx;

    // 1. EOF test: write data, close write end, read to EOF
    ctx.invoke_syscall(kSysPipe, 0x10000ULL);
    const int rfd = static_cast<int>(ctx.cpu.read_register(0));
    const int wfd = static_cast<int>(ctx.cpu.read_register(1));

    const std::string payload = "Buffered Data Before Close";
    ctx.uas->copyin(0x10100ULL, payload.c_str(), payload.size());
    ctx.invoke_syscall(kSysWrite, wfd, 0x10100ULL, payload.size());

    // Close write end
    ctx.invoke_syscall(kSysClose, wfd);

    // Read data
    const std::uint64_t r1 = ctx.invoke_syscall(kSysRead, rfd, 0x14000ULL, 128);
    TEST_CHECK(ctx.last_call_succeeded() && r1 == payload.size(), "Read payload after writer closed");

    // Next read on closed writer pipe returns 0 (EOF)
    const std::uint64_t eof_read = ctx.invoke_syscall(kSysRead, rfd, 0x14000ULL, 128);
    TEST_CHECK(ctx.last_call_succeeded() && eof_read == 0, "Subsequent read returns 0 (EOF)");
    ctx.invoke_syscall(kSysClose, rfd);

    // 2. EPIPE test: open pipe, close read end, write to write end -> EPIPE (32)
    ctx.invoke_syscall(kSysPipe, 0x10000ULL);
    const int rfd2 = static_cast<int>(ctx.cpu.read_register(0));
    const int wfd2 = static_cast<int>(ctx.cpu.read_register(1));

    ctx.invoke_syscall(kSysClose, rfd2); // Close reader

    ctx.uas->copyin(0x10100ULL, "Test", 4);
    const std::uint64_t epipe_ret = ctx.invoke_syscall(kSysWrite, wfd2, 0x10100ULL, 4);
    TEST_CHECK(!ctx.last_call_succeeded() && epipe_ret == kErrnoEPIPE, "Writing to closed reader pipe returns EPIPE (32)");

    ctx.invoke_syscall(kSysClose, wfd2);
}

void test_pipe_security_and_fault_handling() {
    std::cout << "[Test 4] Pipe Security and Fault Handling..." << std::endl;

    PipeTestContext ctx;

    // 1. EFAULT on invalid pointer to pipe(fds)
    const std::uint64_t unmapped_va = 0x50000000ULL;
    const std::uint64_t efault_ret = ctx.invoke_syscall(kSysPipe, unmapped_va);
    TEST_CHECK(!ctx.last_call_succeeded() && efault_ret == kErrnoEFAULT, "SYS_pipe with unmapped pointer returns EFAULT");

    // 2. Writing to read end of pipe returns EBADF
    ctx.invoke_syscall(kSysPipe, 0x10000ULL);
    const int rfd = static_cast<int>(ctx.cpu.read_register(0));
    const int wfd = static_cast<int>(ctx.cpu.read_register(1));

    ctx.uas->copyin(0x10100ULL, "Data", 4);
    const std::uint64_t bad_write = ctx.invoke_syscall(kSysWrite, rfd, 0x10100ULL, 4);
    TEST_CHECK(!ctx.last_call_succeeded() && bad_write == kErrnoEBADF, "Writing to read-only pipe fd returns EBADF");

    // 3. Reading from write end of pipe returns EBADF
    const std::uint64_t bad_read = ctx.invoke_syscall(kSysRead, wfd, 0x14000ULL, 4);
    TEST_CHECK(!ctx.last_call_succeeded() && bad_read == kErrnoEBADF, "Reading from write-only pipe fd returns EBADF");

    // 4. Lseek on pipe returns ESPIPE
    const std::uint64_t espipe = ctx.invoke_syscall(kSysLseek, rfd, 0, kSeekSet);
    TEST_CHECK(!ctx.last_call_succeeded() && espipe == kErrnoESPIPE, "Lseek on pipe returns ESPIPE (29)");

    ctx.invoke_syscall(kSysClose, rfd);
    ctx.invoke_syscall(kSysClose, wfd);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.14 Pipes and Inter-Process Streams Tests ===" << std::endl;
    test_pipe_creation_and_descriptor_pairing();
    test_pipe_data_streaming();
    test_pipe_eof_and_epipe_semantics();
    test_pipe_security_and_fault_handling();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.14: All pipe and stream tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.14: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
