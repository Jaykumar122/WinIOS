#include "memory/kernel_memory_layout.h"

#include <algorithm>

namespace winios::memory {

const char* memory_region_kind_string(MemoryRegionKind kind) noexcept {
    switch (kind) {
        case MemoryRegionKind::KernelText:
            return "KernelText";
        case MemoryRegionKind::KernelData:
            return "KernelData";
        case MemoryRegionKind::KernelHeap:
            return "KernelHeap";
        case MemoryRegionKind::KernelStack:
            return "KernelStack";
        case MemoryRegionKind::GuardPage:
            return "GuardPage";
        case MemoryRegionKind::MmioDevice:
            return "MmioDevice";
        case MemoryRegionKind::DirectPhysicalMap:
            return "DirectPhysicalMap";
        case MemoryRegionKind::Userspace:
            return "Userspace";
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

} // namespace winios::memory
