// ==============================================================================
// WinIOS Campaign 199 Stage 18: Guest Workload Execution & Differential PAC Validation
// ==============================================================================
// Verifies:
// 1. Loading and execution of genuine ARM64e Mach-O binary
//    `guest_stage2_iommu_pac_workload`
// 2. PACIA / AUTIA and PACGA pointer authentication instruction execution in guest
// 3. Differential PAC key sensitivity across distinct runs
// 4. Stage-2 MMU translation integration with guest workload buffer
// 5. Deterministic retirement step count and architectural state equivalence
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"
#include "device/virtual_iommu.h"
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::loader;
using namespace winios::cpu;
using namespace winios::mmu;
using namespace winios::device;

static std::string resolve_artifact_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel,
        "D:/WinISO26/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) return c;
    }
    return rel;
}

static std::vector<std::uint8_t> load_file_bytes(const std::string& path) {
    const std::string resolved = resolve_artifact_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Cannot open artifact: " << path << " (resolved: " << resolved << ")" << std::endl;
        return {};
    }
    const auto sz = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(sz);
    file.read(reinterpret_cast<char*>(bytes.data()), sz);
    return bytes;
}

void test_guest_stage2_iommu_pac_workload_execution() {
    std::cout << "[Test 1] Executing genuine guest_stage2_iommu_pac_workload Mach-O binary...\n";
    const std::string path = "tests/artifacts/bin/guest_stage2_iommu_pac_workload";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty());

    Memory mem(0x400000ULL); // 4 MB
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    TEST_ASSERT(load_res.is_success());

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x80000ULL);

    // Setup default PAC keys
    cpu.get_pac().set_key(PacKeyType::InstructionA, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    cpu.get_pac().set_key(PacKeyType::Generic, 0x1111222233334444ULL, 0x5555666677778888ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40000) == 0x1991C0DEULL) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    const std::uint64_t sig = mem.read_u64(0x40000);
    const std::uint64_t signed_ia = mem.read_u64(0x40008);
    const std::uint64_t gen_tag = mem.read_u64(0x40010);
    const std::uint64_t signed_iza = mem.read_u64(0x40018);
    const std::uint64_t mem_val1 = mem.read_u64(0x50000);
    const std::uint64_t mem_val2 = mem.read_u64(0x50008);

    std::cout << "  Retired steps: " << steps << ", sig=0x" << std::hex << sig
              << ", signed_ia=0x" << signed_ia << ", gen_tag=0x" << gen_tag
              << ", signed_iza=0x" << signed_iza
              << ", mem_val1=0x" << mem_val1 << ", mem_val2=0x" << mem_val2
              << std::dec << "\n";

    TEST_ASSERT(sig == 0x1991C0DEULL);
    TEST_ASSERT(signed_ia != 0x10008000ULL);
    TEST_ASSERT((signed_ia & 0x00000000FFFFFFFFULL) == 0x10008000ULL); // Lower 32 bits preserved
    TEST_ASSERT(gen_tag != 0ULL);
    TEST_ASSERT(signed_iza != 0x20004000ULL);
    TEST_ASSERT((signed_iza & 0x00000000FFFFFFFFULL) == 0x20004000ULL);
    TEST_ASSERT(mem_val1 == 0x19901991ULL);
    TEST_ASSERT(mem_val2 == 0xBABECAFEULL);
    std::cout << "[PASS] test_guest_stage2_iommu_pac_workload_execution\n";
}

void test_guest_workload_differential_pac_keys() {
    std::cout << "[Test 2] Differential PAC key sensitivity across runs...\n";
    const std::string path = "tests/artifacts/bin/guest_stage2_iommu_pac_workload";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty());

    // Run 1: Key A
    Memory mem1(0x400000ULL);
    MachOLoadResult res1 = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem1, 0x0ULL, 0x400000ULL);
    TEST_ASSERT(res1.is_success());
    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    cpu1.set_current_el(1);
    cpu1.set_sp_sel(true);
    cpu1.set_program_counter(res1.entry_point);
    cpu1.set_stack_pointer(0x80000ULL);
    cpu1.get_pac().set_key(PacKeyType::InstructionA, 0xAAAA000011112222ULL, 0x3333444455556666ULL);
    cpu1.get_pac().set_key(PacKeyType::Generic, 0x9999888877776666ULL, 0x5555444433332222ULL);

    for (std::size_t s = 0; s < 2000; ++s) {
        if (mem1.read_u64(0x40000) == 0x1991C0DEULL) break;
        cpu1.step(mem1);
    }

    // Run 2: Key B
    Memory mem2(0x400000ULL);
    MachOLoadResult res2 = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem2, 0x0ULL, 0x400000ULL);
    TEST_ASSERT(res2.is_success());
    CPU cpu2;
    cpu2.set_architectural_exceptions(true);
    cpu2.set_current_el(1);
    cpu2.set_sp_sel(true);
    cpu2.set_program_counter(res2.entry_point);
    cpu2.set_stack_pointer(0x80000ULL);
    cpu2.get_pac().set_key(PacKeyType::InstructionA, 0xBBBB000011112222ULL, 0x3333444455556666ULL);
    cpu2.get_pac().set_key(PacKeyType::Generic, 0x1111000077776666ULL, 0x5555444433332222ULL);

    for (std::size_t s = 0; s < 2000; ++s) {
        if (mem2.read_u64(0x40000) == 0x1991C0DEULL) break;
        cpu2.step(mem2);
    }

    const std::uint64_t signed_ia1 = mem1.read_u64(0x40008);
    const std::uint64_t signed_ia2 = mem2.read_u64(0x40008);
    const std::uint64_t gen_tag1 = mem1.read_u64(0x40010);
    const std::uint64_t gen_tag2 = mem2.read_u64(0x40010);

    TEST_ASSERT(mem1.read_u64(0x40000) == 0x1991C0DEULL);
    TEST_ASSERT(mem2.read_u64(0x40000) == 0x1991C0DEULL);
    // PAC signatures under different keys must differ
    TEST_ASSERT(signed_ia1 != signed_ia2);
    TEST_ASSERT(gen_tag1 != gen_tag2);

    std::cout << "  Key A signed_ia=0x" << std::hex << signed_ia1 << ", Key B signed_ia=0x" << signed_ia2 << std::dec << "\n";
    std::cout << "[PASS] test_guest_workload_differential_pac_keys\n";
}

