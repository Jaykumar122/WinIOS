#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: 16-Byte Stack Alignment Conformance
bool test_16byte_stack_alignment() {
    std::cout << "[Test 1] 16-Byte Stack Alignment Conformance...\n";

    Memory memory(4 * 1024 * 1024); // 4MB
    const std::uint64_t stack_top = 0x200000ULL; // 2MB

    // Test with varying argument lengths
    std::vector<std::vector<std::string>> arg_sets = {
        {"/bin/echo"},
        {"/bin/echo", "a"},
        {"/bin/echo", "hello", "world"},
        {"/bin/echo", "one", "two", "three", "four", "five", "six", "seven"}
    };

    for (const auto& args : arg_sets) {
        std::uint64_t out_sp = 0;
        std::string err;
        bool ok = UserspaceMachOLoader::construct_initial_stack(
            memory, stack_top, args, {"ENV1=A", "ENV2=B"}, args[0], out_sp, err);

        TEST_ASSERT(ok, "Stack construction must succeed");
        TEST_ASSERT((out_sp & 0xFULL) == 0, "Initial SP must be strictly 16-byte aligned (ARM64 Darwin ABI)");
        TEST_ASSERT(out_sp < stack_top, "SP must grow downward below stack_top");
    }

    return true;
}

// Test 2: Darwin Initial Stack Layout Verification (argc, argv, envp, apple vector)
bool test_darwin_initial_stack_layout() {
    std::cout << "[Test 2] Darwin Initial Stack Layout Verification...\n";

    Memory memory(4 * 1024 * 1024);
    const std::uint64_t stack_top = 0x200000ULL;

    std::vector<std::string> argv = {"/usr/bin/tool", "--verbose", "input.txt"};
    std::vector<std::string> envp = {"HOME=/var/root", "PATH=/bin:/usr/bin"};
    std::string exec_path = "/usr/bin/tool";

    std::uint64_t out_sp = 0;
    std::string err;
    bool ok = UserspaceMachOLoader::construct_initial_stack(
        memory, stack_top, argv, envp, exec_path, out_sp, err);
    TEST_ASSERT(ok, "Stack construction succeeded");

    // Read back stack values
    std::size_t sp_off = static_cast<std::size_t>(out_sp);
    std::uint64_t read_argc = memory.read_u64(sp_off);
    TEST_ASSERT(read_argc == 3, "argc must be 3");

    // argv pointers
    std::uint64_t argv0_ptr = memory.read_u64(sp_off + 8);
    std::uint64_t argv1_ptr = memory.read_u64(sp_off + 16);
    std::uint64_t argv2_ptr = memory.read_u64(sp_off + 24);
    std::uint64_t argv_null = memory.read_u64(sp_off + 32);

    TEST_ASSERT(argv0_ptr != 0 && argv1_ptr != 0 && argv2_ptr != 0, "argv pointers must be non-null");
    TEST_ASSERT(argv_null == 0, "argv list must be NULL terminated");

    // Helper to read C-string from memory
    auto read_str = [&](std::uint64_t ptr) {
        std::string s;
        std::size_t off = static_cast<std::size_t>(ptr);
        while (off < memory.size()) {
            char ch = static_cast<char>(memory.read_u8(off++));
            if (ch == '\0') break;
            s.push_back(ch);
        }
        return s;
    };

    TEST_ASSERT(read_str(argv0_ptr) == "/usr/bin/tool", "argv[0] content matches");
    TEST_ASSERT(read_str(argv1_ptr) == "--verbose", "argv[1] content matches");
    TEST_ASSERT(read_str(argv2_ptr) == "input.txt", "argv[2] content matches");

    // envp pointers
    std::uint64_t envp0_ptr = memory.read_u64(sp_off + 40);
    std::uint64_t envp1_ptr = memory.read_u64(sp_off + 48);
    std::uint64_t envp_null = memory.read_u64(sp_off + 56);
    TEST_ASSERT(read_str(envp0_ptr) == "HOME=/var/root", "envp[0] content matches");
    TEST_ASSERT(read_str(envp1_ptr) == "PATH=/bin:/usr/bin", "envp[1] content matches");
    TEST_ASSERT(envp_null == 0, "envp list must be NULL terminated");

    // apple vector
    std::uint64_t apple0_ptr = memory.read_u64(sp_off + 64);
    std::uint64_t apple_null = memory.read_u64(sp_off + 72);
    TEST_ASSERT(read_str(apple0_ptr) == "/usr/bin/tool", "apple[0] must contain executable_path");
    TEST_ASSERT(apple_null == 0, "apple list must be NULL terminated");

    return true;
}

