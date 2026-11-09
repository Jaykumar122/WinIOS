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

constexpr const char* kArtifactPathO0 = "tests/artifacts/bin/compute_arm64_O0";
constexpr const char* kExpectedSha256O0 = "9f196ce18d96a7574cb7c117505042b4e69be4924769cf2d3926eb36115f6652";
constexpr std::size_t kExpectedSizeO0 = 51328;

constexpr const char* kArtifactPathO2 = "tests/artifacts/bin/compute_arm64_O2";
constexpr const char* kExpectedSha256O2 = "c0d45d62d1fbe2fb7ae0fec179301c70b07e2fd4b76fe29dbf9716209c091dd7";
constexpr std::size_t kExpectedSizeO2 = 50864;

constexpr const char* kExpectedStdout = "COMPUTE_OK: 0xCAFEBABE13438009\n";

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

// Test 1: Verify disk load and SHA-256 for REAL-002 and REAL-003
bool test_artifacts_disk_load_and_sha256() {
    std::cout << "[Test 1] Load Real Compiler-Generated Binaries & Verify Provenance...\n";

    // REAL-002 (O0)
    auto bytesO0 = read_binary_file(kArtifactPathO0);
    TEST_ASSERT(!bytesO0.empty(), "REAL-002 (O0) binary must exist on disk");
    TEST_ASSERT(bytesO0.size() == kExpectedSizeO0, "REAL-002 (O0) size must match expected");
    auto shaO0 = RealArtifactHarness::compute_sha256(bytesO0.data(), bytesO0.size());
    std::cout << "  REAL-002: " << kArtifactPathO0 << " (" << bytesO0.size() << " bytes)\n";
    std::cout << "  SHA-256:  " << shaO0 << "\n";
    TEST_ASSERT(shaO0 == kExpectedSha256O0, "REAL-002 SHA-256 must match manifest");

    auto metaO0 = RealArtifactHarness::inspect_buffer(bytesO0.data(), bytesO0.size(), kArtifactPathO0);
    TEST_ASSERT(metaO0.is_macho, "REAL-002 must be valid Mach-O");
    TEST_ASSERT(metaO0.is_arm64, "REAL-002 must be ARM64");
    TEST_ASSERT(!metaO0.is_encrypted, "REAL-002 must not be encrypted");
    TEST_ASSERT(metaO0.can_execute_in_userspace(), "REAL-002 must be admitted for execution");

    // REAL-003 (O2)
    auto bytesO2 = read_binary_file(kArtifactPathO2);
    TEST_ASSERT(!bytesO2.empty(), "REAL-003 (O2) binary must exist on disk");
    TEST_ASSERT(bytesO2.size() == kExpectedSizeO2, "REAL-003 (O2) size must match expected");
    auto shaO2 = RealArtifactHarness::compute_sha256(bytesO2.data(), bytesO2.size());
    std::cout << "  REAL-003: " << kArtifactPathO2 << " (" << bytesO2.size() << " bytes)\n";
    std::cout << "  SHA-256:  " << shaO2 << "\n";
    TEST_ASSERT(shaO2 == kExpectedSha256O2, "REAL-003 SHA-256 must match manifest");

    auto metaO2 = RealArtifactHarness::inspect_buffer(bytesO2.data(), bytesO2.size(), kArtifactPathO2);
    TEST_ASSERT(metaO2.is_macho, "REAL-003 must be valid Mach-O");
    TEST_ASSERT(metaO2.is_arm64, "REAL-003 must be ARM64");
    TEST_ASSERT(!metaO2.is_encrypted, "REAL-003 must not be encrypted");
    TEST_ASSERT(metaO2.can_execute_in_userspace(), "REAL-003 must be admitted for execution");

    return true;
}

// Test 2: Execute REAL-002 (compute_arm64_O0) through DarwinUserspaceEngine
bool test_execute_compute_arm64_o0() {
    std::cout << "[Test 2] Execute REAL-002 (compute_arm64_O0) on Guest CPU...\n";

    auto bytes = read_binary_file(kArtifactPathO0);
    TEST_ASSERT(!bytes.empty(), "Failed to read REAL-002 binary");

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest memory
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/compute_arm64_O0";
    config.max_instructions = 50000;

    auto result = engine.execute_macho(bytes.data(), bytes.size(), config, 202);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error message: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must terminate via SYS_exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 (all computations verified)");
    TEST_ASSERT(result.stdout_output == kExpectedStdout, "Captured stdout must match expected format exactly");

    return true;
}

// Test 3: Execute REAL-003 (compute_arm64_O2) through DarwinUserspaceEngine
bool test_execute_compute_arm64_o2() {
    std::cout << "[Test 3] Execute REAL-003 (compute_arm64_O2) on Guest CPU...\n";

    auto bytes = read_binary_file(kArtifactPathO2);
    TEST_ASSERT(!bytes.empty(), "Failed to read REAL-003 binary");

    CPU cpu;
    Memory memory(64 * 1024 * 1024); // 64 MB guest memory
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/compute_arm64_O2";
    config.max_instructions = 50000;

    auto result = engine.execute_macho(bytes.data(), bytes.size(), config, 203);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error message: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must terminate via SYS_exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 (all computations verified)");
    TEST_ASSERT(result.stdout_output == kExpectedStdout, "Captured stdout must match expected format exactly");

    return true;
}

// Test 4: Adversarial Tamper and Corruption Rejection
bool test_compiler_artifact_tamper_rejection() {
    std::cout << "[Test 4] Adversarial Tamper & Corruption Rejection...\n";

    auto bytes = read_binary_file(kArtifactPathO2);
    TEST_ASSERT(!bytes.empty(), "Binary must load for tamper test");

    // Corrupt Mach-O magic
    bytes[0] = 0x00;
    auto meta = RealArtifactHarness::inspect_buffer(bytes.data(), bytes.size(), "corrupted");
    TEST_ASSERT(!meta.is_macho, "Corrupted magic must not be detected as Mach-O");
    TEST_ASSERT(!meta.can_execute_in_userspace(), "Corrupted binary must be rejected");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE S1: Compiler-Generated ARM64 Mach-O Execution Tests\n";
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

    run_test(test_artifacts_disk_load_and_sha256, "Artifact Disk Load & SHA-256 Provenance");
    run_test(test_execute_compute_arm64_o0, "REAL-002 (compute_arm64_O0) Guest CPU Execution");
    run_test(test_execute_compute_arm64_o2, "REAL-003 (compute_arm64_O2) Guest CPU Execution");
    run_test(test_compiler_artifact_tamper_rejection, "Adversarial Tamper & Corruption Rejection");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
