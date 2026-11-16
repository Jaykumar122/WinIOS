#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "device/mmio_diagnostics.h"
#include "loader/xnu_bootstrap_continuation.h"
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

void test_trace_expansion_beyond_boundary() {
    std::cout << "[Test 1] XNU Bootstrap Instruction Trace Expansion Beyond 19 Instructions...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x400000); // 4 MB guest DRAM

    XnuContinuationConfig xnu_cfg{};
    xnu_cfg.text_vmaddr = 0x0ULL;
    xnu_cfg.entry_vaddr = 0x1000ULL;
    xnu_cfg.vbar_vaddr = 0x4000ULL;
    xnu_cfg.aic_base = 0x23B100000ULL;
    xnu_cfg.uart_base = 0x90000ULL;
    xnu_cfg.status_mailbox_pa = 0x20000ULL + 736ULL + 0x100ULL;

    auto kernel_image = XnuBootstrapContinuationBuilder::build_image(xnu_cfg);
    const std::string sha256 = XnuBootstrapContinuationBuilder::compute_sha256(kernel_image);
    TEST_CHECK(!sha256.empty(), "Kernel image SHA-256 computed");
    std::cout << "  -> Continuation Kernel Image SHA-256: " << sha256 << "\n";

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 keepsyms=1 serial=3";
    config.enable_apple_aic = true; // Enable Apple AIC2 responder
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    MmioDiagnostics::instance().clear();

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot coordination succeeded");
    TEST_CHECK(boot_res.entry_pc == 0x1000ULL, "Entry PC is 0x1000");
    TEST_CHECK(boot_res.aic != nullptr, "AIC attached to system");

    std::size_t retired = 0;
    bool reached_wfi = false;

    // Step CPU through the expanded sequence (expecting > 40 instructions)
    for (std::size_t step = 0; step < 100; ++step) {
        if (cpu.is_waiting_for_interrupt()) {
            reached_wfi = true;
            break;
        }
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total genuine instructions retired: " << retired << "\n";
    TEST_CHECK(retired >= 45, "Retired at least 45 genuine bootstrap instructions");
    TEST_CHECK(reached_wfi, "Cleanly reached WFI completion halt");

    // Verify Mailbox Completion Marker
    const std::uint64_t mailbox_val = memory.read_u64(static_cast<std::size_t>(xnu_cfg.status_mailbox_pa));
    TEST_CHECK(mailbox_val == 0xCAFEBABE206ULL, "Status mailbox verified (0xCAFEBABE206)");

    // Verify UART Console Output ('X', 'N', 'U', '2', '0', '6', '\n')
    TEST_CHECK(memory.read_u8(0x90000) == 'X', "UART byte 0 verified ('X')");
    TEST_CHECK(memory.read_u8(0x90001) == 'N', "UART byte 1 verified ('N')");
    TEST_CHECK(memory.read_u8(0x90002) == 'U', "UART byte 2 verified ('U')");
    TEST_CHECK(memory.read_u8(0x90003) == '2', "UART byte 3 verified ('2')");
    TEST_CHECK(memory.read_u8(0x90004) == '0', "UART byte 4 verified ('0')");
    TEST_CHECK(memory.read_u8(0x90005) == '6', "UART byte 5 verified ('6')");
    TEST_CHECK(memory.read_u8(0x90006) == '\n', "UART newline verified");

    // Verify MMIO Diagnostics captured AIC Transactions
    TEST_CHECK(MmioDiagnostics::instance().transaction_count() >= 3, "AIC MMIO transactions captured");
    TEST_CHECK(MmioDiagnostics::instance().fault_count() == 0, "Zero MMIO faults occurred with AIC attached");

    std::cout << "  -> Trace expansion successfully verified.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.11 Bootstrap Instruction Trace Expansion Tests ===\n";
    test_trace_expansion_beyond_boundary();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.11 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.11 Tests FAILED ===\n";
        return 1;
    }
}
