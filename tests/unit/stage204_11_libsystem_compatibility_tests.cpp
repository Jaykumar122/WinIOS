// =============================================================================
// WinIOS — Stage 204.11: Synthetic libSystem Compatibility Layer Unit Tests
// =============================================================================

#include "runtime/darwin_runtime_library.h"
#include "loader/dyld_engine.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Stub Catalog Completeness & Machine Code Invariants
// -----------------------------------------------------------------------------
void test_stub_catalog_completeness() {
    std::cout << "[Stage 204.11 Test 1] Stub Catalog Completeness...\n";

    DarwinRuntimeLibrary libsys(0x180000000ULL);

    // 1. Verify BSD Syscall Stubs
    const char* expected_bsd[] = {
        "_exit", "_open", "_close", "_read", "_write", "_getpid",
        "_getuid", "_geteuid", "_getgid", "_munmap", "_mmap", "_lseek", "_fstat"
    };
    for (const char* name : expected_bsd) {
        const auto* stub = libsys.find_stub(name);
        TEST_CHECK(stub != nullptr, std::string("Stub exists: ") + name);
        if (stub) {
            TEST_CHECK(stub->kind == RuntimeStubKind::BsdSyscall, "Kind is BsdSyscall");
            TEST_CHECK(stub->code.size() == 3, "BSD stub has 3 instructions (movz, svc, ret)");
            TEST_CHECK(stub->code[1] == 0xD4001001u, "Instruction 2 is 'svc #0x80'");
            TEST_CHECK(stub->code[2] == 0xD65F03C0u, "Instruction 3 is 'ret'");
        }
    }

    // 2. Verify Mach Trap Stubs
    const char* expected_mach[] = {
        "_mach_msg_trap", "_mach_reply_port", "_mach_task_self",
        "_semaphore_signal_trap", "_semaphore_wait_trap", "_swtch_pri", "_swtch"
    };
    for (const char* name : expected_mach) {
        const auto* stub = libsys.find_stub(name);
        TEST_CHECK(stub != nullptr, std::string("Mach trap stub exists: ") + name);
        if (stub) {
            TEST_CHECK(stub->kind == RuntimeStubKind::MachTrap, "Kind is MachTrap");
            TEST_CHECK(stub->code.size() == 3, "Mach stub has 3 instructions (movn, svc, ret)");
            TEST_CHECK(stub->code[1] == 0xD4001021u, "Instruction 2 is 'svc #0x81'");
            TEST_CHECK(stub->code[2] == 0xD65F03C0u, "Instruction 3 is 'ret'");
        }
    }

    std::cout << "  -> Syscall and Mach trap stub catalog verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Errno & Stack Canary Infrastructure
// -----------------------------------------------------------------------------
void test_errno_and_stack_canary() {
    std::cout << "[Stage 204.11 Test 2] Errno & Stack Canary Infrastructure...\n";

    DarwinRuntimeLibrary libsys(0x180000000ULL);
    Memory mem(0x10000); // 64 KB
    std::string err;

    TEST_CHECK(libsys.stage_into_memory(mem, err), "Staged into guest memory");

    // 1. Errno address verification
    std::uint64_t errno_addr = libsys.errno_address();
    TEST_CHECK(errno_addr >= libsys.base_vaddr(), "Errno address is in runtime library range");

    const auto* err_stub = libsys.find_stub("___error");
    TEST_CHECK(err_stub != nullptr, "___error stub exists");
    if (err_stub) {
        TEST_CHECK(err_stub->code.size() >= 2, "___error has at least 2 instructions");
        TEST_CHECK(err_stub->code.back() == 0xD65F03C0u, "Last instruction is 'ret'");
    }

    // 2. Stack Canary Verification
    std::uint64_t guard_addr = libsys.stack_guard_address();
    TEST_CHECK(guard_addr > errno_addr, "Guard address positioned after errno");

    auto it_guard = libsys.exports().find("___stack_chk_guard");
    TEST_CHECK(it_guard != libsys.exports().end(), "___stack_chk_guard exported");
    TEST_CHECK(it_guard->second == guard_addr, "Exported address matches guard address");

    // Verify initial canary value written to memory
    std::size_t guard_off = static_cast<std::size_t>(guard_addr - libsys.base_vaddr());
    std::uint64_t canary_in_mem = mem.read_u64(guard_off);
    TEST_CHECK(canary_in_mem == libsys.stack_guard_value(), "Memory contains valid canary value");
    TEST_CHECK(canary_in_mem == 0x595E9FBD94FDE6B5ULL, "Canary matches Apple Silicon default");

    std::cout << "  -> Errno & stack canary infrastructure verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: String & Memory Machine Code Execution
// -----------------------------------------------------------------------------
void test_string_and_memory_routines() {
    std::cout << "[Stage 204.11 Test 3] String & Memory Machine Code Routines...\n";

    DarwinRuntimeLibrary libsys(0x180000000ULL);

    // 1. _strlen
    const auto* strlen_stub = libsys.find_stub("_strlen");
    TEST_CHECK(strlen_stub != nullptr, "_strlen stub exists");
    if (strlen_stub) {
        TEST_CHECK(strlen_stub->code.size() == 6, "_strlen contains 6 ARM64 instructions");
        TEST_CHECK(strlen_stub->code[0] == 0xAA0003E1u, "mov x1, x0");
        TEST_CHECK(strlen_stub->code[1] == 0x38401422u, "ldrb w2, [x1], #1");
        TEST_CHECK(strlen_stub->code[2] == 0x35FFFFE2u, "cbnz w2, -4");
        TEST_CHECK(strlen_stub->code[5] == 0xD65F03C0u, "ret");
    }

    // 2. _memset
    const auto* memset_stub = libsys.find_stub("_memset");
    TEST_CHECK(memset_stub != nullptr, "_memset stub exists");
    if (memset_stub) {
        TEST_CHECK(memset_stub->code.size() == 6, "_memset contains 6 ARM64 instructions");
        TEST_CHECK(memset_stub->code[0] == 0xAA0003E3u, "mov x3, x0");
        TEST_CHECK(memset_stub->code[1] == 0xB4000082u, "cbz x2, +16");
        TEST_CHECK(memset_stub->code[2] == 0x38001461u, "strb w1, [x3], #1");
        TEST_CHECK(memset_stub->code[5] == 0xD65F03C0u, "ret");
    }

    // 3. _memcpy & _memmove
    const auto* memcpy_stub = libsys.find_stub("_memcpy");
    TEST_CHECK(memcpy_stub != nullptr, "_memcpy stub exists");
    if (memcpy_stub) {
        TEST_CHECK(memcpy_stub->code.size() == 7, "_memcpy contains 7 ARM64 instructions");
        TEST_CHECK(memcpy_stub->code[0] == 0xAA0003E3u, "mov x3, x0");
        TEST_CHECK(memcpy_stub->code[1] == 0xB40000A2u, "cbz x2, +20");
        TEST_CHECK(memcpy_stub->code[2] == 0x38401424u, "ldrb w4, [x1], #1");
        TEST_CHECK(memcpy_stub->code[3] == 0x38001464u, "strb w4, [x3], #1");
        TEST_CHECK(memcpy_stub->code[6] == 0xD65F03C0u, "ret");
    }

    // 4. _strcmp
    const auto* strcmp_stub = libsys.find_stub("_strcmp");
    TEST_CHECK(strcmp_stub != nullptr, "_strcmp stub exists");
    if (strcmp_stub) {
        TEST_CHECK(strcmp_stub->code.size() == 7, "_strcmp contains 7 ARM64 instructions");
        TEST_CHECK(strcmp_stub->code[0] == 0x38401402u, "ldrb w2, [x0], #1");
        TEST_CHECK(strcmp_stub->code[1] == 0x38401423u, "ldrb w3, [x1], #1");
        TEST_CHECK(strcmp_stub->code[2] == 0x34000062u, "cbz w2, +12");
        TEST_CHECK(strcmp_stub->code[3] == 0x6B03005Fu, "cmp w2, w3");
        TEST_CHECK(strcmp_stub->code[4] == 0x54FFFF80u, "b.eq -16");
        TEST_CHECK(strcmp_stub->code[6] == 0xD65F03C0u, "ret");
    }

    std::cout << "  -> String and memory machine code routines verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Dyld Dynamic Linker Integration
// -----------------------------------------------------------------------------
void test_dyld_registration() {
    std::cout << "[Stage 204.11 Test 4] Dyld Dynamic Linker Integration...\n";

    DyldEngine dyld;
    dyld.clear_modules();

    DarwinRuntimeLibrary libsys(0x180000000ULL);
    libsys.register_with_dyld(dyld);

    TEST_CHECK(dyld.has_module("libSystem.B.dylib"), "libSystem.B.dylib registered in DyldEngine");

    std::optional<std::uint64_t> printf_addr = dyld.resolve_symbol("_exit", 1);
    TEST_CHECK(printf_addr.has_value(), "_exit symbol resolvable from libSystem");

    std::optional<std::uint64_t> strlen_addr = dyld.resolve_symbol("_strlen", 1);
    TEST_CHECK(strlen_addr.has_value(), "_strlen resolvable from libSystem");

    std::optional<std::uint64_t> guard_addr = dyld.resolve_symbol("___stack_chk_guard", 1);
    TEST_CHECK(guard_addr.has_value() && *guard_addr == libsys.stack_guard_address(),
               "___stack_chk_guard resolves to canary data address");

    std::cout << "  -> Dyld registration and resolution verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.11 Synthetic libSystem Compatibility Tests ===\n";
    test_stub_catalog_completeness();
    test_errno_and_stack_canary();
    test_string_and_memory_routines();
    test_dyld_registration();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.11 Synthetic libSystem Compatibility passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.11 Synthetic libSystem Compatibility failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}
