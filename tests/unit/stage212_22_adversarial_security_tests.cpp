// ============================================================================
// Stage 212.22: 25-Vector Adversarial Security Suite
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bootstrap_registry.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/pipe.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "kernel/user_stack.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "vfs/vfs.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::vfs;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void run_adversarial_security_suite() {
    std::cout << "[Stage 212.22] Executing 25-Vector Adversarial Security Suite...\n";

    Memory mem(0x2000000); // 32 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    VirtualFileSystem vfs;
    vfs.initialize_standard_hierarchy();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_vfs(&vfs);
    dispatcher.set_process_manager(&proc_mgr);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    PriorityScheduler dummy_sched(thread_mgr, mem);

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "sec_vm");

    // Map one valid read-only page at 0x10000 and one read-write at 0x20000
    uas.map_user_page(0x10000, 0x200000, PagePermissions::UserRead);
    uas.map_user_page(0x20000, 0x204000, PagePermissions::UserReadWrite);

    auto* proc = proc_mgr.create_process("sec_proc", 1);
    CPU cpu;

    // --- Vector 1: Malformed Mach-O Magic ---
    {
        std::uint8_t bad_hdr[32]{0};
        const std::uint32_t bad_magic = 0xDEADBEEFu;
        std::memcpy(bad_hdr, &bad_magic, 4);
        std::string err;
        TEST_CHECK(!MachoUserLoader::validate_header(bad_hdr, sizeof(bad_hdr), &err),
                   "Vector 1: Malformed Mach-O magic (0xDEADBEEF) rejected");
    }

    // --- Vector 2: Malformed CPU Type (non-ARM64) ---
    {
        std::uint8_t bad_hdr[32]{0};
        const std::uint32_t magic = kMhMagic64;
        const std::uint32_t x86_64_cputype = 0x01000007u;
        std::memcpy(bad_hdr, &magic, 4);
        std::memcpy(bad_hdr + 4, &x86_64_cputype, 4);
        std::string err;
        TEST_CHECK(!MachoUserLoader::validate_header(bad_hdr, sizeof(bad_hdr), &err),
                   "Vector 2: Non-ARM64 CPU type rejected");
    }

    // --- Vector 3: Truncated Mach-O Header ---
    {
        std::uint8_t short_hdr[16]{0};
        const std::uint32_t magic = kMhMagic64;
        std::memcpy(short_hdr, &magic, 4);
        std::string err;
        TEST_CHECK(!MachoUserLoader::validate_header(short_hdr, sizeof(short_hdr), &err),
                   "Vector 3: Truncated Mach-O header (< 32 bytes) rejected");
    }

    // --- Vector 4: Malformed Load Command Size (cmdsize < 8) ---
    {
        std::vector<std::uint8_t> malformed(64, 0);
        const std::uint32_t magic = kMhMagic64;
        const std::int32_t cputype = kCpuTypeArm64;
        const std::uint32_t ncmds = 1;
        const std::uint32_t sizeofcmds = 32;
        std::memcpy(malformed.data(), &magic, 4);
        std::memcpy(malformed.data() + 4, &cputype, 4);
        std::memcpy(malformed.data() + 16, &ncmds, 4);
        std::memcpy(malformed.data() + 20, &sizeofcmds, 4);
        // cmdsize = 4 (illegal, must be >= 8)
        const std::uint32_t cmd = 0x19;
        const std::uint32_t cmdsize = 4;
        std::memcpy(malformed.data() + 32, &cmd, 4);
        std::memcpy(malformed.data() + 36, &cmdsize, 4);

        auto img = MachoUserLoader::load_image(malformed, uas, ppm, mem);
        TEST_CHECK(!img.valid, "Vector 4: Malformed load command size (cmdsize < 8) rejected");
    }

    // --- Vector 5: Corrupted Load Command Count (ncmds out of bounds) ---
    {
        std::vector<std::uint8_t> malformed(64, 0);
        const std::uint32_t magic = kMhMagic64;
        const std::int32_t cputype = kCpuTypeArm64;
        const std::uint32_t ncmds = 99999;
        const std::uint32_t sizeofcmds = 32;
        std::memcpy(malformed.data(), &magic, 4);
        std::memcpy(malformed.data() + 4, &cputype, 4);
        std::memcpy(malformed.data() + 16, &ncmds, 4);
        std::memcpy(malformed.data() + 20, &sizeofcmds, 4);

        auto img = MachoUserLoader::load_image(malformed, uas, ppm, mem);
        TEST_CHECK(!img.valid, "Vector 5: Out-of-bounds load command count rejected");
    }

    // --- Vector 6: Overlapping Virtual Address Segments ---
    {
        // Segment attempting to map over an already mapped page
        bool mapped1 = uas.map_user_page(0x30000, 0x300000, PagePermissions::UserRead);
        TEST_CHECK(mapped1, "First mapping at 0x30000 succeeded");
        bool mapped2 = uas.map_user_page(0x30000, 0x400000, PagePermissions::UserReadWrite);
        TEST_CHECK(!mapped2, "Vector 6: Overlapping duplicate page mapping rejected");
    }

    // --- Vector 7: W^X Violation Attempt (simultaneous Write + Execute) ---
    {
        // Attempting to map with UserReadWrite (writable) and checking execute permission
        PageDescriptorFields fields{};
        const std::uint64_t pa = uas.translate(0x20000, &fields);
        TEST_CHECK(pa != 0, "Page at 0x20000 translated");
        TEST_CHECK(!fields.user_execute, "Vector 7: Writable user page is strictly Non-Executable (W^X)");
    }

    // --- Vector 8: Out-of-bounds User Stack Request ---
    {
        UserStackConfig huge_stack;
        huge_stack.stack_size = 0x800000000000ULL; // Far exceeds user space limit
        auto res = UserStackBuilder::build_stack(uas, ppm, mem, huge_stack);
        TEST_CHECK(!res.success, "Vector 8: Excessive stack allocation rejected");
    }

    // --- Vector 9: Unaligned Stack Alignment Enforcement ---
    {
        UserStackConfig cfg;
        cfg.argv = {"test_argv"};
        cfg.stack_size = 0x10000;
        auto res = UserStackBuilder::build_stack(uas, ppm, mem, cfg);
        TEST_CHECK(res.success && (res.final_sp_el0 % 16 == 0),
                   "Vector 9: User stack pointer satisfies strict 16-byte alignment");
    }

    // --- Vector 10: Syscall SYS_write with NULL buffer pointer ---
    {
        cpu.write_register(16, 4); // SYS_write
        cpu.write_register(0, 1);  // stdout
        cpu.write_register(1, 0);  // NULL pointer
        cpu.write_register(2, 16); // 16 bytes
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 14 /* EFAULT */, "Vector 10: SYS_write with NULL pointer returns EFAULT");
    }

    // --- Vector 11: Syscall SYS_write with Out-of-bounds kernel pointer ---
    {
        cpu.write_register(16, 4);
        cpu.write_register(0, 1);
        cpu.write_register(1, 0xFFFF800000001000ULL); // Kernel space address
        cpu.write_register(2, 16);
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 14 /* EFAULT */,
                   "Vector 11: SYS_write with kernel pointer returns EFAULT");
    }

    // --- Vector 12: Syscall SYS_read with NULL pointer ---
    {
        cpu.write_register(16, 3); // SYS_read
        cpu.write_register(0, 0);  // stdin
        cpu.write_register(1, 0);  // NULL pointer
        cpu.write_register(2, 16);
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 14 /* EFAULT */, "Vector 12: SYS_read with NULL pointer returns EFAULT");
    }

    // --- Vector 13: Syscall SYS_stat64 with NULL path pointer ---
    {
        cpu.write_register(16, 339); // SYS_stat64
        cpu.write_register(0, 0);    // NULL path
        cpu.write_register(1, 0x20000); // valid buffer
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 14 /* EFAULT */, "Vector 13: SYS_stat64 with NULL path returns EFAULT");
    }

    // --- Vector 14: Syscall SYS_stat64 with NULL stat buffer ---
    {
        // Write path string to valid user buffer 0x10000
        uas.copyin(0x10000, "/etc/hosts", 11);
        cpu.write_register(16, 339);
        cpu.write_register(0, 0x10000); // valid path
        cpu.write_register(1, 0);       // NULL stat buf
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 14 /* EFAULT */,
                   "Vector 14: SYS_stat64 with NULL stat buffer returns EFAULT");
    }

    // --- Vector 15: File Descriptor Double-Close Attack ---
    {
        const int test_fd = proc->fd_table().allocate_fd(FileType::Console, true, true, "/dev/test");
        TEST_CHECK(test_fd >= 0, "Allocated test fd");

        cpu.write_register(16, 6); // SYS_close
        cpu.write_register(0, static_cast<std::uint64_t>(test_fd));
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 0, "First close succeeded");

        // Second close must fail with EBADF
        cpu.write_register(0, static_cast<std::uint64_t>(test_fd));
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 9 /* EBADF */, "Vector 15: Double-close returns EBADF");
    }

    // --- Vector 16: File Descriptor Invalid Index Attack ---
    {
        cpu.write_register(16, 6);
        cpu.write_register(0, static_cast<std::uint64_t>(-1));
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 9 /* EBADF */, "Vector 16: Closing fd -1 returns EBADF");

        cpu.write_register(0, 9999);
        dispatcher.handle_syscall(cpu, *proc, uas, dummy_sched);
        TEST_CHECK(cpu.read_register(0) == 9 /* EBADF */, "Vector 16: Closing fd 9999 returns EBADF");
    }

    // --- Vector 17: Pipe Write on Closed Reader (EPIPE) ---
    {
        Pipe pipe;
        pipe.close_read();
        const char msg[] = "DATA";
        const std::int64_t ret = pipe.write(msg, 4);
        TEST_CHECK(ret == -32, "Vector 17: Writing to pipe with closed reader returns -EPIPE (32)");
    }

    // --- Vector 18: Pipe Read on Empty/Closed Writer (EOF) ---
    {
        Pipe pipe;
        pipe.close_write();
        char buf[8]{0};
        const std::int64_t ret = pipe.read(buf, sizeof(buf));
        TEST_CHECK(ret == 0, "Vector 18: Reading from pipe with closed writer returns 0 (EOF)");
    }

    // --- Vector 19: Pipe Buffer Overflow Attempt (Exceeding 64KB) ---
    {
        Pipe pipe;
        std::vector<char> large_payload(70000, 'X');
        const std::int64_t written = pipe.write(large_payload.data(), large_payload.size());
        TEST_CHECK(written == 65536, "Vector 19: Pipe capacity strictly bounded at 64KB (no host overflow)");
    }

    // --- Vector 20: VFS Symlink Infinite Recursion (ELOOP) ---
    {
        vfs.symlink("/tmp/loop_a", "/tmp/loop_b");
        vfs.symlink("/tmp/loop_b", "/tmp/loop_a");
        VfsStat st{};
        const bool stat_ok = vfs.stat("/tmp/loop_a", st);
        TEST_CHECK(!stat_ok, "Vector 20: Cyclic symlink resolution safely fails (ELOOP / cycle detected)");
    }

    // --- Vector 21: VFS Path Traversal Attack (/../../../../etc/hosts) ---
    {
        VfsStat st{};
        const bool stat_ok = vfs.stat("/../../../../etc/hosts", st);
        TEST_CHECK(stat_ok, "Vector 21: Root traversal safely normalized to /etc/hosts");
    }

    // --- Vector 22: Bootstrap Unauthorized Privileged Registration ---
    {
        BootstrapServiceRegistry reg;
        const int res = reg.check_in("com.apple.system.root", 50, 42 /* non-PID 1 */, true /* require_privilege */);
        TEST_CHECK(res == kBootstrapNotPrivileged,
                   "Vector 22: Non-PID 1 unauthorized privileged registration rejected");
    }

    // --- Vector 23: Bootstrap Duplicate Active Service Collision ---
    {
        BootstrapServiceRegistry reg;
        reg.check_in("com.example.service", 50, 10, false);
        const int res = reg.check_in("com.example.service", 51, 12, false);
        TEST_CHECK(res == kBootstrapNameInUse, "Vector 23: Duplicate active service registration rejected");
    }

    // --- Vector 24: Cross-Process Address Space Isolation ---
    {
        const std::uint64_t root_l2_b = ppm.allocate_page();
        UserAddressSpace uas_b(mem, ppm, root_l2_b, "sec_vm_b");
        TEST_CHECK(uas.translate(0x10000) != 0, "UAS A translates private page 0x10000");
        TEST_CHECK(uas_b.translate(0x10000) == 0, "Vector 24: UAS B cannot access UAS A private page");
    }

    // --- Vector 25: Deserialization Truncated Stream Attack ---
    {
        std::vector<std::uint8_t> corrupt_blob = {0x01, 0x02}; // Truncated header
        const std::uint8_t* ptr = corrupt_blob.data();
        std::size_t remaining = corrupt_blob.size();

        BsdProcessManager test_proc;
        TEST_CHECK(!test_proc.deserialize_stream(ptr, remaining),
                   "Vector 25: Deserialization of truncated blob safely rejected");
    }
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Stage 212.22: 25-Vector Adversarial Security Suite\n";
    std::cout << "=================================================================\n";

    run_adversarial_security_suite();

    if (g_failures == 0) {
        std::cout << "\n[PASS] All 25 Adversarial Security Vectors PASSED cleanly.\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] " << g_failures << " vector(s) FAILED in Stage 212.22.\n";
        return 1;
    }
}
