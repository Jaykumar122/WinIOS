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
    mem.write_u32(vbar_el1 + 0x400, 0xD503201Fu); // NOP at lower EL synchronous vector
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

// Test 1: SYS_open Conformance (Flags, Path Resolution, Carry Flag)
bool test_bsd_sys_open_conformance() {
    std::cout << "[Test 1] Darwin BSD SYS_open (5) Conformance...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    bsd.vfs().write_file("/var/log/system.log", "boot complete\n", 14);

    const char path_ok[] = "/var/log/system.log";
    const char path_bad[] = "/var/log/missing.log";
    std::memcpy(mem.raw_data() + 0x2000, path_ok, sizeof(path_ok));
    std::memcpy(mem.raw_data() + 0x2100, path_bad, sizeof(path_bad));

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Open existing file
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2840000u); // mov x0, #0x2000
    mem.write_u32(0x1004, 0xD2800001u); // mov x1, #0 (O_RDONLY)
    mem.write_u32(0x1008, 0xD28000B0u); // mov x16, #5 (SYS_open)
    mem.write_u32(0x100C, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 4; ++i) engine.step_one(result, config);

    std::uint64_t fd = cpu.read_register(0);
    TEST_ASSERT(fd >= 3, "SYS_open must allocate descriptor >= 3");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be cleared on open success");

    // Open missing file
    setup_guest_cpu(cpu, mem, 0x1020);
    mem.write_u32(0x1020, 0xD2842000u); // mov x0, #0x2100
    mem.write_u32(0x1024, 0xD2800001u); // mov x1, #0
    mem.write_u32(0x1028, 0xD28000B0u); // mov x16, #5
    mem.write_u32(0x102C, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 4; ++i) engine.step_one(result, config);

    TEST_ASSERT(static_cast<std::int64_t>(cpu.read_register(0)) == -1, "SYS_open failure must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be set on open failure");

    return true;
}

// Test 2: SYS_read and SYS_write Round-Trip Conformance
bool test_bsd_sys_read_write_conformance() {
    std::cout << "[Test 2] Darwin BSD SYS_read (3) & SYS_write (4) Round-Trip...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. SYS_write to stdout (fd=1)
    const char banner[] = "Apple iOS ARM64 Userspace\n";
    std::memcpy(mem.raw_data() + 0x3000, banner, sizeof(banner));

    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2800020u); // mov x0, #1 (stdout)
    mem.write_u32(0x1004, 0xD2860001u); // mov x1, #0x3000 (buf)
    mem.write_u32(0x1008, 0xD2800342u); // mov x2, #26 (count)
    mem.write_u32(0x100C, 0xD2800090u); // mov x16, #4 (SYS_write)
    mem.write_u32(0x1010, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 5; ++i) engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 26, "SYS_write must return bytes written (26)");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be clear on stdout write");
    TEST_ASSERT(result.stdout_output.find("Apple iOS ARM64 Userspace") != std::string::npos,
                "Stdout buffer must capture guest output");

    // 2. SYS_read from created file
    bsd.vfs().write_file("/data/config.plist", "<dict></dict>", 13);
    int fd = bsd.open(1, "/data/config.plist", 0);
    TEST_ASSERT(fd >= 0, "Host open must succeed");

    setup_guest_cpu(cpu, mem, 0x1020);
    mem.write_u32(0x1020, 0xD2800000u | (static_cast<std::uint32_t>(fd) << 5u)); // mov x0, #fd
    mem.write_u32(0x1024, 0xD2880001u); // mov x1, #0x4000 (dest buffer)
    mem.write_u32(0x1028, 0xD28001A2u); // mov x2, #13 (count)
    mem.write_u32(0x102C, 0xD2800070u); // mov x16, #3 (SYS_read)
    mem.write_u32(0x1030, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 5; ++i) engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 13, "SYS_read must return 13 bytes read");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be clear on read success");

    char read_buf[14] = {0};
    std::memcpy(read_buf, mem.raw_data() + 0x4000, 13);
    TEST_ASSERT(std::strcmp(read_buf, "<dict></dict>") == 0,
                "Memory must contain exact data read from file");

    return true;
}

// Test 3: SYS_close Conformance and Double-Close Detection
bool test_bsd_sys_close_conformance() {
    std::cout << "[Test 3] Darwin BSD SYS_close (6) Lifecycle & Double-Close...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    bsd.vfs().write_file("/tmp/scratch.txt", "abc", 3);
    int fd = bsd.open(1, "/tmp/scratch.txt", 0);
    TEST_ASSERT(fd >= 3, "Valid descriptor must be >= 3");

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // First close: success
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2800000u | (static_cast<std::uint32_t>(fd) << 5u)); // mov x0, #fd
    mem.write_u32(0x1004, 0xD28000D0u); // mov x16, #6 (SYS_close)
    mem.write_u32(0x1008, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 3; ++i) engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 0, "SYS_close must return 0 on success");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be cleared on close success");

    // Second close: double-close error (EBADF)
    setup_guest_cpu(cpu, mem, 0x1010);
    mem.write_u32(0x1010, 0xD2800000u | (static_cast<std::uint32_t>(fd) << 5u)); // mov x0, #fd
    mem.write_u32(0x1014, 0xD28000D0u); // mov x16, #6 (SYS_close)
    mem.write_u32(0x1018, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 3; ++i) engine.step_one(result, config);

    TEST_ASSERT(static_cast<std::int64_t>(cpu.read_register(0)) == -1, "Double SYS_close must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be set on double-close");

    return true;
}

