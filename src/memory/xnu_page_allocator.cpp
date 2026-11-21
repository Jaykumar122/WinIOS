#include "memory/xnu_page_allocator.h"
#include <algorithm>
#include <cstring>

namespace winios::memory {

void XnuPhysicalPageAllocator::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ram_base_ = 0;
    ram_size_ = 0;
    total_pages_ = 0;
    free_pages_count_ = 0;
    allocated_pages_count_ = 0;
    reserved_pages_count_ = 0;
    page_states_.clear();
    reserved_regions_.clear();
}

bool XnuPhysicalPageAllocator::initialize(std::uint64_t ram_base, std::uint64_t ram_size, std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ram_size == 0) {
        if (out_error) *out_error = "RAM size cannot be zero";
        return false;
    }

    if ((ram_base & ~kPageMask16K) != 0) {
        if (out_error) *out_error = "RAM base must be 16KB aligned";
        return false;
    }

    if ((ram_size & ~kPageMask16K) != 0) {
        if (out_error) *out_error = "RAM size must be 16KB aligned";
        return false;
    }

    // Check for overflow
    if (ram_base + ram_size < ram_base) {
        if (out_error) *out_error = "RAM range overflows 64-bit address space";
        return false;
    }

    ram_base_ = ram_base;
    ram_size_ = ram_size;
    total_pages_ = static_cast<std::size_t>(ram_size / kPageSize16K);
    free_pages_count_ = total_pages_;
    allocated_pages_count_ = 0;
    reserved_pages_count_ = 0;

    page_states_.assign(total_pages_, PageState::Free);
    reserved_regions_.clear();

    return true;
}

bool XnuPhysicalPageAllocator::reserve_range(
    std::uint64_t base,
    std::uint64_t size,
    const std::string& name,
    std::string* out_error) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (size == 0) return true;

    if (base + size < base) {
        if (out_error) *out_error = "Reserved range overflows 64-bit integer";
        return false;
    }

    // Clamp or check if within RAM
    if (base < ram_base_ || (base + size) > (ram_base_ + ram_size_)) {
        // Outside managed RAM - record region for accounting, but don't flag pages
        PhysicalMemoryRegion r;
        r.name = name;
        r.base = base;
        r.size = size;
        r.is_reserved = true;
        reserved_regions_.push_back(r);
        return true;
    }

    const std::uint64_t aligned_start = base & kPageMask16K;
    const std::uint64_t aligned_end = (base + size + kPageSize16K - 1ULL) & kPageMask16K;

    const std::size_t start_idx = static_cast<std::size_t>((aligned_start - ram_base_) / kPageSize16K);
    const std::size_t end_idx = static_cast<std::size_t>((aligned_end - ram_base_) / kPageSize16K);

    for (std::size_t i = start_idx; i < end_idx && i < total_pages_; ++i) {
        if (page_states_[i] == PageState::Free) {
            page_states_[i] = PageState::Reserved;
            --free_pages_count_;
            ++reserved_pages_count_;
        } else if (page_states_[i] == PageState::Allocated) {
            page_states_[i] = PageState::Reserved;
            --allocated_pages_count_;
            ++reserved_pages_count_;
        }
    }

    PhysicalMemoryRegion r;
    r.name = name;
    r.base = base;
    r.size = size;
    r.is_reserved = true;
    reserved_regions_.push_back(r);

    return true;
}

std::optional<std::uint64_t> XnuPhysicalPageAllocator::allocate_pages(std::size_t num_pages) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (num_pages == 0 || num_pages > free_pages_count_) {
        return std::nullopt;
    }

    // First-fit contiguous allocation
    std::size_t contiguous = 0;
    std::size_t match_start = 0;

    for (std::size_t i = 0; i < total_pages_; ++i) {
        if (page_states_[i] == PageState::Free) {
            if (contiguous == 0) match_start = i;
            ++contiguous;
            if (contiguous == num_pages) {
                // Found suitable contiguous block
                for (std::size_t j = match_start; j < match_start + num_pages; ++j) {
                    page_states_[j] = PageState::Allocated;
                }
                free_pages_count_ -= num_pages;
                allocated_pages_count_ += num_pages;
                return ram_base_ + (match_start * kPageSize16K);
            }
        } else {
            contiguous = 0;
        }
    }

    return std::nullopt;
}

