#include "loader/kernel_interval_map.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::loader {

const char* interval_type_to_string(IntervalType type) noexcept {
    switch (type) {
        case IntervalType::KernelText:      return "KernelText";
        case IntervalType::KernelData:      return "KernelData";
        case IntervalType::DeviceTree:      return "DeviceTree";
        case IntervalType::BootArgs:        return "BootArgs";
        case IntervalType::BootstrapStack:  return "BootstrapStack";
        case IntervalType::PageTables:      return "PageTables";
        case IntervalType::PageMetadata:    return "PageMetadata";
        case IntervalType::DynamicPool:     return "DynamicPool";
        case IntervalType::MmioAic:         return "MmioAic";
        case IntervalType::MmioUart:        return "MmioUart";
        case IntervalType::GenericReserved: return "GenericReserved";
    }
    return "Unknown";
}

// ============================================================================
// KernelPhysicalIntervalMap
// ============================================================================

KernelPhysicalIntervalMap::KernelPhysicalIntervalMap(std::uint64_t dram_base, std::uint64_t dram_size)
    : dram_base_(dram_base), dram_size_(dram_size) {}

bool KernelPhysicalIntervalMap::has_overlap(std::uint64_t base, std::uint64_t size, std::string* out_conflict_name) const {
    if (size == 0) return false;
    for (const auto& iv : intervals_) {
        // MMIO regions outside DRAM do not collide with DRAM internal regions
        if (iv.type == IntervalType::MmioAic || iv.type == IntervalType::MmioUart) {
            if (base < iv.end() && iv.base < (base + size)) {
                if (out_conflict_name) *out_conflict_name = iv.name;
                return true;
            }
            continue;
        }

        if (iv.overlaps_with(base, size)) {
            if (out_conflict_name) *out_conflict_name = iv.name;
            return true;
        }
    }
    return false;
}

bool KernelPhysicalIntervalMap::add_interval(const MemoryInterval& interval, std::string* out_error) {
    if (interval.size == 0) {
        if (out_error) *out_error = "Cannot add interval with size 0";
        return false;
    }

    // Checked arithmetic to prevent integer overflow
    if (interval.base + interval.size < interval.base) {
        if (out_error) *out_error = "Interval address range overflows 64-bit bounds";
        return false;
    }

    // If within DRAM, verify boundary
    if (interval.type != IntervalType::MmioAic && interval.type != IntervalType::MmioUart) {
        if (interval.base < dram_base_ || interval.end() > dram_end()) {
            if (out_error) {
                std::ostringstream oss;
                oss << "Interval '" << interval.name << "' [0x" << std::hex << interval.base
                    << " - 0x" << interval.end() << "] exceeds DRAM capacity [0x"
                    << dram_base_ << " - 0x" << dram_end() << "]";
                *out_error = oss.str();
            }
            return false;
        }
    }

    // Check overlap
    std::string conflict;
    if (has_overlap(interval.base, interval.size, &conflict)) {
        if (out_error) {
            std::ostringstream oss;
            oss << "Interval '" << interval.name << "' [0x" << std::hex << interval.base
                << " - 0x" << interval.end() << "] overlaps existing interval '" << conflict << "'";
            *out_error = oss.str();
        }
        return false;
    }

    intervals_.push_back(interval);
    std::sort(intervals_.begin(), intervals_.end(), [](const MemoryInterval& a, const MemoryInterval& b) {
        return a.base < b.base;
    });
    return true;
}

bool KernelPhysicalIntervalMap::allocate_interval(
    const std::string& name,
    IntervalType type,
    std::uint64_t size,
    std::uint64_t alignment,
    std::uint64_t& out_base,
    std::string* out_error) {

    if (size == 0) {
        if (out_error) *out_error = "Allocation size cannot be zero";
        return false;
    }

    if (alignment == 0) alignment = 1;

    std::uint64_t candidate = dram_base_;
    for (const auto& iv : intervals_) {
        if (iv.type == IntervalType::MmioAic || iv.type == IntervalType::MmioUart) continue;
        const std::uint64_t aligned_candidate = (candidate + alignment - 1ULL) & ~(alignment - 1ULL);
        if (aligned_candidate + size <= iv.base) {
            candidate = aligned_candidate;
            break;
        }
        if (iv.end() > candidate) {
            candidate = iv.end();
        }
    }

    candidate = (candidate + alignment - 1ULL) & ~(alignment - 1ULL);
    if (candidate + size > dram_end()) {
        if (out_error) *out_error = "Insufficient physical memory to allocate " + name;
        return false;
    }

    MemoryInterval iv{};
    iv.name = name;
    iv.type = type;
    iv.base = candidate;
    iv.size = size;
    iv.is_reserved = true;

    if (!add_interval(iv, out_error)) {
        return false;
    }

    out_base = candidate;
    return true;
}

const MemoryInterval* KernelPhysicalIntervalMap::find_interval_by_name(std::string_view name) const {
    for (const auto& iv : intervals_) {
        if (iv.name == name) return &iv;
    }
    return nullptr;
}

