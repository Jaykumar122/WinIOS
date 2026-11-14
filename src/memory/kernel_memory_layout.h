#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::memory {

// -----------------------------------------------------------------------------
// Canonical ARM64 / Darwin Synthetic Kernel Virtual Memory Layout Constants
// Granule: 16 KB (Apple A16 Bionic / iOS architectural default)
// -----------------------------------------------------------------------------
inline constexpr std::uint64_t kPageGranule16KB      = 16384ULL;
inline constexpr std::uint64_t kGuardPageSize        = 16384ULL;

// Top-Half Virtual Address Space (TTBR1_EL1)
inline constexpr std::uint64_t kKernelVirtualBase    = 0xFFFF800000000000ULL;
inline constexpr std::uint64_t kKernelVirtualEnd     = 0xFFFFFFFFFFFFFFFFULL;

// Kernel Image Virtual Addresses
inline constexpr std::uint64_t kKernelTextBase       = 0xFFFF800001000000ULL;
inline constexpr std::uint64_t kKernelDataBase       = 0xFFFF800002000000ULL;

// Kernel Dynamic Allocator (Heap)
inline constexpr std::uint64_t kKernelHeapBase       = 0xFFFF800004000000ULL;
inline constexpr std::uint64_t kKernelHeapSize       = 0x10000000ULL; // 256 MB

// Kernel Stack Window (Per-CPU / Per-Thread with Guard Pages)
inline constexpr std::uint64_t kKernelStackBase      = 0xFFFF800020000000ULL;
inline constexpr std::uint64_t kKernelStackSize      = 0x00040000ULL; // 256 KB total stack pool
inline constexpr std::uint64_t kThreadStackSize      = 0x00010000ULL; // 64 KB per thread

// Memory-Mapped I/O Window
inline constexpr std::uint64_t kKernelMmioBase       = 0xFFFF800030000000ULL;
inline constexpr std::uint64_t kKernelMmioSize       = 0x10000000ULL; // 256 MB

// Linear Direct Physical Mapping Window (PhysMap)
inline constexpr std::uint64_t kKernelDirectMapBase  = 0xFFFF800040000000ULL;
inline constexpr std::uint64_t kKernelDirectMapSize  = 0x80000000ULL; // 2 GB

// Lower-Half Virtual Address Space (TTBR0_EL1) Userspace Bounds
inline constexpr std::uint64_t kUserVirtualBase      = 0x0000000000000000ULL;
inline constexpr std::uint64_t kUserVirtualEnd       = 0x00007FFFFFFFFFFFULL;

// Physical Memory Defaults
inline constexpr std::uint64_t kDefaultPhysicalDramBase = 0x40000000ULL;
inline constexpr std::uint64_t kDefaultPhysicalDramSize = 0x10000000ULL; // 256 MB
inline constexpr std::uint64_t kDefaultMmioConsoleBase  = 0x00090000ULL;
inline constexpr std::uint64_t kDefaultMmioConsoleSize  = 0x00001000ULL;

// -----------------------------------------------------------------------------
// Kernel Memory Region Descriptor
// -----------------------------------------------------------------------------
enum class MemoryRegionKind {
    KernelText,
    KernelData,
    KernelHeap,
    KernelStack,
    GuardPage,
    MmioDevice,
    DirectPhysicalMap,
    Userspace
};

[[nodiscard]] const char* memory_region_kind_string(MemoryRegionKind kind) noexcept;

struct KernelMemoryRegion {
    std::string name;
    MemoryRegionKind kind;
    std::uint64_t virtual_address;
    std::uint64_t size;
    std::uint64_t physical_address;
    bool is_readable{true};
    bool is_writable{false};
    bool is_executable{false};
    bool is_guard{false};

    [[nodiscard]] std::uint64_t end_address() const noexcept {
        return virtual_address + size;
    }

    [[nodiscard]] bool contains(std::uint64_t va) const noexcept {
        return va >= virtual_address && va < end_address();
    }

    [[nodiscard]] bool overlaps_with(const KernelMemoryRegion& other) const noexcept {
        if (size == 0 || other.size == 0) return false;
        return virtual_address < other.end_address() && other.virtual_address < end_address();
    }
};

// -----------------------------------------------------------------------------
// KernelMemoryLayout: Layout Manager & Verification
// -----------------------------------------------------------------------------
class KernelMemoryLayout {
public:
    explicit KernelMemoryLayout(
        std::uint64_t physical_dram_base = kDefaultPhysicalDramBase,
        std::uint64_t physical_dram_size = kDefaultPhysicalDramSize);
    ~KernelMemoryLayout() = default;

    [[nodiscard]] std::uint64_t physical_dram_base() const noexcept { return phys_dram_base_; }
    [[nodiscard]] std::uint64_t physical_dram_size() const noexcept { return phys_dram_size_; }

    [[nodiscard]] static bool is_kernel_address(std::uint64_t va) noexcept {
        return va >= kKernelVirtualBase;
    }

    [[nodiscard]] static bool is_user_address(std::uint64_t va) noexcept {
        return va <= kUserVirtualEnd;
    }

    [[nodiscard]] static bool is_heap_address(std::uint64_t va) noexcept {
        return va >= kKernelHeapBase && va < (kKernelHeapBase + kKernelHeapSize);
    }

    [[nodiscard]] static bool is_stack_address(std::uint64_t va) noexcept {
        return va >= kKernelStackBase && va < (kKernelStackBase + kKernelStackSize);
    }

    [[nodiscard]] static bool is_mmio_address(std::uint64_t va) noexcept {
        return va >= kKernelMmioBase && va < (kKernelMmioBase + kKernelMmioSize);
    }

    [[nodiscard]] static bool is_direct_map_address(std::uint64_t va) noexcept {
        return va >= kKernelDirectMapBase && va < (kKernelDirectMapBase + kKernelDirectMapSize);
    }

    [[nodiscard]] std::uint64_t phys_to_virt(std::uint64_t pa) const noexcept;
    [[nodiscard]] std::uint64_t virt_to_phys(std::uint64_t va) const noexcept;

    [[nodiscard]] const std::vector<KernelMemoryRegion>& regions() const noexcept {
        return regions_;
    }

    [[nodiscard]] const KernelMemoryRegion* find_region(std::uint64_t va) const noexcept;
    [[nodiscard]] bool is_guard_page(std::uint64_t va) const noexcept;

    [[nodiscard]] bool validate_layout(std::string* out_error = nullptr) const;

private:
    std::uint64_t phys_dram_base_;
    std::uint64_t phys_dram_size_;
    std::vector<KernelMemoryRegion> regions_;

    void initialize_canonical_regions();
};

} // namespace winios::memory