bool XnuPhysicalPageAllocator::free_pages(std::uint64_t physical_address, std::size_t num_pages, std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (num_pages == 0) return true;

    if ((physical_address & ~kPageMask16K) != 0) {
        if (out_error) *out_error = "Free address is not 16KB aligned";
        return false;
    }

    if (physical_address < ram_base_ || (physical_address + (num_pages * kPageSize16K)) > (ram_base_ + ram_size_)) {
        if (out_error) *out_error = "Free address is outside managed physical RAM";
        return false;
    }

    const std::size_t start_idx = static_cast<std::size_t>((physical_address - ram_base_) / kPageSize16K);

    // Double-free / reserved detection
    for (std::size_t i = start_idx; i < start_idx + num_pages; ++i) {
        if (page_states_[i] == PageState::Free) {
            if (out_error) *out_error = "Double free detected at page index " + std::to_string(i);
            return false;
        }
        if (page_states_[i] == PageState::Reserved) {
            if (out_error) *out_error = "Attempted to free reserved hardware/kernel page at index " + std::to_string(i);
            return false;
        }
    }

    for (std::size_t i = start_idx; i < start_idx + num_pages; ++i) {
        page_states_[i] = PageState::Free;
    }

    allocated_pages_count_ -= num_pages;
    free_pages_count_ += num_pages;

    return true;
}

std::vector<std::uint8_t> XnuPhysicalPageAllocator::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::uint8_t> out;
    // Magic header "XNUPG208"
    const char magic[8] = {'X', 'N', 'U', 'P', 'G', '2', '0', '8'};
    out.insert(out.end(), magic, magic + 8);

    auto append_u64 = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };

    append_u64(ram_base_);
    append_u64(ram_size_);
    append_u64(total_pages_);
    append_u64(free_pages_count_);
    append_u64(allocated_pages_count_);
    append_u64(reserved_pages_count_);

    // Page states
    for (auto s : page_states_) {
        out.push_back(static_cast<std::uint8_t>(s));
    }

    return out;
}

bool XnuPhysicalPageAllocator::deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!data || size < 56) {
        if (out_error) *out_error = "Data too small for allocator snapshot header";
        return false;
    }

    if (std::memcmp(data, "XNUPG208", 8) != 0) {
        if (out_error) *out_error = "Invalid allocator snapshot magic";
        return false;
    }

    auto read_u64 = [&](std::size_t offset) -> std::uint64_t {
        std::uint64_t val = 0;
        for (int i = 0; i < 8; ++i) {
            val |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
        }
        return val;
    };

    ram_base_ = read_u64(8);
    ram_size_ = read_u64(16);
    total_pages_ = static_cast<std::size_t>(read_u64(24));
    free_pages_count_ = static_cast<std::size_t>(read_u64(32));
    allocated_pages_count_ = static_cast<std::size_t>(read_u64(40));
    reserved_pages_count_ = static_cast<std::size_t>(read_u64(48));

    if (size < 56 + total_pages_) {
        if (out_error) *out_error = "Snapshot truncated before reading all page states";
        return false;
    }

    page_states_.resize(total_pages_);
    for (std::size_t i = 0; i < total_pages_; ++i) {
        page_states_[i] = static_cast<PageState>(data[56 + i]);
    }

    return true;
}

XnuPhysicalPageAllocator::PageState XnuPhysicalPageAllocator::get_frame_state(std::size_t frame_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frame_index >= total_pages_) {
        return PageState::Reserved;
    }
    return page_states_[frame_index];
}

bool XnuPhysicalPageAllocator::is_frame_reserved(std::size_t frame_index) const {
    return get_frame_state(frame_index) == PageState::Reserved;
}

bool XnuPhysicalPageAllocator::is_frame_free(std::size_t frame_index) const {
    return get_frame_state(frame_index) == PageState::Free;
}

bool XnuPhysicalPageAllocator::is_frame_allocated(std::size_t frame_index) const {
    return get_frame_state(frame_index) == PageState::Allocated;
}

} // namespace winios::memory
