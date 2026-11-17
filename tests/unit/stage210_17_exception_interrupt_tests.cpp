#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "kernel/preemption_timer.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_apple_aic2_interrupt_controller() {
    std::cout << "[Test 1] Stage 210.17: Apple AIC2 Interrupt Controller Probe & MMIO Access...\n";

    const std::uint64_t aic_base = 0x23B100000ULL;
    const std::uint64_t aic_size = 0x100000ULL;
    auto aic = std::make_shared<AppleAIC>(0x41494332u, aic_base, aic_size, 6);

    // Read HW INFO at offset 4
    const std::uint32_t info = static_cast<std::uint32_t>(aic->read_register(4, 4));
    TEST_CHECK(info == 0x06010200u, "AIC_INFO matches Apple AIC2 hardware signature (0x06010200)");

    // Read WHOAMI for Core 0 at offset 0x2000
    const std::uint32_t whoami = static_cast<std::uint32_t>(aic->read_register(0x2000, 4));
    TEST_CHECK(whoami == 0, "Core 0 WHOAMI is 0");

    // Enable AIC by writing to AIC_CONFIG at offset 16
    aic->write_register(16, 1, 4);
}

void test_arm64_exception_dispatch_and_vbar() {
    std::cout << "[Test 2] Stage 210.17: VBAR_EL1 Configuration & Exception Vector Routing...\n";

    CPU cpu;
    Memory mem(0x100000);

    const std::uint64_t vbar_pa = 0x8000ULL;
    const std::uint64_t irq_handler_pa = 0x9000ULL;

    TEST_CHECK(PreemptionTimer::setup_vector_table(mem, vbar_pa, irq_handler_pa), "Setup vector table");
    cpu.set_vbar_el1(vbar_pa);
    TEST_CHECK(cpu.get_vbar_el1() == vbar_pa, "VBAR_EL1 configured");

    // Vector entry points for ARM64:
    // Offset 0x200: Current EL with SPx Synchronous
    // Offset 0x280: Current EL with SPx IRQ
    // Offset 0x400: Lower EL using AArch64 Synchronous
    // Offset 0x480: Lower EL using AArch64 IRQ
    const std::uint32_t irq_spx_branch = mem.read_u32(vbar_pa + 0x280);
    const std::uint32_t irq_lower_branch = mem.read_u32(vbar_pa + 0x480);

    TEST_CHECK((irq_spx_branch & 0xFC000000u) == 0x14000000u, "Valid branch at VBAR + 0x280");
    TEST_CHECK((irq_lower_branch & 0xFC000000u) == 0x14000000u, "Valid branch at VBAR + 0x480");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.17 Exception & Interrupt Tests ===\n";
    test_apple_aic2_interrupt_controller();
    test_arm64_exception_dispatch_and_vbar();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.17: All exception and interrupt tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.17: " << g_failures << " tests failed.\n";
        return 1;
    }
}
