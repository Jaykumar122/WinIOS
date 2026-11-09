#include "loader/guest_memory_map.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::loader {

namespace {

constexpr std::uint64_t k2MbAlignment   = 0x200000ULL;
constexpr std::uint64_t k4KbAlignment   = 0x1000ULL;
constexpr std::uint64_t k64KbAlignment  = 0x10000ULL;
constexpr std::uint64_t k8ByteAlignment = 0x8ULL;

inline std::uint64_t align_up(std::uint64_t val, std::uint64_t align) noexcept {
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

inline std::uint64_t align_down(std::uint64_t val, std::uint64_t align) noexcept {
    if (align == 0) return val;
    return val & ~(align - 1);
}

} // anonymous namespace

GuestMemoryMap::GuestMemoryMap(std::uint64_t ram_base, std::size_t ram_size)
    : ram_base_(ram_base), ram_size_(ram_size) {}

void GuestMemoryMap::set_ram(std::uint64_t ram_base, std::uint64_t ram_size) noexcept {
    ram_base_ = ram_base;
    ram_size_ = ram_size;
}

bool GuestMemoryMap::add_region(const GuestMemoryRegion& region) {
    if (region.size == 0) return false;
    regions_.push_back(region);
    return true;
}

void GuestMemoryMap::clear_regions() noexcept {
    regions_.clear();
}

bool GuestMemoryMap::register_kernel_image(std::uint64_t load_address, std::uint64_t image_size) {
    GuestMemoryRegion reg{
        "KernelImage",
        GuestMemoryRegionType::KernelImage,
        load_address,
        image_size,
        true
    };
    return add_region(reg);
}

bool GuestMemoryMap::register_dtb(std::uint64_t dtb_address, std::uint64_t dtb_size) {
    GuestMemoryRegion reg{
        "DeviceTree",
        GuestMemoryRegionType::DeviceTree,
        dtb_address,
        dtb_size,
        true
    };
    return add_region(reg);
}

bool GuestMemoryMap::register_initramfs(std::uint64_t initramfs_address, std::uint64_t initramfs_size) {
    GuestMemoryRegion reg{
        "Initramfs",
        GuestMemoryRegionType::Initramfs,
        initramfs_address,
        initramfs_size,
        true
    };
    return add_region(reg);
}

bool GuestMemoryMap::register_reserved(const std::string& name, std::uint64_t base, std::uint64_t size) {
    GuestMemoryRegion reg{
        name,
        GuestMemoryRegionType::FirmwareReserved,
        base,
        size,
        true
    };
    return add_region(reg);
}

bool GuestMemoryMap::compute_safe_layout(
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    std::uint64_t kernel_text_offset,
    std::uint64_t kernel_image_size,
    std::uint64_t dtb_size,
    std::uint64_t initramfs_size,
    GuestMemoryMap& out_map,
    std::string& out_error) {

    if ((ram_base % k2MbAlignment) != 0) {
        out_error = "RAM base must be aligned to at least 2MB (0x200000)";
        return false;
    }

    if (kernel_image_size == 0) {
        out_error = "Kernel image size cannot be 0";
        return false;
    }

    if (dtb_size == 0) {
        out_error = "DTB size cannot be 0";
        return false;
    }

    out_map.clear_regions();
    out_map.set_ram(ram_base, ram_size);

    // 1. Kernel placement: ram_base + kernel_text_offset
    std::uint64_t kernel_load = ram_base + kernel_text_offset;
    std::uint64_t kernel_end = kernel_load + kernel_image_size;

    if (kernel_end > ram_base + ram_size) {
        out_error = "Kernel image exceeds RAM capacity";
        return false;
    }
    out_map.register_kernel_image(kernel_load, kernel_image_size);

    // 2. DTB placement: placed near top of RAM, 2MB below RAM end or 4KB aligned
    std::uint64_t dtb_aligned_size = align_up(dtb_size, k4KbAlignment);
    if (ram_size < dtb_aligned_size + 0x100000ULL) {
        out_error = "Insufficient RAM for DTB";
        return false;
    }

    // Place DTB 2MB below top of RAM (standard virt pattern)
    std::uint64_t dtb_addr = align_down(ram_base + ram_size - dtb_aligned_size, k64KbAlignment);

    // 3. Initramfs placement (optional)
    if (initramfs_size > 0) {
        // Place initramfs after kernel aligned to 2MB, before DTB
        std::uint64_t initrd_load = align_up(kernel_end, k2MbAlignment);
        std::uint64_t initrd_end = initrd_load + initramfs_size;

        if (initrd_end > dtb_addr) {
            out_error = "Combined Kernel + Initramfs + DTB exceeds RAM capacity";
            return false;
        }

        out_map.register_initramfs(initrd_load, initramfs_size);
    }

    // Register DTB
    if (kernel_end > dtb_addr) {
        out_error = "Kernel image collides with DTB at top of RAM";
        return false;
    }
    out_map.register_dtb(dtb_addr, dtb_size);

    return true;
}

MemoryMapValidationResult GuestMemoryMap::validate(std::string* out_message) const {
    if ((ram_base_ % k2MbAlignment) != 0) {
        if (out_message) *out_message = "RAM base must be 2MB aligned";
        return MemoryMapValidationResult::RamBaseMisaligned;
    }

    const std::uint64_t max_ram = ram_end();

    for (std::size_t i = 0; i < regions_.size(); ++i) {
        const auto& r = regions_[i];

        if (r.size == 0) {
            if (out_message) *out_message = "Region '" + r.name + "' has size 0";
            return MemoryMapValidationResult::ZeroSizeRegion;
        }

        if (r.base_address < ram_base_ || r.end_address() > max_ram) {
            if (out_message) {
                std::ostringstream ss;
                ss << "Region '" << r.name << "' [0x" << std::hex << r.base_address
                   << "..0x" << r.end_address() << "] extends outside RAM [0x"
                   << ram_base_ << "..0x" << max_ram << "]";
                *out_message = ss.str();
            }
            return MemoryMapValidationResult::RegionOutOfBounds;
        }

        // Type-specific alignment checks
        if (r.type == GuestMemoryRegionType::DeviceTree) {
            if ((r.base_address % k8ByteAlignment) != 0) {
                if (out_message) *out_message = "DTB address must be 8-byte aligned";
                return MemoryMapValidationResult::DtbMisaligned;
            }
        } else if (r.type == GuestMemoryRegionType::Initramfs) {
            if ((r.base_address % k4KbAlignment) != 0) {
                if (out_message) *out_message = "Initramfs address must be 4KB page aligned";
                return MemoryMapValidationResult::InitramfsMisaligned;
            }
        }

        // Collision check against other regions
        for (std::size_t j = i + 1; j < regions_.size(); ++j) {
            const auto& other = regions_[j];
            if (r.overlaps_with(other)) {
                if (out_message) {
                    std::ostringstream ss;
                    ss << "Collision between region '" << r.name << "' [0x" << std::hex
                       << r.base_address << "..0x" << r.end_address() << "] and '"
                       << other.name << "' [0x" << other.base_address << "..0x"
                       << other.end_address() << "]";
                    *out_message = ss.str();
                }
                return MemoryMapValidationResult::RegionOverlap;
            }
        }
    }

    if (out_message) *out_message = "Memory map is valid";
    return MemoryMapValidationResult::Valid;
}

const GuestMemoryRegion* GuestMemoryMap::find_region(GuestMemoryRegionType type) const noexcept {
    for (const auto& r : regions_) {
        if (r.type == type) return &r;
    }
    return nullptr;
}

const GuestMemoryRegion* GuestMemoryMap::find_region_by_name(std::string_view name) const noexcept {
    for (const auto& r : regions_) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

std::vector<GuestMemoryMap::MemorySpan> GuestMemoryMap::compute_available_spans() const {
    std::vector<MemorySpan> spans;
    if (ram_size_ == 0) return spans;

    // Sort regions by base address
    std::vector<GuestMemoryRegion> sorted = regions_;
    std::sort(sorted.begin(), sorted.end(), [](const GuestMemoryRegion& a, const GuestMemoryRegion& b) {
        return a.base_address < b.base_address;
    });

    std::uint64_t current = ram_base_;
    for (const auto& r : sorted) {
        if (r.base_address > current) {
            spans.push_back({current, r.base_address - current});
        }
        current = std::max(current, r.end_address());
    }

    if (current < ram_end()) {
        spans.push_back({current, ram_end() - current});
    }

    return spans;
}

std::string GuestMemoryMap::dump_memory_map() const {
    std::ostringstream ss;
    ss << "=== Guest Physical Memory Map ===\n";
    ss << "RAM Base: 0x" << std::hex << ram_base_ << ", Size: 0x" << ram_size_
       << " (" << std::dec << (ram_size_ / (1024 * 1024)) << " MB)\n";

    for (const auto& r : regions_) {
        ss << "  [" << (r.is_reserved ? "RESVD" : "ALLOC") << "] "
           << std::left << std::setw(16) << r.name
           << " 0x" << std::hex << std::setw(10) << r.base_address
           << " - 0x" << std::hex << std::setw(10) << r.end_address()
           << " (size: 0x" << std::hex << r.size << ")\n";
    }

    auto spans = compute_available_spans();
    ss << "Available RAM Spans (" << spans.size() << "):\n";
    for (const auto& s : spans) {
        ss << "  FREE: 0x" << std::hex << s.start << " - 0x" << (s.start + s.size)
           << " (0x" << s.size << " bytes)\n";
    }
    return ss.str();
}

} // namespace winios::loader
