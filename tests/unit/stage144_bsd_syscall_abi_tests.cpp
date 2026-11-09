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

// Helper: check if Carry flag (bit 1 of 4-bit NZCV, mask 0x2u) is set
bool is_carry_set(const CPU& cpu) noexcept {
    return (cpu.get_nzcv() & 0x2u) != 0;
}

// Test 1: SYS_open Success and Failure with Carry Flag
bool test_sys_open_abi() {
    std::cout << "[Test 1] SYS_open (5) ABI: Success and failure handling with carry flag...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create a known file in VFS
    bsd.vfs().write_file("/etc/hosts", "127.0.0.1 localhost\n", 20);

    // Write path strings into guest memory
    const char valid_path[] = "/etc/hosts";
    const char invalid_path[] = "/nonexistent/file.txt";
    std::memcpy(mem.raw_data() + 0x2000, valid_path, sizeof(valid_path));
    std::memcpy(mem.raw_data() + 0x2100, invalid_path, sizeof(invalid_path));

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Open valid file
    setup_guest_cpu(cpu, mem, 0x1000);
    mem.write_u32(0x1000, 0xD2840000u); // mov x0, #0x2000 (path)
    mem.write_u32(0x1004, 0xD2800001u); // mov x1, #0 (O_RDONLY)
    mem.write_u32(0x1008, 0xD28000B0u); // mov x16, #5 (SYS_open)
    mem.write_u32(0x100C, 0xD4001001u); // svc #0x80

    engine.step_one(result, config); // mov x0
    engine.step_one(result, config); // mov x1
    engine.step_one(result, config); // mov x16
    engine.step_one(result, config); // svc #0x80

    std::uint64_t valid_fd = cpu.read_register(0);
    TEST_ASSERT(valid_fd >= 3, "SYS_open must return allocated fd >= 3");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on SYS_open success");

    // 2. Open nonexistent file
    setup_guest_cpu(cpu, mem, 0x1020);
    mem.write_u32(0x1020, 0xD2842000u); // mov x0, #0x2100 (invalid path)
    mem.write_u32(0x1024, 0xD2800001u); // mov x1, #0
    mem.write_u32(0x1028, 0xD28000B0u); // mov x16, #5
    mem.write_u32(0x102C, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);
    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int64_t err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "SYS_open on nonexistent path must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on SYS_open failure");

    return true;
}

// Test 2: SYS_close Success, Double Close, and Invalid FD
bool test_sys_close_abi() {
    std::cout << "[Test 2] SYS_close (6) ABI: Success, double close, and invalid fd...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    bsd.vfs().write_file("/tmp/log.txt", "log entry\n", 10);
    int fd = bsd.open(1, "/tmp/log.txt", 0);
    TEST_ASSERT(fd >= 3, "Open must succeed");

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Close valid fd
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, static_cast<std::uint64_t>(fd)); // x0 = fd
    mem.write_u32(0x1000, 0xD28000D0u); // mov x16, #6 (SYS_close)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config); // mov x16
    engine.step_one(result, config); // svc #0x80

    TEST_ASSERT(cpu.read_register(0) == 0, "SYS_close must return 0 on success");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on SYS_close success");

    // 2. Double close of same fd
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    mem.write_u32(0x1010, 0xD28000D0u); // mov x16, #6
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int64_t err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "Double close must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on double close");

    // 3. Close arbitrary invalid fd (e.g. 999)
    setup_guest_cpu(cpu, mem, 0x1020);
    cpu.write_register(0, 999);
    mem.write_u32(0x1020, 0xD28000D0u); // mov x16, #6
    mem.write_u32(0x1024, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "Close invalid fd must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on invalid fd close");

    return true;
}

