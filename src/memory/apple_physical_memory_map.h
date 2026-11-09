#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "device/apple_hardware_profile.h"
#include "memory/memory.h"

namespace winios::memory {

// -----------------------------------------------------------------------------
// AppleMemoryRegionKind: Classification of Apple Silicon physical regions
// -----------------------------------------------------------------------------
enum class AppleMemoryRegionKind {
    DramKernel,         // Staged 64-bit Mach-O kernelcache
    DramDeviceTree,     // Staged Apple DeviceTree (ADTS)
    DramRamdisk,        // Staged boot ramdisk
    DramFramebuffer,    // Super Retina XDR video display buffer (1179x2556 ARGB)
    DramAvailable,      // General guest RAM (page tables, heap, stacks)
    MmioInterruptCtrl,  // Apple AIC 2 (0x08000000)
    MmioSerialUart,     // Apple Serial UART (0x09000000)
    MmioPowerManager,   // Apple PMGR (0x10000000)
    MmioDisplayCtrl,    // Apple DCP / Display Controller (0x20000000)
    MmioReserved        // Reserved sub-1GB IO aperture
};

// -----------------------------------------------------------------------------
// AppleMemoryRegion: A contiguous address span in physical memory
// -----------------------------------------------------------------------------
struct AppleMemoryRegion {
    std::string name;
    AppleMemoryRegionKind kind{AppleMemoryRegionKind::DramAvailable};
    std::uint64_t phys_base{0};
    std::uint64_t size{0};
    bool is_mmio{false};
    bool is_writable{true};

    [[nodiscard]] std::uint64_t end_address() const noexcept {
        return phys_base + size;
    }

    [[nodiscard]] bool contains(std::uint64_t addr, std::size_t access_len = 1) const noexcept {
        if (size == 0) return false;
        return addr >= phys_base && (addr + access_len) <= end_address();
    }

    [[nodiscard]] bool overlaps_with(const AppleMemoryRegion& other) const noexcept {
        if (size == 0 || other.size == 0) return false;
        return phys_base < other.end_address() && other.phys_base < end_address();
    }
};

// -----------------------------------------------------------------------------
// MmioReadCallback / MmioWriteCallback: Handlers for MMIO apertures
// -----------------------------------------------------------------------------
using MmioReadCallback  = std::function<std::uint32_t(std::uint64_t offset)>;
using MmioWriteCallback = std::function<void(std::uint64_t offset, std::uint32_t value)>;

struct AppleMmioHandler {
    std::string name;
    std::uint64_t base{0};
    std::uint64_t size{0};
    MmioReadCallback read_cb;
    MmioWriteCallback write_cb;
};

// -----------------------------------------------------------------------------
// ApplePhysicalMemoryMap: Unified physical address space for iPhone 15
// -----------------------------------------------------------------------------
class ApplePhysicalMemoryMap {
public:
    // Default constructor instantiates the canonical iPhone 15 A16 Bionic layout
    explicit ApplePhysicalMemoryMap(
        std::uint64_t ram_base = 0x40000000ULL,
        std::uint64_t ram_size = 6ULL * 1024 * 1024 * 1024);
    ~ApplePhysicalMemoryMap() = default;

    // Physical bounds
    [[nodiscard]] std::uint64_t ram_base() const noexcept { return ram_base_; }
    [[nodiscard]] std::uint64_t ram_size() const noexcept { return ram_size_; }
    [[nodiscard]] std::uint64_t ram_end() const noexcept { return ram_base_ + ram_size_; }

    // Address classification
    [[nodiscard]] bool is_ram(std::uint64_t phys_addr, std::size_t len = 1) const noexcept;
    [[nodiscard]] bool is_mmio(std::uint64_t phys_addr, std::size_t len = 1) const noexcept;

    // Translation from physical address to backing Memory buffer offset
    [[nodiscard]] bool translate_ram_address(
        std::uint64_t phys_addr,
        std::size_t len,
        std::size_t& out_offset) const noexcept;

    // Region management
    bool add_region(const AppleMemoryRegion& region, std::string* out_error = nullptr);
    [[nodiscard]] const AppleMemoryRegion* find_region(std::uint64_t phys_addr) const noexcept;
    [[nodiscard]] const std::vector<AppleMemoryRegion>& all_regions() const noexcept { return regions_; }

    // MMIO Handlers
    bool register_mmio_device(
        const std::string& name,
        std::uint64_t base,
        std::uint64_t size,
        MmioReadCallback read_cb,
        MmioWriteCallback write_cb,
        std::string* out_error = nullptr);

    [[nodiscard]] bool dispatch_read_u32(std::uint64_t phys_addr, std::uint32_t& out_val) const;
    [[nodiscard]] bool dispatch_write_u32(std::uint64_t phys_addr, std::uint32_t val);

    // Validation
    [[nodiscard]] bool validate_layout(std::string& out_error) const;
    [[nodiscard]] bool validate_against_profile(
        const device::AppleHardwareProfile& profile,
        std::string& out_error) const;

    // Inspection
    [[nodiscard]] std::string generate_physical_memory_report() const;
    [[nodiscard]] static const char* region_kind_to_string(AppleMemoryRegionKind kind) noexcept;

private:
    void setup_iphone15_default_layout();

    std::uint64_t ram_base_{0x40000000ULL};
    std::uint64_t ram_size_{6ULL * 1024 * 1024 * 1024};
    std::vector<AppleMemoryRegion> regions_;
    std::vector<AppleMmioHandler> mmio_handlers_;
};

} // namespace winios::memory
