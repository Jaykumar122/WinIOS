#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

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

constexpr const char* kSafePath = "tests/artifacts/bin/stack_safe_arm64";
constexpr const char* kSmashPath = "tests/artifacts/bin/stack_smash_arm64";

std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.good()) return c;
    }
    return rel;
}

std::vector<std::uint8_t> read_binary_file(const std::string& rel) {
    const std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

} // namespace

// Test 1: Stack Protector - Intact Canary Exits Cleanly (Exit Code 0)
bool test_stack_safe_execution() {
    std::cout << "[Test 1] Stack Protector: Safe Execution with Intact Canary...\n";

    auto safeBytes = read_binary_file(kSafePath);
    TEST_ASSERT(!safeBytes.empty(), "Failed to read stack_safe_arm64");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    // Segment base virtual addresses
    const std::vector<std::uint64_t> seg_vmaddrs = {
        0x0ULL,
        0x100000000ULL,
        0x100004000ULL,
        0x100008000ULL,
        0x10000c000ULL
    };

    // Parse bind opcodes (binds dyld_stub_binder & ___stack_chk_guard)
    std::vector<DyldBindingRecord> bindings;
    std::string err;
    TEST_ASSERT(DyldEngine::parse_binding_stream(safeBytes.data() + 0xc008, 48, seg_vmaddrs, bindings, err),
                "Parse bind opcodes");

    // Parse lazy bind opcodes (binds ___stack_chk_fail)
    std::vector<DyldBindingRecord> lazy_bindings;
    TEST_ASSERT(DyldEngine::parse_binding_stream(safeBytes.data() + 0xc038, 24, seg_vmaddrs, lazy_bindings, err),
                "Parse lazy bind opcodes");
    bindings.insert(bindings.end(), lazy_bindings.begin(), lazy_bindings.end());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/stack_safe_arm64";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        // Initialize random canary at ___stack_chk_guard (0x1340)
        constexpr std::uint64_t kGuardAddr = 0x1340ULL;
        constexpr std::uint64_t kCanaryVal = 0xBADC0FFEE0DDF00DULL;
        mem.write_u64(kGuardAddr, kCanaryVal);

        // Override builtin libsystem export addresses to be within 64MB RAM
        DyldLoadedModule libsys;
        libsys.name = "libSystem.B.dylib";
        libsys.install_name = "/usr/lib/libSystem.B.dylib";
        libsys.base_address = 0x1000;
        libsys.exports["___stack_chk_guard"] = kGuardAddr;
        libsys.exports["___stack_chk_fail"] = 0x1300ULL;
        libsys.exports["dyld_stub_binder"] = 0x1380ULL;
        dyld.register_module(std::move(libsys));

        std::string bind_err;
        auto st = dyld.apply_bindings(mem, bindings, 0, bind_err);
        if (st != DyldLinkStatus::Success) {
            std::cerr << "Binding failure in hook: " << bind_err << "\n";
        }
    };

    auto result = engine.execute_macho(safeBytes.data(), safeBytes.size(), config, 401);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 for safe canary verification");

    return true;
}

// Test 2: Stack Protector - Corrupted Canary Triggers ___stack_chk_fail (SIGABRT / Exit 134)
bool test_stack_smash_abort() {
    std::cout << "[Test 2] Stack Protector: Corrupted Canary Triggers ___stack_chk_fail (Exit 134)...\n";

    auto smashBytes = read_binary_file(kSmashPath);
    TEST_ASSERT(!smashBytes.empty(), "Failed to read stack_smash_arm64");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    const std::vector<std::uint64_t> seg_vmaddrs = {
        0x0ULL,
        0x100000000ULL,
        0x100004000ULL,
        0x100008000ULL,
        0x10000c000ULL
    };

    std::vector<DyldBindingRecord> bindings;
    std::string err;
    TEST_ASSERT(DyldEngine::parse_binding_stream(smashBytes.data() + 0xc008, 48, seg_vmaddrs, bindings, err),
                "Parse bind opcodes");

    std::vector<DyldBindingRecord> lazy_bindings;
    TEST_ASSERT(DyldEngine::parse_binding_stream(smashBytes.data() + 0xc038, 24, seg_vmaddrs, lazy_bindings, err),
                "Parse lazy bind opcodes");
    bindings.insert(bindings.end(), lazy_bindings.begin(), lazy_bindings.end());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/stack_smash_arm64";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        // Initialize canary value at ___stack_chk_guard (0x1340)
        constexpr std::uint64_t kGuardAddr = 0x1340ULL;
        constexpr std::uint64_t kCanaryVal = 0xBADC0FFEE0DDF00DULL;
        mem.write_u64(kGuardAddr, kCanaryVal);

        // Place defined abort stub at ___stack_chk_fail (0x1300):
        //   mov x0, #134        ; SIGABRT exit code (128 + 6) -> 0xd28010c0
        //   mov x16, #1         ; SYS_exit                    -> 0xd2800030
        //   svc #0x80           ; syscall                     -> 0xd4001001
        constexpr std::uint64_t kFailAddr = 0x1300ULL;
        mem.write_u32(kFailAddr + 0, 0xd28010c0u);
        mem.write_u32(kFailAddr + 4, 0xd2800030u);
        mem.write_u32(kFailAddr + 8, 0xd4001001u);

        // Override builtin libsystem export addresses to be within 64MB RAM
        DyldLoadedModule libsys;
        libsys.name = "libSystem.B.dylib";
        libsys.install_name = "/usr/lib/libSystem.B.dylib";
        libsys.base_address = 0x1000;
        libsys.exports["___stack_chk_guard"] = kGuardAddr;
        libsys.exports["___stack_chk_fail"] = kFailAddr;
        libsys.exports["dyld_stub_binder"] = 0x1380ULL;
        dyld.register_module(std::move(libsys));

        std::string bind_err;
        auto st = dyld.apply_bindings(mem, bindings, 0, bind_err);
        if (st != DyldLinkStatus::Success) {
            std::cerr << "Binding failure in hook: " << bind_err << "\n";
        }
    };

    auto result = engine.execute_macho(smashBytes.data(), smashBytes.size(), config, 402);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must terminate via abort stub");
    TEST_ASSERT(result.exit_code == 134, "Exit code must be 134 (SIGABRT) indicating stack check failure");

    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE T0: STACK PROTECTOR (-fstack-protector-all) TESTS\n";
    std::cout << "====================================================\n";

    bool pass = true;
    pass = test_stack_safe_execution() && pass;
    pass = test_stack_smash_abort() && pass;

    std::cout << "====================================================\n";
    if (pass) {
        std::cout << "[STAGE T0 STACK PROTECTOR TESTS] ALL PASSED\n";
        return 0;
    } else {
        std::cerr << "[STAGE T0 STACK PROTECTOR TESTS] FAILED\n";
        return 1;
    }
}
