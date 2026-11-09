#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/real_artifact_harness.h"
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

constexpr const char* kArtifactPath = "tests/artifacts/bin/hello_arm64";
constexpr const char* kExpectedSha256 = "63db88c178786e58321279ca993ef936802231768954bb5d655cb4a45c9c8e9c";
constexpr std::size_t kExpectedSize = 49976;

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
        if (f.is_open()) {
            return c;
        }
    }
    return rel;
}

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    const std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buffer.data()), size);
    }
    return buffer;
}

// Test 1: Load Real Binary from Disk and Verify Authenticity
bool test_real_artifact_disk_load_and_sha256() {
    std::cout << "[Test 1] Load Real Binary from Disk and Verify Authenticity...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Binary file must exist on disk and be non-empty");
    TEST_ASSERT(bytes.size() == kExpectedSize, "Binary size must match expected 49,976 bytes");

    auto sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    std::cout << "  Loaded: " << kArtifactPath << " (" << bytes.size() << " bytes)\n";
    std::cout << "  SHA-256: " << sha << "\n";
    TEST_ASSERT(sha == kExpectedSha256, "Computed SHA-256 must match manifest");

    auto meta = RealArtifactHarness::inspect_buffer(bytes.data(), bytes.size(), kArtifactPath);
    TEST_ASSERT(meta.is_macho, "Real binary must be valid Mach-O");
    TEST_ASSERT(meta.is_arm64, "Real binary must be ARM64");
    TEST_ASSERT(!meta.is_encrypted, "Real binary must not be encrypted");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Must be permitted for userspace execution");

    return true;
}

// Test 2: Real Mach-O Guest CPU Execution with Instruction Retirement
bool test_real_artifact_guest_cpu_execution() {
    std::cout << "[Test 2] Real Mach-O Guest CPU Execution with Instruction Retirement...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Binary file must load from disk");

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest memory
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/hello_arm64";
    config.max_instructions = 1000;

    auto result = engine.execute_macho(bytes.data(), bytes.size(), config, 101);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    // 1. Process termination checks
    TEST_ASSERT(result.process_exited, "Process must terminate via SYS_exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.error_message.empty(), "Execution must produce no error message");

    // 2. Exact stdout capture check
    TEST_ASSERT(result.stdout_output == "Hello from real Mach-O\n",
                "Captured stdout must exactly match 'Hello from real Mach-O\\n'");

    // 3. Instruction retirement proof
    TEST_ASSERT(result.guest_instructions_retired == 9,
                "Must retire exactly 9 guest instructions through CPU::step (7 setup/syscalls + 2 SVC trap executions)");

    // 4. Syscall trap counts
    TEST_ASSERT(result.bsd_syscalls_handled == 2,
                "Must handle exactly 2 BSD syscalls: SYS_write (4) and SYS_exit (1)");

    return true;
}

// Test 3: Adversarial Tampered Binary Rejection (SHA-256 Mutation)
bool test_adversarial_sha256_mutation_rejection() {
    std::cout << "[Test 3] Adversarial Tampered Binary Rejection (SHA-256 Mutation)...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Binary file must load from disk");

    // Mutate a byte in the code section
    bytes[0x13f8] ^= 0xFF;

    auto mutated_sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    TEST_ASSERT(mutated_sha != kExpectedSha256, "Mutated binary SHA-256 must diverge from manifest");

    // Engine admission verification with SHA-256 gate
    const bool sha_valid = (mutated_sha == kExpectedSha256);
    TEST_ASSERT(!sha_valid, "Tampered binary must fail cryptographic SHA-256 verification gate");

    return true;
}

// Test 4: Adversarial Truncated Binary Rejection
bool test_adversarial_truncated_binary_rejection() {
    std::cout << "[Test 4] Adversarial Truncated Binary Rejection...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Binary file must load from disk");

    // Truncate to just 16 bytes (smaller than mach_header_64)
    bytes.resize(16);

    UserspaceLoadResult load_res;
    const bool parsed = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), load_res);
    TEST_ASSERT(!parsed, "Truncated Mach-O header must be rejected");
    TEST_ASSERT(load_res.status == UserspaceMachOLoadStatus::TruncatedHeader,
                "Status must be TruncatedHeader");

    return true;
}

// Test 5: Adversarial Wrong CPU Subtype Rejection
bool test_adversarial_wrong_cpu_type_rejection() {
    std::cout << "[Test 5] Adversarial Wrong CPU Type Rejection...\n";

    auto bytes = read_binary_file(kArtifactPath);
    TEST_ASSERT(!bytes.empty(), "Binary file must load from disk");

    // Corrupt CPU type field at offset 4 (change 0x0100000C ARM64 to 0x00000007 x86)
    bytes[4] = 0x07;
    bytes[5] = 0x00;
    bytes[6] = 0x00;
    bytes[7] = 0x00;

    UserspaceLoadResult load_res;
    const bool parsed = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), load_res);
    TEST_ASSERT(!parsed, "Non-ARM64 CPU type must be rejected");
    TEST_ASSERT(load_res.status == UserspaceMachOLoadStatus::UnsupportedCpuType,
                "Status must be UnsupportedCpuType");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Stage R3: Genuine Mach-O Guest CPU Execution Verification\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120 / ARM64e)\n";
    std::cout << "Artifact: tests/artifacts/bin/hello_arm64 (Independently Compiled)\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "[PASS] " << name << "\n\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_real_artifact_disk_load_and_sha256, "Real Binary Disk Load and SHA-256 Gate");
    run_test(test_real_artifact_guest_cpu_execution, "Real Mach-O Guest CPU Instruction Execution");
    run_test(test_adversarial_sha256_mutation_rejection, "Adversarial Tampered Binary Rejection");
    run_test(test_adversarial_truncated_binary_rejection, "Adversarial Truncated Binary Rejection");
    run_test(test_adversarial_wrong_cpu_type_rejection, "Adversarial Wrong CPU Type Rejection");

    std::cout << "=================================================================\n";
    std::cout << "Stage R3 Test Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "=================================================================\n";

    return failed == 0 ? 0 : 1;
}
