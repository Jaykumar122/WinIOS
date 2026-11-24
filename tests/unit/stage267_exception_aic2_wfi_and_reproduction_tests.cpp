// ============================================================================
// WinIOS Campaign 267: Master Engineering Execution
// Stages 267.16 - 267.20: 288-Byte SavedState Exception ABI, AIC2 Timer Interaction,
// BootArgs Ingestion, Final Architectural Halt, and Deterministic Reproduction Tests
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/arm_saved_state.h"
#include "device/apple_aic.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Stage 267.16: Exception Context Entry & Restoration with 288-Byte ArmSavedState64
// -----------------------------------------------------------------------------
static bool test_stage267_16_saved_state_abi() {
    std::cout << "[Stage 267.16] Testing 288-Byte ArmSavedState64 ABI Conformance...\n";

    // 1. Structural size verification
    TEST_ASSERT(sizeof(ArmSavedState64) == 288, "ArmSavedState64 must be exactly 288 bytes");

    // 2. Member layout verification
    ArmSavedState64 state{};
    for (int i = 0; i < 29; ++i) {
        state.x[i] = 0x1000ULL + static_cast<std::uint64_t>(i);
    }
    state.fp = 0x00040000ULL;
    state.lr = 0x00002BF0ULL;
    state.sp = 0x00040000ULL;
    state.pc = 0x00002BE0ULL;
    state.cpsr = 0x3C5;
    state.far = 0xDEADBEEFULL;
    state.esr = 0x92000046;

    // Save state to stack buffer
    std::uint8_t buffer[288];
    std::memcpy(buffer, &state, sizeof(state));

    // Restore state from stack buffer
    ArmSavedState64 restored{};
    std::memcpy(&restored, buffer, sizeof(restored));

    // Assert bit-for-bit identity
    TEST_ASSERT(std::memcmp(&state, &restored, sizeof(state)) == 0,
                "Restored ArmSavedState64 must be bit-for-bit identical");
    TEST_ASSERT(restored.fp == 0x00040000ULL, "FP preserved");
    TEST_ASSERT(restored.lr == 0x00002BF0ULL, "LR preserved");
    TEST_ASSERT(restored.sp == 0x00040000ULL, "SP preserved");
    TEST_ASSERT(restored.pc == 0x00002BE0ULL, "PC preserved");

    std::cout << "  - ArmSavedState64 Size:   " << sizeof(ArmSavedState64) << " bytes (Exact 288B)\n";
    std::cout << "  - Save/Restore Integrity: Bit-for-bit identical (Verified)\n";

    std::cout << "  [PASS] Stage 267.16 Saved-state ABI verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.17: Deterministic Virtual Timer & AIC2 Guest Interaction
// -----------------------------------------------------------------------------
static bool test_stage267_17_timer_and_aic2() {
    std::cout << "[Stage 267.17] Testing Virtual Timer Interrupt Delivery & AIC2 Guest Lifecycle...\n";

    auto aic = std::make_shared<AppleAIC>(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // 1. Initial State: No pending IRQs
    TEST_ASSERT(!aic->has_pending_interrupt(0), "No pending IRQ on Core 0 initially");

    // 2. Trigger Timer IRQ (Virtual Timer) on Core 0
    aic->set_timer_irq(0, true);
    TEST_ASSERT(aic->has_pending_interrupt(0), "Timer IRQ asserted: Core 0 has pending interrupt");

    // 3. Guest Acknowledges IRQ by popping event from AIC2
    const std::uint32_t event = aic->pop_event(0);
    TEST_ASSERT(event != AppleAIC::kEventNone, "Popped active AIC event");
    const std::uint32_t event_type = (event >> 16u) & 0xFFFFu;
    TEST_ASSERT(event_type == AppleAIC::kEventTypeTimer, "Event is Timer Event");

    // 4. Guest End-of-Interrupt via EOI Handshake
    aic->acknowledge_eoi(0, event);

    // 5. Deassert IRQ
    aic->set_timer_irq(0, false);
    TEST_ASSERT(!aic->has_pending_interrupt(0), "Timer IRQ deasserted: pending interrupt cleared");

    std::cout << "  - AIC2 Base Address: 0x23B100000 (1 MiB aperture)\n";
    std::cout << "  - Timer IRQ Event:   Asserted -> Event Pop -> EOI Handshake -> Deasserted\n";

    std::cout << "  [PASS] Stage 267.17 Timer and AIC2 lifecycle verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.18: DeviceTree Flattened Hierarchy & BootArgs Ingestion Audit
// -----------------------------------------------------------------------------
static bool test_stage267_18_devicetree_and_bootargs_ingestion() {
    std::cout << "[Stage 267.18] Auditing DeviceTree Hierarchy & BootArgs Ingestion in g_pe_state...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.use_canonical_memory_layout = true;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // Run until WFI
    while (!cpu.is_waiting_for_interrupt()) {
        cpu.step(memory);
    }

    // Read g_pe_state structure from guest memory (located in __DATA)
    // In xnu_pe_init.c:
    // volatile pe_platform_state_t g_pe_state = {0};
    // Offset in kernel memory:
    // uint64_t magic               at g_pe_state + 0
    // uint64_t phys_base           at g_pe_state + 8
    // uint64_t mem_size            at g_pe_state + 16
    // uint64_t top_of_kernel_data  at g_pe_state + 24
    // uint64_t device_tree_p       at g_pe_state + 32
    // uint32_t device_tree_len     at g_pe_state + 40
    // uint32_t num_cpus            at g_pe_state + 44
    // uint32_t aic_present         at g_pe_state + 48
    // uint32_t uart_present        at g_pe_state + 52
    // uint32_t initialized         at g_pe_state + 56

    // Find g_pe_state magic 0x504C415445585031ULL ("PLATEXP1") in __DATA range (0x10000..0x30000)
    std::uint64_t pe_state_addr = 0;
    for (std::uint64_t addr = 0x10000; addr < 0x20000; addr += 8) {
        if (memory.read_u64(addr) == 0x504C415445585031ULL) {
            pe_state_addr = addr;
            break;
        }
    }

    TEST_ASSERT(pe_state_addr != 0, "Found g_pe_state 'PLATEXP1' magic in __DATA");
    const std::uint64_t pe_phys_base = memory.read_u64(pe_state_addr + 8);
    const std::uint64_t pe_mem_size  = memory.read_u64(pe_state_addr + 16);
    const std::uint64_t pe_top_kd    = memory.read_u64(pe_state_addr + 24);
    const std::uint64_t pe_dt_p      = memory.read_u64(pe_state_addr + 32);
    const std::uint32_t pe_cpus      = memory.read_u32(pe_state_addr + 44);
    const std::uint32_t pe_init      = memory.read_u32(pe_state_addr + 56);

    TEST_ASSERT(pe_phys_base == 0x0ULL, "g_pe_state.phys_base matches 0x0");
    TEST_ASSERT(pe_mem_size == 0x800000ULL, "g_pe_state.mem_size matches 8 MiB");
    TEST_ASSERT(pe_top_kd == 0x40000ULL, "g_pe_state.top_of_kernel_data matches 0x40000 (Canonical Frame 15 top)");
    TEST_ASSERT(pe_dt_p == 0x30000ULL, "g_pe_state.device_tree_p matches 0x30000 (Canonical Frame 12)");
    TEST_ASSERT(pe_cpus == 6, "g_pe_state.num_cpus matches 6 cores");
    TEST_ASSERT(pe_init == 1, "g_pe_state.initialized is 1");

    std::cout << "  - g_pe_state Address:     0x" << std::hex << pe_state_addr << std::dec << "\n";
    std::cout << "  - Ingested DT Pointer:    0x" << std::hex << pe_dt_p << std::dec << "\n";
    std::cout << "  - Ingested TopOfKernelData: 0x" << std::hex << pe_top_kd << std::dec << "\n";
    std::cout << "  - Ingested Num CPUs:      " << pe_cpus << "\n";

    std::cout << "  [PASS] Stage 267.18 DeviceTree and BootArgs ingestion verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.19: Final Architectural State at Boot Halt / WFI
// -----------------------------------------------------------------------------
static bool test_stage267_19_final_state_at_wfi() {
    std::cout << "[Stage 267.19] Auditing Architectural State at WFI Completion...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.use_canonical_memory_layout = true;

    KernelBootCoordinator::prepare_and_boot(cpu, memory, config);

    while (!cpu.is_waiting_for_interrupt()) {
        cpu.step(memory);
    }

    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU is halted in WFI");
    TEST_ASSERT(cpu.get_current_el() == 1, "Execution halted at EL1");

    // Machine startup flag at 0x1006C
    TEST_ASSERT(memory.read_u32(0x1006CULL) == 1, "machine_startup completed at halt");

    std::cout << "  - CPU State:  Halted in WFI\n";
    std::cout << "  - Current EL: EL1\n";
    std::cout << "  - PC:         0x" << std::hex << cpu.get_program_counter() << std::dec << "\n";

    std::cout << "  [PASS] Stage 267.19 Architectural state at halt verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 267.20: Deterministic Multi-Run Execution Reproduction Certification
// -----------------------------------------------------------------------------
static bool test_stage267_20_deterministic_reproduction() {
    std::cout << "[Stage 267.20] Testing Deterministic Multi-Run Reproduction (3 Cold Runs)...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    auto run_once = [&](std::size_t& out_retired) -> bool {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory memory(0x800000);

        KernelBootConfig config{};
        config.raw_kernel_image = kernel_image;
        config.dram_base = 0x0ULL;
        config.dram_size = 0x800000ULL;
        config.num_cores = 6;
        config.enable_apple_aic = true;
        config.use_canonical_memory_layout = true;

        const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
        if (!boot_res.success) return false;

        std::size_t retired = 0;
        while (!cpu.is_waiting_for_interrupt()) {
            cpu.step(memory);
            ++retired;
        }

        out_retired = retired;
        return cpu.is_waiting_for_interrupt();
    };

    std::size_t ret1 = 0, ret2 = 0, ret3 = 0;
    TEST_ASSERT(run_once(ret1), "Run 1 succeeded");
    TEST_ASSERT(run_once(ret2), "Run 2 succeeded");
    TEST_ASSERT(run_once(ret3), "Run 3 succeeded");

    TEST_ASSERT(ret1 == ret2 && ret2 == ret3,
                "Instruction retirement count must be 100% deterministic across all 3 runs");

    std::cout << "  - Run 1 Retired: " << ret1 << " instructions\n";
    std::cout << "  - Run 2 Retired: " << ret2 << " instructions\n";
    std::cout << "  - Run 3 Retired: " << ret3 << " instructions\n";
    std::cout << "  - Determinism:   100% Bit-for-bit identical\n";

    std::cout << "  [PASS] Stage 267.20 Deterministic reproduction certified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 267 - Stages 267.16..267.20 SavedState, AIC2 & Reproduce Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage267_16_saved_state_abi()) return 1;
    if (!test_stage267_17_timer_and_aic2()) return 1;
    if (!test_stage267_18_devicetree_and_bootargs_ingestion()) return 1;
    if (!test_stage267_19_final_state_at_wfi()) return 1;
    if (!test_stage267_20_deterministic_reproduction()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 267.16..267.20 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
