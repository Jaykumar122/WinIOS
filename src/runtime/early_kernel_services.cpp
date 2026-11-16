#include "early_kernel_services.h"

#include "memory/memory.h"

namespace winios::runtime {

namespace {

inline std::uint64_t align_up(std::uint64_t val, std::uint64_t alignment) noexcept {
    if (alignment == 0) return val;
    return (val + alignment - 1ULL) & ~(alignment - 1ULL);
}

} // namespace

// -----------------------------------------------------------------------------
// EarlyUartConsole Implementation
// -----------------------------------------------------------------------------
EarlyUartConsole::EarlyUartConsole(std::uint64_t base_addr) noexcept
    : base_addr_(base_addr) {}

void EarlyUartConsole::write_char(::Memory& memory, char c) {
    if (base_addr_ + kDataRegisterOffset < memory.size()) {
        memory.write_u8(static_cast<std::size_t>(base_addr_ + kDataRegisterOffset), static_cast<std::uint8_t>(c));
    }
    log_buffer_.push_back(c);
}

void EarlyUartConsole::write_string(::Memory& memory, const std::string& str) {
    for (char c : str) {
        write_char(memory, c);
    }
}

// -----------------------------------------------------------------------------
// EarlyPhysicalPageAllocator Implementation
// -----------------------------------------------------------------------------
EarlyPhysicalPageAllocator::EarlyPhysicalPageAllocator(
    std::uint64_t base_pa,
    std::uint64_t limit_pa) noexcept
    : base_pa_(base_pa), current_pa_(base_pa), limit_pa_(limit_pa) {}

void EarlyPhysicalPageAllocator::initialize(std::uint64_t base_pa, std::uint64_t limit_pa) noexcept {
    base_pa_ = base_pa;
    current_pa_ = base_pa;
    limit_pa_ = limit_pa;
}

std::uint64_t EarlyPhysicalPageAllocator::allocate_pages(
    ::Memory& memory,
    std::size_t num_pages,
    std::uint64_t alignment,
    bool zero_fill) {

    if (num_pages == 0) return 0;

    const std::uint64_t alloc_bytes = static_cast<std::uint64_t>(num_pages) * kPageSize16K;
    const std::uint64_t aligned_pa = align_up(current_pa_, alignment);

    if (aligned_pa + alloc_bytes > limit_pa_) {
        return 0; // Out of memory
    }

    if (zero_fill) {
        const std::size_t mem_size = memory.size();
        for (std::uint64_t offset = 0; offset < alloc_bytes; ++offset) {
            const std::uint64_t pa = aligned_pa + offset;
            if (pa < mem_size) {
                memory.write_u8(static_cast<std::size_t>(pa), 0);
            }
        }
    }

    current_pa_ = aligned_pa + alloc_bytes;
    return aligned_pa;
}

// -----------------------------------------------------------------------------
// EarlyMonotonicTimer Implementation
// -----------------------------------------------------------------------------
EarlyMonotonicTimer::EarlyMonotonicTimer(std::uint64_t freq_hz) noexcept
    : freq_hz_(freq_hz ? freq_hz : kDefaultFreqHz) {}

void EarlyMonotonicTimer::advance_us(std::uint64_t us) noexcept {
    const std::uint64_t ticks = (us * freq_hz_) / 1000000ULL;
    advance_ticks(ticks);
}

void EarlyMonotonicTimer::advance_ticks(std::uint64_t ticks) noexcept {
    current_ticks_ += ticks;
}

std::uint64_t EarlyMonotonicTimer::elapsed_us() const noexcept {
    return (current_ticks_ * 1000000ULL) / freq_hz_;
}

// -----------------------------------------------------------------------------
// EarlyKernelServicesManager Implementation
// -----------------------------------------------------------------------------
EarlyKernelServicesManager::EarlyKernelServicesManager(
    std::uint64_t uart_base,
    std::uint64_t alloc_base,
    std::uint64_t alloc_limit,
    std::uint64_t timer_freq) noexcept
    : uart_(uart_base),
      allocator_(alloc_base, alloc_limit),
      timer_(timer_freq) {}

} // namespace winios::runtime
