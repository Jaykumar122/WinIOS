#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "app/linux_boot_cli.h"
#include "device/fdt_builder.h"
#include "runtime/linux_boot_session.h"

#define STAGE81_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::app;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

std::vector<std::uint8_t> create_test_kernel_buffer() {
    std::vector<std::uint8_t> buf(0x10000, 0); // 64 KB

    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(buf.data());
    // Instruction at 0: B +64 (branch over 64-byte header: 0x14000010)
    hdr->code0 = 0x14000010u;
    // Instruction at 4: NOP (0xD503201F)
    hdr->code1 = 0xD503201Fu;
    hdr->text_offset = 0x80000ULL;
    hdr->image_size = 0x20000ULL; // 128 KB
    hdr->flags = 0x2u;            // 4KB granule, LE
    hdr->magic = kLinuxArm64ImageMagic; // 0x644D5241

    // Fill remaining instructions with NOPs (0xD503201Fu)
    for (std::size_t i = 64; i < buf.size(); i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + i) = 0xD503201Fu;
    }

    return buf;
}

std::vector<std::uint8_t> create_test_initramfs_buffer() {
    std::vector<std::uint8_t> buf(4096, 0);
    std::memcpy(buf.data(), "070701", 6);
    return buf;
}

// -----------------------------------------------------------------------------
// Suite 1: Boot Session Initialization Lifecycle
// -----------------------------------------------------------------------------
bool test_boot_session_initialization_lifecycle() {
    std::cout << "[1] test_boot_session_initialization_lifecycle...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 0x08000000ULL; // 128 MB

    LinuxBootSession session(cfg);
    STAGE81_ASSERT(session.status() == LinuxSessionStatus::Uninitialized, "Initial uninitialized status");

    std::string err;
    bool init_ok = session.initialize(err);
    STAGE81_ASSERT(init_ok, "Initialize must succeed");
    STAGE81_ASSERT(session.status() == LinuxSessionStatus::Prepared, "Status must be Prepared");

    bool boot_ok = session.boot(err);
    STAGE81_ASSERT(boot_ok, "Boot handoff must succeed");
    STAGE81_ASSERT(session.status() == LinuxSessionStatus::Running, "Status must be Running");

    std::cout << "[PASS] Boot Session Initialization Lifecycle\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Auto DTB Generation and Loading
// -----------------------------------------------------------------------------
bool test_auto_dtb_generation_and_loading() {
    std::cout << "[2] test_auto_dtb_generation_and_loading...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();
    cfg.boot_args = "console=ttyAMA0 earlycon=pl011,0x09000000 loglevel=7";

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Session initialize");

    const auto* dtb_reg = session.memory_map().find_region(GuestMemoryRegionType::DeviceTree);
    STAGE81_ASSERT(dtb_reg != nullptr, "DTB region must be registered in memory map");
    STAGE81_ASSERT(dtb_reg->base_address > 0x40000000ULL, "DTB base address valid");
    STAGE81_ASSERT((dtb_reg->base_address % 8) == 0, "DTB must be 8-byte aligned");

    std::cout << "[PASS] Auto DTB Generation and Loading\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Kernel and Initramfs Coordination
// -----------------------------------------------------------------------------
bool test_kernel_and_initramfs_coordination() {
    std::cout << "[3] test_kernel_and_initramfs_coordination...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();
    cfg.initramfs_data = create_test_initramfs_buffer();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Session initialize with initramfs");

    const auto* k_reg = session.memory_map().find_region(GuestMemoryRegionType::KernelImage);
    const auto* i_reg = session.memory_map().find_region(GuestMemoryRegionType::Initramfs);
    const auto* d_reg = session.memory_map().find_region(GuestMemoryRegionType::DeviceTree);

    STAGE81_ASSERT(k_reg && i_reg && d_reg, "All 3 regions must be registered");
    STAGE81_ASSERT(!k_reg->overlaps_with(*i_reg), "Kernel and Initramfs must not overlap");
    STAGE81_ASSERT(!i_reg->overlaps_with(*d_reg), "Initramfs and DTB must not overlap");

    std::cout << "[PASS] Kernel and Initramfs Coordination\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: CPU Register Handoff Contract
// -----------------------------------------------------------------------------
bool test_cpu_register_handoff_contract() {
    std::cout << "[4] test_cpu_register_handoff_contract...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Initialize");
    STAGE81_ASSERT(session.boot(err), "Boot");

    const CPU& cpu0 = session.platform().get_cpu(0);

    // Architectural contract:
    // PC = kernel_entry (0x40080000)
    STAGE81_ASSERT(cpu0.get_program_counter() == 0x40080000ULL, "PC == kernel_entry");

    // X0 = dtb_address (non-zero, 8-byte aligned)
    STAGE81_ASSERT(cpu0.read_register(0) > 0x40000000ULL, "X0 has DTB address");
    STAGE81_ASSERT((cpu0.read_register(0) % 8) == 0, "X0 DTB 8-byte aligned");

    // X1, X2, X3 == 0 (reserved)
    STAGE81_ASSERT(cpu0.read_register(1) == 0, "X1 == 0");
    STAGE81_ASSERT(cpu0.read_register(2) == 0, "X2 == 0");
    STAGE81_ASSERT(cpu0.read_register(3) == 0, "X3 == 0");

    // CurrentEL == EL1
    STAGE81_ASSERT(cpu0.get_current_el() == 1, "CurrentEL == EL1");

    // DAIF interrupts masked
    STAGE81_ASSERT((cpu0.get_daif() & 0xF) == 0xF, "All interrupts masked in DAIF");

    // SCTLR MMU disabled
    STAGE81_ASSERT((cpu0.get_sctlr_el1() & 0x1) == 0, "SCTLR MMU disabled (M=0)");

    // CPACR FPEN enabled (0b11)
    STAGE81_ASSERT(((cpu0.get_cpacr_el1() >> 20) & 0x3) == 0x3, "CPACR FPEN == 0b11");

    std::cout << "[PASS] CPU Register Handoff Contract\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Earlycon Capture and Streaming
// -----------------------------------------------------------------------------
bool test_earlycon_capture_and_streaming() {
    std::cout << "[5] test_earlycon_capture_and_streaming...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Init");
    STAGE81_ASSERT(session.boot(err), "Boot");

    // Write text to UART DR
    std::string test_msg = "[    0.000000] Linux version 6.6-win_ios\n";
    for (char c : test_msg) {
        session.platform().uart()->write_register(VirtualUartDevice::kRegDR, c, 4);
    }

    STAGE81_ASSERT(session.get_console_output() == test_msg, "Captured console output mismatch");

    std::cout << "[PASS] Earlycon Capture and Streaming\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Round-Robin Multicore Execution
// -----------------------------------------------------------------------------
bool test_round_robin_multicore_execution() {
    std::cout << "[6] test_round_robin_multicore_execution...\n";

    LinuxBootConfig cfg;
    cfg.core_count = 2;
    cfg.kernel_data = create_test_kernel_buffer();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Init");
    STAGE81_ASSERT(session.boot(err), "Boot");

    // Initially secondary core is inactive until PSCI CPU_ON
    STAGE81_ASSERT(!session.platform().is_core_active(1), "Core 1 quiescent before PSCI");

    // Step primary core
    std::size_t stepped = session.step(10);
    STAGE81_ASSERT(stepped == 10, "Stepped 10 instructions");
    STAGE81_ASSERT(session.total_instructions_executed() == 10, "Total instructions 10");

    // Manually activate secondary core to verify SMP stepping
    session.platform().set_core_active(1, true);
    session.platform().get_cpu(1).set_program_counter(0x40080000ULL);

    std::size_t smp_stepped = session.step(20);
    STAGE81_ASSERT(smp_stepped == 20, "Stepped 20 SMP instructions");
    STAGE81_ASSERT(session.total_instructions_executed() == 30, "Total instructions 30");

    std::cout << "[PASS] Round-Robin Multicore Execution\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: CLI Argument Parsing and Defaults
// -----------------------------------------------------------------------------
bool test_cli_argument_parsing_and_defaults() {
    std::cout << "[7] test_cli_argument_parsing_and_defaults...\n";

    const char* argv[] = {
        "win_ios_boot",
        "--kernel", "my_vmlinux.Image",
        "--memory", "256",
        "--smp", "4",
        "--append", "console=ttyAMA0 root=/dev/vda rw",
        "--max-insns", "500000",
        "--console-log", "boot.log"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    LinuxBootConfig parsed_cfg;
    std::string err;
    bool ok = LinuxBootCli::parse_arguments(argc, const_cast<char**>(argv), parsed_cfg, err);

    STAGE81_ASSERT(ok, "CLI parse must succeed");
    STAGE81_ASSERT(parsed_cfg.kernel_path == "my_vmlinux.Image", "Kernel path");
    STAGE81_ASSERT(parsed_cfg.ram_size == 256ULL * 1024ULL * 1024ULL, "RAM size 256 MB");
    STAGE81_ASSERT(parsed_cfg.core_count == 4, "Core count 4");
    STAGE81_ASSERT(parsed_cfg.boot_args == "console=ttyAMA0 root=/dev/vda rw", "Boot args");
    STAGE81_ASSERT(parsed_cfg.max_instructions == 500000, "Max insns");
    STAGE81_ASSERT(parsed_cfg.console_log_path == "boot.log", "Log path");

    std::cout << "[PASS] CLI Argument Parsing and Defaults\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: CLI Invalid Arguments and Missing Kernel
// -----------------------------------------------------------------------------
bool test_cli_invalid_arguments_and_missing_kernel() {
    std::cout << "[8] test_cli_invalid_arguments_and_missing_kernel...\n";

    // 1. Missing --kernel
    {
        const char* argv[] = {"win_ios_boot", "--memory", "128"};
        LinuxBootConfig cfg;
        std::string err;
        bool ok = LinuxBootCli::parse_arguments(3, const_cast<char**>(argv), cfg, err);
        STAGE81_ASSERT(!ok, "Must fail without --kernel");
        STAGE81_ASSERT(err.find("Missing required") != std::string::npos, "Error mentions missing kernel");
    }

    // 2. SMP out of range (>8)
    {
        const char* argv[] = {"win_ios_boot", "--kernel", "img", "--smp", "16"};
        LinuxBootConfig cfg;
        std::string err;
        bool ok = LinuxBootCli::parse_arguments(5, const_cast<char**>(argv), cfg, err);
        STAGE81_ASSERT(!ok, "Must fail with SMP > 8");
        STAGE81_ASSERT(err.find("core count") != std::string::npos, "Error mentions core count");
    }

    std::cout << "[PASS] CLI Invalid Arguments and Missing Kernel\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Deterministic Execution Step Budget
// -----------------------------------------------------------------------------
bool test_deterministic_execution_step_budget() {
    std::cout << "[9] test_deterministic_execution_step_budget...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();
    cfg.max_instructions = 100;

    LinuxBootSession session(cfg);
    std::string err;
    STAGE81_ASSERT(session.initialize(err), "Init");
    STAGE81_ASSERT(session.boot(err), "Boot");

    LinuxSessionStatus status = session.run();
    STAGE81_ASSERT(status == LinuxSessionStatus::MaxInstructionsReached, "Must hit max instructions budget");
    STAGE81_ASSERT(session.total_instructions_executed() >= 100, "Executed at least 100 instructions");

    std::cout << "[PASS] Deterministic Execution Step Budget\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Full Synthetic Boot Session Run
// -----------------------------------------------------------------------------
bool test_full_synthetic_boot_session_run() {
    std::cout << "[10] test_full_synthetic_boot_session_run...\n";

    const std::string log_file = "stage81_console.log";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_test_kernel_buffer();
    cfg.initramfs_data = create_test_initramfs_buffer();
    cfg.max_instructions = 50;
    cfg.console_log_path = log_file;

    LinuxBootSession session(cfg);
    LinuxSessionStatus status = session.run();
    STAGE81_ASSERT(status == LinuxSessionStatus::MaxInstructionsReached, "Run completed to budget");

    // Write something to console to test log output
    session.platform().uart()->write_register(VirtualUartDevice::kRegDR, 'K', 4);
    session.platform().uart()->write_register(VirtualUartDevice::kRegDR, '\n', 4);

    STAGE81_ASSERT(session.get_console_output().find("K") != std::string::npos, "Console output contains 'K'");

    std::cout << "[PASS] Full Synthetic Boot Session Run\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 81: Unified Linux Boot Manager & CLI Tests ===\n" << std::flush;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n" << std::flush;
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n" << std::flush;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n" << std::flush;
        }
    };

    run_test(test_boot_session_initialization_lifecycle, "test_boot_session_initialization_lifecycle");
    run_test(test_auto_dtb_generation_and_loading, "test_auto_dtb_generation_and_loading");
    run_test(test_kernel_and_initramfs_coordination, "test_kernel_and_initramfs_coordination");
    run_test(test_cpu_register_handoff_contract, "test_cpu_register_handoff_contract");
    run_test(test_earlycon_capture_and_streaming, "test_earlycon_capture_and_streaming");
    run_test(test_round_robin_multicore_execution, "test_round_robin_multicore_execution");
    run_test(test_cli_argument_parsing_and_defaults, "test_cli_argument_parsing_and_defaults");
    run_test(test_cli_invalid_arguments_and_missing_kernel, "test_cli_invalid_arguments_and_missing_kernel");
    run_test(test_deterministic_execution_step_budget, "test_deterministic_execution_step_budget");
    run_test(test_full_synthetic_boot_session_run, "test_full_synthetic_boot_session_run");

    std::cout << "\nStage 81 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
