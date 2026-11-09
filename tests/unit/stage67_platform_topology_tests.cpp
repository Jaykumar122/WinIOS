#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"

#define STAGE67_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::device;

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;

// -----------------------------------------------------------------------------
// Suite 1: Platform Composition & Topology Geometry
// -----------------------------------------------------------------------------
bool test_platform_composition() {
    std::cout << "[1] test_platform_composition...\n";
    VirtualPlatformConfig config;
    config.core_count = 4;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x02000000; // 32 MB
    config.boot_entry = 0x40000000ULL;

    VirtualPlatform platform(config);

    STAGE67_ASSERT(platform.core_count() == 4, "Platform instantiated with 4 cores");
    STAGE67_ASSERT(platform.memory().size() == 0x02000000, "RAM size is 32 MB");
    STAGE67_ASSERT(platform.gic().get_core_count() == 4, "GICv3 configured for 4 cores");
    STAGE67_ASSERT(platform.uart() != nullptr, "Virtual UART console attached");
    STAGE67_ASSERT(platform.dma() != nullptr, "Virtual DMA device attached");
    STAGE67_ASSERT(platform.device_bus()->device_count() >= 2, "Device bus has registered peripherals");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Memory Map Non-Overlapping Boundary Validation
// -----------------------------------------------------------------------------
bool test_memory_map_validation() {
    std::cout << "[2] test_memory_map_validation...\n";
    VirtualPlatform platform;

    // Standard platform memory map must validate cleanly
    STAGE67_ASSERT(platform.validate_memory_map(), "Standard memory map has no collisions");

    // Test overlapping configuration
    VirtualPlatformConfig bad_config;
    bad_config.ram_base = 0x08000000ULL; // Collides with GICD
    bad_config.ram_size = 0x01000000;
    bad_config.gicd_base = 0x08000000ULL;

    VirtualPlatform bad_platform(bad_config);
    STAGE67_ASSERT(!bad_platform.validate_memory_map(), "Colliding memory map must fail validation");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Virtual Console UART I/O
// -----------------------------------------------------------------------------
bool test_uart_console_io() {
    std::cout << "[3] test_uart_console_io...\n";
    VirtualPlatform platform;
    auto uart = platform.uart();
    STAGE67_ASSERT(uart != nullptr, "UART exists");

    // Write "WinIOS" to UART DR
    const std::string test_str = "WinIOS Platform Test\n";
    for (char c : test_str) {
        platform.device_bus()->write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR,
                                           static_cast<std::uint32_t>(c), 4);
    }

    STAGE67_ASSERT(uart->get_output_text() == test_str, "Output text matches string written to UART");

    uart->clear_output();
    STAGE67_ASSERT(uart->output_bytes_count() == 0, "UART output cleared");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: UART Interrupt Signaling on Input Injection
// -----------------------------------------------------------------------------
bool test_uart_interrupt_signaling() {
    std::cout << "[4] test_uart_interrupt_signaling...\n";
    VirtualPlatform platform;
    auto uart = platform.uart();

    // Enable RX interrupt mask in IMSC (bit 4)
    platform.device_bus()->write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegIMSC,
                                       (1u << 4), 4);

    // Track interrupt line assertion
    bool irq_asserted = false;
    uart->set_interrupt_callback([&](std::uint32_t irq, bool asserted) {
        if (irq == VirtualUartDevice::kDefaultUartIrq) {
            irq_asserted = asserted;
        }
    });

    // Inject input character 'A'
    uart->inject_input('A');
    STAGE67_ASSERT(irq_asserted == true, "UART asserted IRQ upon character injection");

    // Read back character from DR
    std::uint64_t ch = platform.device_bus()->read_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR, 4);
    STAGE67_ASSERT(ch == 'A', "Read injected character from DR");

    // RX FIFO now empty -> interrupt line deasserted
    STAGE67_ASSERT(irq_asserted == false, "UART deasserted IRQ once FIFO drained");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Cold Reset Sequencing
// -----------------------------------------------------------------------------
bool test_cold_reset_sequencing() {
    std::cout << "[5] test_cold_reset_sequencing...\n";
    VirtualPlatformConfig config;
    config.core_count = 2;
    config.boot_entry = 0x40001000ULL;
    VirtualPlatform platform(config);

    // Mutate state on core 0
    auto& cpu0 = platform.get_cpu(0);
    cpu0.write_register(0, 0x12345678ULL);
    cpu0.write_register(1, 0x9999ULL);
    cpu0.set_program_counter(0x40002000ULL);

    // Write UART output
    platform.uart()->write_register(VirtualUartDevice::kRegDR, 'Z', 4);
    STAGE67_ASSERT(platform.uart()->output_bytes_count() > 0, "UART has output");

    // Perform platform reset
    platform.reset();

    // Verify CPU 0 restored to boot entry and registers cleared
    STAGE67_ASSERT(cpu0.get_program_counter() == 0x40001000ULL, "PC reset to boot entry");
    STAGE67_ASSERT(cpu0.read_register(0) == 0, "X0 cleared on reset");
    STAGE67_ASSERT(cpu0.read_register(1) == 0, "X1 cleared on reset");

    // Verify core 0 is active, core 1 is halted
    STAGE67_ASSERT(platform.is_core_active(0) == true, "Core 0 active after cold reset");
    STAGE67_ASSERT(platform.is_core_active(1) == false, "Core 1 halted after cold reset");

    // Verify UART reset
    STAGE67_ASSERT(platform.uart()->output_bytes_count() == 0, "UART output cleared on platform reset");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Multi-Core SMP Topology Configuration
// -----------------------------------------------------------------------------
bool test_smp_topology_configurations() {
    std::cout << "[6] test_smp_topology_configurations...\n";
    for (std::size_t cores : {1, 2, 4, 8}) {
        VirtualPlatformConfig cfg;
        cfg.core_count = cores;
        VirtualPlatform plat(cfg);
        STAGE67_ASSERT(plat.core_count() == cores, "Core count matches configuration");
        STAGE67_ASSERT(plat.is_core_active(0), "Primary core active");
        for (std::size_t c = 1; c < cores; ++c) {
            STAGE67_ASSERT(!plat.is_core_active(c), "Secondary core initially inactive");
            plat.set_core_active(c, true);
            STAGE67_ASSERT(plat.is_core_active(c), "Secondary core activated");
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Generic Timer PPI and GIC Integration
// -----------------------------------------------------------------------------
bool test_timer_ppi_and_gic_wiring() {
    std::cout << "[7] test_timer_ppi_and_gic_wiring...\n";
    VirtualPlatformConfig config;
    config.core_count = 2;
    VirtualPlatform platform(config);

    // Verify Redistributor PPIs configured for both cores
    for (std::size_t c = 0; c < 2; ++c) {
        const auto& r = platform.gic().get_redistributor(c);
        STAGE67_ASSERT((r.isenabler0 & (1u << 27)) != 0, "Virtual timer PPI 27 enabled");
        STAGE67_ASSERT((r.isenabler0 & (1u << 30)) != 0, "Physical timer PPI 30 enabled");
        STAGE67_ASSERT((r.igroupr0 & (1u << 27)) != 0, "Virtual timer PPI is Group 1");
        STAGE67_ASSERT((r.igroupr0 & (1u << 30)) != 0, "Physical timer PPI is Group 1");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Deterministic Stepping & Execution
// -----------------------------------------------------------------------------
bool test_deterministic_stepping() {
    std::cout << "[8] test_deterministic_stepping...\n";
    VirtualPlatformConfig config;
    config.core_count = 2;
    config.boot_entry = 0x0000;
    VirtualPlatform platform(config);

    // Place NOPs at boot entry in RAM
    platform.memory().write_u32(0, kNop);
    platform.memory().write_u32(4, kNop);
    platform.memory().write_u32(8, kNop);

    // Step primary core
    STAGE67_ASSERT(platform.step_core(0), "Step core 0");
    STAGE67_ASSERT(platform.get_cpu(0).get_program_counter() == 4, "PC advanced to 4");

    // Stepping inactive core 1 returns false
    STAGE67_ASSERT(!platform.step_core(1), "Step inactive core 1 returns false");

    // Activate core 1 and run deterministic round-robin
    platform.set_core_active(1, true);
    platform.get_cpu(1).set_program_counter(0);

    const std::size_t executed = platform.run_deterministic(4);
    STAGE67_ASSERT(executed == 4, "Executed 4 round-robin instructions across active cores");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Platform Canonical State Hashing
// -----------------------------------------------------------------------------
bool test_platform_canonical_state_hash() {
    std::cout << "[9] test_platform_canonical_state_hash...\n";
    VirtualPlatform platform1;
    VirtualPlatform platform2;

    const std::uint64_t hash1 = platform1.compute_platform_hash();
    const std::uint64_t hash2 = platform2.compute_platform_hash();

    STAGE67_ASSERT(hash1 != 0, "Platform hash is non-zero");
    STAGE67_ASSERT(hash1 == hash2, "Identical platform states produce identical hash");

    // Mutate platform 1 CPU register
    platform1.get_cpu(0).write_register(5, 0x1234ULL);
    const std::uint64_t hash1_mutated = platform1.compute_platform_hash();
    STAGE67_ASSERT(hash1 != hash1_mutated, "CPU mutation changes platform hash");

    // Mutate UART output
    platform2.uart()->write_register(VirtualUartDevice::kRegDR, 'X', 4);
    const std::uint64_t hash2_mutated = platform2.compute_platform_hash();
    STAGE67_ASSERT(hash2 != hash2_mutated, "UART output mutation changes platform hash");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Snapshot Serialization Round-Trip
// -----------------------------------------------------------------------------
bool test_snapshot_round_trip() {
    std::cout << "[10] test_snapshot_round_trip...\n";
    VirtualPlatform platform;

    // Write UART output
    platform.uart()->write_register(VirtualUartDevice::kRegDR, 'H', 4);
    platform.uart()->write_register(VirtualUartDevice::kRegDR, 'i', 4);
    STAGE67_ASSERT(platform.uart()->get_output_text() == "Hi", "UART holds 'Hi'");

    // Serialize UART state
    auto uart_state = platform.uart()->serialize_state();
    STAGE67_ASSERT(!uart_state.empty(), "UART serialized state is non-empty");

    // Clear UART
    platform.uart()->reset();
    STAGE67_ASSERT(platform.uart()->output_bytes_count() == 0, "UART reset to empty");

    // Restore UART state
    STAGE67_ASSERT(platform.uart()->deserialize_state(uart_state.data(), uart_state.size()),
                   "UART deserialization succeeded");
    STAGE67_ASSERT(platform.uart()->get_output_text() == "Hi", "UART output text restored");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 67 Virtual Platform Topology Tests ===" << std::endl;

    if (!test_platform_composition()) return 1;
    if (!test_memory_map_validation()) return 2;
    if (!test_uart_console_io()) return 3;
    if (!test_uart_interrupt_signaling()) return 4;
    if (!test_cold_reset_sequencing()) return 5;
    if (!test_smp_topology_configurations()) return 6;
    if (!test_timer_ppi_and_gic_wiring()) return 7;
    if (!test_deterministic_stepping()) return 8;
    if (!test_platform_canonical_state_hash()) return 9;
    if (!test_snapshot_round_trip()) return 10;

    std::cout << "stage67_platform_topology_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
