#include "xnu_zone_allocator.h"

#include <algorithm>
#include <cstring>

namespace winios::memory {

namespace {

inline std::uint64_t align_up(std::uint64_t v, std::uint64_t a) noexcept {
    if (a == 0) return v;
    return (v + a - 1ULL) & ~(a - 1ULL);
}

} // namespace

XnuZoneAllocator::XnuZoneAllocator(std::uint64_t zone_pool_base, std::uint64_t zone_pool_size)
    : zone_pool_base_(zone_pool_base),
      zone_pool_size_(zone_pool_size),
      next_zone_base_(zone_pool_base) {}

bool XnuZoneAllocator::create_zone(const std::string& name,
                                   std::size_t elem_size,
                                   std::size_t max_elements,
                                   std::string* out_error) {
    if (name.empty()) {
        if (out_error) *out_error = "create_zone: name cannot be empty";
        return false;
    }
    if (elem_size == 0) {
        if (out_error) *out_error = "create_zone: element size cannot be zero";
        return false;
    }
    if (max_elements == 0) {
        if (out_error) *out_error = "create_zone: max elements cannot be zero";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (zones_.find(name) != zones_.end()) {
        if (out_error) *out_error = "create_zone: zone with name '" + name + "' already exists";
        return false;
    }

    // Align element size to 16 bytes
    const std::size_t aligned_elem_size = static_cast<std::size_t>(align_up(elem_size, 16));
    if (aligned_elem_size > 0 && max_elements > (UINT64_MAX / static_cast<std::uint64_t>(aligned_elem_size))) {
        if (out_error) *out_error = "create_zone: element size * max_elements integer overflow";
        return false;
    }
    const std::uint64_t total_bytes = static_cast<std::uint64_t>(aligned_elem_size) * max_elements;

    if (next_zone_base_ + total_bytes > zone_pool_base_ + zone_pool_size_) {
        if (out_error) *out_error = "create_zone: insufficient zone pool capacity";
        return false;
    }

    ZoneInfo zi{};
    zi.name = name;
    zi.elem_size = aligned_elem_size;
    zi.max_elements = max_elements;
    zi.base_address = next_zone_base_;
    zi.total_allocated = 0;
    zi.total_alloc_ops = 0;
    zi.total_free_ops = 0;
    zi.allocation_bitmap.assign(max_elements, false);

    for (std::size_t i = 0; i < max_elements; ++i) {
        zi.free_list.push_back(zi.base_address + (i * aligned_elem_size));
    }

    next_zone_base_ = align_up(next_zone_base_ + total_bytes, 16384); // 16KB align next zone
    zones_[name] = std::move(zi);

    return true;
}

std::uint64_t XnuZoneAllocator::zalloc(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = zones_.find(name);
    if (it == zones_.end()) return 0ULL;

    ZoneInfo& zi = it->second;
    if (zi.free_list.empty()) return 0ULL;

    const std::uint64_t addr = zi.free_list.front();
    zi.free_list.pop_front();

    const std::size_t idx = static_cast<std::size_t>((addr - zi.base_address) / zi.elem_size);
    if (idx < zi.allocation_bitmap.size()) {
        zi.allocation_bitmap[idx] = true;
    }

    ++zi.total_allocated;
    ++zi.total_alloc_ops;
    return addr;
}

bool XnuZoneAllocator::zfree(const std::string& name, std::uint64_t ptr, std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = zones_.find(name);
    if (it == zones_.end()) {
        if (out_error) *out_error = "zfree: zone '" + name + "' not found";
        return false;
    }

    ZoneInfo& zi = it->second;
    if (ptr < zi.base_address || ptr >= zi.base_address + (zi.elem_size * zi.max_elements)) {
        if (out_error) *out_error = "zfree: pointer out of bounds for zone '" + name + "'";
        return false;
    }

    const std::uint64_t offset = ptr - zi.base_address;
    if ((offset % zi.elem_size) != 0) {
        if (out_error) *out_error = "zfree: misaligned pointer for zone '" + name + "'";
        return false;
    }

    const std::size_t idx = static_cast<std::size_t>(offset / zi.elem_size);
    if (!zi.allocation_bitmap[idx]) {
        if (out_error) *out_error = "zfree: double free detected in zone '" + name + "'";
        return false;
    }

    zi.allocation_bitmap[idx] = false;
    zi.free_list.push_back(ptr);
    if (zi.total_allocated > 0) {
        --zi.total_allocated;
    }
    ++zi.total_free_ops;
    return true;
}

bool XnuZoneAllocator::get_zone_stats(const std::string& name, ZoneStats& out_stats) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = zones_.find(name);
    if (it == zones_.end()) return false;

    const ZoneInfo& zi = it->second;
    out_stats.zone_name = zi.name;
    out_stats.elem_size = zi.elem_size;
    out_stats.count = zi.total_allocated;
    out_stats.max_count = zi.max_elements;
    out_stats.free_count = zi.free_list.size();
    out_stats.total_alloc_ops = zi.total_alloc_ops;
    out_stats.total_free_ops = zi.total_free_ops;
    return true;
}

std::vector<ZoneStats> XnuZoneAllocator::get_all_zone_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ZoneStats> list;
    for (const auto& [name, zi] : zones_) {
        ZoneStats s{};
        s.zone_name = zi.name;
        s.elem_size = zi.elem_size;
        s.count = zi.total_allocated;
        s.max_count = zi.max_elements;
        s.free_count = zi.free_list.size();
        s.total_alloc_ops = zi.total_alloc_ops;
        s.total_free_ops = zi.total_free_ops;
        list.push_back(s);
    }
    return list;
}

