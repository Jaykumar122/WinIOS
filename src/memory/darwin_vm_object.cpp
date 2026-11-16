#include "memory/darwin_vm_object.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace winios::memory {

namespace {

std::atomic<std::uint64_t> g_next_vm_object_id{1};

} // namespace

// -----------------------------------------------------------------------------
// DarwinVmObject Implementation
// -----------------------------------------------------------------------------
DarwinVmObject::DarwinVmObject(std::size_t size, VmMappingType type, bool is_shared)
    : id_(g_next_vm_object_id.fetch_add(1, std::memory_order_relaxed))
    , size_(static_cast<std::size_t>(align_page_up(size)))
    , type_(type)
    , is_shared_(is_shared)
    , data_(size_, 0) {
}

bool DarwinVmObject::read_bytes(std::size_t offset, void* dst, std::size_t length) const noexcept {
    if (!dst || length == 0) return true;
    if (offset > size_ || length > (size_ - offset)) {
        return false;
    }
    std::memcpy(dst, data_.data() + offset, length);
    return true;
}

bool DarwinVmObject::write_bytes(std::size_t offset, const void* src, std::size_t length) noexcept {
    if (!src || length == 0) return true;
    if (offset > size_ || length > (size_ - offset)) {
        return false;
    }
    std::memcpy(data_.data() + offset, src, length);
    return true;
}

std::shared_ptr<DarwinVmObject> DarwinVmObject::clone_for_cow() const {
    auto clone = std::make_shared<DarwinVmObject>(size_, VmMappingType::CopyOnWrite, false);
    std::memcpy(clone->data_.data(), data_.data(), size_);
    return clone;
}

// -----------------------------------------------------------------------------
// DarwinVmSpace Implementation
// -----------------------------------------------------------------------------
DarwinVmSpace::DarwinVmSpace(std::uint32_t pid, std::uint64_t base_hint)
    : pid_(pid)
    , next_alloc_hint_(align_page_up(base_hint)) {
}

std::uint64_t DarwinVmSpace::find_free_range_locked(std::size_t length, std::uint64_t alignment) {
    std::uint64_t align = std::max(static_cast<std::uint64_t>(kDarwinPageSize), alignment);
    std::uint64_t candidate = (next_alloc_hint_ + (align - 1)) & ~(align - 1);
    std::uint64_t req_size = align_page_up(length);

    while (true) {
        bool collision = false;
        std::uint64_t cand_end = candidate + req_size;

        for (const auto& e : entries_) {
            if (std::max(candidate, e.start_va) < std::min(cand_end, e.end_va())) {
                collision = true;
                candidate = (e.end_va() + (align - 1)) & ~(align - 1);
                break;
            }
        }

        if (!collision) {
            next_alloc_hint_ = candidate + req_size;
            return candidate;
        }
    }
}

void DarwinVmSpace::split_entry_at_locked(std::size_t entry_index, std::uint64_t split_va) {
    if (entry_index >= entries_.size()) return;
    auto& e = entries_[entry_index];
    if (split_va <= e.start_va || split_va >= e.end_va()) return;

    DarwinVmEntry right;
    right.start_va = split_va;
    right.size = e.end_va() - split_va;
    right.offset_in_object = e.offset_in_object + static_cast<std::size_t>(split_va - e.start_va);
    right.prot = e.prot;
    right.max_prot = e.max_prot;
    right.flags = e.flags;
    right.is_cow = e.is_cow;
    right.object = e.object;

    e.size = split_va - e.start_va;

    entries_.insert(entries_.begin() + entry_index + 1, std::move(right));
}

