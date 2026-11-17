#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
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

struct TestContext {
    Memory mem{0x800000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr{};
    PriorityScheduler sched{thread_mgr, mem};
    VirtualConsole console{};
    UserSyscallDispatcher dispatcher{console};
    VirtualFileSystem vfs{};
    std::uint64_t root_l2_pa{0};
    std::unique_ptr<UserAddressSpace> uas;
    BsdProcess proc{100, 1, "test_proc"};
    CPU cpu;

    TestContext() {
        ppm.initialize(false);
        sched.initialize();
        root_l2_pa = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "test_uas");
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

void test_open_close_lifecycle() {
    std::cout << "[Test 1] SYS_open and SYS_close Lifecycle..." << std::endl;

    TestContext ctx;

    // 1. Write path "/etc/hosts" into user space at 0x10000
    const std::string path = "/etc/hosts";
    ctx.uas->copyin(0x10000ULL, path.c_str(), path.size() + 1);

    // Call open("/etc/hosts", O_RDONLY)
    const std::uint64_t fd_hosts = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_RDONLY, 0);
    TEST_CHECK(ctx.last_call_succeeded(), "SYS_open(/etc/hosts) succeeded");
    TEST_CHECK(fd_hosts >= 3, "Allocated fd >= 3");

    // Close fd_hosts
    const std::uint64_t close_ret = ctx.invoke_syscall(kSysClose, fd_hosts);
    TEST_CHECK(ctx.last_call_succeeded() && close_ret == 0, "SYS_close succeeded");

    // Close again -> EBADF (9)
    const std::uint64_t err_ret = ctx.invoke_syscall(kSysClose, fd_hosts);
    TEST_CHECK(!ctx.last_call_succeeded() && err_ret == kErrnoEBADF, "Double close returns EBADF");

    // Open non-existent without O_CREAT -> ENOENT (2)
    const std::string bad_path = "/tmp/nonexistent_file.txt";
    ctx.uas->copyin(0x10000ULL, bad_path.c_str(), bad_path.size() + 1);
    const std::uint64_t noent_ret = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_RDONLY, 0);
    TEST_CHECK(!ctx.last_call_succeeded() && noent_ret == kErrnoENOENT, "Open non-existent without O_CREAT returns ENOENT");

    // Open with O_CREAT | O_RDWR -> succeeds
    const std::uint64_t fd_new = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_CREAT | kO_RDWR, 0644);
    TEST_CHECK(ctx.last_call_succeeded(), "Open with O_CREAT | O_RDWR succeeded");
    TEST_CHECK(fd_new >= 3, "Allocated valid fd for newly created file");
    ctx.invoke_syscall(kSysClose, fd_new);
}

void test_read_write_seek_pipeline() {
    std::cout << "[Test 2] SYS_write, SYS_read, and SYS_lseek Pipeline..." << std::endl;

    TestContext ctx;

    const std::string file_path = "/tmp/pipeline_data.txt";
    ctx.uas->copyin(0x10000ULL, file_path.c_str(), file_path.size() + 1);

    // Open with O_CREAT | O_RDWR
    const std::uint64_t fd = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_CREAT | kO_RDWR, 0644);
    TEST_CHECK(ctx.last_call_succeeded(), "Created and opened /tmp/pipeline_data.txt");

    // Write "Dynamic Darwin VFS Stream" (26 bytes) from 0x10100
    const std::string test_data = "Dynamic Darwin VFS Stream!";
    ctx.uas->copyin(0x10100ULL, test_data.c_str(), test_data.size());
    const std::uint64_t bytes_written = ctx.invoke_syscall(kSysWrite, fd, 0x10100ULL, test_data.size());
    TEST_CHECK(ctx.last_call_succeeded() && bytes_written == test_data.size(), "SYS_write wrote all 26 bytes");

    // Seek back to beginning with SEEK_SET
    const std::uint64_t seek_ret = ctx.invoke_syscall(kSysLseek, fd, 0, kSeekSet);
    TEST_CHECK(ctx.last_call_succeeded() && seek_ret == 0, "SYS_lseek SEEK_SET to 0");

    // Read 7 bytes into 0x10200 ("Dynamic")
    const std::uint64_t r1 = ctx.invoke_syscall(kSysRead, fd, 0x10200ULL, 7);
    TEST_CHECK(ctx.last_call_succeeded() && r1 == 7, "SYS_read read 7 bytes");
    char r1_buf[8]{0};
    ctx.uas->copyout(reinterpret_cast<const void*>(0x10200ULL), r1_buf, 7);
    TEST_CHECK(std::string(r1_buf) == "Dynamic", "Read matches 'Dynamic'");

    // Seek forward 1 byte (skip space) with SEEK_CUR
    const std::uint64_t seek_cur = ctx.invoke_syscall(kSysLseek, fd, 1, kSeekCur);
    TEST_CHECK(ctx.last_call_succeeded() && seek_cur == 8, "SYS_lseek SEEK_CUR to 8");

    // Read 6 bytes into 0x10200 ("Darwin")
    const std::uint64_t r2 = ctx.invoke_syscall(kSysRead, fd, 0x10200ULL, 6);
    TEST_CHECK(ctx.last_call_succeeded() && r2 == 6, "SYS_read read 6 bytes");
    char r2_buf[8]{0};
    ctx.uas->copyout(reinterpret_cast<const void*>(0x10200ULL), r2_buf, 6);
    TEST_CHECK(std::string(r2_buf) == "Darwin", "Read matches 'Darwin'");

    // Seek to end with SEEK_END and append " WinIOS"
    const std::uint64_t seek_end = ctx.invoke_syscall(kSysLseek, fd, 0, kSeekEnd);
    TEST_CHECK(ctx.last_call_succeeded() && seek_end == test_data.size(), "SYS_lseek SEEK_END to EOF");

    const std::string extra = " WinIOS";
    ctx.uas->copyin(0x10100ULL, extra.c_str(), extra.size());
    const std::uint64_t w_extra = ctx.invoke_syscall(kSysWrite, fd, 0x10100ULL, extra.size());
    TEST_CHECK(ctx.last_call_succeeded() && w_extra == extra.size(), "Appended extra bytes");

    ctx.invoke_syscall(kSysClose, fd);
}