std::string KernelPhysicalIntervalMap::to_string() const {
    std::ostringstream oss;
    oss << "=== Kernel Physical Interval Map ===\n";
    oss << "DRAM Base: 0x" << std::hex << dram_base_ << " Size: 0x" << dram_size_
        << " (End: 0x" << dram_end() << ")\n";
    for (const auto& iv : intervals_) {
        oss << "  [" << std::setw(16) << std::left << iv.name << "] "
            << "0x" << std::setw(9) << std::right << std::setfill('0') << std::hex << iv.base
            << " - 0x" << std::setw(9) << std::right << std::setfill('0') << iv.end()
            << " (" << std::setw(8) << std::dec << iv.size << " B) "
            << interval_type_to_string(iv.type) << "\n";
    }
    return oss.str();
}

std::string KernelPhysicalIntervalMap::export_json() const {
    std::ostringstream oss;
    oss << "{\n  \"dram_base\": " << dram_base_ << ",\n  \"dram_size\": " << dram_size_
        << ",\n  \"intervals\": [\n";
    for (std::size_t i = 0; i < intervals_.size(); ++i) {
        const auto& iv = intervals_[i];
        oss << "    {\n"
            << "      \"name\": \"" << iv.name << "\",\n"
            << "      \"type\": \"" << interval_type_to_string(iv.type) << "\",\n"
            << "      \"base\": " << iv.base << ",\n"
            << "      \"size\": " << iv.size << ",\n"
            << "      \"end\": " << iv.end() << ",\n"
            << "      \"reserved\": " << (iv.is_reserved ? "true" : "false") << "\n"
            << "    }" << (i + 1 < intervals_.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}";
    return oss.str();
}

// ============================================================================
// KernelVirtualIntervalMap
// ============================================================================

KernelVirtualIntervalMap::KernelVirtualIntervalMap(std::uint64_t min_va, std::uint64_t max_va)
    : min_va_(min_va), max_va_(max_va) {}

bool KernelVirtualIntervalMap::has_overlap(std::uint64_t va_base, std::uint64_t size, std::string* out_conflict_name) const {
    if (size == 0) return false;
    for (const auto& iv : intervals_) {
        if (iv.overlaps_with(va_base, size)) {
            if (out_conflict_name) *out_conflict_name = iv.name;
            return true;
        }
    }
    return false;
}

bool KernelVirtualIntervalMap::add_interval(const MemoryInterval& interval, std::string* out_error) {
    if (interval.size == 0) {
        if (out_error) *out_error = "Virtual interval size cannot be zero";
        return false;
    }

    if (interval.base + interval.size < interval.base) {
        if (out_error) *out_error = "Virtual interval address overflow";
        return false;
    }

    if (interval.is_wx_violation()) {
        if (out_error) {
            std::ostringstream oss;
            oss << "W^X violation in virtual interval '" << interval.name
                << "': cannot be simultaneously writable and executable (prot=0x"
                << std::hex << interval.prot << ", max_prot=0x" << interval.max_prot << ")";
            *out_error = oss.str();
        }
        return false;
    }

    std::string conflict;
    if (has_overlap(interval.base, interval.size, &conflict)) {
        if (out_error) {
            std::ostringstream oss;
            oss << "Virtual interval '" << interval.name << "' [0x" << std::hex << interval.base
                << " - 0x" << interval.end() << "] overlaps existing interval '" << conflict << "'";
            *out_error = oss.str();
        }
        return false;
    }

    intervals_.push_back(interval);
    std::sort(intervals_.begin(), intervals_.end(), [](const MemoryInterval& a, const MemoryInterval& b) {
        return a.base < b.base;
    });
    return true;
}

bool KernelVirtualIntervalMap::verify_wx_compliance(std::string* out_violation) const {
    for (const auto& iv : intervals_) {
        if (iv.is_wx_violation()) {
            if (out_violation) {
                *out_violation = "W^X violation found in interval '" + iv.name + "'";
            }
            return false;
        }
    }
    return true;
}

std::string KernelVirtualIntervalMap::to_string() const {
    std::ostringstream oss;
    oss << "=== Kernel Virtual Interval Map ===\n";
    for (const auto& iv : intervals_) {
        oss << "  [" << std::setw(16) << std::left << iv.name << "] "
            << "0x" << std::setw(16) << std::right << std::setfill('0') << std::hex << iv.base
            << " - 0x" << std::setw(16) << std::right << std::setfill('0') << iv.end()
            << " (" << std::setw(8) << std::dec << iv.size << " B) "
            << "prot: " << ((iv.prot & 1) ? "r" : "-") << ((iv.prot & 2) ? "w" : "-") << ((iv.prot & 4) ? "x" : "-")
            << " maxprot: " << ((iv.max_prot & 1) ? "r" : "-") << ((iv.max_prot & 2) ? "w" : "-") << ((iv.max_prot & 4) ? "x" : "-")
            << " " << interval_type_to_string(iv.type) << "\n";
    }
    return oss.str();
}

std::string KernelVirtualIntervalMap::export_json() const {
    std::ostringstream oss;
    oss << "{\n  \"intervals\": [\n";
    for (std::size_t i = 0; i < intervals_.size(); ++i) {
        const auto& iv = intervals_[i];
        oss << "    {\n"
            << "      \"name\": \"" << iv.name << "\",\n"
            << "      \"type\": \"" << interval_type_to_string(iv.type) << "\",\n"
            << "      \"base\": " << iv.base << ",\n"
            << "      \"size\": " << iv.size << ",\n"
            << "      \"end\": " << iv.end() << ",\n"
            << "      \"prot\": " << iv.prot << ",\n"
            << "      \"max_prot\": " << iv.max_prot << "\n"
            << "    }" << (i + 1 < intervals_.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}";
    return oss.str();
}

} // namespace winios::loader
