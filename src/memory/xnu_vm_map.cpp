#include "xnu_vm_map.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::memory {

namespace {

inline std::uint64_t align_up(std::uint64_t v, std::uint64_t a) noexcept {
    if (a == 0) return v;
    return (v + a - 1ULL) & ~(a - 1ULL);
}

} // namespace

XnuVmMap::XnuVmMap(std::uint64_t min_offset, std::uint64_t max_offset, std::string name)
    : min_offset_(min_offset), max_offset_(max_offset), name_(std::move(name)) {}

void XnuVmMap::sort_entries_locked() {
    std::sort(entries_.begin(), entries_.end(), [](const VmMapEntry& a, const VmMapEntry& b) {
        return a.start < b.start;
    });
}

bool XnuVmMap::vm_map_enter(std::uint64_t start,
                            std::uint64_t size,
                            std::uint32_t prot,
                            std::uint32_t max_prot,
                            const std::string& tag,
                            std::string* out_error) {
    if (size == 0) {
        if (out_error) *out_error = "vm_map_enter: size cannot be zero";
        return false;
    }
    const std::uint64_t end = start + size;
    if (end <= start) {
        if (out_error) *out_error = "vm_map_enter: integer overflow in address range";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (start < min_offset_ || end > max_offset_) {
        if (out_error) *out_error = "vm_map_enter: requested range out of map bounds";
        return false;
    }

    // Check for overlap
    for (const auto& entry : entries_) {
        if (entry.overlaps(start, end)) {
            if (out_error) *out_error = "vm_map_enter: requested range overlaps existing entry";
            return false;
        }
    }

    VmMapEntry new_entry{};
    new_entry.start = start;
    new_entry.end = end;
    new_entry.protection = prot;
    new_entry.max_protection = max_prot;
    new_entry.inheritance = 0;
    new_entry.wired_count = 0;
    new_entry.tag = tag;
    new_entry.is_submap = false;

    entries_.push_back(new_entry);
    sort_entries_locked();
    return true;
}

bool XnuVmMap::vm_map_remove(std::uint64_t start,
                             std::uint64_t size,
                             std::string* out_error) {
    if (size == 0) {
        if (out_error) *out_error = "vm_map_remove: size cannot be zero";
        return false;
    }
    const std::uint64_t end = start + size;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::remove_if(entries_.begin(), entries_.end(), [start, end](const VmMapEntry& e) {
        return e.start >= start && e.end <= end;
    });

    if (it == entries_.end()) {
        if (out_error) *out_error = "vm_map_remove: no exact matching entries found in range";
        return false;
    }

    entries_.erase(it, entries_.end());
    return true;
}

bool XnuVmMap::vm_map_protect(std::uint64_t start,
                              std::uint64_t size,
                              std::uint32_t new_prot,
                              bool set_max,
                              std::string* out_error) {
    if (size == 0) {
        if (out_error) *out_error = "vm_map_protect: size cannot be zero";
        return false;
    }
    const std::uint64_t end = start + size;

    std::lock_guard<std::mutex> lock(mutex_);

    bool found = false;
    for (auto& entry : entries_) {
        if (entry.overlaps(start, end)) {
            if (!set_max && (new_prot & ~entry.max_protection) != 0) {
                if (out_error) *out_error = "vm_map_protect: new protection exceeds maximum allowed protection";
                return false;
            }
            if (set_max) {
                entry.max_protection = new_prot;
                if ((entry.protection & ~new_prot) != 0) {
                    entry.protection = new_prot;
                }
            } else {
                entry.protection = new_prot;
            }
            found = true;
        }
    }

    if (!found) {
        if (out_error) *out_error = "vm_map_protect: no entry matched specified range";
        return false;
    }

    return true;
}

const VmMapEntry* XnuVmMap::vm_map_lookup(std::uint64_t va) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : entries_) {
        if (entry.contains(va)) {
            return &entry;
        }
    }
    return nullptr;
}

bool XnuVmMap::vm_map_find_space(std::uint64_t size,
                                 std::uint64_t alignment,
                                 std::uint64_t& out_start,
                                 std::string* out_error) const {
    if (size == 0) {
        if (out_error) *out_error = "vm_map_find_space: size cannot be zero";
        return false;
    }
    if (alignment == 0) alignment = 1;

    std::lock_guard<std::mutex> lock(mutex_);

    std::uint64_t candidate = align_up(min_offset_, alignment);

    for (const auto& entry : entries_) {
        if (candidate + size <= entry.start) {
            // Gap before this entry is large enough
            out_start = candidate;
            return true;
        }
        // Advance candidate past this entry
        candidate = align_up(std::max(candidate, entry.end), alignment);
    }

    if (candidate + size <= max_offset_) {
        out_start = candidate;
        return true;
    }

    if (out_error) *out_error = "vm_map_find_space: insufficient address space available";
    return false;
}

