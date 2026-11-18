#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>

namespace winios::runtime {

struct AllocationRecord {
    uint64_t guest_address;
    size_t size;
    size_t requested_size;
    uint32_t process_id;
    uint32_t tag;
    bool is_active;
};

struct AllocatorStats {
    size_t total_heap_size;
    size_t quota_bytes;
    size_t bytes_allocated;
    size_t peak_bytes_allocated;
    size_t active_allocations_count;
    size_t total_allocations_count;
    size_t total_frees_count;
    size_t rejected_allocations_count;
};

class GuestAllocator {
public:
    GuestAllocator(uint64_t heap_base, size_t heap_size, size_t quota_bytes, uint32_t process_id);
    ~GuestAllocator();

    uint64_t Allocate(size_t size, size_t alignment = 16, uint32_t tag = 0);
    uint64_t AllocateZeroed(size_t size, size_t alignment = 16, uint32_t tag = 0);
    uint64_t Reallocate(uint64_t ptr, size_t new_size, size_t alignment = 16);
    bool     Free(uint64_t ptr);

    bool     IsValidPointer(uint64_t ptr) const;
    size_t   GetAllocationSize(uint64_t ptr) const;
    
    AllocatorStats GetStats() const;
    size_t   DetectLeaks(std::vector<AllocationRecord>* leaked_out = nullptr) const;
    void     Reset();

    uint64_t GetHeapBase() const { return heap_base_; }
    size_t   GetHeapSize() const { return heap_size_; }
    uint32_t GetProcessId() const { return process_id_; }

private:
    uint64_t heap_base_{0};
    size_t   heap_size_{0};
    size_t   quota_bytes_{0};
    uint32_t process_id_{0};

    mutable std::mutex mutex_;
    uint64_t current_bump_{0};
    size_t   bytes_allocated_{0};
    size_t   peak_bytes_{0};
    size_t   total_alloc_count_{0};
    size_t   total_free_count_{0};
    size_t   rejected_count_{0};

    std::unordered_map<uint64_t, AllocationRecord> active_allocations_;
};

} // namespace winios::runtime
