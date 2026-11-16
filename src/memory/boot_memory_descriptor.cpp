#include "memory/boot_memory_descriptor.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::memory {

const char* boot_memory_type_string(BootMemoryType type) noexcept {
    switch (type) {
        case BootMemoryType::UsableRAM:    return "UsableRAM";
        case BootMemoryType::Reserved:     return "Reserved";
        case BootMemoryType::KernelImage:  return "KernelImage";
        case BootMemoryType::PageTables:   return "PageTables";
        case BootMemoryType::InitialStack: return "InitialStack";
        case BootMemoryType::BootArgs:     return "BootArgs";
        case BootMemoryType::DeviceTree:   return "DeviceTree";
        case BootMemoryType::Framebuffer:  return "Framebuffer";
        case BootMemoryType::MMIO:         return "MMIO";
    }
    return "Unknown";
}

BootMemoryMap::BootMemoryMap(std::uint64_t ram_base, std::uint64_t ram_size)
    : ram_base_(ram_base), ram_size_(ram_size) {}

bool BootMemoryMap::add_range(const BootMemoryRange& range, std::string* out_error) {
    if (range.size == 0) {
        if (out_error) *out_error = "Cannot add zero-sized memory range: " + range.name;
        return false;
    }

    // Check for 64-bit integer overflow in physical address
    if (range.phys_start + range.size < range.phys_start) {
        if (out_error) *out_error = "Physical range overflows 64-bit address: " + range.name;
        return false;
    }

    // Check pairwise overlap with existing ranges
    for (const auto& existing : ranges_) {
        if (existing.overlaps_with(range)) {
            if (out_error) {
                *out_error = "Range '" + range.name + "' [0x" + std::to_string(range.phys_start) +
                             " - 0x" + std::to_string(range.phys_end()) + "] overlaps with '" +
                             existing.name + "' [0x" + std::to_string(existing.phys_start) +
                             " - 0x" + std::to_string(existing.phys_end()) + "]";
            }
            return false;
        }
    }

    ranges_.push_back(range);
    return true;
}

std::vector<BootMemoryRange> BootMemoryMap::get_ranges_by_type(BootMemoryType type) const {
    std::vector<BootMemoryRange> result;
    for (const auto& r : ranges_) {
        if (r.type == type) {
            result.push_back(r);
        }
    }
    return result;
}

std::uint64_t BootMemoryMap::total_usable_ram() const noexcept {
    std::uint64_t total = 0;
    for (const auto& r : ranges_) {
        if (r.type == BootMemoryType::UsableRAM) {
            total += r.size;
        }
    }
    return total;
}

std::uint64_t BootMemoryMap::total_reserved_ram() const noexcept {
    std::uint64_t total = 0;
    for (const auto& r : ranges_) {
        if (r.type != BootMemoryType::UsableRAM && r.type != BootMemoryType::MMIO) {
            total += r.size;
        }
    }
    return total;
}

bool BootMemoryMap::validate(std::uint8_t address_width_bits, std::string* out_error) const {
    if (address_width_bits > 64) {
        if (out_error) *out_error = "Invalid address width bits: " + std::to_string(address_width_bits);
        return false;
    }

    std::uint64_t max_allowed_addr = (address_width_bits == 64)
                                         ? ~0ULL
                                         : ((1ULL << address_width_bits) - 1ULL);

    for (const auto& r : ranges_) {
        if (r.phys_end() > max_allowed_addr) {
            if (out_error) {
                *out_error = "Range '" + r.name + "' physical end 0x" +
                             std::to_string(r.phys_end()) + " exceeds address width limit 0x" +
                             std::to_string(max_allowed_addr);
            }
            return false;
        }

        // Check RAM bounds if within RAM
        if (ram_size_ > 0 && r.type != BootMemoryType::MMIO) {
            if (r.phys_start < ram_base_ || r.phys_end() > (ram_base_ + ram_size_)) {
                if (out_error) {
                    *out_error = "Range '" + r.name + "' falls outside configured RAM [0x" +
                                 std::to_string(ram_base_) + " - 0x" +
                                 std::to_string(ram_base_ + ram_size_) + "]";
                }
                return false;
            }
        }
    }

    return true;
}

std::string BootMemoryMap::serialize_summary() const {
    std::ostringstream oss;
    oss << "=== WinIOS Boot Memory Map ===\n";
    oss << "Configured RAM: [0x" << std::hex << ram_base_ << " - 0x"
        << (ram_base_ + ram_size_) << "] (" << std::dec << (ram_size_ / (1024 * 1024)) << " MB)\n";
    oss << "Total Usable: " << (total_usable_ram() / (1024 * 1024)) << " MB\n";
    oss << "Total Reserved: " << (total_reserved_ram() / 1024) << " KB\n\n";

    oss << "| Index | Region Name | Type | Physical Range | Size (KB) | Virtual Address |\n";
    oss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";

    for (std::size_t i = 0; i < ranges_.size(); ++i) {
        const auto& r = ranges_[i];
        oss << "| " << i << " | " << r.name << " | " << boot_memory_type_string(r.type)
            << " | 0x" << std::hex << r.phys_start << " - 0x" << r.phys_end()
            << " | " << std::dec << (r.size / 1024)
            << " | 0x" << std::hex << r.virt_start << std::dec << " |\n";
    }

    return oss.str();
}

} // namespace winios::memory
