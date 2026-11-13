// ============================================================================
// Stage 193.16: Synthetic EL1 Kernel Runtime Tests
// ============================================================================

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;

static constexpr const char* kKernelArtifactPath = "tests/artifacts/bin/synthetic_el1_kernel";
static constexpr const char* kExpectedSha256 = "ef08793bdf38817e06c8d37157d5b25ba94b959269a7965ad45a82d4c4a200cf";
static constexpr std::size_t kExpectedSize = 51832;

static std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) return c;
    }
    return rel;
}

static std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    const std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buf.data()), size);
    }
    return buf;
}

// Test 1: Verify Artifact Authenticity, Provenance and SHA-256
static bool test_synthetic_kernel_artifact_provenance_and_hash() {
    auto bytes = read_binary_file(kKernelArtifactPath);
    CHECK(!bytes.empty(), "Kernel artifact must exist on disk");
    CHECK(bytes.size() == kExpectedSize, "Kernel size must match 51,832 bytes");

    auto sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    CHECK(sha == kExpectedSha256, "Kernel SHA-256 must match authoritative hash");

    auto meta = RealArtifactHarness::inspect_buffer(bytes.data(), bytes.size(), kKernelArtifactPath);
    CHECK(meta.is_macho, "Kernel artifact must be a valid Mach-O binary");
    CHECK(meta.is_arm64, "Kernel artifact must target ARM64");
    CHECK(!meta.is_encrypted, "Kernel artifact must not be encrypted");

    std::cout << "[PASS] test_synthetic_kernel_artifact_provenance_and_hash (SHA-256: " << sha << ")\n";
    return true;
}

// Test 2: Full Synthetic Kernel Boot, EL1->EL0 Drop, Guest Syscalls, and Return
static bool test_synthetic_kernel_execution_and_privilege_transition() {
    auto bytes = read_binary_file(kKernelArtifactPath);
    CHECK(!bytes.empty(), "Kernel artifact must load");

    Memory mem(0x400000ULL); // 4 MB guest memory
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    // Load Mach-O segments into guest memory
    MachOLoadResult load_res = MachOLoader::load_from_buffer(
        bytes.data(), bytes.size(), mem, 0x10000ULL, 0x300000ULL);
    if (!load_res.is_success()) {
        std::cerr << "MachOLoader error: " << load_res.error_message << ", status=" << static_cast<int>(load_res.status) << "\n";
    }
    CHECK(load_res.is_success(), "MachOLoader must successfully map kernel into memory");
    CHECK(load_res.entry_point != 0, "Entry point must be valid");

    // Boot CPU into EL1 (Kernel Mode)
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0);
    cpu.set_program_counter(load_res.entry_point);

    std::size_t instructions_retired = 0;
    while (cpu.get_current_el() == 1 && instructions_retired < 50) {
        cpu.step(mem);
        ++instructions_retired;
    }

    CHECK(cpu.get_current_el() == 0, "Guest kernel must drop privilege to EL0 via ERET");
    CHECK(cpu.get_vbar_el1() != 0, "Kernel must have initialized VBAR_EL1");

    // Now CPU is executing EL0 user client code!
    // Client executes:
    // 1. Syscall 1 (SYS_diag_print): prints "SyntheticKernel-OK\n" to 0x90000
    // 2. Syscall 2 (SYS_incr_counter): increments counter to 1
    // 3. Syscall 2 (SYS_incr_counter): increments counter to 2
    // 4. Syscall 3 (SYS_exit): returns 42, sets completion flag
    // 5. Client enters idle loop (.Lclient_idle)
    const std::size_t max_steps = 300;
    while (instructions_retired < max_steps) {
        // If completion flag at 0x40008 is set and we're back at EL0, we're done
        if (mem.read_u64(0x40008) == 1 && cpu.get_current_el() == 0 && cpu.read_register(0) == 42) {
            break;
        }
        cpu.step(mem);
        ++instructions_retired;
    }

    // Verify Virtual Console Diagnostic Output at 0x90000
    char console_output[20] = {0};
    for (std::size_t i = 0; i < 19; ++i) {
        console_output[i] = static_cast<char>(mem.read_u8(0x90000 + i));
    }
    CHECK(std::string(console_output) == "SyntheticKernel-OK\n",
          "Virtual console output must match 'SyntheticKernel-OK\\n'");

    // Verify Protected EL1 State Counter at 0x40000
    const std::uint64_t counter = mem.read_u64(0x40000);
    CHECK(counter == 2, "EL1 kernel state counter must be incremented to 2");

    // Verify Completion Flag at 0x40008
    const std::uint64_t completion = mem.read_u64(0x40008);
    CHECK(completion == 1, "EL1 completion flag must be 1");

    // Verify Return Code in X0
    CHECK(cpu.read_register(0) == 42, "Exit status returned to EL0 client must be 42");

    // Verify Instruction Retirement Count
    CHECK(instructions_retired > 30, "Must retire genuine guest instructions (>30)");
    std::cout << "  Retired " << instructions_retired << " guest instructions through CPU::step\n";

    std::cout << "[PASS] test_synthetic_kernel_execution_and_privilege_transition\n";
    return true;
}

