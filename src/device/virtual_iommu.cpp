#include "device/virtual_iommu.h"

#include <algorithm>

namespace winios::device {

// -----------------------------------------------------------------------------
// IommuDomain Implementation
// -----------------------------------------------------------------------------
bool IommuDomain::map(std::uint64_t iova, std::uint64_t pa, std::uint64_t size, IommuAccess permissions) {
    if (size == 0) return false;
    if (iova + size < iova) return false; // Overflow check
    if (pa + size < pa) return false;     // Overflow check

    // Remove any overlapping existing mappings
    unmap(iova, size);

    mappings_.push_back(IommuMapping{iova, pa, size, permissions});
    return true;
}

bool IommuDomain::unmap(std::uint64_t iova, std::uint64_t size) {
    if (size == 0) return false;
    if (iova + size < iova) return false;
    const std::uint64_t unmap_end = iova + size;

    auto it = std::remove_if(mappings_.begin(), mappings_.end(), [&](const IommuMapping& m) {
        const std::uint64_t m_end = m.iova + m.size;
        return (iova < m_end && m.iova < unmap_end);
    });

    const bool changed = (it != mappings_.end());
    mappings_.erase(it, mappings_.end());
    return changed;
}

std::optional<std::uint64_t> IommuDomain::translate(
    std::uint64_t iova,
    std::size_t length,
    IommuAccess access,
    std::size_t memory_limit,
    IommuFault* out_fault) const {

    if (length == 0) length = 1;
    if (iova + length < iova) {
        if (out_fault) {
            out_fault->type = IommuFaultType::AddressOverflow;
            out_fault->domain_id = domain_id_;
            out_fault->iova = iova;
            out_fault->access = access;
            out_fault->message = "IOMMU address overflow";
        }
        return std::nullopt;
    }

    for (const auto& m : mappings_) {
        if (m.contains(iova, length)) {
            // Check permissions
            if (has_iommu_read(access) && !has_iommu_read(m.permissions)) {
                if (out_fault) {
                    out_fault->type = IommuFaultType::PermissionDenied;
                    out_fault->domain_id = domain_id_;
                    out_fault->iova = iova;
                    out_fault->access = access;
                    out_fault->message = "Read permission denied in IOMMU domain";
                }
                return std::nullopt;
            }
            if (has_iommu_write(access) && !has_iommu_write(m.permissions)) {
                if (out_fault) {
                    out_fault->type = IommuFaultType::PermissionDenied;
                    out_fault->domain_id = domain_id_;
                    out_fault->iova = iova;
                    out_fault->access = access;
                    out_fault->message = "Write permission denied in IOMMU domain";
                }
                return std::nullopt;
            }

            const std::uint64_t offset = iova - m.iova;
            const std::uint64_t final_pa = m.pa + offset;

            if (memory_limit > 0 && (final_pa + length > memory_limit)) {
                if (out_fault) {
                    out_fault->type = IommuFaultType::OutOfBounds;
                    out_fault->domain_id = domain_id_;
                    out_fault->iova = iova;
                    out_fault->access = access;
                    out_fault->message = "Translated PA exceeds physical memory limit";
                }
                return std::nullopt;
            }

            return final_pa;
        }
    }

    if (out_fault) {
        out_fault->type = IommuFaultType::UnmappedIova;
        out_fault->domain_id = domain_id_;
        out_fault->iova = iova;
        out_fault->access = access;
        out_fault->message = "Unmapped IOVA in IOMMU domain";
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// VirtualIommu Implementation
// -----------------------------------------------------------------------------
void VirtualIommu::reset() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    domains_.clear();
    device_to_domain_.clear();
    iotlb_.clear();
    total_translations_ = 0;
    iotlb_hits_ = 0;
    iotlb_misses_ = 0;
    fault_count_ = 0;
}

bool VirtualIommu::create_domain(DomainId dom_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (domains_.contains(dom_id)) return false;
    domains_[dom_id] = std::make_unique<IommuDomain>(dom_id);
    return true;
}

bool VirtualIommu::destroy_domain(DomainId dom_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = domains_.find(dom_id);
    if (it == domains_.end()) return false;

    // Detach all devices using this domain
    for (auto dev_it = device_to_domain_.begin(); dev_it != device_to_domain_.end();) {
        if (dev_it->second == dom_id) {
            dev_it = device_to_domain_.erase(dev_it);
        } else {
            ++dev_it;
        }
    }

    domains_.erase(it);
    invalidate_iotlb_domain(dom_id);
    return true;
}

bool VirtualIommu::has_domain(DomainId dom_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return domains_.contains(dom_id);
}

bool VirtualIommu::attach_device(DeviceId dev_id, DomainId dom_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!domains_.contains(dom_id)) return false;
    device_to_domain_[dev_id] = dom_id;
    invalidate_iotlb_device(dev_id);
    return true;
}

bool VirtualIommu::detach_device(DeviceId dev_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = device_to_domain_.find(dev_id);
    if (it == device_to_domain_.end()) return false;
    device_to_domain_.erase(it);
    invalidate_iotlb_device(dev_id);
    return true;
}

bool VirtualIommu::is_device_attached(DeviceId dev_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return device_to_domain_.contains(dev_id);
}

std::optional<DomainId> VirtualIommu::get_device_domain(DeviceId dev_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = device_to_domain_.find(dev_id);
    if (it != device_to_domain_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool VirtualIommu::map(DomainId dom_id, std::uint64_t iova, std::uint64_t pa, std::uint64_t size, IommuAccess permissions) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = domains_.find(dom_id);
    if (it == domains_.end()) return false;
    const bool res = it->second->map(iova, pa, size, permissions);
    if (res) {
        invalidate_iotlb_domain(dom_id);
    }
    return res;
}

bool VirtualIommu::unmap(DomainId dom_id, std::uint64_t iova, std::uint64_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = domains_.find(dom_id);
    if (it == domains_.end()) return false;
    const bool res = it->second->unmap(iova, size);
    if (res) {
        invalidate_iotlb_domain(dom_id);
    }
    return res;
}

std::optional<std::uint64_t> VirtualIommu::translate(
    DeviceId dev_id,
    std::uint64_t iova,
    std::size_t length,
    IommuAccess access,
    std::size_t memory_limit,
    IommuFault* out_fault) {

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ++total_translations_;

    auto dev_it = device_to_domain_.find(dev_id);
    if (dev_it == device_to_domain_.end()) {
        ++fault_count_;
        if (out_fault) {
            out_fault->type = IommuFaultType::DeviceNotAttached;
            out_fault->device_id = dev_id;
            out_fault->iova = iova;
            out_fault->access = access;
            out_fault->message = "Device not attached to any IOMMU domain";
        }
        return std::nullopt;
    }

    const DomainId dom_id = dev_it->second;

    // Check IOTLB
    for (const auto& entry : iotlb_) {
        if (!entry.valid) continue;
        if (entry.device_id == dev_id && entry.domain_id == dom_id) {
            if (iova >= entry.iova_base && (iova + length <= entry.iova_base + entry.size)) {
                // Verify permissions
                if (has_iommu_read(access) && !has_iommu_read(entry.permissions)) break;
                if (has_iommu_write(access) && !has_iommu_write(entry.permissions)) break;

                const std::uint64_t offset = iova - entry.iova_base;
                const std::uint64_t final_pa = entry.pa_base + offset;
                if (memory_limit > 0 && (final_pa + length > memory_limit)) break;

                ++iotlb_hits_;
                return final_pa;
            }
        }
    }

    ++iotlb_misses_;

    auto dom_it = domains_.find(dom_id);
    if (dom_it == domains_.end()) {
        ++fault_count_;
        if (out_fault) {
            out_fault->type = IommuFaultType::DomainNotFound;
            out_fault->device_id = dev_id;
            out_fault->domain_id = dom_id;
            out_fault->iova = iova;
            out_fault->access = access;
            out_fault->message = "Domain not found for attached device";
        }
        return std::nullopt;
    }

    auto pa_opt = dom_it->second->translate(iova, length, access, memory_limit, out_fault);
    if (!pa_opt.has_value()) {
        ++fault_count_;
        if (out_fault) {
            out_fault->device_id = dev_id;
            out_fault->domain_id = dom_id;
        }
        return std::nullopt;
    }

    // Insert into IOTLB
    IotlbEntry entry;
    entry.valid = true;
    entry.device_id = dev_id;
    entry.domain_id = dom_id;
    entry.iova_base = iova & ~0xFFFULL; // 4KB aligned chunk
    entry.pa_base = pa_opt.value() & ~0xFFFULL;
    entry.size = 4096ULL;
    entry.permissions = access;

    if (iotlb_.size() >= kIotlbSize) {
        iotlb_.erase(iotlb_.begin());
    }
    iotlb_.push_back(entry);

    return pa_opt.value();
}

void VirtualIommu::invalidate_iotlb_all() noexcept {
    iotlb_.clear();
}

void VirtualIommu::invalidate_iotlb_domain(DomainId dom_id) noexcept {
    auto it = std::remove_if(iotlb_.begin(), iotlb_.end(), [&](const IotlbEntry& e) {
        return e.domain_id == dom_id;
    });
    iotlb_.erase(it, iotlb_.end());
}

void VirtualIommu::invalidate_iotlb_device(DeviceId dev_id) noexcept {
    auto it = std::remove_if(iotlb_.begin(), iotlb_.end(), [&](const IotlbEntry& e) {
        return e.device_id == dev_id;
    });
    iotlb_.erase(it, iotlb_.end());
}

std::vector<std::uint8_t> VirtualIommu::serialize() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::uint8_t> buf;

    auto append_u32 = [&buf](std::uint32_t val) {
        const std::size_t cur = buf.size();
        buf.resize(cur + 4);
        std::memcpy(buf.data() + cur, &val, 4);
    };

    auto append_u64 = [&buf](std::uint64_t val) {
        const std::size_t cur = buf.size();
        buf.resize(cur + 8);
        std::memcpy(buf.data() + cur, &val, 8);
    };

    // Magic 'IOMU'
    append_u32(0x494F4D55u);

    // Domains
    append_u32(static_cast<std::uint32_t>(domains_.size()));
    for (const auto& [dom_id, dom_ptr] : domains_) {
        append_u32(dom_id);
        const auto& maps = dom_ptr->mappings();
        append_u32(static_cast<std::uint32_t>(maps.size()));
        for (const auto& m : maps) {
            append_u64(m.iova);
            append_u64(m.pa);
            append_u64(m.size);
            append_u32(static_cast<std::uint32_t>(m.permissions));
        }
    }

    // Devices
    append_u32(static_cast<std::uint32_t>(device_to_domain_.size()));
    for (const auto& [dev_id, dom_id] : device_to_domain_) {
        append_u32(dev_id);
        append_u32(dom_id);
    }

    return buf;
}

bool VirtualIommu::deserialize(const void* data, std::size_t size) {
    if (!data || size < 8) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    reset();

    const auto* p = static_cast<const std::uint8_t*>(data);
    const auto* end = p + size;

    auto read_u32 = [&p, end](std::uint32_t& out) -> bool {
        if (p + 4 > end) return false;
        std::memcpy(&out, p, 4);
        p += 4;
        return true;
    };

    auto read_u64 = [&p, end](std::uint64_t& out) -> bool {
        if (p + 8 > end) return false;
        std::memcpy(&out, p, 8);
        p += 8;
        return true;
    };

    std::uint32_t magic = 0;
    if (!read_u32(magic) || magic != 0x494F4D55u) return false;

    std::uint32_t num_domains = 0;
    if (!read_u32(num_domains)) return false;

    for (std::uint32_t i = 0; i < num_domains; ++i) {
        std::uint32_t dom_id = 0;
        std::uint32_t num_maps = 0;
        if (!read_u32(dom_id) || !read_u32(num_maps)) return false;

        domains_[dom_id] = std::make_unique<IommuDomain>(dom_id);
        for (std::uint32_t j = 0; j < num_maps; ++j) {
            std::uint64_t iova = 0;
            std::uint64_t pa = 0;
            std::uint64_t sz = 0;
            std::uint32_t perm_val = 0;
            if (!read_u64(iova) || !read_u64(pa) || !read_u64(sz) || !read_u32(perm_val)) {
                return false;
            }
            domains_[dom_id]->map(iova, pa, sz, static_cast<IommuAccess>(perm_val));
        }
    }

    std::uint32_t num_devs = 0;
    if (!read_u32(num_devs)) return false;

    for (std::uint32_t i = 0; i < num_devs; ++i) {
        std::uint32_t dev_id = 0;
        std::uint32_t dom_id = 0;
        if (!read_u32(dev_id) || !read_u32(dom_id)) return false;
        device_to_domain_[dev_id] = dom_id;
    }

    return true;
}

} // namespace winios::device
