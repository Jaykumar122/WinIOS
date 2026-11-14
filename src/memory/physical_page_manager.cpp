#include "memory/physical_page_manager.h"

#include <algorithm>

namespace winios::memory {

PhysicalPageManager::PhysicalPageManager(
    Memory& mem,
    std::uint64_t managed_base,
    std::uint64_t managed_size,
    std::uint64_t bitmap_guest_addr)
    : mem_(mem),
      managed_base_(managed_base),
      managed_size_(managed_size),
      bitmap_addr_(bitmap_guest_addr) {
    total_pages_ = managed_size_ / kPhysicalPageSize16KB;
    bitmap_size_bytes_ = (total_pages_ + 7) / 8;
}

bool PhysicalPageManager::get_bit(std::size_t page_idx) const {
    if (page_idx >= total_pages_) return true; // Treat out-of-bounds as allocated
    const std::size_t byte_off = page_idx / 8;
    const std::uint8_t bit_off = static_cast<std::uint8_t>(page_idx % 8);
    const std::uint8_t b = mem_.read_u8(static_cast<std::size_t>(bitmap_addr_ + byte_off));
    return (b & (1u << bit_off)) != 0;
}

void PhysicalPageManager::set_bit(std::size_t page_idx, bool val) {
    if (page_idx >= total_pages_) return;
    const std::size_t byte_off = page_idx / 8;
    const std::uint8_t bit_off = static_cast<std::uint8_t>(page_idx % 8);
    std::uint8_t b = mem_.read_u8(static_cast<std::size_t>(bitmap_addr_ + byte_off));
    if (val) {
        b |= static_cast<std::uint8_t>(1u << bit_off);
    } else {
        b &= static_cast<std::uint8_t>(~(1u << bit_off));
    }
    mem_.write_u8(static_cast<std::size_t>(bitmap_addr_ + byte_off), b);
}

bool PhysicalPageManager::initialize(bool reserve_bitmap_pages) {
    if (bitmap_addr_ + bitmap_size_bytes_ > mem_.size()) {
        return false;
    }

    // Zero out all bitmap bytes in guest RAM
    for (std::size_t i = 0; i < bitmap_size_bytes_; ++i) {
        mem_.write_u8(static_cast<std::size_t>(bitmap_addr_ + i), 0);
    }

    // If bitmap resides in the managed memory range, reserve its pages
    if (reserve_bitmap_pages &&
        bitmap_addr_ >= managed_base_ &&
        bitmap_addr_ < (managed_base_ + managed_size_)) {
        return reserve_range(bitmap_addr_, bitmap_size_bytes_);
    }

    return true;
}

bool PhysicalPageManager::reserve_range(std::uint64_t start_addr, std::uint64_t size) {
    if (size == 0) return true;
    const std::uint64_t end_addr = start_addr + size;

    if (start_addr >= (managed_base_ + managed_size_) || end_addr <= managed_base_) {
        return true; // No intersection with managed range
    }

    const std::uint64_t clamped_start = std::max(start_addr, managed_base_);
    const std::uint64_t clamped_end = std::min(end_addr, managed_base_ + managed_size_);

    const std::size_t first_pfn = static_cast<std::size_t>((clamped_start - managed_base_) / kPhysicalPageSize16KB);
    const std::size_t last_pfn = static_cast<std::size_t>((clamped_end - managed_base_ + (kPhysicalPageSize16KB - 1)) / kPhysicalPageSize16KB);

    for (std::size_t pfn = first_pfn; pfn < last_pfn && pfn < total_pages_; ++pfn) {
        set_bit(pfn, true);
    }

    return true;
}

std::uint64_t PhysicalPageManager::allocate_pages(std::size_t count) {
    if (count == 0 || count > total_pages_) {
        return 0;
    }

    std::size_t contiguous_free = 0;
    std::size_t start_idx = 0;

    for (std::size_t i = 0; i < total_pages_; ++i) {
        if (!get_bit(i)) {
            if (contiguous_free == 0) {
                start_idx = i;
            }
            ++contiguous_free;
            if (contiguous_free == count) {
                // Found suitable block, mark as allocated
                for (std::size_t k = 0; k < count; ++k) {
                    set_bit(start_idx + k, true);
                }
                return managed_base_ + (start_idx * kPhysicalPageSize16KB);
            }
        } else {
            contiguous_free = 0;
        }
    }

    return 0; // Out of memory
}

bool PhysicalPageManager::free_pages(std::uint64_t physical_addr, std::size_t count) {
    if (count == 0 || physical_addr < managed_base_) {
        return false;
    }

    if ((physical_addr % kPhysicalPageSize16KB) != 0) {
        return false; // Misaligned physical address
    }

    const std::size_t start_pfn = static_cast<std::size_t>((physical_addr - managed_base_) / kPhysicalPageSize16KB);
    if (start_pfn + count > total_pages_) {
        return false; // Out of bounds
    }

    for (std::size_t i = 0; i < count; ++i) {
        set_bit(start_pfn + i, false);
    }

    return true;
}

bool PhysicalPageManager::is_page_allocated(std::uint64_t physical_addr) const {
    if (physical_addr < managed_base_ || (physical_addr % kPhysicalPageSize16KB) != 0) {
        return true;
    }
    const std::size_t pfn = static_cast<std::size_t>((physical_addr - managed_base_) / kPhysicalPageSize16KB);
    return get_bit(pfn);
}

PhysicalPageStats PhysicalPageManager::get_stats() const {
    PhysicalPageStats stats;
    stats.total_pages = total_pages_;
    stats.bitmap_address = bitmap_addr_;
    stats.bitmap_size_bytes = bitmap_size_bytes_;
    stats.managed_base_address = managed_base_;
    stats.managed_size_bytes = managed_size_;

    std::uint64_t alloc_count = 0;
    for (std::size_t i = 0; i < total_pages_; ++i) {
        if (get_bit(i)) {
            ++alloc_count;
        }
    }
    stats.allocated_pages = alloc_count;
    stats.free_pages = total_pages_ - alloc_count;
    return stats;
}

std::vector<std::uint32_t> PhysicalPageManager::emit_guest_alloc_routine() {
    // ARM64 routine: scans bitmap at [X0] for up to X1 pages, allocates 1 bit, returns page index in X0 (or -1)
    return {
        0xD2800002u, // [00] mov x2, #0
        0xD37DF043u, // [04] lsl x3, x2, #3
        0xEB01007Fu, // [08] cmp x3, x1
        0x5400028Au, // [0c] b.ge .Lalloc_fail
        0x38626804u, // [10] ldrb w4, [x0, x2]
        0x7103FC9Fu, // [14] cmp w4, #0xFF
        0x54000061u, // [18] b.ne .Lbit_search
        0x91000442u, // [1c] add x2, x2, #1
        0x17FFFFF9u, // [20] b .Lbyte_loop
        0xD2800005u, // [24] mov x5, #0
        0x52800026u, // [28] mov w6, #1
        0x1AC520C6u, // [2c] lsl w6, w6, w5
        0x6A06009Fu, // [30] tst w4, w6
        0x540000A0u, // [34] b.eq .Lbit_found
        0x910004A5u, // [38] add x5, x5, #1
        0xF10020BFu, // [3c] cmp x5, #8
        0x54FFFF4Bu, // [40] b.lt .Lbit_loop
        0x14000006u, // [44] b .Lalloc_fail
        0x2A060084u, // [48] orr w4, w4, w6
        0x38226804u, // [4c] strb w4, [x0, x2]
        0xD37DF042u, // [50] lsl x2, x2, #3
        0x8B050040u, // [54] add x0, x2, x5
        0xD65F03C0u, // [58] ret
        0x92800000u, // [5c] mov x0, #-1
        0xD65F03C0u  // [60] ret
    };
}

std::vector<std::uint32_t> PhysicalPageManager::emit_guest_free_routine() {
    // ARM64 routine: clears bit for page X1 in bitmap at [X0], returns 0 in X0
    return {
        0xD343FC22u, // [00] lsr x2, x1, #3
        0x92400823u, // [04] and x3, x1, #7
        0x38626804u, // [08] ldrb w4, [x0, x2]
        0x52800025u, // [0c] mov w5, #1
        0x1AC320A5u, // [10] lsl w5, w5, w3
        0x0A250084u, // [14] bic w4, w4, w5
        0x38226804u, // [18] strb w4, [x0, x2]
        0xD2800000u, // [1c] mov x0, #0
        0xD65F03C0u  // [20] ret
    };
}

} // namespace winios::memory
