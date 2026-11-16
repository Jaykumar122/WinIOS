#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Memory;

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Early UART Console
// Emulates physical UART (PL011/Apple UART) at base address (default 0x90000)
// -----------------------------------------------------------------------------
class EarlyUartConsole {
public:
    static constexpr std::uint64_t kDefaultUartBase = 0x00090000ULL;
    static constexpr std::uint64_t kDataRegisterOffset = 0x00ULL;

    explicit EarlyUartConsole(std::uint64_t base_addr = kDefaultUartBase) noexcept;

    void write_char(::Memory& memory, char c);
    void write_string(::Memory& memory, const std::string& str);

    std::uint64_t base_address() const noexcept { return base_addr_; }
    const std::string& get_log_buffer() const noexcept { return log_buffer_; }
    void clear_log() noexcept { log_buffer_.clear(); }

private:
    std::uint64_t base_addr_;
    std::string   log_buffer_;
};

// -----------------------------------------------------------------------------
// Early Physical Page Allocator (Boot Memory Stealer / pmap_steal_memory)
// Allocates 16KB-aligned contiguous physical pages before full VM init
// -----------------------------------------------------------------------------
class EarlyPhysicalPageAllocator {
public:
    static constexpr std::uint64_t kPageSize16K = 0x4000ULL; // 16 KB

    EarlyPhysicalPageAllocator() noexcept = default;
    EarlyPhysicalPageAllocator(std::uint64_t base_pa, std::uint64_t limit_pa) noexcept;

    void initialize(std::uint64_t base_pa, std::uint64_t limit_pa) noexcept;

    std::uint64_t allocate_pages(
        ::Memory& memory,
        std::size_t num_pages,
        std::uint64_t alignment = kPageSize16K,
        bool zero_fill = true);

    std::uint64_t current_watermark() const noexcept { return current_pa_; }
    std::uint64_t limit() const noexcept { return limit_pa_; }
    std::uint64_t allocated_bytes() const noexcept { return current_pa_ - base_pa_; }
    std::uint64_t remaining_bytes() const noexcept {
        return (current_pa_ <= limit_pa_) ? (limit_pa_ - current_pa_) : 0ULL;
    }

private:
    std::uint64_t base_pa_{0};
    std::uint64_t current_pa_{0};
    std::uint64_t limit_pa_{0};
};

// -----------------------------------------------------------------------------
// Early Monotonic Timer Service
// -----------------------------------------------------------------------------
class EarlyMonotonicTimer {
public:
    static constexpr std::uint64_t kDefaultFreqHz = 24000000ULL; // 24 MHz

    explicit EarlyMonotonicTimer(std::uint64_t freq_hz = kDefaultFreqHz) noexcept;

    void advance_us(std::uint64_t us) noexcept;
    void advance_ticks(std::uint64_t ticks) noexcept;

    std::uint64_t current_ticks() const noexcept { return current_ticks_; }
    std::uint64_t elapsed_us() const noexcept;

private:
    std::uint64_t freq_hz_{kDefaultFreqHz};
    std::uint64_t current_ticks_{0};
};

// -----------------------------------------------------------------------------
// Early Kernel Runtime Services Manager
// -----------------------------------------------------------------------------
class EarlyKernelServicesManager {
public:
    EarlyKernelServicesManager(
        std::uint64_t uart_base = EarlyUartConsole::kDefaultUartBase,
        std::uint64_t alloc_base = 0,
        std::uint64_t alloc_limit = 0,
        std::uint64_t timer_freq = EarlyMonotonicTimer::kDefaultFreqHz) noexcept;

    EarlyUartConsole& uart() noexcept { return uart_; }
    const EarlyUartConsole& uart() const noexcept { return uart_; }

    EarlyPhysicalPageAllocator& allocator() noexcept { return allocator_; }
    const EarlyPhysicalPageAllocator& allocator() const noexcept { return allocator_; }

    EarlyMonotonicTimer& timer() noexcept { return timer_; }
    const EarlyMonotonicTimer& timer() const noexcept { return timer_; }

private:
    EarlyUartConsole           uart_;
    EarlyPhysicalPageAllocator allocator_;
    EarlyMonotonicTimer        timer_;
};

} // namespace winios::runtime
