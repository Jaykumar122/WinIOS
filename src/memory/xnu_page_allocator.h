#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <optional>

namespace winios::memory {

struct PhysicalMemoryRegion {
    std::string name;
    std::uint64_t base{0};
    std::uint64_t size{0};
    bool is_reserved{false};
};

class XnuPhysicalPageAllocator {
public:
    static constexpr std::uint64_t kPageSize16K = 16384ULL;
    static constexpr std::uint64_t kPageMask16K = ~16383ULL;

    enum class PageState : std::uint8_t {
        Free = 0,
        Allocated = 1,
        Reserved = 2
    };

    XnuPhysicalPageAllocator() = default;
    ~XnuPhysicalPageAllocator() = default;

    // Initialize physical memory boundaries and reserved regions
    bool initialize(std::uint64_t ram_base, std::uint64_t ram_size, std::string* out_error = nullptr);

    // Reserve an address range (e.g., kernel binary, stack, stolen page tables, MMIO)
    bool reserve_range(std::uint64_t base, std::uint64_t size, const std::string& name, std::string* out_error = nullptr);

    // Allocate single or multiple contiguous 16KB physical pages
    std::optional<std::uint64_t> allocate_pages(std::size_t num_pages = 1);

    // Free previously allocated physical pages
    bool free_pages(std::uint64_t physical_address, std::size_t num_pages = 1, std::string* out_error = nullptr);

    // Query status
    std::size_t total_pages() const noexcept { return total_pages_; }
    std::size_t free_pages_count() const noexcept { return free_pages_count_; }
    std::size_t allocated_pages_count() const noexcept { return allocated_pages_count_; }
    std::size_t reserved_pages_count() const noexcept { return reserved_pages_count_; }

    PageState get_frame_state(std::size_t frame_index) const;
    bool is_frame_reserved(std::size_t frame_index) const;
    bool is_frame_free(std::size_t frame_index) const;
    bool is_frame_allocated(std::size_t frame_index) const;

    std::uint64_t ram_base() const noexcept { return ram_base_; }
    std::uint64_t ram_size() const noexcept { return ram_size_; }

    // Serialization for deterministic snapshot & replay
    std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error = nullptr);

    void reset() noexcept;

private:
    std::uint64_t ram_base_{0};
    std::uint64_t ram_size_{0};
    std::size_t total_pages_{0};
    std::size_t free_pages_count_{0};
    std::size_t allocated_pages_count_{0};
    std::size_t reserved_pages_count_{0};

    std::vector<PageState> page_states_;
    std::vector<PhysicalMemoryRegion> reserved_regions_;
    mutable std::mutex mutex_;
};

} // namespace winios::memory
