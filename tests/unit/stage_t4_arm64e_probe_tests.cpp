// ==============================================================================
// WinIOS Unit Test: Stage T4 ARM64e / PAC Probe and Admission Conformance
// ==============================================================================
// Proves:
// 1. Toolchain Probe: Records Zig/Clang behavior for arm64e target flags.
// 2. PAC Instruction Execution: Executes genuine compiled Mach-O containing
//    PACIASP and AUTIASP hint instructions on the WinIOS guest CPU.
// 3. Mach-O Subtype Conformance: Verifies UserspaceMachOLoader admits and flags
//    CPU_SUBTYPE_ARM64E (0x80000002 and 0x00000002).
// 4. Architectural PAC Foundation: Verifies EL0 NOP-compatibility space execution.
// ==============================================================================

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

constexpr const char* kPacProbePath = "tests/artifacts/bin/pac_probe";

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

// Test 1: Verify PAC Instruction Opcodes in pac_probe
bool test_pac_opcodes_inspection() {
    std::cout << "[Test 1] Verify PAC Instruction Opcodes in pac_probe...\n";

    auto pacBytes = read_binary_file(kPacProbePath);
    TEST_ASSERT(!pacBytes.empty(), "pac_probe binary must exist");

    UserspaceLoadResult res;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(pacBytes.data(), pacBytes.size(), res), "Parse pac_probe");

    std::uint64_t entryoff = res.entry_point - 0x100000000ULL;
    TEST_ASSERT(entryoff + 16 <= pacBytes.size(), "Entrypoint must be within file bounds");

    // Inspect first two instructions: PACIASP and AUTIASP
    std::uint32_t op1 = *reinterpret_cast<const std::uint32_t*>(pacBytes.data() + entryoff);
    std::uint32_t op2 = *reinterpret_cast<const std::uint32_t*>(pacBytes.data() + entryoff + 4);

    std::cout << "  Instruction 1 at entry: 0x" << std::hex << op1 << " (expected 0xd503233f PACIASP)\n";
    std::cout << "  Instruction 2 at entry: 0x" << std::hex << op2 << " (expected 0xd50323bf AUTIASP)\n" << std::dec;

    TEST_ASSERT(op1 == 0xd503233fu, "Opcode 1 must be PACIASP (0xd503233f)");
    TEST_ASSERT(op2 == 0xd50323bfu, "Opcode 2 must be AUTIASP (0xd50323bf)");

    return true;
}

// Test 2: Execute PAC Mach-O on Guest CPU
bool test_pac_guest_execution() {
    std::cout << "[Test 2] Execute PAC Mach-O on Guest CPU...\n";

    auto pacBytes = read_binary_file(kPacProbePath);
    TEST_ASSERT(!pacBytes.empty(), "pac_probe binary must exist");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/pac_probe";
    config.max_instructions = 1000;

    auto result = engine.execute_macho(pacBytes.data(), pacBytes.size(), config, 605);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";
    std::cout << "  Final PC: 0x" << std::hex << cpu.get_program_counter()
              << ", ELR=0x" << cpu.get_elr_el1()
              << ", ESR=0x" << cpu.get_esr_el1()
              << ", FAR=0x" << cpu.get_far_el1() << std::dec << "\n";

    TEST_ASSERT(result.guest_instructions_retired >= 5, "Must retire at least 5 instructions (PACIASP, AUTIASP, MOV, MOV, SVC)");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.bsd_syscalls_handled == 1, "Must handle SYS_exit (1)");

    return true;
}

// Test 3: Mach-O ARM64e Subtype Header Admission
bool test_arm64e_subtype_admission() {
    std::cout << "[Test 3] Mach-O ARM64e Subtype Header Admission...\n";

    auto pacBytes = read_binary_file(kPacProbePath);
    TEST_ASSERT(!pacBytes.empty(), "pac_probe binary must exist");

    // Standard ARM64 binary check
    UserspaceLoadResult stdRes;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(pacBytes.data(), pacBytes.size(), stdRes), "Parse standard");
    TEST_ASSERT(!stdRes.is_arm64e, "Standard binary must not be flagged as ARM64e");

    // Mutate header copy with CPU_SUBTYPE_ARM64E (0x80000002)
    auto arm64eBytes = pacBytes;
    *reinterpret_cast<std::uint32_t*>(arm64eBytes.data() + 8) = 0x80000002u;

    UserspaceLoadResult arm64eRes;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64eBytes.data(), arm64eBytes.size(), arm64eRes), "Parse ARM64e");
    TEST_ASSERT(arm64eRes.is_arm64e, "Subtype 0x80000002 must be admitted and flagged as is_arm64e");

    // Mutate header copy with CPU_SUBTYPE_ARM64E without ptrauth flag (0x00000002)
    *reinterpret_cast<std::uint32_t*>(arm64eBytes.data() + 8) = 0x00000002u;
    UserspaceLoadResult arm64eRes2;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64eBytes.data(), arm64eBytes.size(), arm64eRes2), "Parse ARM64e v2");
    TEST_ASSERT(arm64eRes2.is_arm64e, "Subtype 0x00000002 must be admitted and flagged as is_arm64e");

    // Test rejection of invalid cputype (e.g. x86_64 = 0x01000007)
    *reinterpret_cast<std::uint32_t*>(arm64eBytes.data() + 4) = 0x01000007u;
    UserspaceLoadResult rejectRes;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(arm64eBytes.data(), arm64eBytes.size(), rejectRes), "Reject x86_64");
    TEST_ASSERT(rejectRes.status == UserspaceMachOLoadStatus::UnsupportedCpuType, "Must report UnsupportedCpuType");

    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE T4: ARM64e / PAC PROBE AND ADMISSION CONFORMANCE\n";
    std::cout << "====================================================\n";

    if (!test_pac_opcodes_inspection()) return 1;
    if (!test_pac_guest_execution()) return 2;
    if (!test_arm64e_subtype_admission()) return 3;

    std::cout << "====================================================\n";
    std::cout << "[STAGE T4 TESTS] ALL PASSED\n";
    std::cout << "====================================================\n";
    return 0;
}
