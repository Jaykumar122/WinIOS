// ==============================================================================
// WinIOS Campaign 203 Stage 203.21: Process Startup and Initial Thread State Tests
// ==============================================================================
// Independent verification of:
// 1. ARM64 Darwin userspace stack layout: argc, argv[], envp[], and apple[] vector
// 2. 16-byte stack pointer alignment compliant with ARM64 Darwin ABI
// 3. Guest register configuration: SP, PC, X0 (argc), X1 (argv), X2 (envp), X3 (apple)
// 4. NULL terminators separating argv, envp, and apple pointer tables
// 5. Automatic apple[] dictionary generation (executable_path, stack_guard, etc.)
// 6. Parameter validation, bounds checking, and kernel-address rejection
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/darwin_process_startup.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios::runtime;
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

// Helper to read null-terminated string from memory
std::string read_guest_string(const Memory& mem, std::uint64_t vaddr, std::uint64_t stack_top) {
    std::string s;
    std::uint64_t curr = vaddr;
    while (true) {
        std::size_t off = DarwinProcessStartup::translate_vaddr(curr, mem.size(), stack_top);
        if (off >= mem.size()) break;
        std::uint8_t b = mem.read_u8(off);
        if (b == 0) break;
        s.push_back(static_cast<char>(b));
        ++curr;
    }
    return s;
}

// -----------------------------------------------------------------------------
// Test 1: Standard Darwin Launchd Process Startup Stack & Registers
// -----------------------------------------------------------------------------
void test_standard_launchd_startup() {
    std::cout << "[Stage 203.21 Test 1] Standard Darwin Launchd Process Startup...\n";

    Memory mem(0x100000); // 1MB guest memory
    CPU cpu;

    DarwinProcessStartupConfig cfg{};
    cfg.stack_top = 0x80000ULL; // 512KB stack top
    cfg.entry_point = 0x1000ULL;
    cfg.executable_path = "/sbin/launchd";
    cfg.argv = {"/sbin/launchd", "--verbose"};
    cfg.envp = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "HOME=/var/root"};
    cfg.stack_guard = 0xCAFEBABE12345678ULL;
    cfg.malloc_entropy = 0xAA55AA5511223344ULL;
    cfg.populate_registers = true;

    DarwinProcessStartupInfo info{};
    std::string err;
    bool ok = DarwinProcessStartup::setup_process(cpu, mem, cfg, info, err);
    TEST_CHECK(ok, "setup_process succeeds for launchd configuration");
    TEST_CHECK(err.empty(), "No error reported");

    // Verify 16-byte SP alignment
    TEST_CHECK((info.initial_sp & 0xFULL) == 0, "Initial SP is 16-byte aligned");
    TEST_CHECK(cpu.get_stack_pointer() == info.initial_sp, "CPU SP matches initial_sp");
    TEST_CHECK(cpu.get_program_counter() == cfg.entry_point, "CPU PC matches entry_point");

    // Verify CPU General-Purpose Registers
    TEST_CHECK(cpu.read_register(0) == 2, "X0 holds argc (2)");
    TEST_CHECK(cpu.read_register(1) == info.argv_ptr, "X1 holds argv pointer slot address");
    TEST_CHECK(cpu.read_register(2) == info.envp_ptr, "X2 holds envp pointer slot address");
    TEST_CHECK(cpu.read_register(3) == info.apple_ptr, "X3 holds apple pointer slot address");

    // Verify Stack Memory Layout at [SP]
    std::size_t sp_off = DarwinProcessStartup::translate_vaddr(info.initial_sp, mem.size(), cfg.stack_top);
    TEST_CHECK(sp_off < mem.size(), "SP offset in physical memory bounds");

    // [SP] = argc
    std::uint64_t mem_argc = mem.read_u64(sp_off);
    TEST_CHECK(mem_argc == 2, "Memory at [SP] contains argc 2");

    // argv array
    std::size_t argv_off = DarwinProcessStartup::translate_vaddr(info.argv_ptr, mem.size(), cfg.stack_top);
    std::uint64_t arg0_ptr = mem.read_u64(argv_off);
    std::uint64_t arg1_ptr = mem.read_u64(argv_off + 8);
    std::uint64_t argv_null = mem.read_u64(argv_off + 16);
    TEST_CHECK(argv_null == 0, "argv list is NULL-terminated");
    TEST_CHECK(read_guest_string(mem, arg0_ptr, cfg.stack_top) == "/sbin/launchd", "argv[0] string matches");
    TEST_CHECK(read_guest_string(mem, arg1_ptr, cfg.stack_top) == "--verbose", "argv[1] string matches");

    // envp array
    std::size_t envp_off = DarwinProcessStartup::translate_vaddr(info.envp_ptr, mem.size(), cfg.stack_top);
    std::uint64_t env0_ptr = mem.read_u64(envp_off);
    std::uint64_t env1_ptr = mem.read_u64(envp_off + 8);
    std::uint64_t envp_null = mem.read_u64(envp_off + 16);
    TEST_CHECK(envp_null == 0, "envp list is NULL-terminated");
    TEST_CHECK(read_guest_string(mem, env0_ptr, cfg.stack_top) == "PATH=/usr/bin:/bin:/usr/sbin:/sbin", "envp[0] string matches");
    TEST_CHECK(read_guest_string(mem, env1_ptr, cfg.stack_top) == "HOME=/var/root", "envp[1] string matches");

    // apple array
    std::size_t apple_off = DarwinProcessStartup::translate_vaddr(info.apple_ptr, mem.size(), cfg.stack_top);
    std::uint64_t app0_ptr = mem.read_u64(apple_off);
    std::uint64_t app1_ptr = mem.read_u64(apple_off + 8);
    std::uint64_t app2_ptr = mem.read_u64(apple_off + 16);
    std::uint64_t app3_ptr = mem.read_u64(apple_off + 24);
    std::uint64_t apple_null = mem.read_u64(apple_off + 32);
    TEST_CHECK(apple_null == 0, "apple list is NULL-terminated");
    TEST_CHECK(read_guest_string(mem, app0_ptr, cfg.stack_top) == "executable_path=/sbin/launchd", "apple[0] executable_path");
    TEST_CHECK(read_guest_string(mem, app1_ptr, cfg.stack_top) == "stack_guard=0xcafebabe12345678", "apple[1] stack_guard");
    TEST_CHECK(read_guest_string(mem, app2_ptr, cfg.stack_top) == "malloc_entropy=0xaa55aa5511223344", "apple[2] malloc_entropy");
    TEST_CHECK(read_guest_string(mem, app3_ptr, cfg.stack_top) == "pfz=0x100000000", "apple[3] pfz pagezero");

    std::cout << "  -> Standard launchd startup verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Custom Multi-Argument and Custom Apple Dictionary
