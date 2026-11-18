#include "memory/user_address_space.h"

#include <algorithm>
#include <cstring>

namespace winios::memory {

UserAddressSpace::UserAddressSpace(Memory& mem,
                                   PhysicalPageManager& ppm,
                                   std::uint64_t root_l2_table_pa,
                                   std::string name)
    : mem_(mem),
      ppm_(ppm),
      root_l2_table_pa_(root_l2_table_pa),
      name_(std::move(name)),
      vpt_(mem),
      vm_map_(kUserSpaceBaseVa, kUserSpaceLimitVa, name_) {
    // Zero out the root L2 table
    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        if (root_l2_table_pa_ + i < mem_.size()) {
            mem_.write_u8(static_cast<std::size_t>(root_l2_table_pa_ + i), 0);
        }
    }
}

UserAddressSpace::~UserAddressSpace() {
    // Deallocate physical pages allocated for this address space
    for (std::uint64_t pa : allocated_physical_pages_) {
        if (pa != 0) {
            ppm_.free_page(pa);
        }
    }
    allocated_physical_pages_.clear();
    if (root_l2_table_pa_ != 0) {
        ppm_.free_page(root_l2_table_pa_);
        root_l2_table_pa_ = 0;
    }
    mapped_page_count_ = 0;
}

bool UserAddressSpace::owns_physical_page(std::uint64_t pa) const noexcept {
    if (pa == 0) return false;
    if (pa == root_l2_table_pa_) return true;
    return std::find(allocated_physical_pages_.begin(), allocated_physical_pages_.end(), pa) != allocated_physical_pages_.end();
}

void UserAddressSpace::record_allocated_page(std::uint64_t pa) {
    if (pa != 0 && std::find(allocated_physical_pages_.begin(), allocated_physical_pages_.end(), pa) == allocated_physical_pages_.end()) {
        allocated_physical_pages_.push_back(pa);
    }
}

bool UserAddressSpace::map_user_page(std::uint64_t va,
                                     std::uint64_t pa,
                                     PagePermissions perms) {
    if (root_l2_table_pa_ == 0 || va >= kUserSpaceLimitVa || (va % kPhysicalPageSize16KB) != 0) {
        return false;
    }

    // Only allow EL0 user permissions
    if (perms != PagePermissions::UserRead &&
        perms != PagePermissions::UserReadWrite &&
        perms != PagePermissions::UserExecute) {
        return false;
    }

    if (translate(va) != 0) {
        return false; // Already mapped (prevent overlapping duplicate mappings)
    }

    if (!vpt_.map_page(root_l2_table_pa_, va, pa, perms, PageMemoryType::NormalMemory, &ppm_)) {
        return false;
    }

    record_allocated_page(pa);
    mapped_page_count_++;

    // Update VM map
    std::uint32_t vm_prot = kVmProtRead;
    if (perms == PagePermissions::UserReadWrite) vm_prot |= kVmProtWrite;
    if (perms == PagePermissions::UserExecute) vm_prot |= kVmProtExecute;

    std::string err;
    vm_map_.vm_map_enter(va, kPhysicalPageSize16KB, vm_prot, vm_prot, "user_page", &err);
    return true;
}

bool UserAddressSpace::map_user_range(std::uint64_t start_va,
                                      std::uint64_t start_pa,
                                      std::size_t num_pages,
                                      PagePermissions perms) {
    for (std::size_t i = 0; i < num_pages; ++i) {
        const std::uint64_t va = start_va + (i * kPhysicalPageSize16KB);
        const std::uint64_t pa = start_pa + (i * kPhysicalPageSize16KB);
        if (!map_user_page(va, pa, perms)) {
            return false;
        }
    }
    return true;
}

bool UserAddressSpace::unmap_user_page(std::uint64_t va) {
    if (root_l2_table_pa_ == 0 || va >= kUserSpaceLimitVa || (va % kPhysicalPageSize16KB) != 0) {
        return false;
    }
    const std::uint64_t pa = translate(va);
    if (pa == 0) {
        return false;
    }
    std::string err;
    vm_map_.vm_map_remove(va, kPhysicalPageSize16KB, &err);
    if (!vpt_.unmap_page(root_l2_table_pa_, va)) {
        return false;
    }
    auto it = std::find(allocated_physical_pages_.begin(), allocated_physical_pages_.end(), pa);
    if (it != allocated_physical_pages_.end()) {
        allocated_physical_pages_.erase(it);
        ppm_.free_page(pa);
    }
    if (mapped_page_count_ > 0) {
        mapped_page_count_--;
    }
    unmapped_page_count_++;
    return true;
}

std::uint64_t UserAddressSpace::translate(std::uint64_t va, PageDescriptorFields* out_fields) const {
    if (root_l2_table_pa_ == 0 || va >= kUserSpaceLimitVa) {
        return 0;
    }
    return vpt_.translate(root_l2_table_pa_, va, out_fields);
}

bool UserAddressSpace::validate_user_buffer(std::uint64_t va,
                                            std::size_t size,
                                            bool require_writable) const {
    if (size == 0) return true;
    if (va + size < va) return false; // Overflow check
    if (va + size > kUserSpaceLimitVa) return false;

    std::uint64_t cur = va;
    const std::uint64_t end = va + size;

    while (cur < end) {
        PageDescriptorFields fields{};
        const std::uint64_t pa = translate(cur, &fields);
        if (pa == 0 || !fields.valid || !fields.el0_accessible) {
            return false; // Not mapped or forbidden to EL0
        }
        if (require_writable && !fields.writable) {
            return false; // Writable access required
        }
        // Advance to next page boundary
        const std::uint64_t next_page = (cur & ~(kPhysicalPageSize16KB - 1ULL)) + kPhysicalPageSize16KB;
        cur = (next_page < end) ? next_page : end;
    }
    return true;
}

