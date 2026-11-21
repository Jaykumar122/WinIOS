#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

namespace winios::loader {

enum class IntervalType {
    KernelText,
    KernelData,
    DeviceTree,
    BootArgs,
    BootstrapStack,
    PageTables,
    PageMetadata,
    DynamicPool,
    MmioAic,
    MmioUart,
    GenericReserved
};

const char* interval_type_to_string(IntervalType type) noexcept;

struct MemoryInterval {
    std::string   name;
    IntervalType  type{IntervalType::GenericReserved};
    std::uint64_t base{0};
    std::uint64_t size{0};
    std::uint32_t prot{0};     // 1=R, 2=W, 4=X
    std::uint32_t max_prot{0}; // 1=R, 2=W, 4=X
    bool          is_reserved{false};
    std::string   source_tag;

    [[nodiscard]] std::uint64_t end() const noexcept {
        return base + size;
    }

    [[nodiscard]] bool overlaps_with(std::uint64_t other_base, std::uint64_t other_size) const noexcept {
        if (size == 0 || other_size == 0) return false;
        // Checked addition to prevent 64-bit overflow
        if (other_base + other_size < other_base) return true;
        const std::uint64_t other_end = other_base + other_size;
        return base < other_end && other_base < end();
    }

    [[nodiscard]] bool is_wx_violation() const noexcept {
        // Enforce W^X: cannot have both Write (0x2) and Execute (0x4)
        return ((prot & 0x6u) == 0x6u) || ((max_prot & 0x6u) == 0x6u);
    }
};

class KernelPhysicalIntervalMap {
public:
    KernelPhysicalIntervalMap(std::uint64_t dram_base = 0x0ULL, std::uint64_t dram_size = 0x800000ULL);

    [[nodiscard]] std::uint64_t dram_base() const noexcept { return dram_base_; }
    [[nodiscard]] std::uint64_t dram_size() const noexcept { return dram_size_; }
    [[nodiscard]] std::uint64_t dram_end() const noexcept { return dram_base_ + dram_size_; }

    bool add_interval(const MemoryInterval& interval, std::string* out_error = nullptr);

    bool allocate_interval(
        const std::string& name,
        IntervalType type,
        std::uint64_t size,
        std::uint64_t alignment,
        std::uint64_t& out_base,
        std::string* out_error = nullptr);

    [[nodiscard]] bool has_overlap(std::uint64_t base, std::uint64_t size, std::string* out_conflict_name = nullptr) const;

    [[nodiscard]] const std::vector<MemoryInterval>& intervals() const noexcept { return intervals_; }

    [[nodiscard]] const MemoryInterval* find_interval_by_name(std::string_view name) const;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string export_json() const;

private:
    std::uint64_t dram_base_{0};
    std::uint64_t dram_size_{0};
    std::vector<MemoryInterval> intervals_;
};

class KernelVirtualIntervalMap {
public:
    KernelVirtualIntervalMap(std::uint64_t min_va = 0x0ULL, std::uint64_t max_va = 0xFFFFFFFFFFFFFFFFULL);

    bool add_interval(const MemoryInterval& interval, std::string* out_error = nullptr);

    [[nodiscard]] bool has_overlap(std::uint64_t va_base, std::uint64_t size, std::string* out_conflict_name = nullptr) const;

    [[nodiscard]] const std::vector<MemoryInterval>& intervals() const noexcept { return intervals_; }

    [[nodiscard]] bool verify_wx_compliance(std::string* out_violation = nullptr) const;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string export_json() const;

private:
    std::uint64_t min_va_{0};
    std::uint64_t max_va_{0};
    std::vector<MemoryInterval> intervals_;
};

} // namespace winios::loader