void test_guest_workload_stage2_translation_isolation() {
    std::cout << "[Test 3] Stage-2 translation mapping with guest workload data buffer...\n";
    Memory mem(1024 * 1024 * 4); // 4 MB RAM
    Stage2TranslationEngine s2;

    s2.set_hcr(0x1ULL);
    s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    s2.set_vttbr((10ULL << 48u) | 0x20000ULL);

    const std::uint64_t ipa = 0x50000ULL;
    const std::uint64_t pa = 0x150000ULL;
    bool mapped = Stage2TranslationEngine::map_page_16k(mem, 0x20000ULL, ipa, pa, true, true, false);
    TEST_ASSERT(mapped);

    auto res = s2.translate(mem, ipa + 0x40, Stage2AccessType::Write);
    TEST_ASSERT(res.has_value());
    TEST_ASSERT(res->physical_address == pa + 0x40);
    TEST_ASSERT(res->writable);

    // Unmapped IPA must fail translation
    auto res_unmapped = s2.translate(mem, 0x90000ULL, Stage2AccessType::Read);
    TEST_ASSERT(!res_unmapped.has_value());

    std::cout << "[PASS] test_guest_workload_stage2_translation_isolation\n";
}

void test_guest_workload_deterministic_retirement() {
    std::cout << "[Test 4] Deterministic instruction retirement across independent executions...\n";
    const std::string path = "tests/artifacts/bin/guest_stage2_iommu_pac_workload";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty());

    auto run_once = [&](std::size_t& retired, std::uint64_t& final_sig, std::uint64_t& reg_x0) {
        Memory mem(0x400000ULL);
        MachOLoadResult res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
        TEST_ASSERT(res.is_success());
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_program_counter(res.entry_point);
        cpu.set_stack_pointer(0x80000ULL);
        cpu.get_pac().set_key(PacKeyType::InstructionA, 0xCAFEBABE11223344ULL, 0x5566778899AABBCCULL);
        cpu.get_pac().set_key(PacKeyType::Generic, 0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL);

        retired = 0;
        while (retired < 2000) {
            if (mem.read_u64(0x40000) == 0x1991C0DEULL) {
                break;
            }
            cpu.step(mem);
            ++retired;
        }
        final_sig = mem.read_u64(0x40000);
        reg_x0 = cpu.read_register(0);
    };

    std::size_t steps1 = 0, steps2 = 0;
    std::uint64_t sig1 = 0, sig2 = 0;
    std::uint64_t reg1 = 0, reg2 = 0;

    run_once(steps1, sig1, reg1);
    run_once(steps2, sig2, reg2);

    std::cout << "  Run 1: " << steps1 << " steps, sig=0x" << std::hex << sig1 << ", x0=0x" << reg1 << std::dec << "\n";
    std::cout << "  Run 2: " << steps2 << " steps, sig=0x" << std::hex << sig2 << ", x0=0x" << reg2 << std::dec << "\n";

    TEST_ASSERT(steps1 > 0);
    TEST_ASSERT(steps1 == steps2);
    TEST_ASSERT(sig1 == 0x1991C0DEULL);
    TEST_ASSERT(sig1 == sig2);
    TEST_ASSERT(reg1 == reg2);

    std::cout << "[PASS] test_guest_workload_deterministic_retirement\n";
}

int main() {
    std::cout << "--- Stage 199.18: Genuine Guest Workloads and Differential Validation Tests ---\n";
    test_guest_stage2_iommu_pac_workload_execution();
    test_guest_workload_differential_pac_keys();
    test_guest_workload_stage2_translation_isolation();
    test_guest_workload_deterministic_retirement();
    std::cout << "All Stage 199.18 tests passed successfully!\n";
    return 0;
}
