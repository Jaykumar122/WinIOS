#include "memory/apple_physical_memory_map.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::memory {

ApplePhysicalMemoryMap::ApplePhysicalMemoryMap(std::uint64_t ram_base, std::uint64_t ram_size)
    : ram_base_(ram_base), ram_size_(ram_size) {
    setup_iphone15_default_layout();
}

void ApplePhysicalMemoryMap::setup_iphone15_default_layout() {
    regions_.clear();
    mmio_handlers_.clear();

    // 1. Sub-1GB MMIO Apertures (Apple Silicon iPhone 15 / T8120)
    // AIC 2 Interrupt Controller: 0x08000000, 1 MB
    AppleMemoryRegion aic;
    aic.name = "AppleAIC2_InterruptController";
    aic.kind = AppleMemoryRegionKind::MmioInterruptCtrl;
    aic.phys_base = 0x08000000ULL;
    aic.size = 0x00100000ULL; // 1 MB
    aic.is_mmio = true;
    regions_.push_back(aic);

    // Apple Serial UART: 0x09000000, 4 KB
    AppleMemoryRegion uart;
    uart.name = "AppleSerialUART";
    uart.kind = AppleMemoryRegionKind::MmioSerialUart;
    uart.phys_base = 0x09000000ULL;
    uart.size = 0x00001000ULL; // 4 KB
    uart.is_mmio = true;
    regions_.push_back(uart);

    // Apple PMGR Power Manager: 0x10000000, 1 MB
    AppleMemoryRegion pmgr;
    pmgr.name = "ApplePMGR_PowerManager";
    pmgr.kind = AppleMemoryRegionKind::MmioPowerManager;
    pmgr.phys_base = 0x10000000ULL;
    pmgr.size = 0x00100000ULL; // 1 MB
    pmgr.is_mmio = true;
    regions_.push_back(pmgr);

    // Apple DCP Display Controller: 0x20000000, 1 MB
    AppleMemoryRegion dcp;
    dcp.name = "AppleDCP_DisplayController";
    dcp.kind = AppleMemoryRegionKind::MmioDisplayCtrl;
    dcp.phys_base = 0x20000000ULL;
    dcp.size = 0x00100000ULL; // 1 MB
    dcp.is_mmio = true;
    regions_.push_back(dcp);

    // 2. Physical DRAM Layout (Starting at ram_base_, typically 0x40000000)
    // Kernel Mach-O Staging Area: 64 MB
    AppleMemoryRegion kernel;
    kernel.name = "KernelCache_MachO_Staging";
    kernel.kind = AppleMemoryRegionKind::DramKernel;
    kernel.phys_base = ram_base_;
    kernel.size = 0x04000000ULL; // 64 MB
    regions_.push_back(kernel);

    // Apple DeviceTree (ADTS): 0x48000000, 1 MB
    AppleMemoryRegion dt;
    dt.name = "DeviceTree_ADTS";
    dt.kind = AppleMemoryRegionKind::DramDeviceTree;
    dt.phys_base = 0x48000000ULL;
    dt.size = 0x00100000ULL; // 1 MB
    regions_.push_back(dt);

    // Boot Ramdisk Staging: 0x4A000000, 64 MB
    AppleMemoryRegion rd;
    rd.name = "BootRamdisk_Staging";
    rd.kind = AppleMemoryRegionKind::DramRamdisk;
    rd.phys_base = 0x4A000000ULL;
    rd.size = 0x04000000ULL; // 64 MB
    regions_.push_back(rd);

    // Super Retina XDR Framebuffer: 0x4F000000, 12,054,096 bytes (1179 x 2556 x 4)
    AppleMemoryRegion fb;
    fb.name = "SuperRetinaXDR_Framebuffer";
    fb.kind = AppleMemoryRegionKind::DramFramebuffer;
    fb.phys_base = 0x4F000000ULL;
    fb.size = 1179ULL * 2556ULL * 4ULL; // 12,054,096 bytes
    regions_.push_back(fb);

    // Available General DRAM
    if (ram_base_ + ram_size_ > 0x50000000ULL) {
        AppleMemoryRegion avail;
        avail.name = "GeneralAvailable_DRAM";
        avail.kind = AppleMemoryRegionKind::DramAvailable;
        avail.phys_base = 0x50000000ULL;
        avail.size = (ram_base_ + ram_size_) - 0x50000000ULL;
        regions_.push_back(avail);
    }
}

