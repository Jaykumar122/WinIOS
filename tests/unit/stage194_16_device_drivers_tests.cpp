#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "kernel/driver_manager.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_driver_registration_and_lifecycle() {
    std::cout << "Running test_driver_registration_and_lifecycle...\n";
    DriverManager dm;
    EXPECT_EQ(dm.driver_count(), 0u);

    auto uart = std::make_unique<SyntheticUartDriver>();
    EXPECT_TRUE(!uart->is_initialized());
    EXPECT_EQ(uart->name(), "SyntheticUartDriver");
    EXPECT_EQ(uart->mmio_base(), 0x09000000ULL);
    EXPECT_EQ(uart->mmio_size(), 0x1000ULL);
    EXPECT_EQ(uart->irq_number(), 33u);

    EXPECT_TRUE(dm.register_driver(std::move(uart)));
    EXPECT_EQ(dm.driver_count(), 1u);

    EXPECT_TRUE(dm.probe_and_initialize_all());

    DeviceDriver* found = dm.find_driver_by_name("SyntheticUartDriver");
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found->is_initialized());
}

void test_mmio_register_read_write() {
    std::cout << "Running test_mmio_register_read_write...\n";
    DriverManager dm;
    dm.register_driver(std::make_unique<SyntheticUartDriver>());
    dm.probe_and_initialize_all();

    const std::uint64_t uart_base = 0x09000000ULL;

    // Read Flag Register (offset 0x18)
    std::uint64_t fr_val = 0;
    EXPECT_TRUE(dm.read_mmio(uart_base + 0x18, 4, fr_val));
    // TXFE (bit 7) must be set
    EXPECT_TRUE((fr_val & device::VirtualUartDevice::kFR_TXFE) != 0);

    // Write 'A', 'B', 'C' to Data Register (offset 0x00)
    EXPECT_TRUE(dm.write_mmio(uart_base + 0x00, 'A', 4));
    EXPECT_TRUE(dm.write_mmio(uart_base + 0x00, 'B', 4));
    EXPECT_TRUE(dm.write_mmio(uart_base + 0x00, 'C', 4));

    auto* uart_drv = dynamic_cast<SyntheticUartDriver*>(dm.find_driver_by_address(uart_base));
    EXPECT_TRUE(uart_drv != nullptr);
    EXPECT_EQ(uart_drv->get_console_output(), "ABC");
}

void test_high_level_console_io() {
    std::cout << "Running test_high_level_console_io...\n";
    SyntheticUartDriver uart;
    uart.probe();
    uart.initialize();

    uart.write_string("Synthetic Kernel Boot\n");
    EXPECT_EQ(uart.get_console_output(), "Synthetic Kernel Boot\n");

    uart.clear_console_output();
    EXPECT_EQ(uart.get_console_output(), "");
}

void test_input_injection_and_reading() {
    std::cout << "Running test_input_injection_and_reading...\n";
    SyntheticUartDriver uart;
    uart.probe();
    uart.initialize();

    uart.inject_input("PING");

    // Read characters back via DR register (offset 0x00)
    std::uint64_t c1 = uart.read_mmio(0x00, 4);
    std::uint64_t c2 = uart.read_mmio(0x00, 4);
    std::uint64_t c3 = uart.read_mmio(0x00, 4);
    std::uint64_t c4 = uart.read_mmio(0x00, 4);

    EXPECT_EQ(c1, static_cast<std::uint64_t>('P'));
    EXPECT_EQ(c2, static_cast<std::uint64_t>('I'));
    EXPECT_EQ(c3, static_cast<std::uint64_t>('N'));
    EXPECT_EQ(c4, static_cast<std::uint64_t>('G'));
}

void test_driver_irq_dispatch() {
    std::cout << "Running test_driver_irq_dispatch...\n";
    DriverManager dm;
    dm.register_driver(std::make_unique<SyntheticUartDriver>());
    dm.probe_and_initialize_all();

    auto* uart = dynamic_cast<SyntheticUartDriver*>(dm.find_driver_by_irq(33));
    EXPECT_TRUE(uart != nullptr);
    EXPECT_EQ(uart->irq_handled_count(), 0u);

    EXPECT_TRUE(dm.dispatch_irq(33));
    EXPECT_EQ(uart->irq_handled_count(), 1u);

    EXPECT_TRUE(dm.dispatch_irq(33));
    EXPECT_EQ(uart->irq_handled_count(), 2u);

    // Unregistered IRQ
    EXPECT_TRUE(!dm.dispatch_irq(99));
}

int main() {
    std::cout << "=== WINIOS STAGE 194.16: KERNEL DEVICE-DRIVER INTERFACE & MMIO TESTS ===\n";

    test_driver_registration_and_lifecycle();
    test_mmio_register_read_write();
    test_high_level_console_io();
    test_input_injection_and_reading();
    test_driver_irq_dispatch();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