// -----------------------------------------------------------------------------
void test_custom_arguments_and_apple_dict() {
    std::cout << "[Stage 203.21 Test 2] Custom Multi-Argument and Custom Apple Dictionary...\n";

    Memory mem(0x100000);
    CPU cpu;

    DarwinProcessStartupConfig cfg{};
    cfg.stack_top = 0x90000ULL;
    cfg.entry_point = 0x4000ULL;
    cfg.executable_path = "/usr/bin/codesign";
    cfg.argv = {"codesign", "-s", "Apple Development", "-f", "/Applications/Test.app"};
    cfg.envp = {"SECURITYSESSIONID=100004"};
    cfg.apple_entries = {
        "executable_path=/usr/bin/codesign",
        "custom_key=custom_value_42",
        "arm64e_abi_version=2"
    };

    DarwinProcessStartupInfo info{};
    std::string err;
    bool ok = DarwinProcessStartup::setup_process(cpu, mem, cfg, info, err);
    TEST_CHECK(ok, "Custom process setup succeeds");
    TEST_CHECK(info.argc == 5, "argc is 5");
    TEST_CHECK(cpu.read_register(0) == 5, "X0 holds argc 5");

    // Check argv pointers count
    TEST_CHECK(info.argv_string_ptrs.size() == 5, "5 argv string pointers registered");
    for (std::size_t i = 0; i < cfg.argv.size(); ++i) {
        TEST_CHECK(read_guest_string(mem, info.argv_string_ptrs[i], cfg.stack_top) == cfg.argv[i],
                   "argv element matches exactly");
    }

    // Check custom apple entries
    TEST_CHECK(info.apple_string_ptrs.size() == 3, "3 custom apple string pointers registered");
    for (std::size_t i = 0; i < cfg.apple_entries.size(); ++i) {
        TEST_CHECK(read_guest_string(mem, info.apple_string_ptrs[i], cfg.stack_top) == cfg.apple_entries[i],
                   "apple element matches custom entry");
    }

    std::cout << "  -> Custom arguments and apple dictionary verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: High Canonical Userspace Address Stack Translation
// -----------------------------------------------------------------------------
void test_high_canonical_address_stack() {
    std::cout << "[Stage 203.21 Test 3] High Canonical Userspace Address Stack Translation...\n";

    Memory mem(0x100000); // 1MB physical memory backing
    CPU cpu;

    DarwinProcessStartupConfig cfg{};
    // Canonical 48-bit userspace stack top: 0x0000700000080000
    cfg.stack_top = 0x0000700000080000ULL;
    cfg.entry_point = 0x100000000ULL;
    cfg.executable_path = "/usr/bin/id";
    cfg.argv = {"id", "-u"};
    cfg.envp.clear(); // Empty envp

    DarwinProcessStartupInfo info{};
    std::string err;
    bool ok = DarwinProcessStartup::setup_process(cpu, mem, cfg, info, err);
    TEST_CHECK(ok, "High canonical address stack setup succeeds");
    TEST_CHECK((info.initial_sp & 0xFULL) == 0, "High stack SP is 16-byte aligned");

    // Verify virtual-to-physical address translation
    std::size_t off = DarwinProcessStartup::translate_vaddr(info.initial_sp, mem.size(), cfg.stack_top);
    TEST_CHECK(off < mem.size(), "High stack virtual SP correctly translates into physical memory buffer");

    std::cout << "  -> High canonical address stack verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Error Handling and Boundary Checks
// -----------------------------------------------------------------------------
void test_error_handling_and_boundaries() {
    std::cout << "[Stage 203.21 Test 4] Error Handling and Boundary Validation...\n";

    Memory mem(0x10000);
    CPU cpu;
    DarwinProcessStartupInfo info{};
    std::string err;

    // 1. Zero stack top
    DarwinProcessStartupConfig cfg_zero_sp{};
    cfg_zero_sp.stack_top = 0;
    bool r1 = DarwinProcessStartup::setup_process(cpu, mem, cfg_zero_sp, info, err);
    TEST_CHECK(!r1, "Zero stack top rejected");
    TEST_CHECK(!err.empty(), "Error message returned for zero stack top");

    // 2. Kernel-space stack top (high bit set)
    DarwinProcessStartupConfig cfg_kern_sp{};
    cfg_kern_sp.stack_top = 0xFFFFFE0007000000ULL;
    bool r2 = DarwinProcessStartup::setup_process(cpu, mem, cfg_kern_sp, info, err);
    TEST_CHECK(!r2, "Kernel address stack top rejected");
    TEST_CHECK(!err.empty(), "Error message returned for kernel stack address");

    // 3. Overflowing stack parameters (buffer too small for large arguments)
    Memory tiny_mem(128); // Only 128 bytes of RAM
    DarwinProcessStartupConfig cfg_tiny{};
    cfg_tiny.stack_top = 120;
    cfg_tiny.argv = {"very_long_program_name_exceeding_total_allocated_guest_memory_space_1234567890"};
    cfg_tiny.envp = {"ANOTHER_LONG_ENVIRONMENT_VARIABLE_THAT_WILL_DEFINITELY_OVERFLOW_THE_STACK_FRAME"};
    bool r3 = DarwinProcessStartup::setup_process(cpu, tiny_mem, cfg_tiny, info, err);
    TEST_CHECK(!r3, "Exhausted memory rejected with boundary error");
    TEST_CHECK(!err.empty(), "Error message returned for memory exhaustion");

    std::cout << "  -> Error handling and boundaries verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.21: Process Startup and Initial Thread State Tests\n";
    std::cout << "==================================================================\n";

    test_standard_launchd_startup();
    test_custom_arguments_and_apple_dict();
    test_high_canonical_address_stack();
    test_error_handling_and_boundaries();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.21 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