std::uint64_t DarwinVmSpace::mmap(
    std::uint64_t addr,
    std::size_t length,
    std::uint32_t prot,
    std::uint32_t flags,
    int fd,
    std::uint64_t offset) {

    (void)fd;
    (void)offset;

    if (length == 0) return 0;

    // Strict Apple Silicon W^X policy: Simultaneous Write + Execute rejected unless MAP_JIT
    if ((prot & (kDarwinVmProtWrite | kDarwinVmProtExecute)) == (kDarwinVmProtWrite | kDarwinVmProtExecute)) {
        if ((flags & kDarwinMapJit) == 0) {
            return 0; // W^X Violation
        }
    }

    std::lock_guard<std::mutex> guard(lock_);
    const std::uint64_t aligned_len = align_page_up(length);

    std::uint64_t alloc_va = 0;
    if (flags & kDarwinMapFixed) {
        if (!is_page_aligned(addr) || addr == 0) {
            return 0; // Invalid address
        }
        alloc_va = addr;

        // In MAP_FIXED, unmap any existing overlapping ranges
        const std::uint64_t new_end = alloc_va + aligned_len;
        for (std::size_t i = 0; i < entries_.size();) {
            if (std::max(alloc_va, entries_[i].start_va) < std::min(new_end, entries_[i].end_va())) {
                // Remove overlapping entry
                entries_.erase(entries_.begin() + i);
            } else {
                ++i;
            }
        }
    } else if (addr != 0 && is_page_aligned(addr)) {
        // Attempt hint placement if free
        const std::uint64_t new_end = addr + aligned_len;
        bool collision = false;
        for (const auto& e : entries_) {
            if (std::max(addr, e.start_va) < std::min(new_end, e.end_va())) {
                collision = true;
                break;
            }
        }
        alloc_va = collision ? find_free_range_locked(aligned_len, kDarwinPageSize) : addr;
    } else {
        alloc_va = find_free_range_locked(aligned_len, kDarwinPageSize);
    }

    VmMappingType map_type = (flags & kDarwinMapShared) ? VmMappingType::Shared : VmMappingType::Anonymous;
    auto obj = std::make_shared<DarwinVmObject>(aligned_len, map_type, (flags & kDarwinMapShared) != 0);

    DarwinVmEntry entry;
    entry.start_va = alloc_va;
    entry.size = aligned_len;
    entry.offset_in_object = 0;
    entry.prot = prot;
    entry.max_prot = kDarwinVmProtAll;
    entry.flags = flags;
    entry.is_cow = false;
    entry.object = std::move(obj);

    entries_.push_back(std::move(entry));
    std::sort(entries_.begin(), entries_.end(), [](const DarwinVmEntry& a, const DarwinVmEntry& b) {
        return a.start_va < b.start_va;
    });

    return alloc_va;
}

std::uint64_t DarwinVmSpace::map_object(
    std::shared_ptr<DarwinVmObject> object,
    std::uint64_t addr,
    std::size_t length,
    std::uint32_t prot,
    std::uint32_t flags,
    std::size_t offset) {

    if (!object || length == 0) return 0;

    // Strict Apple Silicon W^X policy: Simultaneous Write + Execute rejected unless MAP_JIT
    if ((prot & (kDarwinVmProtWrite | kDarwinVmProtExecute)) == (kDarwinVmProtWrite | kDarwinVmProtExecute)) {
        if ((flags & kDarwinMapJit) == 0) {
            return 0; // W^X Violation
        }
    }

    std::lock_guard<std::mutex> guard(lock_);
    const std::uint64_t aligned_len = align_page_up(length);

    std::uint64_t alloc_va = 0;
    if (flags & kDarwinMapFixed) {
        if (!is_page_aligned(addr) || addr == 0) {
            return 0; // Invalid address
        }
        alloc_va = addr;

        const std::uint64_t new_end = alloc_va + aligned_len;
        for (std::size_t i = 0; i < entries_.size();) {
            if (std::max(alloc_va, entries_[i].start_va) < std::min(new_end, entries_[i].end_va())) {
                entries_.erase(entries_.begin() + i);
            } else {
                ++i;
            }
        }
    } else if (addr != 0 && is_page_aligned(addr)) {
        const std::uint64_t new_end = addr + aligned_len;
        bool collision = false;
        for (const auto& e : entries_) {
            if (std::max(addr, e.start_va) < std::min(new_end, e.end_va())) {
                collision = true;
                break;
            }
        }
        alloc_va = collision ? find_free_range_locked(aligned_len, kDarwinPageSize) : addr;
    } else {
        alloc_va = find_free_range_locked(aligned_len, kDarwinPageSize);
    }

    DarwinVmEntry entry;
    entry.start_va = alloc_va;
    entry.size = aligned_len;
    entry.offset_in_object = offset;
    entry.prot = prot;
    entry.max_prot = kDarwinVmProtAll;
    entry.flags = flags;
    entry.is_cow = false;
    entry.object = std::move(object);

    entries_.push_back(std::move(entry));
    std::sort(entries_.begin(), entries_.end(), [](const DarwinVmEntry& a, const DarwinVmEntry& b) {
        return a.start_va < b.start_va;
    });

    return alloc_va;
}