// Test 3: SYS_write to Stdout and File, and Invalid FD
bool test_sys_write_abi() {
    std::cout << "[Test 3] SYS_write (4) ABI: Stdout, VFS node write, and invalid fd...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    bsd.vfs().write_file("/var/run/test.pid", "", 0);
    int fd = bsd.open(1, "/var/run/test.pid", 1);
    TEST_ASSERT(fd >= 3, "Open must succeed");

    const char msg[] = "12345\n";
    std::memcpy(mem.raw_data() + 0x3000, msg, sizeof(msg) - 1);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Write to stdout (fd=1)
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0x3000); // buffer
    cpu.write_register(2, sizeof(msg) - 1); // count = 6
    mem.write_u32(0x1000, 0xD2800090u); // mov x16, #4 (SYS_write)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 6, "SYS_write to stdout must return count 6");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on stdout write");
    TEST_ASSERT(result.stdout_output == "12345\n", "Stdout output must match written message");

    // 2. Write to VFS file fd
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, 0x3000);
    cpu.write_register(2, 5); // write "12345"
    mem.write_u32(0x1010, 0xD2800090u); // mov x16, #4
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 5, "SYS_write to file must return 5");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on file write");

    auto file_bytes = bsd.vfs().read_file("/var/run/test.pid");
    TEST_ASSERT(file_bytes.has_value(), "File must exist");
    TEST_ASSERT(file_bytes->size() == 5, "File size must be 5 bytes");

    // 3. Write to invalid fd (999)
    setup_guest_cpu(cpu, mem, 0x1020);
    cpu.write_register(0, 999);
    cpu.write_register(1, 0x3000);
    cpu.write_register(2, 5);
    mem.write_u32(0x1020, 0xD2800090u); // mov x16, #4
    mem.write_u32(0x1024, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int64_t err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "SYS_write to invalid fd must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on invalid write");

    return true;
}

// Test 4: SYS_read Full, Partial, EOF, and Invalid FD
bool test_sys_read_abi() {
    std::cout << "[Test 4] SYS_read (3) ABI: Full read, partial read, EOF, and invalid fd...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Create a 10-byte file: "ABCDEFGHIJ"
    bsd.vfs().write_file("/data/alphabet.txt", "ABCDEFGHIJ", 10);
    int fd = bsd.open(1, "/data/alphabet.txt", 0);
    TEST_ASSERT(fd >= 3, "Open must succeed");

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Partial read: request 4 bytes
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, 0x5000); // destination buffer in RAM
    cpu.write_register(2, 4);      // request 4 bytes
    mem.write_u32(0x1000, 0xD2800070u); // mov x16, #3 (SYS_read)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 4, "Partial read must return 4");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR");
    TEST_ASSERT(mem.read_u8(0x5000) == 'A' && mem.read_u8(0x5003) == 'D', "Buffer must contain ABCD");

    // 2. Read next 6 bytes (reaches end of file)
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, 0x5010);
    cpu.write_register(2, 20); // request 20 bytes (only 6 remain)
    mem.write_u32(0x1010, 0xD2800070u); // mov x16, #3
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 6, "Read remaining must return 6");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR");
    TEST_ASSERT(mem.read_u8(0x5010) == 'E' && mem.read_u8(0x5015) == 'J', "Buffer must contain EFGHIJ");

    // 3. Read at EOF: must return 0
    setup_guest_cpu(cpu, mem, 0x1020);
    cpu.write_register(0, static_cast<std::uint64_t>(fd));
    cpu.write_register(1, 0x5020);
    cpu.write_register(2, 10);
    mem.write_u32(0x1020, 0xD2800070u); // mov x16, #3
    mem.write_u32(0x1024, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    TEST_ASSERT(cpu.read_register(0) == 0, "Read at EOF must return 0");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on EOF");

    // 4. Read from invalid fd: must return -1
    setup_guest_cpu(cpu, mem, 0x1030);
    cpu.write_register(0, 888);
    cpu.write_register(1, 0x5030);
    cpu.write_register(2, 10);
    mem.write_u32(0x1030, 0xD2800070u); // mov x16, #3
    mem.write_u32(0x1034, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int64_t err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "Read from invalid fd must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on invalid read");

    return true;
}

