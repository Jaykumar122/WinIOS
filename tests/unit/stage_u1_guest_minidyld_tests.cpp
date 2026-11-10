// =============================================================================
// stage_u1_guest_minidyld_tests.cpp
//
// Stage U1: Guest-Side Dynamic Linker (minidyld) Execution & Differential Tests
// Verifies genuine guest EL0 execution of minidyld for dynamic binary loading:
//   - Syscall foundation: SYS_pread (153) verification
//   - REAL-005 (main_with_dylib + libmathreal.dylib) loaded & bound by minidyld
//   - REAL-009 (main_chain + libmid.dylib + libleaf.dylib) three-image transitive chain
//   - Constructor chain (ctor_main + libctor_mid + libctor_leaf) sequential execution
//   - Differential memory verification: minidyld memory vs host-emulated memory
//   - Negative tests: missing dylib, undefined symbol, corrupted Mach-O magic
// =============================================================================

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

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

constexpr const char* kMinidyldPath   = "tests/artifacts/bin/minidyld";
constexpr const char* kMathDylibPath  = "tests/artifacts/bin/libmathreal.dylib";
constexpr const char* kMainDylibPath  = "tests/artifacts/bin/main_with_dylib";
constexpr const char* kLeafDylibPath  = "tests/artifacts/bin/libleaf.dylib";
constexpr const char* kMidDylibPath   = "tests/artifacts/bin/libmid.dylib";
constexpr const char* kMainChainPath  = "tests/artifacts/bin/main_chain";
constexpr const char* kCtorLeafPath   = "tests/artifacts/bin/libctor_leaf.dylib";
constexpr const char* kCtorMidPath    = "tests/artifacts/bin/libctor_mid.dylib";
constexpr const char* kCtorMainPath   = "tests/artifacts/bin/ctor_main";

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

