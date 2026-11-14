#include "memory/kernel_heap_allocator.h"

#include <algorithm>
#include <cstring>

namespace winios::memory {

KernelHeapAllocator::KernelHeapAllocator(
    Memory& mem,
    std::uint64_t heap_base,
    std::uint64_t heap_size)
    : mem_(mem),
      heap_base_(heap_base),
      heap_size_(heap_size) {}

HeapBlockHeader KernelHeapAllocator::read_header(std::uint64_t addr) const {
    HeapBlockHeader hdr;
    if (addr + sizeof(HeapBlockHeader) <= mem_.size()) {
        hdr.size_and_flags = mem_.read_u64(static_cast<std::size_t>(addr));
        hdr.magic = mem_.read_u64(static_cast<std::size_t>(addr + 8));
    }
    return hdr;
}

void KernelHeapAllocator::write_header(std::uint64_t addr, const HeapBlockHeader& hdr) {
    if (addr + sizeof(HeapBlockHeader) <= mem_.size()) {
        mem_.write_u64(static_cast<std::size_t>(addr), hdr.size_and_flags);
        mem_.write_u64(static_cast<std::size_t>(addr + 8), hdr.magic);
    }
}

bool KernelHeapAllocator::initialize() {
    if (heap_size_ < 64 || heap_base_ + heap_size_ > mem_.size()) {
        return false;
    }

    // Align base to 16 bytes
    if ((heap_base_ % kHeapAlignment) != 0 || (heap_size_ % kHeapAlignment) != 0) {
        return false;
    }

    // Initialize root single free block covering the entire heap space
    HeapBlockHeader root_hdr;
    root_hdr.size_and_flags = heap_size_ & ~1ULL; // Free
    root_hdr.magic = kHeapBlockMagic;
    write_header(heap_base_, root_hdr);

    active_allocations_ = 0;
    total_alloc_ops_ = 0;
    total_free_ops_ = 0;
    return true;
}

std::uint64_t KernelHeapAllocator::kmalloc(std::size_t bytes) {
    if (bytes == 0) {
        return 0;
    }

    // Align requested bytes to 16-byte boundary and add header size
    const std::size_t aligned_payload = (bytes + (kHeapAlignment - 1)) & ~(kHeapAlignment - 1);
    const std::size_t needed_block_size = aligned_payload + kHeapHeaderSize;

    std::uint64_t curr = heap_base_;
    const std::uint64_t end = heap_base_ + heap_size_;

    while (curr < end) {
        HeapBlockHeader hdr = read_header(curr);
        if (hdr.magic != kHeapBlockMagic || hdr.block_size() == 0) {
            return 0; // Heap corruption detected
        }

        if (!hdr.is_allocated() && hdr.block_size() >= needed_block_size) {
            // Check if we can split this free block
            if (hdr.block_size() >= needed_block_size + kHeapHeaderSize + kHeapAlignment) {
                const std::uint64_t rem_size = hdr.block_size() - needed_block_size;
                HeapBlockHeader rem_hdr;
                rem_hdr.size_and_flags = rem_size & ~1ULL; // Free
                rem_hdr.magic = kHeapBlockMagic;
                write_header(curr + needed_block_size, rem_hdr);

                hdr.set_size(needed_block_size);
            }

            hdr.set_allocated(true);
            write_header(curr, hdr);

            ++active_allocations_;
            ++total_alloc_ops_;
            return curr + kHeapHeaderSize;
        }

        curr += hdr.block_size();
    }

    return 0; // Out of memory
}

bool KernelHeapAllocator::kfree(std::uint64_t ptr) {
    if (ptr < heap_base_ + kHeapHeaderSize || ptr >= heap_base_ + heap_size_) {
        return false; // Out of bounds
    }

    const std::uint64_t block_addr = ptr - kHeapHeaderSize;
    HeapBlockHeader hdr = read_header(block_addr);
    if (hdr.magic != kHeapBlockMagic || !hdr.is_allocated()) {
        return false; // Corrupt header or double free
    }

    hdr.set_allocated(false);
    write_header(block_addr, hdr);

    // Coalesce forward if next contiguous block is also free
    const std::uint64_t end = heap_base_ + heap_size_;
    const std::uint64_t next_addr = block_addr + hdr.block_size();
    if (next_addr < end) {
        const HeapBlockHeader next_hdr = read_header(next_addr);
        if (next_hdr.magic == kHeapBlockMagic && !next_hdr.is_allocated()) {
            hdr.set_size(hdr.block_size() + next_hdr.block_size());
            write_header(block_addr, hdr);
        }
    }

    if (active_allocations_ > 0) {
        --active_allocations_;
    }
    ++total_free_ops_;
    return true;
}

std::uint64_t KernelHeapAllocator::krealloc(std::uint64_t ptr, std::size_t new_bytes) {
    if (ptr == 0) {
        return kmalloc(new_bytes);
    }
    if (new_bytes == 0) {
        kfree(ptr);
        return 0;
    }

    const std::uint64_t block_addr = ptr - kHeapHeaderSize;
    const HeapBlockHeader hdr = read_header(block_addr);
    if (hdr.magic != kHeapBlockMagic || !hdr.is_allocated()) {
        return 0;
    }

    const std::size_t current_payload = static_cast<std::size_t>(hdr.block_size() - kHeapHeaderSize);
    if (new_bytes <= current_payload) {
        return ptr; // Existing block is already large enough
    }

    const std::uint64_t new_ptr = kmalloc(new_bytes);
    if (new_ptr == 0) {
        return 0; // Allocation failure, original remains intact
    }

    // Copy original data to new block
    const std::size_t copy_len = std::min(current_payload, new_bytes);
    for (std::size_t i = 0; i < copy_len; ++i) {
        mem_.write_u8(static_cast<std::size_t>(new_ptr + i),
                      mem_.read_u8(static_cast<std::size_t>(ptr + i)));
    }

    kfree(ptr);
    return new_ptr;
}

std::uint64_t KernelHeapAllocator::kcalloc(std::size_t count, std::size_t size) {
    const std::size_t total = count * size;
    const std::uint64_t ptr = kmalloc(total);
    if (ptr != 0) {
        for (std::size_t i = 0; i < total; ++i) {
            mem_.write_u8(static_cast<std::size_t>(ptr + i), 0);
        }
    }
    return ptr;
}

KernelHeapStats KernelHeapAllocator::get_stats() const {
    KernelHeapStats stats;
    stats.total_heap_bytes = heap_size_;
    stats.active_allocations = active_allocations_;
    stats.total_alloc_operations = total_alloc_ops_;
    stats.total_free_operations = total_free_ops_;

    std::uint64_t curr = heap_base_;
    const std::uint64_t end = heap_base_ + heap_size_;

    while (curr < end) {
        HeapBlockHeader hdr = read_header(curr);
        if (hdr.magic != kHeapBlockMagic || hdr.block_size() == 0) {
            break;
        }

        if (hdr.is_allocated()) {
            stats.allocated_bytes += hdr.block_size();
        } else {
            stats.free_bytes += hdr.block_size();
            stats.largest_free_block = std::max(stats.largest_free_block, hdr.block_size());
        }

        curr += hdr.block_size();
    }

    return stats;
}

bool KernelHeapAllocator::validate_heap_integrity(std::string* out_error) const {
    std::uint64_t curr = heap_base_;
    const std::uint64_t end = heap_base_ + heap_size_;
    std::uint64_t total_scanned = 0;

    while (curr < end) {
        HeapBlockHeader hdr = read_header(curr);
        if (hdr.magic != kHeapBlockMagic) {
            if (out_error != nullptr) *out_error = "Corrupted block magic at 0x" + std::to_string(curr);
            return false;
        }
        if (hdr.block_size() < kHeapHeaderSize || (hdr.block_size() % kHeapAlignment) != 0) {
            if (out_error != nullptr) *out_error = "Invalid block size at 0x" + std::to_string(curr);
            return false;
        }

        total_scanned += hdr.block_size();
        curr += hdr.block_size();
    }

    if (total_scanned != heap_size_) {
        if (out_error != nullptr) *out_error = "Heap size mismatch during boundary traversal";
        return false;
    }

    return true;
}

std::vector<std::uint32_t> KernelHeapAllocator::emit_guest_kmalloc_routine() {
    // 13-instruction ARM64 guest allocator routine:
    // Input:  X0 = pointer to control slot (holds current_heap_ptr and end_heap_ptr)
    //         X1 = requested bytes
    // Output: X0 = allocated payload address (or 0 on OOM)
    return {
        0x91003C21u, // [00] add x1, x1, #15
        0xD28001E2u, // [04] mov x2, #15
        0x8A220021u, // [08] bic x1, x1, x2     (align requested bytes to 16)
        0xF9400003u, // [0c] ldr x3, [x0]        (load curr_ptr)
        0xF9400404u, // [10] ldr x4, [x0, #8]    (load end_ptr)
        0x8B010065u, // [14] add x5, x3, x1      (new_curr = curr_ptr + size)
        0xEB0400BFu, // [18] cmp x5, x4
        0x54000088u, // [1c] b.hi .Loom
        0xF9000005u, // [20] str x5, [x0]        (update curr_ptr in RAM)
        0xAA0303E0u, // [24] mov x0, x3          (return allocated pointer)
        0xD65F03C0u, // [28] ret
        0xD2800000u, // [2c] mov x0, #0
        0xD65F03C0u  // [30] ret
    };
}

} // namespace winios::memory