void test_stat64_and_fstat64_syscalls() {
    std::cout << "[Test 3] SYS_fstat64 and SYS_stat64 Syscalls..." << std::endl;

    TestContext ctx;

    const std::string path = "/tmp/stat_target.txt";
    ctx.uas->copyin(0x10000ULL, path.c_str(), path.size() + 1);
    const std::uint64_t fd = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_CREAT | kO_RDWR, 0644);
    TEST_CHECK(ctx.last_call_succeeded(), "Opened file for stat test");

    const std::string content = "12345678901234567890"; // 20 bytes
    ctx.uas->copyin(0x10100ULL, content.c_str(), content.size());
    ctx.invoke_syscall(kSysWrite, fd, 0x10100ULL, content.size());

    // 1. Call SYS_fstat64 into 0x14000
    const std::uint64_t fstat_ret = ctx.invoke_syscall(kSysFstat64, fd, 0x14000ULL);
    TEST_CHECK(ctx.last_call_succeeded() && fstat_ret == 0, "SYS_fstat64 succeeded");

    DarwinStat64 fstat_res{};
    ctx.uas->copyout(reinterpret_cast<const void*>(0x14000ULL), &fstat_res, sizeof(fstat_res));
    TEST_CHECK(fstat_res.st_size == 20, "fstat reported st_size == 20");
    TEST_CHECK((fstat_res.st_mode & 0170000) == 0100000, "fstat mode is regular file");

    // 2. Call SYS_stat64 on path into 0x14000
    const std::uint64_t stat_ret = ctx.invoke_syscall(kSysStat64, 0x10000ULL, 0x14000ULL);
    TEST_CHECK(ctx.last_call_succeeded() && stat_ret == 0, "SYS_stat64 succeeded");

    DarwinStat64 stat_res{};
    ctx.uas->copyout(reinterpret_cast<const void*>(0x14000ULL), &stat_res, sizeof(stat_res));
    TEST_CHECK(stat_res.st_size == 20, "stat reported st_size == 20");
    TEST_CHECK(stat_res.st_ino == fstat_res.st_ino, "stat and fstat inodes match");

    // 3. Call SYS_fstat64 on stdout (fd 1)
    const std::uint64_t fstat_stdout = ctx.invoke_syscall(kSysFstat64, 1, 0x14000ULL);
    TEST_CHECK(ctx.last_call_succeeded() && fstat_stdout == 0, "SYS_fstat64 on stdout succeeded");
    ctx.uas->copyout(reinterpret_cast<const void*>(0x14000ULL), &fstat_res, sizeof(fstat_res));
    TEST_CHECK((fstat_res.st_mode & 0170000) == 0020000, "stdout st_mode is character device (S_IFCHR)");

    ctx.invoke_syscall(kSysClose, fd);
}

void test_security_faults_and_boundaries() {
    std::cout << "[Test 4] Syscall Security Boundaries and Fault Injection..." << std::endl;

    TestContext ctx;

    // 1. EFAULT on unmapped memory for open path
    const std::uint64_t unmapped_va = 0x50000000ULL;
    const std::uint64_t efault_open = ctx.invoke_syscall(kSysOpen, unmapped_va, kO_RDONLY, 0);
    TEST_CHECK(!ctx.last_call_succeeded() && efault_open == kErrnoEFAULT, "SYS_open on unmapped VA returns EFAULT");

    // 2. EFAULT on unmapped memory for read buffer
    const std::string path = "/etc/hosts";
    ctx.uas->copyin(0x10000ULL, path.c_str(), path.size() + 1);
    const std::uint64_t fd = ctx.invoke_syscall(kSysOpen, 0x10000ULL, kO_RDONLY, 0);
    TEST_CHECK(ctx.last_call_succeeded(), "Opened /etc/hosts");

    const std::uint64_t efault_read = ctx.invoke_syscall(kSysRead, fd, unmapped_va, 64);
    TEST_CHECK(!ctx.last_call_succeeded() && efault_read == kErrnoEFAULT, "SYS_read into unmapped buffer returns EFAULT");

    // 3. ESPIPE on seeking stdout (fd 1)
    const std::uint64_t espipe = ctx.invoke_syscall(kSysLseek, 1, 0, kSeekSet);
    TEST_CHECK(!ctx.last_call_succeeded() && espipe == kErrnoESPIPE, "SYS_lseek on stdout returns ESPIPE");

    // 4. EINVAL on negative seek
    const std::uint64_t einval_seek = ctx.invoke_syscall(kSysLseek, fd, static_cast<std::uint64_t>(-100LL), kSeekSet);
    TEST_CHECK(!ctx.last_call_succeeded() && einval_seek == kErrnoEINVAL, "Negative SYS_lseek returns EINVAL");

    ctx.invoke_syscall(kSysClose, fd);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.13 File Operations Syscall Tests ===" << std::endl;
    test_open_close_lifecycle();
    test_read_write_seek_pipeline();
    test_stat64_and_fstat64_syscalls();
    test_security_faults_and_boundaries();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.13: All file operations syscall tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.13: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