bool XnuVmMap::vm_map_wire(std::uint64_t start, std::uint64_t size, std::string* out_error) {
    if (size == 0) return true;
    const std::uint64_t end = start + size;

    std::lock_guard<std::mutex> lock(mutex_);
    bool modified = false;
    for (auto& entry : entries_) {
        if (entry.overlaps(start, end)) {
            ++entry.wired_count;
            modified = true;
        }
    }

    if (!modified) {
        if (out_error) *out_error = "vm_map_wire: range not mapped";
        return false;
    }
    return true;
}

bool XnuVmMap::vm_map_unwire(std::uint64_t start, std::uint64_t size, std::string* out_error) {
    if (size == 0) return true;
    const std::uint64_t end = start + size;

    std::lock_guard<std::mutex> lock(mutex_);
    bool modified = false;
    for (auto& entry : entries_) {
        if (entry.overlaps(start, end)) {
            if (entry.wired_count > 0) {
                --entry.wired_count;
                modified = true;
            }
        }
    }

    if (!modified) {
        if (out_error) *out_error = "vm_map_unwire: range not mapped or not wired";
        return false;
    }
    return true;
}

VmMapStats XnuVmMap::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    VmMapStats stats{};
    stats.min_offset = min_offset_;
    stats.max_offset = max_offset_;
    stats.entry_count = entries_.size();
    for (const auto& entry : entries_) {
        stats.total_mapped_bytes += entry.size();
        if (entry.wired_count > 0) {
            stats.wired_bytes += entry.size();
        }
    }
    return stats;
}

std::vector<std::uint8_t> XnuVmMap::serialize() const {
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

    append_u64(kVmMapMagic);
    append_u64(min_offset_);
    append_u64(max_offset_);
    append_u32(static_cast<std::uint32_t>(entries_.size()));

    for (const auto& e : entries_) {
        append_u64(e.start);
        append_u64(e.end);
        append_u32(e.protection);
        append_u32(e.max_protection);
        append_u32(e.inheritance);
        append_u32(e.wired_count);
        append_u32(static_cast<std::uint32_t>(e.tag.size()));
        buf.insert(buf.end(), e.tag.begin(), e.tag.end());
    }

    return buf;
}

bool XnuVmMap::deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error) {
    if (!data || size < 28) {
        if (out_error) *out_error = "XnuVmMap::deserialize: buffer too small";
        return false;
    }

    std::uint64_t magic = 0;
    std::memcpy(&magic, data, 8);
    if (magic != kVmMapMagic) {
        if (out_error) *out_error = "XnuVmMap::deserialize: invalid magic";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t off = 8;
    std::memcpy(&min_offset_, data + off, 8); off += 8;
    std::memcpy(&max_offset_, data + off, 8); off += 8;

    std::uint32_t count = 0;
    std::memcpy(&count, data + off, 4); off += 4;

    std::vector<VmMapEntry> new_entries;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (off + 36 > size) {
            if (out_error) *out_error = "XnuVmMap::deserialize: truncated entry";
            return false;
        }
        VmMapEntry e{};
        std::memcpy(&e.start, data + off, 8); off += 8;
        std::memcpy(&e.end, data + off, 8); off += 8;
        std::memcpy(&e.protection, data + off, 4); off += 4;
        std::memcpy(&e.max_protection, data + off, 4); off += 4;
        std::memcpy(&e.inheritance, data + off, 4); off += 4;
        std::memcpy(&e.wired_count, data + off, 4); off += 4;

        std::uint32_t tag_len = 0;
        std::memcpy(&tag_len, data + off, 4); off += 4;
        if (off + tag_len > size) {
            if (out_error) *out_error = "XnuVmMap::deserialize: truncated tag";
            return false;
        }
        e.tag.assign(reinterpret_cast<const char*>(data + off), tag_len);
        off += tag_len;

        new_entries.push_back(e);
    }

    entries_ = std::move(new_entries);
    sort_entries_locked();
    return true;
}

} // namespace winios::memory