bool UserAddressSpace::allocate_user_stack(std::uint64_t stack_top_va,
                                           std::size_t stack_size,
                                           std::uint64_t& out_stack_bottom_va) {
    if ((stack_top_va % kPhysicalPageSize16KB) != 0 || stack_size == 0) {
        return false;
    }
    const std::size_t pages = (stack_size + kPhysicalPageSize16KB - 1) / kPhysicalPageSize16KB;
    const std::uint64_t total_bytes = pages * kPhysicalPageSize16KB;
    if (stack_top_va < total_bytes) {
        return false;
    }

    out_stack_bottom_va = stack_top_va - total_bytes;

    for (std::size_t i = 0; i < pages; ++i) {
        const std::uint64_t page_va = out_stack_bottom_va + (i * kPhysicalPageSize16KB);
        const std::uint64_t pa = ppm_.allocate_page();
        if (pa == 0) {
            return false;
        }
        allocated_physical_pages_.push_back(pa);
        if (!map_user_page(page_va, pa, PagePermissions::UserReadWrite)) {
            return false;
        }
    }

    return true;
}

bool UserAddressSpace::copyin(std::uint64_t dst_user_va, const void* src_host, std::size_t len) {
    if (!validate_user_buffer(dst_user_va, len, true)) {
        return false;
    }
    const auto* src = static_cast<const std::uint8_t*>(src_host);
    std::size_t copied = 0;

    while (copied < len) {
        const std::uint64_t cur_va = dst_user_va + copied;
        const std::uint64_t page_offset = cur_va & (kPhysicalPageSize16KB - 1ULL);
        const std::size_t chunk = std::min(len - copied, static_cast<std::size_t>(kPhysicalPageSize16KB - page_offset));

        const std::uint64_t pa = translate(cur_va);
        if (pa == 0 || pa + chunk > mem_.size()) {
            return false;
        }

        for (std::size_t i = 0; i < chunk; ++i) {
            mem_.write_u8(static_cast<std::size_t>(pa + i), src[copied + i]);
        }

        copied += chunk;
    }
    return true;
}

bool UserAddressSpace::copyout(const void* src_user_va, void* dst_host, std::size_t len) {
    const std::uint64_t va = reinterpret_cast<std::uint64_t>(src_user_va);
    if (!validate_user_buffer(va, len, false)) {
        return false;
    }
    auto* dst = static_cast<std::uint8_t*>(dst_host);
    std::size_t copied = 0;

    while (copied < len) {
        const std::uint64_t cur_va = va + copied;
        const std::uint64_t page_offset = cur_va & (kPhysicalPageSize16KB - 1ULL);
        const std::size_t chunk = std::min(len - copied, static_cast<std::size_t>(kPhysicalPageSize16KB - page_offset));

        const std::uint64_t pa = translate(cur_va);
        if (pa == 0 || pa + chunk > mem_.size()) {
            return false;
        }

        for (std::size_t i = 0; i < chunk; ++i) {
            dst[copied + i] = mem_.read_u8(static_cast<std::size_t>(pa + i));
        }

        copied += chunk;
    }
    return true;
}

void UserAddressSpace::serialize(std::vector<std::uint8_t>& dst) const {
    const auto* p_root = reinterpret_cast<const std::uint8_t*>(&root_l2_table_pa_);
    dst.insert(dst.end(), p_root, p_root + sizeof(root_l2_table_pa_));

    const std::uint32_t name_len = static_cast<std::uint32_t>(name_.size());
    const auto* p_len = reinterpret_cast<const std::uint8_t*>(&name_len);
    dst.insert(dst.end(), p_len, p_len + sizeof(name_len));
    dst.insert(dst.end(), name_.begin(), name_.end());

    const auto vm_blob = vm_map_.serialize();
    const std::uint32_t vm_blob_len = static_cast<std::uint32_t>(vm_blob.size());
    const auto* p_vmlen = reinterpret_cast<const std::uint8_t*>(&vm_blob_len);
    dst.insert(dst.end(), p_vmlen, p_vmlen + sizeof(vm_blob_len));
    dst.insert(dst.end(), vm_blob.begin(), vm_blob.end());
}

std::unique_ptr<UserAddressSpace> UserAddressSpace::deserialize(
    Memory& mem, PhysicalPageManager& ppm, const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(std::uint64_t) + sizeof(std::uint32_t)) return nullptr;

    std::uint64_t root_pa = 0;
    std::memcpy(&root_pa, src, sizeof(root_pa));
    src += sizeof(root_pa);
    remaining -= sizeof(root_pa);

    std::uint32_t name_len = 0;
    std::memcpy(&name_len, src, sizeof(name_len));
    src += sizeof(name_len);
    remaining -= sizeof(name_len);

    if (remaining < name_len) return nullptr;
    std::string name(reinterpret_cast<const char*>(src), name_len);
    src += name_len;
    remaining -= name_len;

    if (remaining < sizeof(std::uint32_t)) return nullptr;
    std::uint32_t vm_blob_len = 0;
    std::memcpy(&vm_blob_len, src, sizeof(vm_blob_len));
    src += sizeof(vm_blob_len);
    remaining -= sizeof(vm_blob_len);

    if (remaining < vm_blob_len) return nullptr;
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_pa, std::move(name));
    std::string err;
    if (!uas->vm_map_.deserialize(src, vm_blob_len, &err)) {
        return nullptr;
    }
    src += vm_blob_len;
    remaining -= vm_blob_len;

    return uas;
}

} // namespace winios::memory
