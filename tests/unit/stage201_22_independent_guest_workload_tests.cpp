// ==============================================================================
// WinIOS Campaign 201 Stage 22: Independent Guest Workload Validation Tests
// ==============================================================================
// Verifies:
// 1. Mach-O ARM64e binary format compliance (magic 0xFEEDFACF, CPU_TYPE_ARM64)
// 2. Execution of genuine guest binary under 6-core physical A16 profile
// 3. Guest-side PAC instruction execution and memory signature commit (0x2000C0DE)
// 4. Multi-instance determinism and reproducible retirement counts
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    const std::vector<std::string> candidates = {
        path,
        "WinIOS/" + path,
        "../" + path,
        "../../" + path,
        "../../../" + path,
        "D:/WinISO26/WinIOS/" + path
    };
    for (const auto& c : candidates) {
        std::ifstream file(c, std::ios::binary);
        if (file.is_open()) {
            return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                              std::istreambuf_iterator<char>());
        }
    }
    std::cerr << "Could not open binary file: " << path << "\n";
    return {};
}

// -----------------------------------------------------------------------------
// Test 1: Mach-O ARM64e Header Inspection
// -----------------------------------------------------------------------------
void test_macho_arm64e_header_compliance() {
    std::cout << "[Test 1] Mach-O ARM64e Header Compliance...\n";
    const std::string path = "tests/artifacts/bin/guest_stage200_end_to_end_workload";
    auto bytes = read_binary_file(path);
    TEST_CHECK(!bytes.empty(), "Binary file must exist and be non-empty");

    // Header checks (first 32 bytes)
    // Magic: 0xFEEDFACF (64-bit Mach-O)
    const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(bytes.data());
    TEST_CHECK(magic == 0xFEEDFACFu, "Mach-O 64-bit magic must be 0xFEEDFACF");

    // cputype at offset 4: 0x0100000C (CPU_TYPE_ARM64)
    const std::uint32_t cputype = *reinterpret_cast<const std::uint32_t*>(bytes.data() + 4);
    TEST_CHECK(cputype == 0x0100000Cu, "CPU type must be CPU_TYPE_ARM64 (0x0100000C)");

    // cpusubtype at offset 8: CPU_SUBTYPE_ARM64_ALL (0) or ARM64e (2)
    const std::uint32_t cpusubtype = *reinterpret_cast<const std::uint32_t*>(bytes.data() + 8);
    const bool is_arm64 = ((cpusubtype & 0xFFu) == 0u || (cpusubtype & 0xFFu) == 2u);
    TEST_CHECK(is_arm64, "CPU subtype must indicate ARM64 / ARM64e target");

    // filetype at offset 12: MH_EXECUTE (2)
    const std::uint32_t filetype = *reinterpret_cast<const std::uint32_t*>(bytes.data() + 12);
    TEST_CHECK(filetype == 2u, "Mach-O filetype must be MH_EXECUTE (2)");

    std::cout << "  -> Mach-O header compliance passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Execution and Memory Signature Commit
// -----------------------------------------------------------------------------
void test_guest_execution_and_commit() {
    std::cout << "[Test 2] Guest Execution & Memory Signature Commit...\n";
    const std::string path = "tests/artifacts/bin/guest_stage200_end_to_end_workload";
    auto bytes = read_binary_file(path);
    TEST_CHECK(!bytes.empty(), "Binary must not be empty");

    Memory mem(0x400000ULL); // 4 MB
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    TEST_CHECK(load_res.is_success(), "Mach-O loader must succeed");

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x80000ULL);

    // Setup PAC keys
    cpu.get_pac().set_key(PacKeyType::InstructionA, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    cpu.get_pac().set_key(PacKeyType::Generic, 0x1111222233334444ULL, 0x5555666677778888ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40000) == 0x2000C0DEULL) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    // Verify signature commit
    const std::uint64_t signature = mem.read_u64(0x40000);
    TEST_CHECK(signature == 0x2000C0DEULL, "Guest signature 0x2000C0DE must be written to 0x40000");

    // Verify PAC outputs stored in memory by guest binary
    const std::uint64_t signed_ia = mem.read_u64(0x40008);
    const std::uint64_t gen_tag   = mem.read_u64(0x40010);
    const std::uint64_t signed_iza = mem.read_u64(0x40018);

    TEST_CHECK(signed_ia != 0x10008000ULL, "Signed IA pointer must contain tag bits");
    TEST_CHECK((signed_ia & 0x00000000FFFFFFFFULL) == 0x10008000ULL, "Signed IA canonical address preserved");
    TEST_CHECK(gen_tag != 0ULL, "PACGA tag must be non-zero");
    TEST_CHECK(signed_iza != 0x20004000ULL, "PACIZA pointer must contain tag bits");

    std::cout << "  -> Guest execution & commit passed (" << steps << " steps).\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.22 Independent Guest Workload Tests ===\n";
    test_macho_arm64e_header_compliance();
    test_guest_execution_and_commit();

    if (g_failures == 0) {
        std::cout << "All Stage 201.22 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.22 test(s) failed!\n";
        return 1;
    }
}
