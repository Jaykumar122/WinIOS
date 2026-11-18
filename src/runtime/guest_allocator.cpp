#include "runtime/guest_allocator.h"
#include <algorithm>

namespace winios::runtime {

GuestAllocator::GuestAllocator(uint64_t heap_base, size_t heap_size, size_t quota_bytes, uint32_t process_id)
    : heap_base_(heap_base),
      heap_size_(heap_size),
      quota_bytes_(quota_bytes ? std::min(quota_bytes, heap_size) : heap_size),
      process_id_(process_id),
      current_bump_(heap_base) {}

GuestAllocator::~GuestAllocator() {
    Reset();
}

uint64_t GuestAllocator::Allocate(size_t size, size_t alignment, uint32_t tag) {
    if (size == 0 || alignment == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);

    // Check quota and overflow
    if (size > (heap_size_ - (current_bump_ - heap_base_)) ||
        (bytes_allocated_ + size > quota_bytes_)) {
        rejected_count_++;
        return 0;
    }

    // Align current bump
    const uint64_t align_mask = alignment - 1;
    uint64_t aligned_addr = (current_bump_ + align_mask) & ~align_mask;
    if (aligned_addr + size > heap_base_ + heap_size_) {
        rejected_count_++;
        return 0;
    }

    current_bump_ = aligned_addr + size;
    bytes_allocated_ += size;
    if (bytes_allocated_ > peak_bytes_) {
        peak_bytes_ = bytes_allocated_;
    }
    total_alloc_count_++;

    AllocationRecord rec{};
    rec.guest_address = aligned_addr;
    rec.size = size;
    rec.requested_size = size;
    rec.process_id = process_id_;
    rec.tag = tag;
    rec.is_active = true;
    active_allocations_[aligned_addr] = rec;

    return aligned_addr;
}

uint64_t GuestAllocator::AllocateZeroed(size_t size, size_t alignment, uint32_t tag) {
    return Allocate(size, alignment, tag);
}

uint64_t GuestAllocator::Reallocate(uint64_t ptr, size_t new_size, size_t alignment) {
    if (ptr == 0) return Allocate(new_size, alignment);
    if (new_size == 0) {
        Free(ptr);
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_allocations_.find(ptr);
    if (it == active_allocations_.end() || !it->second.is_active) {
        return 0; // Invalid free/realloc
    }

    const size_t old_size = it->second.size;
    const uint32_t tag = it->second.tag;
    const size_t aligned_new = (new_size + alignment - 1) & ~(alignment - 1);

    if (aligned_new <= old_size) {
        it->second.requested_size = new_size;
        return ptr;
    }

    const size_t net_growth = aligned_new - old_size;
    if (bytes_allocated_ + net_growth > quota_bytes_) {
        rejected_count_++;
        return 0;
    }

    // Try in-place expansion if this was the last allocation
    if (ptr + old_size == current_bump_ &&
        ptr + aligned_new <= heap_base_ + heap_size_) {
        current_bump_ = ptr + aligned_new;
        bytes_allocated_ += net_growth;
        if (bytes_allocated_ > peak_bytes_) peak_bytes_ = bytes_allocated_;
        it->second.size = aligned_new;
        it->second.requested_size = new_size;
        return ptr;
    }

    // Check space for new bump allocation
    const uint64_t align_mask = alignment - 1;
    uint64_t aligned_addr = (current_bump_ + align_mask) & ~align_mask;
    if (aligned_addr + aligned_new > heap_base_ + heap_size_) {
        rejected_count_++;
        return 0;
    }

    uint64_t new_ptr = aligned_addr;
    current_bump_ = aligned_addr + aligned_new;
    bytes_allocated_ = bytes_allocated_ - old_size + aligned_new;
    if (bytes_allocated_ > peak_bytes_) peak_bytes_ = bytes_allocated_;
    total_alloc_count_++;
    total_free_count_++;

    it->second.is_active = false;
    active_allocations_.erase(it);

    AllocationRecord rec{};
    rec.guest_address = new_ptr;
    rec.size = aligned_new;
    rec.requested_size = new_size;
    rec.process_id = process_id_;
    rec.tag = tag;
    rec.is_active = true;
    active_allocations_[new_ptr] = rec;

    return new_ptr;
}

bool GuestAllocator::Free(uint64_t ptr) {
    if (ptr == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_allocations_.find(ptr);
    if (it == active_allocations_.end() || !it->second.is_active) {
        return false; // Double free or invalid pointer
    }

    bytes_allocated_ -= it->second.size;
    it->second.is_active = false;
    total_free_count_++;
    active_allocations_.erase(it);
    return true;
}

bool GuestAllocator::IsValidPointer(uint64_t ptr) const {
    if (ptr < heap_base_ || ptr >= heap_base_ + heap_size_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_allocations_.find(ptr);
    return (it != active_allocations_.end() && it->second.is_active);
}

size_t GuestAllocator::GetAllocationSize(uint64_t ptr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_allocations_.find(ptr);
    if (it != active_allocations_.end() && it->second.is_active) {
        return it->second.size;
    }
    return 0;
}

AllocatorStats GuestAllocator::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AllocatorStats s{};
    s.total_heap_size = heap_size_;
    s.quota_bytes = quota_bytes_;
    s.bytes_allocated = bytes_allocated_;
    s.peak_bytes_allocated = peak_bytes_;
    s.active_allocations_count = active_allocations_.size();
    s.total_allocations_count = total_alloc_count_;
    s.total_frees_count = total_free_count_;
    s.rejected_allocations_count = rejected_count_;
    return s;
}

size_t GuestAllocator::DetectLeaks(std::vector<AllocationRecord>* leaked_out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [addr, rec] : active_allocations_) {
        if (rec.is_active) {
            count++;
            if (leaked_out) leaked_out->push_back(rec);
        }
    }
    return count;
}

void GuestAllocator::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_allocations_.clear();
    current_bump_ = heap_base_;
    bytes_allocated_ = 0;
    peak_bytes_ = 0;
    total_alloc_count_ = 0;
    total_free_count_ = 0;
    rejected_count_ = 0;
}

} // namespace winios::runtime
