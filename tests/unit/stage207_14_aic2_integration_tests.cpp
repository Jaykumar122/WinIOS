#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "device/mmio_diagnostics.h"
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

void test_aic2_integration_audit() {
    std::cout << "[Test 1] Stage 207.14: Apple AIC2 Interrupt Controller Integration Audit...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x400000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    MmioDiagnostics::instance().clear();

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    // Execute genuine bootstrap sequence
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    // Verify MMIO Diagnostics captured exactly the 3 compiled C accesses
    const auto txs = MmioDiagnostics::instance().get_transactions();
    std::cout << "  -> Total MMIO transactions: " << txs.size() << "\n";
    TEST_CHECK(txs.size() >= 3, "Captured at least 3 MMIO transactions");

    bool read_info = false;
    bool read_whoami = false;
    bool wrote_config = false;

    for (const auto& tx : txs) {
        if (tx.physical_address == 0x23B100004ULL && !tx.is_write && tx.value == 0x06010200ULL) {
            read_info = true;
        }
        if (tx.physical_address == 0x23B102000ULL && !tx.is_write && tx.value == 0x0ULL) {
            read_whoami = true;
        }
        if (tx.physical_address == 0x23B100010ULL && tx.is_write && tx.value == 0x1ULL) {
            wrote_config = true;
        }
    }

    TEST_CHECK(read_info, "Read AIC_INFO register (0x06010200)");
    TEST_CHECK(read_whoami, "Read AIC_WHOAMI register (0x0)");
    TEST_CHECK(wrote_config, "Wrote AIC_CONFIG register (0x1)");

    // Verify global state record in guest memory (g_arm_init_state at 0x10000)
    // struct arm_init_state_t:
    // uint64_t arm_init_magic (offset 0)
    // uint64_t boot_args_addr (offset 8)
    // uint64_t l1_page_table_pa (offset 16)
    // uint32_t aic_info (offset 24)
    // uint32_t aic_whoami (offset 28)
    const std::uint64_t magic = memory.read_u64(0x10000ULL);
    const std::uint32_t aic_info = memory.read_u32(0x10018ULL);
    const std::uint32_t aic_whoami = memory.read_u32(0x1001CULL);

    std::cout << "  -> g_arm_init_state magic: 0x" << std::hex << magic << std::dec << "\n";
    std::cout << "  -> g_arm_init_state aic_info: 0x" << std::hex << aic_info << std::dec << "\n";
    std::cout << "  -> g_arm_init_state aic_whoami: 0x" << std::hex << aic_whoami << std::dec << "\n";

    TEST_CHECK(magic == 0x41524D494E495431ULL, "g_arm_init_state magic matches 'ARMINIT1'");
    TEST_CHECK(aic_info == 0x06010200U, "g_arm_init_state recorded AIC_INFO");
    TEST_CHECK(aic_whoami == 0U, "g_arm_init_state recorded AIC_WHOAMI");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.14 Apple AIC2 Integration Audit Tests ===\n";
    test_aic2_integration_audit();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.14: All AIC2 integration tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.14: " << g_failures << " tests failed.\n";
        return 1;
    }
}
