// ============================================================================
// Stage 194.1: Synthetic Kernel Image Loading and Entry Contract Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "loader/synthetic_kernel_loader.h"
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

// -----------------------------------------------------------------------------
// Test 1: Boot Info Struct Layout, Alignment, and Packed Offsets
// -----------------------------------------------------------------------------
static bool test_boot_info_struct_layout_and_offsets() {
    CHECK(sizeof(SyntheticKernelBootInfo) == 256, "SyntheticKernelBootInfo must be exactly 256 bytes");

    CHECK(offsetof(SyntheticKernelBootInfo, magic) == 0, "magic offset must be 0");
    CHECK(offsetof(SyntheticKernelBootInfo, version) == 8, "version offset must be 8");
    CHECK(offsetof(SyntheticKernelBootInfo, flags) == 12, "flags offset must be 12");
    CHECK(offsetof(SyntheticKernelBootInfo, dram_base) == 16, "dram_base offset must be 16");
    CHECK(offsetof(SyntheticKernelBootInfo, dram_size) == 24, "dram_size offset must be 24");
    CHECK(offsetof(SyntheticKernelBootInfo, kernel_text_base) == 32, "kernel_text_base offset must be 32");
    CHECK(offsetof(SyntheticKernelBootInfo, kernel_text_size) == 40, "kernel_text_size offset must be 40");
    CHECK(offsetof(SyntheticKernelBootInfo, kernel_data_base) == 48, "kernel_data_base offset must be 48");
    CHECK(offsetof(SyntheticKernelBootInfo, kernel_data_size) == 56, "kernel_data_size offset must be 56");
    CHECK(offsetof(SyntheticKernelBootInfo, entry_point) == 64, "entry_point offset must be 64");
    CHECK(offsetof(SyntheticKernelBootInfo, initial_sp_el1) == 72, "initial_sp_el1 offset must be 72");
    CHECK(offsetof(SyntheticKernelBootInfo, initial_sp_el0) == 80, "initial_sp_el0 offset must be 80");
    CHECK(offsetof(SyntheticKernelBootInfo, mmio_console_base) == 88, "mmio_console_base offset must be 88");
    CHECK(offsetof(SyntheticKernelBootInfo, mmio_console_size) == 96, "mmio_console_size offset must be 96");
    CHECK(offsetof(SyntheticKernelBootInfo, free_memory_base) == 104, "free_memory_base offset must be 104");
    CHECK(offsetof(SyntheticKernelBootInfo, free_memory_size) == 112, "free_memory_size offset must be 112");
    CHECK(offsetof(SyntheticKernelBootInfo, cmdline_addr) == 120, "cmdline_addr offset must be 120");
    CHECK(offsetof(SyntheticKernelBootInfo, cmdline_size) == 128, "cmdline_size offset must be 128");
    CHECK(offsetof(SyntheticKernelBootInfo, dtb_base) == 136, "dtb_base offset must be 136");
    CHECK(offsetof(SyntheticKernelBootInfo, dtb_size) == 144, "dtb_size offset must be 144");
    CHECK(offsetof(SyntheticKernelBootInfo, boot_timestamp_ticks) == 152, "boot_timestamp_ticks offset must be 152");
    CHECK(offsetof(SyntheticKernelBootInfo, cpu_count) == 160, "cpu_count offset must be 160");
    CHECK(offsetof(SyntheticKernelBootInfo, page_granule_bytes) == 168, "page_granule_bytes offset must be 168");
    CHECK(offsetof(SyntheticKernelBootInfo, reserved) == 176, "reserved offset must be 176");

    std::cout << "[PASS] test_boot_info_struct_layout_and_offsets\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Memory Serialization and Deserialization Round-Trip
// -----------------------------------------------------------------------------
static bool test_boot_info_serialization_round_trip() {
    Memory mem(0x20000); // 128 KB
    const std::uint64_t addr = 0x4000;

    SyntheticKernelBootInfo src{};
    src.magic = kSyntheticKernelMagic;
    src.version = kSyntheticKernelVersion;
    src.flags = kBootFlagConsolePresent | kBootFlagTimerEnabled;
    src.dram_base = 0x40000000ULL;
    src.dram_size = 0x10000000ULL;
    src.kernel_text_base = 0x40010000ULL;
    src.kernel_text_size = 0x20000ULL;
    src.kernel_data_base = 0x40030000ULL;
    src.kernel_data_size = 0x10000ULL;
    src.entry_point = 0x40010040ULL;
    src.initial_sp_el1 = 0x40050000ULL;
    src.initial_sp_el0 = 0x40060000ULL;
    src.mmio_console_base = 0x090000ULL;
    src.mmio_console_size = 0x1000ULL;
    src.free_memory_base = 0x40070000ULL;
    src.free_memory_size = 0x0FF90000ULL;
    src.cmdline_addr = 0x40004100ULL;
    src.cmdline_size = 23;
    src.dtb_base = 0x40001000ULL;
    src.dtb_size = 0x3000ULL;
    src.boot_timestamp_ticks = 12345678ULL;
    src.cpu_count = 4;
    src.page_granule_bytes = 16384;

    CHECK(SyntheticKernelLoader::write_boot_info(mem, addr, src), "Writing boot info to memory must succeed");

    SyntheticKernelBootInfo dst{};
    CHECK(SyntheticKernelLoader::read_boot_info(mem, addr, dst), "Reading boot info from memory must succeed");

    CHECK(dst.magic == src.magic, "magic must match");
    CHECK(dst.version == src.version, "version must match");
    CHECK(dst.flags == src.flags, "flags must match");
    CHECK(dst.dram_base == src.dram_base, "dram_base must match");
    CHECK(dst.dram_size == src.dram_size, "dram_size must match");
    CHECK(dst.kernel_text_base == src.kernel_text_base, "kernel_text_base must match");
    CHECK(dst.kernel_text_size == src.kernel_text_size, "kernel_text_size must match");
    CHECK(dst.kernel_data_base == src.kernel_data_base, "kernel_data_base must match");
    CHECK(dst.kernel_data_size == src.kernel_data_size, "kernel_data_size must match");
    CHECK(dst.entry_point == src.entry_point, "entry_point must match");
    CHECK(dst.initial_sp_el1 == src.initial_sp_el1, "initial_sp_el1 must match");
    CHECK(dst.initial_sp_el0 == src.initial_sp_el0, "initial_sp_el0 must match");
    CHECK(dst.free_memory_base == src.free_memory_base, "free_memory_base must match");
    CHECK(dst.free_memory_size == src.free_memory_size, "free_memory_size must match");
    CHECK(dst.cmdline_addr == src.cmdline_addr, "cmdline_addr must match");
    CHECK(dst.cmdline_size == src.cmdline_size, "cmdline_size must match");
    CHECK(dst.cpu_count == src.cpu_count, "cpu_count must match");
    CHECK(dst.page_granule_bytes == src.page_granule_bytes, "page_granule_bytes must match");

    std::cout << "[PASS] test_boot_info_serialization_round_trip\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Boot Contract Validation Matrix
// -----------------------------------------------------------------------------
static bool test_boot_contract_validation_matrix() {
    SyntheticKernelBootInfo info{};
    info.magic = kSyntheticKernelMagic;
    info.version = kSyntheticKernelVersion;
    info.dram_size = 0x1000000ULL;
    info.entry_point = 0x10040ULL;
    info.page_granule_bytes = 16384;

    std::string err;
    CHECK(SyntheticKernelLoader::validate_boot_info(info, &err), "Valid boot info must pass validation");

    // Invalid magic
    info.magic = 0xDEADBEEFULL;
    CHECK(!SyntheticKernelLoader::validate_boot_info(info, &err), "Invalid magic must fail");
    info.magic = kSyntheticKernelMagic;

    // Invalid version
    info.version = 99;
    CHECK(!SyntheticKernelLoader::validate_boot_info(info, &err), "Invalid version must fail");
    info.version = kSyntheticKernelVersion;

    // Zero dram size
    info.dram_size = 0;
    CHECK(!SyntheticKernelLoader::validate_boot_info(info, &err), "Zero dram_size must fail");
    info.dram_size = 0x1000000ULL;

    // Zero entry point
    info.entry_point = 0;
    CHECK(!SyntheticKernelLoader::validate_boot_info(info, &err), "Zero entry_point must fail");
    info.entry_point = 0x10040ULL;

    // Invalid page granule
    info.page_granule_bytes = 8192;
    CHECK(!SyntheticKernelLoader::validate_boot_info(info, &err), "Unsupported page granule must fail");
    info.page_granule_bytes = 16384;

    std::cout << "[PASS] test_boot_contract_validation_matrix\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Real Synthetic Kernel Loading via SyntheticKernelLoader
// -----------------------------------------------------------------------------
static bool test_real_synthetic_kernel_loading() {
    Memory mem(0x400000ULL); // 4 MB guest memory
    SyntheticKernelLoadConfig cfg;
    cfg.dram_base = 0x0ULL;
    cfg.dram_size = 0x400000ULL;
    cfg.boot_info_addr = 0x8000ULL;
    cfg.cmdline = "boot=synthetic_kernel loglevel=7";

    SyntheticKernelLoadResult res = SyntheticKernelLoader::load_from_file(kKernelArtifactPath, mem, cfg);
    if (!res.is_success()) {
        std::cerr << "Load error: " << res.error_message << "\n";
    }
    CHECK(res.is_success(), "SyntheticKernelLoader must successfully load kernel artifact");
    CHECK(res.sha256_hex == kExpectedSha256, "Kernel SHA-256 must match authentic value");
    CHECK(res.entry_point != 0, "Kernel entry point must be non-zero");
    CHECK(res.boot_info_guest_addr == 0x8000ULL, "Boot info address must match config");

    // Inspect Boot Info written into memory
    SyntheticKernelBootInfo read_back{};
    CHECK(SyntheticKernelLoader::read_boot_info(mem, res.boot_info_guest_addr, read_back),
          "Read back of boot info from memory must succeed");
    CHECK(read_back.magic == kSyntheticKernelMagic, "Read-back magic must match");
    CHECK(read_back.version == kSyntheticKernelVersion, "Read-back version must be 1");
    CHECK(read_back.entry_point == res.entry_point, "Read-back entry point must match");
    CHECK(read_back.page_granule_bytes == 16384, "Read-back page granule must be 16KB");
    CHECK(read_back.free_memory_base > 0, "Free memory base must be allocated");
    CHECK((read_back.free_memory_base % 16384) == 0, "Free memory base must be 16KB aligned");

    // Inspect command line written in memory
    std::string cmdline_read;
    for (std::uint64_t i = 0; i < read_back.cmdline_size; ++i) {
        cmdline_read.push_back(static_cast<char>(mem.read_u8(static_cast<std::size_t>(read_back.cmdline_addr + i))));
    }
    CHECK(cmdline_read == cfg.cmdline, "Command line in memory must match configured string");

    std::cout << "[PASS] test_real_synthetic_kernel_loading (Entry: 0x" << std::hex << res.entry_point
              << ", FreeMemBase: 0x" << read_back.free_memory_base << std::dec << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: CPU Register Setup & Kernel Boot Execution Verification
// -----------------------------------------------------------------------------
static bool test_kernel_cpu_boot_and_execution() {
    Memory mem(0x400000ULL); // 4 MB guest memory
    SyntheticKernelLoadConfig cfg;
    cfg.dram_base = 0x0ULL;
    cfg.dram_size = 0x400000ULL;
    cfg.boot_info_addr = 0x8000ULL;

    SyntheticKernelLoadResult res = SyntheticKernelLoader::load_from_file(kKernelArtifactPath, mem, cfg);
    CHECK(res.is_success(), "Kernel load must succeed");

    CPU cpu;
    res.configure_cpu(cpu);

    // Verify initial CPU state configured by boot contract
    CHECK(cpu.get_current_el() == 1, "CPU must be initialized at EL1 (Kernel Mode)");
    CHECK(cpu.get_sp_sel() == true, "CPU must use SP_EL1");
    CHECK(cpu.get_program_counter() == res.entry_point, "CPU PC must point to entry point");
    CHECK(cpu.read_register(0) == res.boot_info_guest_addr, "X0 must hold pointer to boot info");
    CHECK(cpu.read_register(1) == kSyntheticKernelMagic, "X1 must hold boot magic");

    // Step kernel through initialization and transition to EL0
    std::size_t steps = 0;
    while (cpu.get_current_el() == 1 && steps < 50) {
        cpu.step(mem);
        ++steps;
    }
    CHECK(cpu.get_current_el() == 0, "Kernel must drop privilege to EL0");

    // Execute user client until completion
    while (steps < 300) {
        if (mem.read_u64(0x40008) == 1 && cpu.get_current_el() == 0 && cpu.read_register(0) == 42) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    CHECK(mem.read_u64(0x40000) == 2, "Kernel state counter must be 2");
    CHECK(mem.read_u64(0x40008) == 1, "Completion flag must be 1");
    CHECK(cpu.read_register(0) == 42, "Exit status in X0 must be 42");

    std::cout << "[PASS] test_kernel_cpu_boot_and_execution (Retired " << steps << " instructions)\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.1 Synthetic Kernel Boot Contract Tests...\n";

    if (!test_boot_info_struct_layout_and_offsets()) return 1;
    if (!test_boot_info_serialization_round_trip()) return 1;
    if (!test_boot_contract_validation_matrix()) return 1;
    if (!test_real_synthetic_kernel_loading()) return 1;
    if (!test_kernel_cpu_boot_and_execution()) return 1;

    std::cout << "\nAll Stage 194.1 Kernel Boot Contract tests PASSED successfully.\n";
    return 0;
}
