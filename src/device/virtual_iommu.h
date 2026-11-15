#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace winios::device {

using DeviceId = std::uint32_t;
using DomainId = std::uint32_t;

// -----------------------------------------------------------------------------
// IOMMU Access & Permissions
// -----------------------------------------------------------------------------
enum class IommuAccess : std::uint8_t {
    None      = 0,
    Read      = 1u << 0,
    Write     = 1u << 1,
    ReadWrite = Read | Write
};

[[nodiscard]] constexpr bool has_iommu_read(IommuAccess a) noexcept {
    return (static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(IommuAccess::Read)) != 0;
}

[[nodiscard]] constexpr bool has_iommu_write(IommuAccess a) noexcept {
    return (static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(IommuAccess::Write)) != 0;
}

// -----------------------------------------------------------------------------
// IommuFaultType & Fault Reporting
// -----------------------------------------------------------------------------
enum class IommuFaultType : std::uint8_t {
    None = 0,
    DeviceNotAttached,
    DomainNotFound,
    UnmappedIova,
    PermissionDenied,
    AddressOverflow,
    OutOfBounds
};

struct IommuFault {
    IommuFaultType type{IommuFaultType::None};
    DeviceId device_id{0};
    DomainId domain_id{0};
    std::uint64_t iova{0};
    IommuAccess access{IommuAccess::None};
    std::string message;

    [[nodiscard]] bool has_fault() const noexcept { return type != IommuFaultType::None; }
};

// -----------------------------------------------------------------------------
// IommuMapping: Contiguous mapping interval [iova, iova + size) -> pa
// -----------------------------------------------------------------------------
struct IommuMapping {
    std::uint64_t iova{0};
    std::uint64_t pa{0};
    std::uint64_t size{0};
    IommuAccess permissions{IommuAccess::None};

    [[nodiscard]] bool contains(std::uint64_t addr, std::size_t len) const noexcept {
        if (size == 0) return false;
        if (addr < iova) return false;
        const std::uint64_t offset = addr - iova;
        return (offset + len <= size);
    }
};

// -----------------------------------------------------------------------------
// IommuDomain: Isolated address translation container
// -----------------------------------------------------------------------------
class IommuDomain {
public:
    explicit IommuDomain(DomainId id) : domain_id_(id) {}

    [[nodiscard]] DomainId id() const noexcept { return domain_id_; }

    bool map(std::uint64_t iova, std::uint64_t pa, std::uint64_t size, IommuAccess permissions);
    bool unmap(std::uint64_t iova, std::uint64_t size);

    [[nodiscard]] std::optional<std::uint64_t> translate(
        std::uint64_t iova,
        std::size_t length,
        IommuAccess access,
        std::size_t memory_limit,
        IommuFault* out_fault = nullptr) const;

    [[nodiscard]] const std::vector<IommuMapping>& mappings() const noexcept { return mappings_; }

private:
    DomainId domain_id_{0};
    std::vector<IommuMapping> mappings_;
};

// -----------------------------------------------------------------------------
// VirtualIommu: Virtual I/O Memory Management Unit
// -----------------------------------------------------------------------------
class VirtualIommu {
public:
    VirtualIommu() = default;

    void reset() noexcept;

    // Domain management
    bool create_domain(DomainId dom_id);
    bool destroy_domain(DomainId dom_id);
    [[nodiscard]] bool has_domain(DomainId dom_id) const noexcept;

    // Device attachment
    bool attach_device(DeviceId dev_id, DomainId dom_id);
    bool detach_device(DeviceId dev_id);
    [[nodiscard]] bool is_device_attached(DeviceId dev_id) const noexcept;
    [[nodiscard]] std::optional<DomainId> get_device_domain(DeviceId dev_id) const noexcept;

    // Mapping in a domain
    bool map(DomainId dom_id, std::uint64_t iova, std::uint64_t pa, std::uint64_t size, IommuAccess permissions);
    bool unmap(DomainId dom_id, std::uint64_t iova, std::uint64_t size);

    // Core Translation: IOVA -> PA for Device DMA
    [[nodiscard]] std::optional<std::uint64_t> translate(
        DeviceId dev_id,
        std::uint64_t iova,
        std::size_t length,
        IommuAccess access,
        std::size_t memory_limit,
        IommuFault* out_fault = nullptr);

    // IOTLB Invalidation
    void invalidate_iotlb_all() noexcept;
    void invalidate_iotlb_domain(DomainId dom_id) noexcept;
    void invalidate_iotlb_device(DeviceId dev_id) noexcept;

    // Telemetry & metrics
    [[nodiscard]] std::uint64_t total_translations() const noexcept { return total_translations_; }
    [[nodiscard]] std::uint64_t iotlb_hits() const noexcept { return iotlb_hits_; }
    [[nodiscard]] std::uint64_t iotlb_misses() const noexcept { return iotlb_misses_; }
    [[nodiscard]] std::uint64_t fault_count() const noexcept { return fault_count_; }

    // Snapshot serialization & restoration
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const void* data, std::size_t size);

private:
    struct IotlbEntry {
        bool valid{false};
        DeviceId device_id{0};
        DomainId domain_id{0};
        std::uint64_t iova_base{0};
        std::uint64_t pa_base{0};
        std::uint64_t size{0};
        IommuAccess permissions{IommuAccess::None};
    };

    mutable std::recursive_mutex mutex_;
    std::unordered_map<DomainId, std::unique_ptr<IommuDomain>> domains_;
    std::unordered_map<DeviceId, DomainId> device_to_domain_;

    static constexpr std::size_t kIotlbSize = 32;
    std::vector<IotlbEntry> iotlb_;

    std::uint64_t total_translations_{0};
    std::uint64_t iotlb_hits_{0};
    std::uint64_t iotlb_misses_{0};
    std::uint64_t fault_count_{0};
};

} // namespace winios::device
