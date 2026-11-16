#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

using namespace winios;
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

void test_early_uart_console() {
    std::cout << "[Test 1] Early UART Console Output and Buffering..." << std::endl;

    Memory memory(0x100000); // 1 MB RAM
    EarlyUartConsole uart(0x90000ULL);

    TEST_CHECK(uart.base_address() == 0x90000ULL, "UART base address mismatch");
    TEST_CHECK(uart.get_log_buffer().empty(), "Initial log buffer not empty");

    uart.write_char(memory, 'X');
    TEST_CHECK(uart.get_log_buffer() == "X", "UART single char log buffer mismatch");
    TEST_CHECK(memory.read_u8(0x90000) == 'X', "UART memory write mismatch");

    uart.write_string(memory, "NU_BOOT_205\n");
    TEST_CHECK(uart.get_log_buffer() == "XNU_BOOT_205\n", "UART string log buffer mismatch");

    // Clear log
    uart.clear_log();
    TEST_CHECK(uart.get_log_buffer().empty(), "Cleared log buffer should be empty");

    // Out of bounds write test (UART at high address beyond memory size)
    EarlyUartConsole oob_uart(0x200000ULL);
    oob_uart.write_char(memory, 'A');
    TEST_CHECK(oob_uart.get_log_buffer() == "A", "OOB UART should still buffer to log");
}

void test_early_page_allocator() {
    std::cout << "[Test 2] Early Physical Page Allocator (16KB Steal Memory)..." << std::endl;

    Memory memory(0x200000); // 2 MB RAM
    // Base 0x100000 (1 MB), Limit 0x180000 (1.5 MB) -> 512 KB = 32 x 16KB pages
    constexpr std::uint64_t kBase = 0x100000ULL;
    constexpr std::uint64_t kLimit = 0x180000ULL;

    EarlyPhysicalPageAllocator allocator(kBase, kLimit);

    TEST_CHECK(allocator.current_watermark() == kBase, "Initial watermark mismatch");
    TEST_CHECK(allocator.limit() == kLimit, "Limit mismatch");
    TEST_CHECK(allocator.allocated_bytes() == 0, "Initial allocated bytes mismatch");
    TEST_CHECK(allocator.remaining_bytes() == (kLimit - kBase), "Initial remaining bytes mismatch");

    // Write sentinel bytes into memory to verify zero-filling
    for (std::size_t i = 0x100000; i < 0x110000; ++i) {
        memory.write_u8(i, 0xAA);
    }

    // Allocate 2 pages (32 KB)
    const std::uint64_t pa1 = allocator.allocate_pages(memory, 2, EarlyPhysicalPageAllocator::kPageSize16K, true);
    TEST_CHECK(pa1 == kBase, "First allocation should be at base");
    TEST_CHECK((pa1 % 0x4000) == 0, "Allocation must be 16KB aligned");
    TEST_CHECK(allocator.current_watermark() == kBase + 0x8000ULL, "Watermark did not advance 32KB");
    TEST_CHECK(allocator.allocated_bytes() == 0x8000ULL, "Allocated bytes mismatch");

    // Check zero fill
    bool all_zero = true;
    for (std::size_t i = static_cast<std::size_t>(pa1); i < static_cast<std::size_t>(pa1 + 0x8000); ++i) {
        if (memory.read_u8(i) != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_CHECK(all_zero, "Allocated pages must be zero-filled");

    // Allocate 1 page with 64KB alignment
    const std::uint64_t pa2 = allocator.allocate_pages(memory, 1, 0x10000ULL, false);
    TEST_CHECK(pa2 == 0x110000ULL, "64KB aligned allocation mismatch");
    TEST_CHECK((pa2 % 0x10000ULL) == 0, "Alignment not respected");
    TEST_CHECK(allocator.current_watermark() == 0x114000ULL, "Watermark mismatch after aligned alloc");

    // Zero allocation count test
    const std::uint64_t pa_zero = allocator.allocate_pages(memory, 0);
    TEST_CHECK(pa_zero == 0, "Zero pages allocation should return 0");

    // Out of memory test (ask for more than remaining)
    const std::uint64_t pa_oom = allocator.allocate_pages(memory, 100);
    TEST_CHECK(pa_oom == 0, "OOM allocation must return 0");
    TEST_CHECK(allocator.current_watermark() == 0x114000ULL, "Watermark must not change on OOM");
}

void test_early_monotonic_timer() {
    std::cout << "[Test 3] Early Monotonic Timer Calculations..." << std::endl;

    constexpr std::uint64_t kFreq = 24000000ULL; // 24 MHz
    EarlyMonotonicTimer timer(kFreq);

    TEST_CHECK(timer.current_ticks() == 0, "Initial timer ticks must be 0");
    TEST_CHECK(timer.elapsed_us() == 0, "Initial elapsed us must be 0");

    // Advance 1000 microseconds (1 ms) -> 24000 ticks
    timer.advance_us(1000);
    TEST_CHECK(timer.current_ticks() == 24000ULL, "Ticks after 1ms mismatch");
    TEST_CHECK(timer.elapsed_us() == 1000ULL, "Elapsed us mismatch");

    // Advance 12000 ticks -> 500 us
    timer.advance_ticks(12000ULL);
    TEST_CHECK(timer.current_ticks() == 36000ULL, "Ticks after advance mismatch");
    TEST_CHECK(timer.elapsed_us() == 1500ULL, "Elapsed us mismatch");
}

void test_early_services_manager() {
    std::cout << "[Test 4] Early Kernel Services Manager Integration..." << std::endl;

    Memory memory(0x100000);
    EarlyKernelServicesManager manager(0x90000ULL, 0x40000ULL, 0x80000ULL, 24000000ULL);

    manager.uart().write_string(memory, "Booting XNU Stage 205\n");
    TEST_CHECK(manager.uart().get_log_buffer() == "Booting XNU Stage 205\n", "UART integration mismatch");

    const std::uint64_t page = manager.allocator().allocate_pages(memory, 1);
    TEST_CHECK(page == 0x40000ULL, "Manager allocator page base mismatch");

    manager.timer().advance_us(500);
    TEST_CHECK(manager.timer().elapsed_us() == 500ULL, "Manager timer advance mismatch");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.17 Early Kernel Runtime Services Tests ===" << std::endl;

    test_early_uart_console();
    test_early_page_allocator();
    test_early_monotonic_timer();
    test_early_services_manager();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.17 Early Kernel Services Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.17 Tests FAILED ===" << std::endl;
        return 1;
    }
}