// Test 4: Identity Syscalls: SYS_getpid (20), SYS_getuid (24), SYS_geteuid (25)
bool test_bsd_identity_syscalls() {
    std::cout << "[Test 4] Darwin BSD Identity Syscalls: getpid (20), getuid (24), geteuid (25)...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create process 42 with uid 501
    bsd.create_process_with_pid(42, 1, "/usr/bin/test_app", 501);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. SYS_getpid (20)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2800290u); // mov x16, #20 (SYS_getpid)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be clear on SYS_getpid");

    // 2. SYS_getuid (24)
    setup_guest_cpu(cpu, mem, 0x1010);
    mem.write_u32(0x1010, 0xD2800310u); // mov x16, #24 (SYS_getuid)
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be clear on SYS_getuid");

    // 3. SYS_geteuid (25)
    setup_guest_cpu(cpu, mem, 0x1020);
    mem.write_u32(0x1020, 0xD2800330u); // mov x16, #25 (SYS_geteuid)
    mem.write_u32(0x1024, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be clear on SYS_geteuid");

    return true;
}

// Test 5: SYS_mmap (197) and SYS_munmap (73) Memory Allocation
bool test_bsd_mmap_munmap_conformance() {
    std::cout << "[Test 5] Darwin BSD SYS_mmap (197) and SYS_munmap (73)...\n";

    Memory mem(2 * 1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // SYS_mmap(addr=0, len=16384, prot=PROT_READ|PROT_WRITE, flags=MAP_ANON|MAP_PRIVATE, fd=-1, offset=0)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2800000u); // mov x0, #0 (addr hint)
    mem.write_u32(0x1004, 0xD2880001u); // mov x1, #16384 (len = 16KB)
    mem.write_u32(0x1008, 0xD28018B0u); // mov x16, #197 (SYS_mmap)
    mem.write_u32(0x100C, 0xD4001001u); // svc #0x80

    for (int i = 0; i < 4; ++i) engine.step_one(result, config);

    std::uint64_t mapped_addr = cpu.read_register(0);
    TEST_ASSERT(mapped_addr != 0 && mapped_addr != static_cast<std::uint64_t>(-1),
                "SYS_mmap must return valid non-zero virtual address");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be cleared on successful mmap");

    // SYS_munmap(addr=mapped_addr, len=16384)
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, mapped_addr);
    cpu.write_register(1, 16384);
    mem.write_u32(0x1010, 0xD2800930u); // mov x16, #73 (SYS_munmap)
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 0, "SYS_munmap must return 0");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be cleared on munmap");

    return true;
}

// Test 6: Unimplemented Syscall Returns ENOSYS (78) with Carry Flag Set
bool test_bsd_unimplemented_syscall_enosys() {
    std::cout << "[Test 6] Unimplemented Syscall Returns ENOSYS (78) with Carry Flag Set...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // SYS_unimplemented (e.g. 999)
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2807CE0u); // mov x16, #999
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 78u, "Unimplemented syscall must return ENOSYS (78) in X0");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be set for ENOSYS");

    return true;
}

// Test 7: SYS_exit (1) Terminates Process and Sets Exit Code
bool test_bsd_sys_exit_termination() {
    std::cout << "[Test 7] Darwin BSD SYS_exit (1) Process Termination...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2800540u); // mov x0, #42 (exit status)
    mem.write_u32(0x1004, 0xD2800030u); // mov x16, #1 (SYS_exit)
    mem.write_u32(0x1008, 0xD4001001u); // svc #0x80

    engine.step_one(result, config); // mov x0, #42
    engine.step_one(result, config); // mov x16, #1
    bool can_continue = engine.step_one(result, config); // svc #0x80

    TEST_ASSERT(!can_continue, "Execution loop must terminate on SYS_exit");
    TEST_ASSERT(result.process_exited, "process_exited flag must be true");
    TEST_ASSERT(result.exit_code == 42, "exit_code must be 42");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 158: Darwin BSD Syscall ABI Conformance\n";
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

    run_test(test_bsd_sys_open_conformance, "Darwin BSD SYS_open Conformance");
    run_test(test_bsd_sys_read_write_conformance, "SYS_read and SYS_write Round-Trip");
    run_test(test_bsd_sys_close_conformance, "SYS_close and Double-Close EBADF");
    run_test(test_bsd_identity_syscalls, "Identity Syscalls (getpid, getuid, geteuid)");
    run_test(test_bsd_mmap_munmap_conformance, "SYS_mmap and SYS_munmap Memory Allocation");
    run_test(test_bsd_unimplemented_syscall_enosys, "Unimplemented Syscall ENOSYS (78)");
    run_test(test_bsd_sys_exit_termination, "SYS_exit Process Termination (Exit Code 42)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