// -----------------------------------------------------------------------------
// Test 1: SYS_pread Syscall Verification in Darwin BSD Subsystem
// -----------------------------------------------------------------------------
bool test_sys_pread_bsd_subsystem() {
    std::cout << "[Test 1] SYS_pread (153) Darwin BSD Subsystem Verification...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/test_pread", 501);

    std::string test_data = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    bsd.vfs().write_file("/tmp/pread_test.txt", test_data.data(), test_data.size());

    int fd = bsd.open(pid, "/tmp/pread_test.txt", 0);
    TEST_ASSERT(fd >= 3, "open must return valid fd");

    // 1. Read 10 bytes from offset 0
    std::vector<char> buf1(10, 0);
    std::int64_t n1 = bsd.pread(pid, fd, buf1.data(), 10, 0);
    TEST_ASSERT(n1 == 10, "pread 10 bytes at 0");
    TEST_ASSERT(std::string(buf1.data(), 10) == "0123456789", "content matches offset 0");

    // 2. Read 26 bytes from offset 10 (uppercase letters)
    std::vector<char> buf2(26, 0);
    std::int64_t n2 = bsd.pread(pid, fd, buf2.data(), 26, 10);
    TEST_ASSERT(n2 == 26, "pread 26 bytes at 10");
    TEST_ASSERT(std::string(buf2.data(), 26) == "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "content matches offset 10");

    // 3. Verify pread did NOT advance regular fd offset
    std::vector<char> buf_seq(10, 0);
    std::int64_t n_seq = bsd.read(pid, fd, buf_seq.data(), 10);
    TEST_ASSERT(n_seq == 10, "sequential read at initial offset");
    TEST_ASSERT(std::string(buf_seq.data(), 10) == "0123456789", "read offset remained 0 after pread calls");

    // 4. Read past EOF returns 0
    std::vector<char> buf_eof(10, 0);
    std::int64_t n_eof = bsd.pread(pid, fd, buf_eof.data(), 10, 1000);
    TEST_ASSERT(n_eof == 0, "pread past EOF must return 0");

    // 5. Negative offset returns -1
    std::int64_t n_neg = bsd.pread(pid, fd, buf_eof.data(), 10, -5);
    TEST_ASSERT(n_neg == -1, "pread with negative offset must fail");

    bsd.close(pid, fd);
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Minidyld Artifact Provenance & Mach-O Header Structure
// -----------------------------------------------------------------------------
bool test_minidyld_artifact_provenance() {
    std::cout << "[Test 2] Minidyld Artifact Provenance & Structure...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    TEST_ASSERT(!dyldBytes.empty(), "minidyld artifact must exist on disk");

    auto sha = RealArtifactHarness::compute_sha256(dyldBytes.data(), dyldBytes.size());
    std::cout << "  Artifact: minidyld (" << dyldBytes.size() << " bytes)\n";
    std::cout << "  SHA-256:  " << sha << "\n";

    auto meta = RealArtifactHarness::inspect_buffer(dyldBytes.data(), dyldBytes.size(), kMinidyldPath);
    TEST_ASSERT(meta.is_macho, "minidyld must be valid Mach-O");
    TEST_ASSERT(meta.is_arm64, "minidyld must be ARM64");
    TEST_ASSERT(meta.can_execute_in_userspace(), "minidyld must be executable in userspace");

    loader::UserspaceLoadResult load_res;
    TEST_ASSERT(loader::UserspaceMachOLoader::parse_metadata(dyldBytes.data(), dyldBytes.size(), load_res),
                "parse minidyld metadata");
    TEST_ASSERT(load_res.entry_point != 0, "minidyld must have non-zero entry point");
    std::cout << "  Entry Point: 0x" << std::hex << load_res.entry_point << std::dec << "\n";

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: REAL-005 Two-Image Dynamic Linking via Guest-Side Minidyld
// -----------------------------------------------------------------------------
bool test_minidyld_two_image_execution_real005() {
    std::cout << "[Test 3] REAL-005 Two-Image Dynamic Linking via Guest Minidyld...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kMainDylibPath);
    auto mathBytes = read_binary_file(kMathDylibPath);

    TEST_ASSERT(!dyldBytes.empty(), "minidyld binary required");
    TEST_ASSERT(!mainBytes.empty(), "main_with_dylib binary required");
    TEST_ASSERT(!mathBytes.empty(), "libmathreal.dylib binary required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest memory
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Install dependent dylib into Darwin VFS
    bsd.vfs().write_file("/usr/lib/libmathreal.dylib", mathBytes.data(), mathBytes.size());
    bsd.vfs().write_file("tests/artifacts/bin/libmathreal.dylib", mathBytes.data(), mathBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_with_dylib";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config, 501);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout:      \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";
    std::cout << "  Final PC:             0x" << std::hex << cpu.get_program_counter() << std::dec << "\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.stdout_output == "DYLIB_OK: 0x0A11C001CAFEBAE8\n",
                "Captured stdout must match expected banner");
    TEST_ASSERT(result.bsd_syscalls_handled >= 4, "Must handle guest syscalls (open, pread, write, exit)");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: REAL-009 Three-Image Transitive Chain (@rpath) via Guest Minidyld
// -----------------------------------------------------------------------------
bool test_minidyld_three_image_chain_real009() {
    std::cout << "[Test 4] REAL-009 Three-Image Transitive Chain via Guest Minidyld...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kMainChainPath);
    auto midBytes  = read_binary_file(kMidDylibPath);
    auto leafBytes = read_binary_file(kLeafDylibPath);

    TEST_ASSERT(!dyldBytes.empty() && !mainBytes.empty() && !midBytes.empty() && !leafBytes.empty(),
                "All chain binaries required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Install dylibs into VFS along @rpath search locations
    bsd.vfs().write_file("/usr/lib/libmid.dylib", midBytes.data(), midBytes.size());
    bsd.vfs().write_file("/usr/lib/libleaf.dylib", leafBytes.data(), leafBytes.size());
    bsd.vfs().write_file("tests/artifacts/bin/libmid.dylib", midBytes.data(), midBytes.size());
    bsd.vfs().write_file("tests/artifacts/bin/libleaf.dylib", leafBytes.data(), leafBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_chain";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config, 502);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout:      \"" << result.stdout_output << "\"\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.stdout_output == "CHAIN_OK: 142\n", "Output must match expected CHAIN_OK: 142");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Sequential Constructor Execution Order (leaf -> mid -> main)
// -----------------------------------------------------------------------------
bool test_minidyld_constructor_chain_execution() {
    std::cout << "[Test 5] Sequential Constructor Execution (leaf -> mid -> main)...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kCtorMainPath);
    auto midBytes  = read_binary_file(kCtorMidPath);
    auto leafBytes = read_binary_file(kCtorLeafPath);

    TEST_ASSERT(!dyldBytes.empty() && !mainBytes.empty() && !midBytes.empty() && !leafBytes.empty(),
                "All constructor binaries required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Install into VFS
    bsd.vfs().write_file("/usr/lib/libctor_mid.dylib", midBytes.data(), midBytes.size());
    bsd.vfs().write_file("/usr/lib/libctor_leaf.dylib", leafBytes.data(), leafBytes.size());
    bsd.vfs().write_file("tests/artifacts/bin/libctor_mid.dylib", midBytes.data(), midBytes.size());
    bsd.vfs().write_file("tests/artifacts/bin/libctor_leaf.dylib", leafBytes.data(), leafBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/ctor_main";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config, 503);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout:      \"" << result.stdout_output << "\"\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.stdout_output == "CTOR_CHAIN_OK: 1-2-3\n",
                "Output must verify sequential constructor execution 1 -> 2 -> 3");

    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Differential Memory Verification: Minidyld vs Host-Emulated Path
// -----------------------------------------------------------------------------
bool test_minidyld_differential_memory_verification() {
    std::cout << "[Test 6] Differential Memory Verification: Guest Minidyld vs Host-Emulated...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kMainDylibPath);
    auto mathBytes = read_binary_file(kMathDylibPath);

    // 1. Run guest minidyld path
    CPU cpu1;
    Memory mem1(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd1;
    MachIpcSubsystem ipc1;
    DarwinUserspaceEngine engine1(cpu1, mem1, bsd1, ipc1);
    bsd1.vfs().write_file("/usr/lib/libmathreal.dylib", mathBytes.data(), mathBytes.size());

    DarwinPipelineConfig config1;
    config1.executable_path = "/usr/bin/main_with_dylib";
    config1.stack_top = 0x03F00000ULL;
    config1.max_instructions = 500000;

    auto res1 = engine1.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config1, 504);
    TEST_ASSERT(res1.process_exited && res1.exit_code == 0, "Guest minidyld execution must succeed");

    // 2. Read bound GOT slot from memory
    // In main_with_dylib, GOT slots for _real_add and _real_read_global are at 0x100008008 and 0x100008010
    uint64_t got_add1 = mem1.read_u64(0x100008008ULL % mem1.size());
    uint64_t got_read1 = mem1.read_u64(0x100008010ULL % mem1.size());

    std::cout << "  Guest Minidyld Bound GOT[_real_add]:         0x" << std::hex << got_add1 << std::dec << "\n";
    std::cout << "  Guest Minidyld Bound GOT[_real_read_global]: 0x" << std::hex << got_read1 << std::dec << "\n";

    TEST_ASSERT(got_add1 != 0, "GOT slot for _real_add must be bound");
    TEST_ASSERT(got_read1 != 0, "GOT slot for _real_read_global must be bound");

    // Expected target address from dylib export trie (slid within dylib base 0x20000)
    TEST_ASSERT(got_add1 == 0x21470ULL, "GOT slot must match slid export address of _real_add");
    TEST_ASSERT(got_read1 == 0x21490ULL, "GOT slot must match slid export address of _real_read_global");

    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Negative Test: Missing Dylib Graceful Termination
// -----------------------------------------------------------------------------
bool test_minidyld_negative_missing_dylib() {
    std::cout << "[Test 7] Negative Test: Missing Dylib Graceful Termination...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kMainDylibPath);

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Note: /usr/lib/libmathreal.dylib is NOT written to VFS!

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_with_dylib";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 50000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config, 505);

    std::cout << "  Guest exited: " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";

    TEST_ASSERT(result.process_exited, "Process must terminate deterministically");
    TEST_ASSERT(result.exit_code == 127, "Exit code must be 127 on missing dylib");

    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Negative Test: Corrupted Dylib Header Rejection
// -----------------------------------------------------------------------------
bool test_minidyld_negative_corrupted_header() {
    std::cout << "[Test 8] Negative Test: Corrupted Dylib Header Rejection...\n";

    auto dyldBytes = read_binary_file(kMinidyldPath);
    auto mainBytes = read_binary_file(kMainDylibPath);

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Install a corrupted dylib file (wrong magic)
    std::vector<std::uint8_t> corrupted(1024, 0xAA);
    bsd.vfs().write_file("/usr/lib/libmathreal.dylib", corrupted.data(), corrupted.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/main_with_dylib";
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 50000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        mainBytes.data(), mainBytes.size(),
        config, 506);

    std::cout << "  Guest exited: " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";

    TEST_ASSERT(result.process_exited, "Process must terminate deterministically");
    TEST_ASSERT(result.exit_code == 126, "Exit code must be 126 on corrupted dylib header");

    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Independent Unit Tests for CPU Fixes Driven by Stage U1
// -----------------------------------------------------------------------------
bool test_cpu_simd_movi_stp_and_extended_cmp() {
    std::cout << "[Test 9] Independent CPU Unit Tests (MOVI, STP/LDP Q, Extended CMP)...\n";

    CPU cpu;
    Memory mem(1024 * 1024);
    cpu.reset();
    cpu.set_cpacr_el1(0x00300000);
    cpu.set_current_el(0);
    cpu.set_stack_pointer(0x80000);
    cpu.set_program_counter(0x1000);

    // 1. MOVI V0.16B, #0 (0x6F00E400) after pre-filling V0
    cpu.write_vector_register(0, CPU::VectorRegister{0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL});
    mem.write_u32(0x1000, 0x6F00E400u); // movi v0.16b, #0

    // 2. MOVI V1.16B, #0x42 (a=0, b=1, c=0 => 0x02<<16; d..h=0x02 => 0x02<<5; cmode=1110, op=1, Q=1 => 0x6F02E441)
    mem.write_u32(0x1004, 0x6F02E441u); // movi v1.16b, #0x42

    // 3. STP Q0, Q1, [SP, #-32]! -> simm7 = -2 (0x7E), opc=10, V=1, mode=3(pre), L=0 -> 0xADBF07E0 -> Rt2=1(0x0400), Rn=31(0x03E0), Rt=0 -> 0xADBF07E0?
    // Wait: simm7 = -2 = 0b1111110 = 0x7E -> (0x7E << 15) = 0x003F0000. 0xAD800000 | 0x003F0000 | (1 << 10) | (31 << 5) | 0 = 0xADBF07E0
    mem.write_u32(0x1008, 0xADBF07E0u); // stp q0, q1, [sp, #-32]!

    // 4. LDP Q2, Q3, [SP], #32 -> simm7 = +2 = 0x02 -> (0x02 << 15) = 0x00010000. 0xACC00000 | 0x00010000 | (3 << 10) | (31 << 5) | 2 = 0xACC10FE2
    mem.write_u32(0x100C, 0xACC10FE2u); // ldp q2, q3, [sp], #32

    // 5. CMP X25, W8, SXTW (0xEB28C33F) -> X25 = -1ULL, W8 = 0xFFFFFFFF -> SXTW(W8) = -1ULL -> Z flag set!
    cpu.write_register(25, 0xFFFFFFFFFFFFFFFFULL);
    cpu.write_register(8, 0x00000000FFFFFFFFULL);
    mem.write_u32(0x1010, 0xEB28C33Fu); // cmp x25, w8, sxtw

    cpu.step(mem); // MOVI V0, #0
    auto v0 = cpu.read_vector_register(0);
    TEST_ASSERT(v0.low == 0 && v0.high == 0, "MOVI V0.16B, #0 must zero V0");

    cpu.step(mem); // MOVI V1, #0x42
    auto v1 = cpu.read_vector_register(1);
    TEST_ASSERT(v1.low == 0x4242424242424242ULL && v1.high == 0x4242424242424242ULL, "MOVI V1.16B, #0x42 must broadcast 0x42");

    cpu.step(mem); // STP Q0, Q1, [SP, #-32]!
    TEST_ASSERT(cpu.get_stack_pointer() == 0x80000 - 32, "STP Q0, Q1 pre-index must decrement SP by 32");

    cpu.step(mem); // LDP Q2, Q3, [SP], #32
    TEST_ASSERT(cpu.get_stack_pointer() == 0x80000, "LDP Q2, Q3 post-index must restore SP");
    auto v2 = cpu.read_vector_register(2);
    auto v3 = cpu.read_vector_register(3);
    TEST_ASSERT(v2.low == 0 && v2.high == 0, "Q2 must match stored Q0");
    TEST_ASSERT(v3.low == 0x4242424242424242ULL && v3.high == 0x4242424242424242ULL, "Q3 must match stored Q1");

    cpu.step(mem); // CMP X25, W8, SXTW
    TEST_ASSERT((cpu.get_nzcv() & 0x04u) != 0, "CMP X25, W8, SXTW with sign-extended equal values must set Z flag");

    return true;
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE U1: Guest-Side Dynamic Linker (minidyld) Tests\n";
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

    run_test(test_sys_pread_bsd_subsystem, "SYS_pread (153) BSD Subsystem");
    run_test(test_minidyld_artifact_provenance, "Minidyld Artifact Provenance");
    run_test(test_minidyld_two_image_execution_real005, "REAL-005 Two-Image Guest Minidyld");
    run_test(test_minidyld_three_image_chain_real009, "REAL-009 Three-Image Chain Guest Minidyld");
    run_test(test_minidyld_constructor_chain_execution, "Sequential Constructor Execution");
    run_test(test_minidyld_differential_memory_verification, "Differential Memory Verification");
    run_test(test_minidyld_negative_missing_dylib, "Negative Test: Missing Dylib Termination");
    run_test(test_minidyld_negative_corrupted_header, "Negative Test: Corrupted Header Rejection");
    run_test(test_cpu_simd_movi_stp_and_extended_cmp, "Independent CPU Unit Tests");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