// Test 3: Thread-Local Storage & TPIDR_EL0 Register Initialization
bool test_tpidr_el0_thread_pointer() {
    std::cout << "[Test 3] Thread-Local Storage & TPIDR_EL0 Initialization...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/sbin/launchd", 0);

    const std::uint64_t expected_tpidr = 0x7FFF1000ULL;
    std::uint64_t tid = bsd.create_thread(pid, 0x10001000ULL, 0x7FFF0000ULL, expected_tpidr);
    TEST_ASSERT(tid > 0, "Thread created");

    DarwinThreadContext ctx{};
    TEST_ASSERT(bsd.get_thread_context(pid, tid, ctx), "Thread context retrieved");
    TEST_ASSERT(ctx.tpidr_el0 == expected_tpidr, "Thread context records TPIDR_EL0 correctly");

    // Verify CPU system register persistence
    CPU cpu;
    cpu.set_tpidr_el0(expected_tpidr);
    TEST_ASSERT(cpu.get_tpidr_el0() == expected_tpidr, "CPU TPIDR_EL0 register getter/setter match");

    return true;
}

// Test 4: Entry Register State Conformance (PSTATE, SP_EL0, ELR_EL1)
bool test_entry_register_state() {
    std::cout << "[Test 4] Entry Register State Conformance...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(0); // EL0 Userspace
    cpu.set_sp_sel(false); // Use SP_EL0
    cpu.set_stack_pointer(0x7FFE0000ULL);
    cpu.set_program_counter(0x100001000ULL);

    TEST_ASSERT(cpu.get_current_el() == 0, "Current EL is EL0");
    TEST_ASSERT(cpu.uses_sp_el0(), "CPU is using SP_EL0");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x7FFE0000ULL, "Stack pointer matches SP_EL0");
    TEST_ASSERT(cpu.get_program_counter() == 0x100001000ULL, "Program counter matches entry point");

    return true;
}

// Test 5: Process Image & Descriptor Environment Initialization
bool test_process_descriptor_environment() {
    std::cout << "[Test 5] Process Image & Descriptor Environment Initialization...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/bin/sh", 501);

    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc != nullptr, "Process exists");
    TEST_ASSERT(proc->state == ProcessState::Running, "Starts in Running state");
    TEST_ASSERT(proc->ppid == 1, "PPID is 1 (launchd)");
    TEST_ASSERT(proc->uid == 501, "UID is 501 (mobile user)");

    // Standard POSIX file descriptors (0 = stdin, 1 = stdout, 2 = stderr)
    TEST_ASSERT(proc->fds.find(0) != proc->fds.end(), "FD 0 (stdin) initialized");
    TEST_ASSERT(proc->fds.find(1) != proc->fds.end(), "FD 1 (stdout) initialized");
    TEST_ASSERT(proc->fds.find(2) != proc->fds.end(), "FD 2 (stderr) initialized");

    return true;
}

// Test 6: Multi-Argument String Vector Serialization
bool test_large_argument_vector() {
    std::cout << "[Test 6] Multi-Argument String Vector Serialization...\n";

    Memory memory(4 * 1024 * 1024);
    const std::uint64_t stack_top = 0x200000ULL;

    std::vector<std::string> args = {"/usr/bin/cmd"};
    for (int i = 1; i <= 20; ++i) {
        args.push_back("arg_" + std::to_string(i));
    }

    std::vector<std::string> envs = {"A=1", "B=2", "C=3", "D=4", "E=5"};

    std::uint64_t out_sp = 0;
    std::string err;
    bool ok = UserspaceMachOLoader::construct_initial_stack(
        memory, stack_top, args, envs, args[0], out_sp, err);

    TEST_ASSERT(ok, "Stack construction with 21 args succeeded");
    TEST_ASSERT((out_sp & 0xFULL) == 0, "SP strictly 16-byte aligned");

    std::uint64_t read_argc = memory.read_u64(static_cast<std::size_t>(out_sp));
    TEST_ASSERT(read_argc == 21, "argc must be 21");

    return true;
}

// Test 7: Environment Termination & Status Propagation
bool test_environment_termination_propagation() {
    std::cout << "[Test 7] Environment Termination & Status Propagation...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/daemon", 0);

    bsd.exit_process(pid, 42);
    auto proc = bsd.get_process(pid);
    TEST_ASSERT(proc->state == ProcessState::Zombie, "Transitions to Zombie on exit");
    TEST_ASSERT(proc->exit_code == 42, "Exit code 42 recorded");

    int reaped_status = 0;
    std::int32_t reaped_pid = bsd.waitpid(1, static_cast<std::int32_t>(pid), reaped_status, 0);
    TEST_ASSERT(reaped_pid == static_cast<std::int32_t>(pid), "Reaped PID matches");
    TEST_ASSERT(reaped_status == 42, "Reaped status is 42");
    TEST_ASSERT(proc->state == ProcessState::Terminated, "Final state is Terminated");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 174: Minimal Guest Process Environment\n";
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

    run_test(test_16byte_stack_alignment, "16-Byte Stack Alignment Conformance");
    run_test(test_darwin_initial_stack_layout, "Darwin Initial Stack Layout Verification");
    run_test(test_tpidr_el0_thread_pointer, "TLS & TPIDR_EL0 Initialization");
    run_test(test_entry_register_state, "Entry Register State Conformance");
    run_test(test_process_descriptor_environment, "Process Descriptor Environment");
    run_test(test_large_argument_vector, "Multi-Argument Vector Serialization");
    run_test(test_environment_termination_propagation, "Environment Termination & Status Propagation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