// Test 5: SYS_getuid (24) for Root and Non-Root Users
bool test_sys_getuid_abi() {
    std::cout << "[Test 5] SYS_getuid (24) ABI: Root and non-root UID...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Process 1 defaults to root (uid=0)
    {
        DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
        setup_guest_cpu(cpu, mem, 0x1000);
        mem.write_u32(0x1000, 0xD2800310u); // mov x16, #24 (SYS_getuid)
        mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

        engine.step_one(result, config);
        engine.step_one(result, config);

        TEST_ASSERT(cpu.read_register(0) == 0, "Process 1 (launchd) UID must be 0");
        TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR");
    }

    // Process 501 (mobile user)
    std::uint32_t mobile_pid = bsd.create_process_with_pid(501, 1, "/Applications/Preferences.app", 501);
    {
        DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
        engine.execute_macho(nullptr, 0, config, mobile_pid); // updates current_pid_
        setup_guest_cpu(cpu, mem, 0x2000);
        mem.write_u32(0x2000, 0xD2800310u); // mov x16, #24 (SYS_getuid)
        mem.write_u32(0x2004, 0xD4001001u); // svc #0x80

        engine.step_one(result, config);
        engine.step_one(result, config);

        TEST_ASSERT(cpu.read_register(0) == 501, "Mobile process UID must be 501");
        TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR");
    }

    return true;
}

// Test 6: SYS_mmap (197) Allocation, Alignment, and Zero Length Rejection
bool test_sys_mmap_abi() {
    std::cout << "[Test 6] SYS_mmap (197) ABI: 16KB alignment and zero length error...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // 1. Allocate 32 KB anonymous memory (len = 0x8000)
    setup_guest_cpu(cpu, mem, 0x1000);
    cpu.write_register(0, 0);       // addr = 0 (system choice)
    cpu.write_register(1, 0x8000);  // len = 32 KB
    cpu.write_register(2, 3);       // PROT_READ | PROT_WRITE
    cpu.write_register(3, 0x1002);  // MAP_ANON | MAP_PRIVATE
    cpu.write_register(4, static_cast<std::uint64_t>(-1)); // fd = -1
    cpu.write_register(5, 0);       // offset = 0
    mem.write_u32(0x1000, 0xD28018B0u); // mov x16, #197 (SYS_mmap)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::uint64_t mapped_addr = cpu.read_register(0);
    TEST_ASSERT(mapped_addr != 0 && mapped_addr != static_cast<std::uint64_t>(-1), "mmap must return valid address");
    TEST_ASSERT((mapped_addr & 0x3FFFULL) == 0, "mmap address must be 16 KB page-aligned");
    TEST_ASSERT(!is_carry_set(cpu), "Carry flag must be CLEAR on mmap success");

    // 2. Reject zero length mmap
    setup_guest_cpu(cpu, mem, 0x1010);
    cpu.write_register(0, 0);
    cpu.write_register(1, 0);       // len = 0 (invalid)
    cpu.write_register(2, 3);
    cpu.write_register(3, 0x1002);
    cpu.write_register(4, static_cast<std::uint64_t>(-1));
    cpu.write_register(5, 0);
    mem.write_u32(0x1010, 0xD28018B0u); // mov x16, #197
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int64_t err_ret = static_cast<std::int64_t>(cpu.read_register(0));
    TEST_ASSERT(err_ret == -1, "Zero-length mmap must return -1");
    TEST_ASSERT(is_carry_set(cpu), "Carry flag must be SET on zero-length mmap");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 144: DARWIN BSD SYSCALL ABI VERIFICATION SUITE   \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_sys_open_abi, "SYS_open (5) ABI and Carry Flag Handling");
    run_test(test_sys_close_abi, "SYS_close (6) ABI: Success, Double Close, Invalid FD");
    run_test(test_sys_write_abi, "SYS_write (4) ABI: Stdout, File Write, Invalid FD");
    run_test(test_sys_read_abi, "SYS_read (3) ABI: Full, Partial, EOF, Invalid FD");
    run_test(test_sys_getuid_abi, "SYS_getuid (24) ABI for Root and Non-Root");
    run_test(test_sys_mmap_abi, "SYS_mmap (197) ABI: 16KB Alignment and Zero Len");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 144 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