int DarwinVmSpace::munmap(std::uint64_t addr, std::size_t length) {
    if (!is_page_aligned(addr) || length == 0) {
        return kKernInvalidArgument;
    }

    std::lock_guard<std::mutex> guard(lock_);
    const std::uint64_t unmap_start = addr;
    const std::uint64_t unmap_end = addr + align_page_up(length);

    bool modified = false;

    for (std::size_t i = 0; i < entries_.size();) {
        auto& e = entries_[i];

        if (unmap_start >= e.end_va() || unmap_end <= e.start_va) {
            // No overlap
            ++i;
            continue;
        }

        modified = true;

        if (unmap_start <= e.start_va && unmap_end >= e.end_va()) {
            // Entire entry unmapped
            entries_.erase(entries_.begin() + i);
        } else if (unmap_start > e.start_va && unmap_end < e.end_va()) {
            // Middle unmapped: split into left and right
            split_entry_at_locked(i, unmap_start);
            split_entry_at_locked(i + 1, unmap_end);
            entries_.erase(entries_.begin() + i + 1);
            i += 2;
        } else if (unmap_start <= e.start_va && unmap_end < e.end_va()) {
            // Left portion unmapped
            std::uint64_t trimmed = unmap_end - e.start_va;
            e.start_va = unmap_end;
            e.size -= trimmed;
            e.offset_in_object += static_cast<std::size_t>(trimmed);
            ++i;
        } else {
            // Right portion unmapped (unmap_start > e.start_va && unmap_end >= e.end_va())
            e.size = unmap_start - e.start_va;
            ++i;
        }
    }

    return modified ? kKernSuccess : kKernInvalidAddress;
}

int DarwinVmSpace::mprotect(std::uint64_t addr, std::size_t length, std::uint32_t new_prot) {
    return mach_vm_protect(addr, length, false, new_prot);
}

int DarwinVmSpace::mach_vm_allocate(std::uint64_t* address, std::size_t size, int flags) {
    if (!address || size == 0) {
        return kKernInvalidArgument;
    }

    const std::size_t aligned_len = static_cast<std::size_t>(align_page_up(size));
    std::lock_guard<std::mutex> guard(lock_);

    std::uint64_t alloc_va = 0;
    if ((flags & kDarwinVmFlagsAnywhere) == 0) {
        // Fixed allocation at *address
        if (!is_page_aligned(*address) || *address == 0) {
            return kKernInvalidArgument;
        }
        alloc_va = *address;
        const std::uint64_t req_end = alloc_va + aligned_len;
        for (const auto& e : entries_) {
            if (std::max(alloc_va, e.start_va) < std::min(req_end, e.end_va())) {
                return kKernNoSpace; // Collision: fixed allocation does not overwrite
            }
        }
    } else {
        // Anywhere allocation
        alloc_va = find_free_range_locked(aligned_len, kDarwinPageSize);
        if (alloc_va == 0) {
            return kKernNoSpace;
        }
    }

    auto obj = std::make_shared<DarwinVmObject>(aligned_len, VmMappingType::Anonymous, false);

    DarwinVmEntry entry;
    entry.start_va = alloc_va;
    entry.size = aligned_len;
    entry.offset_in_object = 0;
    entry.prot = kDarwinVmProtRead | kDarwinVmProtWrite;
    entry.max_prot = kDarwinVmProtAll;
    entry.flags = kDarwinMapAnonymous | kDarwinMapPrivate;
    entry.is_cow = false;
    entry.object = std::move(obj);

    entries_.push_back(std::move(entry));
    std::sort(entries_.begin(), entries_.end(), [](const DarwinVmEntry& a, const DarwinVmEntry& b) {
        return a.start_va < b.start_va;
    });

    *address = alloc_va;
    return kKernSuccess;
}

int DarwinVmSpace::mach_vm_deallocate(std::uint64_t address, std::size_t size) {
    return munmap(address, size);
}