bool ApplePhysicalMemoryMap::is_ram(std::uint64_t phys_addr, std::size_t len) const noexcept {
    if (len == 0) return false;
    return phys_addr >= ram_base_ && (phys_addr + len) <= (ram_base_ + ram_size_);
}

bool ApplePhysicalMemoryMap::is_mmio(std::uint64_t phys_addr, std::size_t len) const noexcept {
    if (len == 0) return false;
    for (const auto& reg : regions_) {
        if (reg.is_mmio && reg.contains(phys_addr, len)) {
            return true;
        }
    }
    for (const auto& h : mmio_handlers_) {
        if (phys_addr >= h.base && (phys_addr + len) <= (h.base + h.size)) {
            return true;
        }
    }
    return false;
}

bool ApplePhysicalMemoryMap::translate_ram_address(
    std::uint64_t phys_addr,
    std::size_t len,
    std::size_t& out_offset) const noexcept {
    if (!is_ram(phys_addr, len)) {
        return false;
    }
    out_offset = static_cast<std::size_t>(phys_addr - ram_base_);
    return true;
}

bool ApplePhysicalMemoryMap::add_region(const AppleMemoryRegion& region, std::string* out_error) {
    if (region.size == 0) {
        if (out_error) *out_error = "Region size cannot be zero";
        return false;
    }

    // Check overlaps
    for (const auto& existing : regions_) {
        if (region.overlaps_with(existing)) {
            if (out_error) {
                *out_error = "New region '" + region.name + "' overlaps with existing region '" + existing.name + "'";
            }
            return false;
        }
    }

    // Check bounds
    if (!region.is_mmio) {
        if (region.phys_base < ram_base_ || region.end_address() > (ram_base_ + ram_size_)) {
            if (out_error) {
                *out_error = "DRAM region '" + region.name + "' exceeds configured RAM boundaries";
            }
            return false;
        }
    }

    regions_.push_back(region);
    return true;
}

const AppleMemoryRegion* ApplePhysicalMemoryMap::find_region(std::uint64_t phys_addr) const noexcept {
    for (const auto& reg : regions_) {
        if (reg.contains(phys_addr, 1)) {
            return &reg;
        }
    }
    return nullptr;
}

bool ApplePhysicalMemoryMap::register_mmio_device(
    const std::string& name,
    std::uint64_t base,
    std::uint64_t size,
    MmioReadCallback read_cb,
    MmioWriteCallback write_cb,
    std::string* out_error) {
    if (size == 0) {
        if (out_error) *out_error = "MMIO aperture size cannot be zero";
        return false;
    }

    if (base >= ram_base_ || (base + size) > ram_base_) {
        if (out_error) *out_error = "MMIO device must reside in sub-1GB IO space (< 0x" + std::to_string(ram_base_) + ")";
        return false;
    }

    for (const auto& h : mmio_handlers_) {
        if (base < (h.base + h.size) && (base + size) > h.base) {
            if (out_error) *out_error = "MMIO aperture overlaps with existing handler '" + h.name + "'";
            return false;
        }
    }

    AppleMmioHandler handler;
    handler.name = name;
    handler.base = base;
    handler.size = size;
    handler.read_cb = std::move(read_cb);
    handler.write_cb = std::move(write_cb);
    mmio_handlers_.push_back(std::move(handler));

    return true;
}

bool ApplePhysicalMemoryMap::dispatch_read_u32(std::uint64_t phys_addr, std::uint32_t& out_val) const {
    for (const auto& h : mmio_handlers_) {
        if (phys_addr >= h.base && (phys_addr + 4) <= (h.base + h.size)) {
            if (h.read_cb) {
                out_val = h.read_cb(phys_addr - h.base);
                return true;
            }
            out_val = 0;
            return true;
        }
    }
    return false;
}

