#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::loader {

enum class GuestMemoryRegionType {
    AvailableRam,
    KernelImage,
    DeviceTree,
    Initramfs,
    FirmwareReserved,
    MmioDevice
};

struct GuestMemoryRegion {
    std::string name;
    GuestMemoryRegionType type;
    std::uint64_t base_address;
    std::uint64_t size;
    bool is_reserved{false};

    [[nodiscard]] std::uint64_t end_address() const noexcept {
        return base_address + size;
    }

    [[nodiscard]] bool overlaps_with(const GuestMemoryRegion& other) const noexcept {
        if (size == 0 || other.size == 0) return false;
        return base_address < other.end_address() && other.base_address < end_address();
    }
};

enum class MemoryMapValidationResult {
    Valid,
    RamBaseMisaligned,
    RegionOutOfBounds,
    RegionOverlap,
    KernelMisaligned,
    DtbMisaligned,
    InitramfsMisaligned,
    ZeroSizeRegion,
    InsufficientRamCapacity
};

class GuestMemoryMap {
public:
    GuestMemoryMap(std::uint64_t ram_base = 0x40000000ULL, std::size_t ram_size = 0x08000000); // 128 MB default
    ~GuestMemoryMap() = default;

    [[nodiscard]] std::uint64_t ram_base() const noexcept { return ram_base_; }
    [[nodiscard]] std::uint64_t ram_size() const noexcept { return ram_size_; }
    [[nodiscard]] std::uint64_t ram_end() const noexcept { return ram_base_ + ram_size_; }

    void set_ram(std::uint64_t ram_base, std::uint64_t ram_size) noexcept;

    // Region registration
    bool add_region(const GuestMemoryRegion& region);
    void clear_regions() noexcept;

    // Explicit placement helpers
    bool register_kernel_image(std::uint64_t load_address, std::uint64_t image_size);
    bool register_dtb(std::uint64_t dtb_address, std::uint64_t dtb_size);
    bool register_initramfs(std::uint64_t initramfs_address, std::uint64_t initramfs_size);
    bool register_reserved(const std::string& name, std::uint64_t base, std::uint64_t size);

    // Automated layout calculator
    [[nodiscard]] static bool compute_safe_layout(
        std::uint64_t ram_base,
        std::uint64_t ram_size,
        std::uint64_t kernel_text_offset,
        std::uint64_t kernel_image_size,
        std::uint64_t dtb_size,
        std::uint64_t initramfs_size,
        GuestMemoryMap& out_map,
        std::string& out_error);

    // Validation
    [[nodiscard]] MemoryMapValidationResult validate(std::string* out_message = nullptr) const;

    // Query APIs
    [[nodiscard]] const std::vector<GuestMemoryRegion>& regions() const noexcept { return regions_; }
    [[nodiscard]] const GuestMemoryRegion* find_region(GuestMemoryRegionType type) const noexcept;
    [[nodiscard]] const GuestMemoryRegion* find_region_by_name(std::string_view name) const noexcept;

    // Available memory spans for OS allocator
    struct MemorySpan {
        std::uint64_t start;
        std::uint64_t size;
    };
    [[nodiscard]] std::vector<MemorySpan> compute_available_spans() const;

    // Diagnostics
    [[nodiscard]] std::string dump_memory_map() const;

private:
    std::uint64_t ram_base_{0x40000000ULL};
    std::uint64_t ram_size_{0x08000000ULL};
    std::vector<GuestMemoryRegion> regions_;
};

} // namespace winios::loader