int DarwinVmSpace::mach_vm_protect(std::uint64_t address, std::size_t size, bool set_maximum, std::uint32_t new_prot) {
    if (!is_page_aligned(address) || size == 0) {
        return kKernInvalidArgument;
    }

    // Strict Apple Silicon W^X policy
    if ((new_prot & (kDarwinVmProtWrite | kDarwinVmProtExecute)) == (kDarwinVmProtWrite | kDarwinVmProtExecute)) {
        return kKernProtectionFailure;
    }

    std::lock_guard<std::mutex> guard(lock_);
    const std::uint64_t prot_start = address;
    const std::uint64_t prot_end = address + align_page_up(size);

    // Ensure range is covered
    std::uint64_t cur_va = prot_start;
    for (const auto& e : entries_) {
        if (e.start_va <= cur_va && e.end_va() > cur_va) {
            cur_va = std::min(prot_end, e.end_va());
            if (cur_va >= prot_end) break;
        }
    }
    if (cur_va < prot_end) {
        return kKernInvalidAddress;
    }

    // Split boundary entries
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].start_va < prot_start && entries_[i].end_va() > prot_start) {
            split_entry_at_locked(i, prot_start);
            break;
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].start_va < prot_end && entries_[i].end_va() > prot_end) {
            split_entry_at_locked(i, prot_end);
            break;
        }
    }

    if (!set_maximum) {
        // Validate max_prot for all target entries before modifying
        for (const auto& e : entries_) {
            if (e.start_va >= prot_start && e.end_va() <= prot_end) {
                if ((new_prot & ~e.max_prot) != 0) {
                    return kKernProtectionFailure;
                }
            }
        }

        // Apply new protection
        for (auto& e : entries_) {
            if (e.start_va >= prot_start && e.end_va() <= prot_end) {
                e.prot = new_prot;
            }
        }
    } else {
        // Update maximum protection
        for (auto& e : entries_) {
            if (e.start_va >= prot_start && e.end_va() <= prot_end) {
                e.max_prot = new_prot;
                e.prot &= new_prot; // current protection cannot exceed maximum
            }
        }
    }

    return kKernSuccess;
}

int DarwinVmSpace::mach_vm_read(std::uint64_t address, void* buffer, std::size_t size) const {
    if (!buffer || size == 0) {
        return kKernInvalidArgument;
    }

    std::lock_guard<std::mutex> guard(lock_);
    const DarwinVmEntry* e = find_entry(address);
    if (!e || (address + size) > e->end_va()) {
        return kKernInvalidAddress;
    }

    if ((e->prot & kDarwinVmProtRead) == 0) {
        return kKernProtectionFailure;
    }

    std::size_t obj_off = e->offset_in_object + static_cast<std::size_t>(address - e->start_va);
    if (!e->object->read_bytes(obj_off, buffer, size)) {
        return kKernMemoryError;
    }

    return kKernSuccess;
}

int DarwinVmSpace::mach_vm_write(std::uint64_t address, const void* buffer, std::size_t size) {
    if (!buffer || size == 0) {
        return kKernInvalidArgument;
    }

    std::lock_guard<std::mutex> guard(lock_);
    for (auto& e : entries_) {
        if (address >= e.start_va && address < e.end_va()) {
            if ((address + size) > e.end_va()) {
                return kKernInvalidAddress;
            }

            if ((e.prot & kDarwinVmProtWrite) == 0) {
                return kKernProtectionFailure;
            }

            // Copy-On-Write trigger
            if (e.is_cow) {
                e.object = e.object->clone_for_cow();
                e.is_cow = false;
            }

            std::size_t obj_off = e.offset_in_object + static_cast<std::size_t>(address - e.start_va);
            if (!e.object->write_bytes(obj_off, buffer, size)) {
                return kKernMemoryError;
            }

            return kKernSuccess;
        }
    }

    return kKernInvalidAddress;
}

bool DarwinVmSpace::check_access(std::uint64_t addr, std::size_t length, std::uint32_t required_prot) const noexcept {
    if (length == 0 || (addr + length < addr)) return false;

    std::lock_guard<std::mutex> guard(lock_);
    std::uint64_t cur = addr;
    const std::uint64_t target_end = addr + length;

    for (const auto& e : entries_) {
        if (e.start_va <= cur && e.end_va() > cur) {
            if ((e.prot & required_prot) != required_prot) {
                return false;
            }
            cur = e.end_va();
            if (cur >= target_end) {
                return true;
            }
        }
    }

    return false;
}

const DarwinVmEntry* DarwinVmSpace::find_entry(std::uint64_t addr) const noexcept {
    for (const auto& e : entries_) {
        if (addr >= e.start_va && addr < e.end_va()) {
            return &e;
        }
    }
    return nullptr;
}

std::size_t DarwinVmSpace::region_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return entries_.size();
}

std::shared_ptr<DarwinVmSpace> DarwinVmSpace::clone_for_fork(std::uint32_t new_pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto child_space = std::make_shared<DarwinVmSpace>(new_pid, next_alloc_hint_);

    for (const auto& e : entries_) {
        DarwinVmEntry child_e = e;
        if (!e.object->is_shared() && (e.prot & kDarwinVmProtWrite) != 0) {
            // Mark both parent and child as COW
            const_cast<DarwinVmEntry&>(e).is_cow = true;
            child_e.is_cow = true;
        }
        child_space->entries_.push_back(std::move(child_e));
    }

    return child_space;
}

} // namespace winios::memory