bool ApplePhysicalMemoryMap::dispatch_write_u32(std::uint64_t phys_addr, std::uint32_t val) {
    for (auto& h : mmio_handlers_) {
        if (phys_addr >= h.base && (phys_addr + 4) <= (h.base + h.size)) {
            if (h.write_cb) {
                h.write_cb(phys_addr - h.base, val);
            }
            return true;
        }
    }
    return false;
}

bool ApplePhysicalMemoryMap::validate_layout(std::string& out_error) const {
    if (ram_base_ % 16384ULL != 0) {
        out_error = "RAM base is not aligned to 16 KB page granule";
        return false;
    }

    if (ram_size_ < 256ULL * 1024 * 1024) {
        out_error = "RAM size is less than 256 MB minimum";
        return false;
    }

    // Check for any internal overlaps among all regions
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        for (std::size_t j = i + 1; j < regions_.size(); ++j) {
            if (regions_[i].overlaps_with(regions_[j])) {
                out_error = "Region overlap detected between '" + regions_[i].name + "' and '" + regions_[j].name + "'";
                return false;
            }
        }
    }

    return true;
}

bool ApplePhysicalMemoryMap::validate_against_profile(
    const device::AppleHardwareProfile& profile,
    std::string& out_error) const {
    if (!validate_layout(out_error)) {
        return false;
    }

    // Verify MMIO peripherals from profile
    for (const auto& p : profile.peripherals) {
        bool found = false;
        for (const auto& r : regions_) {
            if (r.phys_base == p.base_address && r.size >= p.size) {
                found = true;
                break;
            }
        }
        if (!found) {
            out_error = "Hardware profile peripheral '" + p.name + "' (base 0x" +
                        std::to_string(p.base_address) + ") not mapped in physical memory map";
            return false;
        }
    }

    return true;
}

const char* ApplePhysicalMemoryMap::region_kind_to_string(AppleMemoryRegionKind kind) noexcept {
    switch (kind) {
        case AppleMemoryRegionKind::DramKernel:        return "DRAM (Kernel Mach-O)";
        case AppleMemoryRegionKind::DramDeviceTree:    return "DRAM (Apple DeviceTree)";
        case AppleMemoryRegionKind::DramRamdisk:       return "DRAM (Boot Ramdisk)";
        case AppleMemoryRegionKind::DramFramebuffer:   return "DRAM (Display Framebuffer)";
        case AppleMemoryRegionKind::DramAvailable:     return "DRAM (Available Memory)";
        case AppleMemoryRegionKind::MmioInterruptCtrl: return "MMIO (Apple AIC 2)";
        case AppleMemoryRegionKind::MmioSerialUart:    return "MMIO (Apple Serial UART)";
        case AppleMemoryRegionKind::MmioPowerManager:  return "MMIO (Apple PMGR)";
        case AppleMemoryRegionKind::MmioDisplayCtrl:   return "MMIO (Apple DCP Display)";
        case AppleMemoryRegionKind::MmioReserved:      return "MMIO (Reserved)";
    }
    return "Unknown";
}

std::string ApplePhysicalMemoryMap::generate_physical_memory_report() const {
    std::stringstream ss;
    ss << "# iPhone 15 Unified Physical Address Space Map\n\n";
    ss << "| Region Name | Kind | Physical Range (Start - End) | Size (Bytes / MB) | Type |\n";
    ss << "| :--- | :--- | :--- | :--- | :--- |\n";

    for (const auto& reg : regions_) {
        double mb = static_cast<double>(reg.size) / (1024.0 * 1024.0);
        ss << "| `" << reg.name << "` | `" << region_kind_to_string(reg.kind)
           << "` | `0x" << std::hex << reg.phys_base << " - 0x" << reg.end_address() << std::dec
           << "` | " << reg.size << " (" << std::fixed << std::setprecision(2) << mb << " MB) | "
           << (reg.is_mmio ? "**MMIO**" : "**DRAM**") << " |\n";
    }

    return ss.str();
}

} // namespace winios::memory
