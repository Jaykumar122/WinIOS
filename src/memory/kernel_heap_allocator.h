#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"

namespace winios::memory {

inline constexpr std::uint64_t kHeapBlockMagic = 0x4B48454150424C4BULL; // "KHEAPBLK"
inline constexpr std::size_t kHeapHeaderSize = 16;
inline constexpr std::size_t kHeapAlignment = 16;

#pragma pack(push, 1)
struct HeapBlockHeader {
    std::uint64_t size_and_flags{0}; // Bit 0: 1 = allocated, 0 = free; Bits [63:1]: block size including header
    std::uint64_t magic{kHeapBlockMagic};

    [[nodiscard]] std::uint64_t block_size() const noexcept {
        return size_and_flags & ~1ULL;
    }

    [[nodiscard]] bool is_allocated() const noexcept {
        return (size_and_flags & 1ULL) != 0;
    }

    void set_allocated(bool allocated) noexcept {
        if (allocated) {
            size_and_flags |= 1ULL;
        } else {
            size_and_flags &= ~1ULL;
        }
    }

    void set_size(std::uint64_t size) noexcept {
        const bool alloc = is_allocated();
        size_and_flags = (size & ~1ULL) | (alloc ? 1ULL : 0ULL);
    }
};
#pragma pack(pop)
static_assert(sizeof(HeapBlockHeader) == 16, "HeapBlockHeader must be 16 bytes");

struct KernelHeapStats {
    std::uint64_t total_heap_bytes{0};
    std::uint64_t allocated_bytes{0};
    std::uint64_t free_bytes{0};
    std::uint64_t active_allocations{0};
    std::uint64_t total_alloc_operations{0};
    std::uint64_t total_free_operations{0};
    std::uint64_t largest_free_block{0};
};

class KernelHeapAllocator {
public:
    KernelHeapAllocator(Memory& mem, std::uint64_t heap_base, std::uint64_t heap_size);
    ~KernelHeapAllocator() = default;

    // Initialize heap in guest RAM
    bool initialize();

    // Allocate bytes from kernel heap. Returns guest address of payload (or 0 on failure)
    [[nodiscard]] std::uint64_t kmalloc(std::size_t bytes);

    // Free previously allocated pointer
    bool kfree(std::uint64_t ptr);

    // Reallocate memory block
    [[nodiscard]] std::uint64_t krealloc(std::uint64_t ptr, std::size_t new_bytes);

    // Allocate and zero memory
    [[nodiscard]] std::uint64_t kcalloc(std::size_t count, std::size_t size);

    // Audit and stats
    [[nodiscard]] KernelHeapStats get_stats() const;
    [[nodiscard]] bool validate_heap_integrity(std::string* out_error = nullptr) const;

    [[nodiscard]] std::uint64_t heap_base() const noexcept { return heap_base_; }
    [[nodiscard]] std::uint64_t heap_size() const noexcept { return heap_size_; }

    // Generates a self-contained ARM64 EL1 bump/free-list allocator routine for guest execution
    // Input: X0 = heap_control_pa, X1 = size_bytes
    // Output: X0 = allocated_ptr (or 0)
    [[nodiscard]] static std::vector<std::uint32_t> emit_guest_kmalloc_routine();

private:
    Memory& mem_;
    std::uint64_t heap_base_;
    std::uint64_t heap_size_;
    std::uint64_t active_allocations_{0};
    std::uint64_t total_alloc_ops_{0};
    std::uint64_t total_free_ops_{0};

    [[nodiscard]] HeapBlockHeader read_header(std::uint64_t addr) const;
    void write_header(std::uint64_t addr, const HeapBlockHeader& hdr);
};

} // namespace winios::memory
