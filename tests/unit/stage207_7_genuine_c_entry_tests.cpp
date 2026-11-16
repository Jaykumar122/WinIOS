#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "device/mmio_diagnostics.h"
#include "loader/kernel_image_parser.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_genuine_c_runtime_entry() {
    std::cout << "[Test 1] Stage 207.7: Genuine XNU C Function Entry (_start -> arm_init)...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x400000); // 4 MB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Mach-O image loaded");

    const std::string sha256 = XnuGenuineInitBuilder::compute_sha256(kernel_image);
    TEST_CHECK(!sha256.empty(), "Computed image SHA-256");
    std::cout << "  -> Genuine Kernel Image SHA-256: " << sha256 << "\n";

    // Parse image with KernelImageParser
    KernelParsedImage parsed{};
    const auto parse_st = KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);
    TEST_CHECK(parse_st == KernelImageParseStatus::Success, "KernelImageParser successfully parsed genuine Mach-O");
    TEST_CHECK(parsed.format == KernelImageFormat::MachO64, "Format is MachO64");
    TEST_CHECK(parsed.entry_point_vaddr == 0x1BA0ULL, "Entry point is _start (0x1BA0)");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 serial=3 cpus=6";
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    MmioDiagnostics::instance().clear();

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot coordinator succeeded");
    TEST_CHECK(boot_res.entry_pc == 0x1BA0ULL, "Entry PC matches _start (0x1BA0)");
    TEST_CHECK(boot_res.aic != nullptr, "Apple AIC2 attached");

    std::size_t retired = 0;
    bool reached_wfi = false;
    bool entered_arm_init = false;
    bool entered_pe_init = false;

    // Step CPU through genuine execution
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) {
            reached_wfi = true;
            break;
        }

        const std::uint64_t current_pc = cpu.get_program_counter();
        if (current_pc == 0x14DCULL) {
            entered_arm_init = true;
        }
        if (current_pc == 0x1B8CULL || current_pc == 0x19B4ULL) {
            entered_pe_init = true;
        }

        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total genuine instructions retired: " << retired << "\n";
    std::cout << "  -> Entered _arm_init: " << (entered_arm_init ? "YES" : "NO") << "\n";
    std::cout << "  -> Entered PE_init_platform: " << (entered_pe_init ? "YES" : "NO") << "\n";
    std::cout << "  -> Reached WFI: " << (reached_wfi ? "YES" : "NO") << "\n";

    TEST_CHECK(retired >= 50, "Retired at least 50 genuine instructions");
    TEST_CHECK(entered_arm_init, "Genuine arm_init C entry point reached");
    TEST_CHECK(entered_pe_init, "Genuine PE_init_platform C entry point reached");
    TEST_CHECK(reached_wfi, "Execution completed cleanly into WFI halt");

    // Verify Completion Mailbox (written inside arm_init)
    const std::uint64_t mailbox_val = memory.read_u64(0x50000ULL);
    std::cout << "  -> Status Mailbox at 0x50000: 0x" << std::hex << mailbox_val << std::dec << "\n";
    TEST_CHECK(mailbox_val == 0xCAFEBABE207ULL, "Mailbox holds 0xCAFEBABE207");

    // Verify UART Console Output
    const std::uint8_t uart_byte = memory.read_u8(0x90000);
    std::cout << "  -> UART byte at 0x90000: '" << static_cast<char>(uart_byte) << "' (0x" << std::hex << static_cast<int>(uart_byte) << std::dec << ")\n";
    TEST_CHECK(uart_byte != 0, "UART byte written by compiled C code");

    // Verify MMIO Diagnostics captured AIC Transactions from C code
    const std::size_t aic_tx = MmioDiagnostics::instance().transaction_count();
    std::cout << "  -> AIC MMIO transactions captured: " << aic_tx << "\n";
    TEST_CHECK(aic_tx >= 3, "Captured AIC MMIO transactions from compiled C code");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.7 Genuine C Function Entry Tests ===\n";
    test_genuine_c_runtime_entry();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.7: All genuine C entry tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.7: " << g_failures << " tests failed.\n";
        return 1;
    }
}
