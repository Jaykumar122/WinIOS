// =============================================================================
// WinIOS — Stage 204.15: Environment, Arguments & Process Identity Tests
// =============================================================================

#include "loader/userspace_macho_loader.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// Helper to read null-terminated string from guest Memory
static std::string read_guest_string(const Memory& mem, std::uint64_t vaddr, std::size_t max_len = 1024) {
    std::string s;
    for (std::size_t i = 0; i < max_len; ++i) {
        if (vaddr + i >= mem.size()) break;
        char c = static_cast<char>(mem.read_u8(vaddr + i));
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

// -----------------------------------------------------------------------------
// Test 1: ARM64 Darwin Stack Staging (argc, argv[], envp[], apple[])
// -----------------------------------------------------------------------------
void test_arm64_stack_staging() {
    std::cout << "[Stage 204.15 Test 1] ARM64 Darwin Initial Userspace Stack Staging...\n";

    Memory mem(0x100000); // 1 MB
    std::uint64_t stack_top = 0x80000; // Place stack top inside RAM
    std::vector<std::string> argv = {"/Applications/Calculator.app/Calculator", "--verbose", "-n", "42"};
    std::vector<std::string> envp = {"PATH=/usr/bin:/bin", "USER=mobile", "HOME=/var/mobile", "SHELL=/bin/sh"};
    std::string exec_path = "/Applications/Calculator.app/Calculator";

    std::uint64_t sp = 0;
    std::string err;
    bool ok = UserspaceMachOLoader::construct_initial_stack(mem, stack_top, argv, envp, exec_path, sp, err);
    TEST_CHECK(ok, "Stack construction succeeded: " + err);
    TEST_CHECK(sp > 0 && sp < stack_top, "SP is valid and below stack top");
    TEST_CHECK((sp & 0xFULL) == 0, "Initial SP satisfies 16-byte ARM64 alignment");

    // 1. Read argc at SP
    std::uint64_t argc = mem.read_u64(sp);
    TEST_CHECK(argc == 4, "argc matches argv.size() (4)");

    // 2. Read argv pointers
    std::uint64_t curr_sp = sp + 8;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        std::uint64_t str_ptr = mem.read_u64(curr_sp);
        TEST_CHECK(str_ptr > 0 && str_ptr < stack_top, "argv pointer is within stack range");
        std::string val = read_guest_string(mem, str_ptr);
        TEST_CHECK(val == argv[i], "argv[" + std::to_string(i) + "] matches: " + val);
        curr_sp += 8;
    }
    // argv terminator NULL
    TEST_CHECK(mem.read_u64(curr_sp) == 0, "argv list is terminated by NULL");
    curr_sp += 8;

    // 3. Read envp pointers
    for (std::size_t i = 0; i < envp.size(); ++i) {
        std::uint64_t env_ptr = mem.read_u64(curr_sp);
        TEST_CHECK(env_ptr > 0 && env_ptr < stack_top, "envp pointer is within stack range");
        std::string val = read_guest_string(mem, env_ptr);
        TEST_CHECK(val == envp[i], "envp[" + std::to_string(i) + "] matches: " + val);
        curr_sp += 8;
    }
    // envp terminator NULL
    TEST_CHECK(mem.read_u64(curr_sp) == 0, "envp list is terminated by NULL");
    curr_sp += 8;

    // 4. Read apple[] pointers
    std::uint64_t apple0 = mem.read_u64(curr_sp);
    TEST_CHECK(apple0 > 0 && apple0 < stack_top, "apple[0] pointer is valid");
    std::string apple_str = read_guest_string(mem, apple0);
    TEST_CHECK(apple_str == exec_path, "apple[0] executable path matches: " + apple_str);
    curr_sp += 8;
    // apple terminator NULL
    TEST_CHECK(mem.read_u64(curr_sp) == 0, "apple list is terminated by NULL");
}

// -----------------------------------------------------------------------------
// Test 2: Process Credentials & Privilege Transitions
// -----------------------------------------------------------------------------
void test_process_credentials() {
    std::cout << "[Stage 204.15 Test 2] Process Credentials & Privilege Transitions...\n";

    DarwinBsdSubsystem bsd;

    // Process 1: Default launchd root process
    auto root_proc = bsd.get_process(1);
    TEST_CHECK(root_proc != nullptr, "launchd exists");
    TEST_CHECK(bsd.getuid(1) == 0, "launchd uid is 0");
    TEST_CHECK(bsd.geteuid(1) == 0, "launchd euid is 0");
    TEST_CHECK(bsd.getgid(1) == 0, "launchd gid is 0");
    TEST_CHECK(bsd.getegid(1) == 0, "launchd egid is 0");

    // Create mobile user process (UID 501)
    std::uint32_t mobile_pid = bsd.create_process(1, "/Applications/MobileApp", 501);
    TEST_CHECK(mobile_pid >= 2, "mobile app PID created");
    TEST_CHECK(bsd.getuid(mobile_pid) == 501, "mobile uid is 501");
    TEST_CHECK(bsd.geteuid(mobile_pid) == 501, "mobile euid is 501");
    TEST_CHECK(bsd.getgid(mobile_pid) == 501, "mobile gid is 501");
    TEST_CHECK(bsd.getegid(mobile_pid) == 501, "mobile egid is 501");

    // Test seteuid / setegid (e.g. temporary privilege drop or raise)
    TEST_CHECK(bsd.seteuid(mobile_pid, 502), "seteuid succeeded");
    TEST_CHECK(bsd.getuid(mobile_pid) == 501, "real uid unchanged");
    TEST_CHECK(bsd.geteuid(mobile_pid) == 502, "effective uid updated to 502");

    TEST_CHECK(bsd.setegid(mobile_pid, 502), "setegid succeeded");
    TEST_CHECK(bsd.getgid(mobile_pid) == 501, "real gid unchanged");
    TEST_CHECK(bsd.getegid(mobile_pid) == 502, "effective gid updated to 502");

    // Test setuid (changes both uid and euid)
    TEST_CHECK(bsd.setuid(mobile_pid, 1000), "setuid succeeded");
    TEST_CHECK(bsd.getuid(mobile_pid) == 1000, "uid is 1000");
    TEST_CHECK(bsd.geteuid(mobile_pid) == 1000, "euid is 1000");
}

// -----------------------------------------------------------------------------
// Test 3: Working Directory Operations (chdir, getcwd)
// -----------------------------------------------------------------------------
void test_working_directory_lifecycle() {
    std::cout << "[Stage 204.15 Test 3] Working Directory Operations (chdir & getcwd)...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/tool", 501);

    // Initial working directory should be "/"
    TEST_CHECK(bsd.getcwd(pid) == "/", "Initial cwd is /");

    // 1. Absolute chdir to existing directory "/usr/bin"
    TEST_CHECK(bsd.chdir(pid, "/usr/bin") == 0, "chdir /usr/bin succeeded");
    TEST_CHECK(bsd.getcwd(pid) == "/usr/bin", "cwd is /usr/bin");

    // 2. Relative chdir ".."
    TEST_CHECK(bsd.chdir(pid, "..") == 0, "chdir .. succeeded");
    TEST_CHECK(bsd.getcwd(pid) == "/usr", "cwd is /usr");

    // 3. Relative chdir "lib"
    TEST_CHECK(bsd.chdir(pid, "lib") == 0, "chdir lib succeeded");
    TEST_CHECK(bsd.getcwd(pid) == "/usr/lib", "cwd is /usr/lib");

    // 4. chdir to non-existent directory must fail with ENOENT (-2)
    TEST_CHECK(bsd.chdir(pid, "/nonexistent/path/xyz") == -2, "chdir to missing path returns -2 (ENOENT)");
    TEST_CHECK(bsd.getcwd(pid) == "/usr/lib", "cwd unchanged after failed chdir");

    // 5. chdir to regular file must fail with ENOTDIR (-20)
    bsd.vfs().write_file("/usr/lib/libtest.dylib", "stub", 4);
    TEST_CHECK(bsd.chdir(pid, "/usr/lib/libtest.dylib") == -20, "chdir to regular file returns -20 (ENOTDIR)");
    TEST_CHECK(bsd.getcwd(pid) == "/usr/lib", "cwd unchanged after ENOTDIR");
}

