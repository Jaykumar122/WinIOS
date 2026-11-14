#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "memory/memory.h"

namespace winios::memory {

inline constexpr std::uint64_t kPhysicalPageSize16KB = 16384ULL;

struct PhysicalPageStats {
    std::uint64_t total_pages{0};
    std::uint64_t free_pages{0};
    std::uint64_t allocated_pages{0};
    std::uint64_t bitmap_address{0};
    std::uint64_t bitmap_size_bytes{0};
    std::uint64_t managed_base_address{0};
    std::uint64_t managed_size_bytes{0};
};

class PhysicalPageManager {
public:
    PhysicalPageManager(
        Memory& mem,
        std::uint64_t managed_base,
        std::uint64_t managed_size,
        std::uint64_t bitmap_guest_addr);
    ~PhysicalPageManager() = default;

    // Initialize bitmap in guest memory
    bool initialize(bool reserve_bitmap_pages = true);

    // Reserve a specific physical address range (e.g. kernel image, boot info)
    bool reserve_range(std::uint64_t start_addr, std::uint64_t size);

    // Allocates n contiguous 16KB pages. Returns physical base address, or 0 on failure.
    [[nodiscard]] std::uint64_t allocate_pages(std::size_t count);

    // Allocates a single 16KB page
    [[nodiscard]] std::uint64_t allocate_page() {
        return allocate_pages(1);
    }

    // Frees n contiguous 16KB pages starting at physical_addr
    bool free_pages(std::uint64_t physical_addr, std::size_t count);

    // Frees a single 16KB page
    bool free_page(std::uint64_t physical_addr) {
        return free_pages(physical_addr, 1);
    }

    // Checks if a physical page is currently allocated
    [[nodiscard]] bool is_page_allocated(std::uint64_t physical_addr) const;

    // Queries current allocation stats
    [[nodiscard]] PhysicalPageStats get_stats() const;

    [[nodiscard]] std::uint64_t managed_base() const noexcept { return managed_base_; }
    [[nodiscard]] std::uint64_t managed_size() const noexcept { return managed_size_; }
    [[nodiscard]] std::uint64_t bitmap_address() const noexcept { return bitmap_addr_; }
    [[nodiscard]] std::uint64_t total_pages() const noexcept { return total_pages_; }

    // Generates a self-contained ARM64 machine code routine for guest EL1 allocation
    // Input: X0 = bitmap_address, X1 = total_pages
    // Output: X0 = allocated page index (or -1 if full)
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_alloc_routine();

    // Generates a self-contained ARM64 machine code routine for guest EL1 deallocation
    // Input: X0 = bitmap_address, X1 = page_index
    // Output: X0 = 0 on success, -1 on invalid page
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_free_routine();

private:
    Memory& mem_;
    std::uint64_t managed_base_;
    std::uint64_t managed_size_;
    std::uint64_t bitmap_addr_;
    std::uint64_t total_pages_;
    std::uint64_t bitmap_size_bytes_;
    mutable std::recursive_mutex mutex_;

    [[nodiscard]] bool get_bit(std::size_t page_idx) const;
    void set_bit(std::size_t page_idx, bool val);
};

} // namespace winios::memory