std::size_t XnuZoneAllocator::zone_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_.size();
}

std::vector<std::uint8_t> XnuZoneAllocator::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint8_t> buf;

    auto append_u64 = [&buf](std::uint64_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 8);
    };
    auto append_u32 = [&buf](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 4);
    };

    append_u64(kZoneMagic);
    append_u64(zone_pool_base_);
    append_u64(zone_pool_size_);
    append_u64(next_zone_base_);
    append_u32(static_cast<std::uint32_t>(zones_.size()));

    for (const auto& [name, zi] : zones_) {
        append_u32(static_cast<std::uint32_t>(name.size()));
        buf.insert(buf.end(), name.begin(), name.end());
        append_u64(static_cast<std::uint64_t>(zi.elem_size));
        append_u64(static_cast<std::uint64_t>(zi.max_elements));
        append_u64(zi.base_address);
        append_u64(static_cast<std::uint64_t>(zi.total_allocated));
        append_u64(static_cast<std::uint64_t>(zi.total_alloc_ops));
        append_u64(static_cast<std::uint64_t>(zi.total_free_ops));

        append_u32(static_cast<std::uint32_t>(zi.allocation_bitmap.size()));
        for (bool b : zi.allocation_bitmap) {
            buf.push_back(b ? 1 : 0);
        }
    }

    return buf;
}

bool XnuZoneAllocator::deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error) {
    if (!data || size < 36) {
        if (out_error) *out_error = "XnuZoneAllocator::deserialize: buffer too small";
        return false;
    }

    std::uint64_t magic = 0;
    std::memcpy(&magic, data, 8);
    if (magic != kZoneMagic) {
        if (out_error) *out_error = "XnuZoneAllocator::deserialize: invalid magic";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t off = 8;
    std::memcpy(&zone_pool_base_, data + off, 8); off += 8;
    std::memcpy(&zone_pool_size_, data + off, 8); off += 8;
    std::memcpy(&next_zone_base_, data + off, 8); off += 8;

    std::uint32_t num_zones = 0;
    std::memcpy(&num_zones, data + off, 4); off += 4;

    std::unordered_map<std::string, ZoneInfo> new_zones;
    for (std::uint32_t z = 0; z < num_zones; ++z) {
        if (off + 4 > size) {
            if (out_error) *out_error = "XnuZoneAllocator::deserialize: truncated zone name size";
            return false;
        }
        std::uint32_t name_len = 0;
        std::memcpy(&name_len, data + off, 4); off += 4;
        if (off + name_len + 48 > size) {
            if (out_error) *out_error = "XnuZoneAllocator::deserialize: truncated zone data";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(data + off), name_len);
        off += name_len;

        ZoneInfo zi{};
        zi.name = name;
        std::uint64_t elem_sz = 0, max_elem = 0, base_addr = 0, tot_alloc = 0, tot_alloc_ops = 0, tot_free_ops = 0;
        std::memcpy(&elem_sz, data + off, 8); off += 8;
        std::memcpy(&max_elem, data + off, 8); off += 8;
        std::memcpy(&base_addr, data + off, 8); off += 8;
        std::memcpy(&tot_alloc, data + off, 8); off += 8;
        std::memcpy(&tot_alloc_ops, data + off, 8); off += 8;
        std::memcpy(&tot_free_ops, data + off, 8); off += 8;

        zi.elem_size = static_cast<std::size_t>(elem_sz);
        zi.max_elements = static_cast<std::size_t>(max_elem);
        zi.base_address = base_addr;
        zi.total_allocated = static_cast<std::size_t>(tot_alloc);
        zi.total_alloc_ops = static_cast<std::size_t>(tot_alloc_ops);
        zi.total_free_ops = static_cast<std::size_t>(tot_free_ops);

        std::uint32_t bmp_size = 0;
        std::memcpy(&bmp_size, data + off, 4); off += 4;
        if (off + bmp_size > size) {
            if (out_error) *out_error = "XnuZoneAllocator::deserialize: truncated bitmap";
            return false;
        }
        zi.allocation_bitmap.resize(bmp_size);
        for (std::uint32_t i = 0; i < bmp_size; ++i) {
            const bool allocated = (data[off + i] != 0);
            zi.allocation_bitmap[i] = allocated;
            if (!allocated) {
                zi.free_list.push_back(zi.base_address + (i * zi.elem_size));
            }
        }
        off += bmp_size;

        new_zones[name] = std::move(zi);
    }

    zones_ = std::move(new_zones);
    return true;
}

} // namespace winios::memory