// -----------------------------------------------------------------------------
// Test 4: Syscall Dispatcher chdir & getcwd Integration
// -----------------------------------------------------------------------------
void test_chdir_getcwd_syscall_dispatch() {
    std::cout << "[Stage 204.15 Test 4] Syscall Dispatcher chdir & getcwd...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(1, "/usr/bin/testapp", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    Memory mem(0x100000);
    CPU cpu;

    // 1. SYS_chdir to "/tmp"
    const char dir[] = "/tmp";
    const std::uint64_t dir_va = 0x6000;
    std::memcpy(mem.raw_data() + dir_va, dir, sizeof(dir));

    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Chdir));
    cpu.write_register(0, dir_va);

    std::int64_t r_chdir = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_chdir == 0, "SYS_chdir succeeded");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) == 0, "Carry clear on chdir success");
    TEST_CHECK(bsd->getcwd(pid) == "/tmp", "Subsystem cwd is /tmp");

    // 2. SYS___getcwd (343)
    const std::uint64_t cwd_buf_va = 0x7000;
    std::memset(mem.raw_data() + cwd_buf_va, 0, 128);

    cpu.write_register(16, 343); // SYS___getcwd
    cpu.write_register(0, cwd_buf_va);
    cpu.write_register(1, 128);

    std::int64_t r_getcwd = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_getcwd == 0, "SYS___getcwd succeeded");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) == 0, "Carry clear on getcwd success");
    std::string read_cwd = read_guest_string(mem, cwd_buf_va);
    TEST_CHECK(read_cwd == "/tmp", "Retrieved cwd matches /tmp: " + read_cwd);
}

