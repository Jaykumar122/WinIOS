#include "memory/kernel_memory_layout.h"

#include <algorithm>
#include "loader/kernel_image_parser.h"

namespace winios::memory {

const char* memory_region_kind_string(MemoryRegionKind kind) noexcept {
    switch (kind) {
        case MemoryRegionKind::KernelText:        return "KernelText";
        case MemoryRegionKind::ReadOnlyData:      return "ReadOnlyData";
        case MemoryRegionKind::WritableData:      return "WritableData";
        case MemoryRegionKind::ZeroInitData:      return "ZeroInitData";
        case MemoryRegionKind::BootInfo:          return "BootInfo";
        case MemoryRegionKind::DeviceTree:        return "DeviceTree";
        case MemoryRegionKind::PageTables:        return "PageTables";
        case MemoryRegionKind::KernelHeap:        return "KernelHeap";
        case MemoryRegionKind::KernelStack:       return "KernelStack";
        case MemoryRegionKind::GuardPage:         return "GuardPage";
        case MemoryRegionKind::Reserved:          return "Reserved";
        case MemoryRegionKind::MmioDevice:        return "MmioDevice";
        case MemoryRegionKind::DirectPhysicalMap: return "DirectPhysicalMap";
        case MemoryRegionKind::Userspace:         return "Userspace";
        case MemoryRegionKind::KernelData:        return "KernelData";
    }
    return "Unknown";
}

KernelMemoryLayout::KernelMemoryLayout(
    std::uint64_t physical_dram_base,
    std::uint64_t physical_dram_size)
    : phys_dram_base_(physical_dram_base),
      phys_dram_size_(physical_dram_size) {
    initialize_canonical_regions();
}

void KernelMemoryLayout::initialize_canonical_regions() {
    regions_.clear();

    // 1. Kernel __TEXT Segment: RX (Read + Execute)
    regions_.push_back(KernelMemoryRegion{
        .name = "KernelText",
        .kind = MemoryRegionKind::KernelText,
        .virtual_address = kKernelTextBase,
        .size = 0x01000000ULL, // 16 MB
        .physical_address = phys_dram_base_ + 0x01000000ULL,
        .is_readable = true,
        .is_writable = false,
        .is_executable = true,
        .is_guard = false
    });

    // 2. Kernel __DATA Segment: RW (Read + Write)
    regions_.push_back(KernelMemoryRegion{
        .name = "KernelData",
        .kind = MemoryRegionKind::KernelData,
        .virtual_address = kKernelDataBase,
        .size = 0x01000000ULL, // 16 MB
        .physical_address = phys_dram_base_ + 0x02000000ULL,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    });

    // 3. Guard Page before Kernel Heap
    regions_.push_back(KernelMemoryRegion{
        .name = "HeapPreGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = kKernelHeapBase - kGuardPageSize,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    });

    // 4. Kernel Dynamic Heap: RW
    regions_.push_back(KernelMemoryRegion{
        .name = "KernelHeap",
        .kind = MemoryRegionKind::KernelHeap,
        .virtual_address = kKernelHeapBase,
        .size = kKernelHeapSize,
        .physical_address = phys_dram_base_ + 0x04000000ULL,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    });

    // 5. Guard Page after Kernel Heap
    regions_.push_back(KernelMemoryRegion{
        .name = "HeapPostGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = kKernelHeapBase + kKernelHeapSize,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    });

    // 6. Guard Page before Kernel Stack
    regions_.push_back(KernelMemoryRegion{
        .name = "StackPreGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = kKernelStackBase - kGuardPageSize,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    });

    // 7. Kernel Stack Pool: RW
    regions_.push_back(KernelMemoryRegion{
        .name = "KernelStack",
        .kind = MemoryRegionKind::KernelStack,
        .virtual_address = kKernelStackBase,
        .size = kKernelStackSize,
        .physical_address = phys_dram_base_ + 0x08000000ULL,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    });

    // 8. Guard Page after Kernel Stack
    regions_.push_back(KernelMemoryRegion{
        .name = "StackPostGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = kKernelStackBase + kKernelStackSize,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    });

    // 9. MMIO Virtual Window: RW (Device Memory)
    regions_.push_back(KernelMemoryRegion{
        .name = "MmioDevices",
        .kind = MemoryRegionKind::MmioDevice,
        .virtual_address = kKernelMmioBase,
        .size = kKernelMmioSize,
        .physical_address = 0,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    });

    // 10. Direct Physical RAM Map (PhysMap): RW
    regions_.push_back(KernelMemoryRegion{
        .name = "DirectPhysicalMap",
        .kind = MemoryRegionKind::DirectPhysicalMap,
        .virtual_address = kKernelDirectMapBase,
        .size = std::min(kKernelDirectMapSize, phys_dram_size_),
        .physical_address = phys_dram_base_,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    });
}

std::uint64_t KernelMemoryLayout::phys_to_virt(std::uint64_t pa) const noexcept {
    if (pa >= phys_dram_base_ && pa < (phys_dram_base_ + phys_dram_size_)) {
        return kKernelDirectMapBase + (pa - phys_dram_base_);
    }
    return 0;
}

std::uint64_t KernelMemoryLayout::virt_to_phys(std::uint64_t va) const noexcept {
    if (is_direct_map_address(va)) {
        const std::uint64_t offset = va - kKernelDirectMapBase;
        if (offset < phys_dram_size_) {
            return phys_dram_base_ + offset;
        }
    }
    return 0;
}

const KernelMemoryRegion* KernelMemoryLayout::find_region(std::uint64_t va) const noexcept {
    for (const auto& r : regions_) {
        if (r.contains(va)) {
            return &r;
        }
    }
    return nullptr;
}

bool KernelMemoryLayout::is_guard_page(std::uint64_t va) const noexcept {
    const auto* r = find_region(va);
    return (r != nullptr && r->is_guard);
}

bool KernelMemoryLayout::validate_layout(std::string* out_error) const {
    if ((kPageGranule16KB & (kPageGranule16KB - 1)) != 0) {
        if (out_error != nullptr) *out_error = "16KB granule must be a power of two";
        return false;
    }

    for (std::size_t i = 0; i < regions_.size(); ++i) {
        const auto& r1 = regions_[i];

        // Check alignment
        if ((r1.virtual_address % kPageGranule16KB) != 0) {
            if (out_error != nullptr) {
                *out_error = "Region " + r1.name + " virtual address is not 16KB aligned";
            }
            return false;
        }
        if ((r1.size % kPageGranule16KB) != 0) {
            if (out_error != nullptr) {
                *out_error = "Region " + r1.name + " size is not a multiple of 16KB";
            }
            return false;
        }

        // Guard pages must have zero permissions
        if (r1.is_guard) {
            if (r1.is_readable || r1.is_writable || r1.is_executable) {
                if (out_error != nullptr) {
                    *out_error = "Guard region " + r1.name + " has non-zero permissions";
                }
                return false;
            }
        }

        // Check for pairwise overlaps
        for (std::size_t j = i + 1; j < regions_.size(); ++j) {
            const auto& r2 = regions_[j];
            if (r1.overlaps_with(r2)) {
                if (out_error != nullptr) {
                    *out_error = "Region overlap detected between " + r1.name + " and " + r2.name;
                }
                return false;
            }
        }
    }

    return true;
}

const KernelMemoryRegion* KernelMemoryLayout::find_region_by_kind(MemoryRegionKind kind) const noexcept {
    for (const auto& r : regions_) {
        if (r.kind == kind) {
            return &r;
        }
    }
    return nullptr;
}

bool KernelMemoryLayout::add_region(const KernelMemoryRegion& region, std::string* out_error) {
    if (region.size == 0) {
        if (out_error != nullptr) *out_error = "Cannot add zero-sized region: " + region.name;
        return false;
    }
    if ((region.virtual_address % kPageGranule16KB) != 0) {
        if (out_error != nullptr) *out_error = "Region " + region.name + " virtual address is not 16KB aligned";
        return false;
    }
    if (region.is_writable && region.is_executable) {
        if (out_error != nullptr) {
            *out_error = "W^X violation in region " + region.name + ": simultaneously writable and executable";
        }
        return false;
    }
    for (const auto& r : regions_) {
        if (r.overlaps_with(region)) {
            if (out_error != nullptr) {
                *out_error = "Region " + region.name + " overlaps with existing region " + r.name;
            }
            return false;
        }
    }
    regions_.push_back(region);
    return true;
}

bool KernelMemoryLayout::check_wx_policy(std::string* out_error) const {
    for (const auto& r : regions_) {
        if (r.is_writable && r.is_executable) {
            if (out_error != nullptr) {
                *out_error = "W^X policy violation in region: " + r.name;
            }
            return false;
        }
    }
    return true;
}

bool KernelMemoryLayout::build_from_parsed_image(
    const loader::KernelParsedImage& image,
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    std::uint64_t boot_args_size,
    std::uint64_t dt_size,
    std::string* out_error) {

    clear_regions();
    phys_dram_base_ = ram_base;
    phys_dram_size_ = ram_size;

    std::uint64_t curr_phys = ram_base;

    // 1. Process all segments from parsed kernel image
    for (const auto& seg : image.segments) {
        if (seg.vmsize == 0) continue;

        MemoryRegionKind kind = MemoryRegionKind::KernelData;
        if (seg.name == "__TEXT") {
            kind = MemoryRegionKind::KernelText;
        } else if (seg.name == "__DATA_CONST") {
            kind = MemoryRegionKind::ReadOnlyData;
        } else if (seg.name == "__DATA") {
            kind = MemoryRegionKind::WritableData;
        } else if (seg.name == "__BSS" || seg.filesize == 0) {
            kind = MemoryRegionKind::ZeroInitData;
        }

        bool is_exec = (seg.initprot & 4) != 0;
        bool is_write = (seg.initprot & 2) != 0;
        bool is_read = (seg.initprot & 1) != 0;

        // Strict W^X enforcement
        if (is_exec && is_write) {
            is_write = false;
        }

        std::uint64_t aligned_size = (seg.vmsize + kPageGranule16KB - 1) & ~(kPageGranule16KB - 1);

        KernelMemoryRegion reg{
            .name = seg.name,
            .kind = kind,
            .virtual_address = seg.vmaddr,
            .size = aligned_size,
            .physical_address = curr_phys,
            .is_readable = is_read,
            .is_writable = is_write,
            .is_executable = is_exec,
            .is_guard = false
        };

        if (!add_region(reg, out_error)) {
            return false;
        }

        curr_phys += aligned_size;
    }

    // 2. Boot Arguments Region
    if (boot_args_size > 0) {
        std::uint64_t aligned_boot = (boot_args_size + kPageGranule16KB - 1) & ~(kPageGranule16KB - 1);
        std::uint64_t boot_va = kKernelVirtualBase + (curr_phys - ram_base);
        KernelMemoryRegion boot_reg{
            .name = "BootArgs",
            .kind = MemoryRegionKind::BootInfo,
            .virtual_address = boot_va,
            .size = aligned_boot,
            .physical_address = curr_phys,
            .is_readable = true,
            .is_writable = false,
            .is_executable = false,
            .is_guard = false
        };
        if (!add_region(boot_reg, out_error)) return false;
        curr_phys += aligned_boot;
    }

    // 3. Device Tree Region
    if (dt_size > 0) {
        std::uint64_t aligned_dt = (dt_size + kPageGranule16KB - 1) & ~(kPageGranule16KB - 1);
        std::uint64_t dt_va = kKernelVirtualBase + (curr_phys - ram_base);
        KernelMemoryRegion dt_reg{
            .name = "DeviceTree",
            .kind = MemoryRegionKind::DeviceTree,
            .virtual_address = dt_va,
            .size = aligned_dt,
            .physical_address = curr_phys,
            .is_readable = true,
            .is_writable = false,
            .is_executable = false,
            .is_guard = false
        };
        if (!add_region(dt_reg, out_error)) return false;
        curr_phys += aligned_dt;
    }

    // 4. Initial Bootstrap Page Tables (64 KB = 4 x 16KB pages)
    constexpr std::uint64_t kPageTablesSize = 0x10000ULL;
    std::uint64_t pt_va = kKernelVirtualBase + (curr_phys - ram_base);
    KernelMemoryRegion pt_reg{
        .name = "PageTables",
        .kind = MemoryRegionKind::PageTables,
        .virtual_address = pt_va,
        .size = kPageTablesSize,
        .physical_address = curr_phys,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    };
    if (!add_region(pt_reg, out_error)) return false;
    curr_phys += kPageTablesSize;

    // 5. Initial Kernel Stack (64 KB + guard pages)
    std::uint64_t pre_guard_va = kKernelStackBase - kGuardPageSize;
    KernelMemoryRegion stack_pre_guard{
        .name = "StackPreGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = pre_guard_va,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    };
    if (!add_region(stack_pre_guard, out_error)) return false;

    constexpr std::uint64_t kStackSize = 0x10000ULL;
    KernelMemoryRegion stack_reg{
        .name = "InitialKernelStack",
        .kind = MemoryRegionKind::KernelStack,
        .virtual_address = kKernelStackBase,
        .size = kStackSize,
        .physical_address = curr_phys,
        .is_readable = true,
        .is_writable = true,
        .is_executable = false,
        .is_guard = false
    };
    if (!add_region(stack_reg, out_error)) return false;
    curr_phys += kStackSize;

    KernelMemoryRegion stack_post_guard{
        .name = "StackPostGuard",
        .kind = MemoryRegionKind::GuardPage,
        .virtual_address = kKernelStackBase + kStackSize,
        .size = kGuardPageSize,
        .physical_address = 0,
        .is_readable = false,
        .is_writable = false,
        .is_executable = false,
        .is_guard = true
    };
    if (!add_region(stack_post_guard, out_error)) return false;

    return validate_layout(out_error);
}

} // namespace winios::memory