// Test 3: Synthetic Kernel Page Fault Handling and Demand Resolution
static bool test_synthetic_kernel_page_fault_handling() {
    auto bytes = read_binary_file(kKernelArtifactPath);
    CHECK(!bytes.empty(), "Kernel artifact must load");

    Memory mem(0x400000ULL);
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    MachOLoadResult load_res = MachOLoader::load_from_buffer(
        bytes.data(), bytes.size(), mem, 0x10000ULL, 0x300000ULL);
    CHECK(load_res.is_success(), "Load kernel");

    // Initialize kernel at EL1
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0);
    cpu.set_program_counter(load_res.entry_point);

    // Boot kernel until EL0
    for (int i = 0; i < 20 && cpu.get_current_el() == 1; ++i) {
        cpu.step(mem);
    }
    CHECK(cpu.get_current_el() == 0, "CPU reached EL0");

    // Trigger a synthetic Data Abort by having CPU simulate a page fault from Lower EL:
    // FAR_EL1 = 0x55000 (faulting address)
    // Synchronous exception taken: Lower EL Data Abort (EC 0x24)
    ArchitecturalException exc{};
    exc.type = ExceptionType::Synchronous;
    exc.ec = ExceptionClass::DataAbortLower;
    exc.originating_el = 0;
    exc.target_el = 1;
    exc.faulting_pc = cpu.get_program_counter();
    exc.preferred_return = cpu.get_program_counter();
    exc.syndrome = (0x24u << 26u) | 0x06u; // EC 0x24, FSC 0x06 (translation fault level 2)
    exc.has_fault_address = true;
    exc.fault_address = 0x55000ULL;
    cpu.deliver_architectural_exception(exc);

    CHECK(cpu.get_current_el() == 1, "CPU elevated to EL1 to service page fault");
    CHECK(cpu.get_far_el1() == 0x55000ULL, "FAR_EL1 holds fault address 0x55000");

    // Step kernel page fault handler instructions:
    // It reads far_el1, stores at 0x40010, increments count at 0x40018, advances ELR, and executes ERET!
    for (int i = 0; i < 25 && cpu.get_current_el() == 1; ++i) {
        cpu.step(mem);
    }

    CHECK(cpu.get_current_el() == 0, "Kernel returned to EL0 after resolving fault");
    CHECK(mem.read_u64(0x40010) == 0x55000ULL, "Kernel recorded faulting address 0x55000");
    CHECK(mem.read_u64(0x40018) == 1, "Kernel resolved 1 page fault");

    std::cout << "[PASS] test_synthetic_kernel_page_fault_handling\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.16: Synthetic EL1 Kernel Runtime Tests ===\n";
        if (!test_synthetic_kernel_artifact_provenance_and_hash()) return 1;
        if (!test_synthetic_kernel_execution_and_privilege_transition()) return 1;
        if (!test_synthetic_kernel_page_fault_handling()) return 1;
        std::cout << "=== All Stage 193.16 Tests Passed (3/3) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