// -----------------------------------------------------------------------------
// Test 5: Fork Inheritance & Process Isolation
// -----------------------------------------------------------------------------
void test_fork_credentials_and_cwd_inheritance() {
    std::cout << "[Stage 204.15 Test 5] Fork Inheritance & Process Isolation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t parent_pid = bsd.create_process(1, "/usr/bin/parent", 501);
    bsd.chdir(parent_pid, "/Applications");
    bsd.seteuid(parent_pid, 505);
    bsd.set_process_args(parent_pid, {"parent_app", "--worker"}, {"PORT=8080", "MODE=prod"});

    // Fork child
    std::uint32_t child_pid = bsd.fork(parent_pid);
    TEST_CHECK(child_pid > parent_pid, "Child process created");

    // Check inherited values
    TEST_CHECK(bsd.getcwd(child_pid) == "/Applications", "Child inherited parent cwd");
    TEST_CHECK(bsd.getuid(child_pid) == 501, "Child inherited parent uid");
    TEST_CHECK(bsd.geteuid(child_pid) == 505, "Child inherited parent euid");
    auto child_argv = bsd.get_process_argv(child_pid);
    TEST_CHECK(child_argv.size() == 2 && child_argv[0] == "parent_app", "Child inherited argv");
    auto child_envp = bsd.get_process_envp(child_pid);
    TEST_CHECK(child_envp.size() == 2 && child_envp[0] == "PORT=8080", "Child inherited envp");

    // Verify isolation: modify child cwd and euid
    bsd.chdir(child_pid, "/tmp");
    bsd.seteuid(child_pid, 600);

    TEST_CHECK(bsd.getcwd(child_pid) == "/tmp", "Child cwd changed to /tmp");
    TEST_CHECK(bsd.getcwd(parent_pid) == "/Applications", "Parent cwd isolated and unchanged");
    TEST_CHECK(bsd.geteuid(child_pid) == 600, "Child euid changed to 600");
    TEST_CHECK(bsd.geteuid(parent_pid) == 505, "Parent euid isolated and unchanged");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Running Stage 204.15: Environment, Arguments & Process Identity Tests\n";
    std::cout << "=================================================================\n";

    test_arm64_stack_staging();
    test_process_credentials();
    test_working_directory_lifecycle();
    test_chdir_getcwd_syscall_dispatch();
    test_fork_credentials_and_cwd_inheritance();

    if (g_failed_tests == 0) {
        std::cout << "=================================================================\n";
        std::cout << "Stage 204.15: ALL TESTS PASSED!\n";
        std::cout << "=================================================================\n";
        return 0;
    } else {
        std::cerr << "=================================================================\n";
        std::cerr << "Stage 204.15: " << g_failed_tests << " TESTS FAILED!\n";
        std::cerr << "=================================================================\n";
        return 1;
    }
}
