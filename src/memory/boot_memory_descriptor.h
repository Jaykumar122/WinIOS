#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::memory {

// -----------------------------------------------------------------------------
// Boot-Time Physical Memory Range Classification
// -----------------------------------------------------------------------------
enum class BootMemoryType {
    UsableRAM,
    Reserved,
    KernelImage,
    PageTables,
    InitialStack,
    BootArgs,
    DeviceTree,
    Framebuffer,
    MMIO
};

const char* boot_memory_type_string(BootMemoryType type) noexcept;

struct BootMemoryRange {
    BootMemoryType type{BootMemoryType::UsableRAM};
    std::uint64_t  phys_start{0};
    std::uint64_t  size{0};
    std::uint64_t  virt_start{0}; // Corresponding VA if mapped (0 if unmapped)
    std::string    name;

    [[nodiscard]] std::uint64_t phys_end() const noexcept {
        return phys_start + size;
    }

    [[nodiscard]] bool overlaps_with(const BootMemoryRange& other) const noexcept {
        if (size == 0 || other.size == 0) return false;
        return phys_start < other.phys_end() && other.phys_start < phys_end();
    }
};

// -----------------------------------------------------------------------------
// BootMemoryMap: Physical Memory Layout Descriptor for Boot Handoff
// -----------------------------------------------------------------------------
class BootMemoryMap {
public:
    BootMemoryMap(std::uint64_t ram_base = 0, std::uint64_t ram_size = 0);

    bool add_range(const BootMemoryRange& range, std::string* out_error = nullptr);
    void clear() noexcept { ranges_.clear(); }

    [[nodiscard]] const std::vector<BootMemoryRange>& ranges() const noexcept { return ranges_; }
    [[nodiscard]] std::vector<BootMemoryRange> get_ranges_by_type(BootMemoryType type) const;

    [[nodiscard]] std::uint64_t ram_base() const noexcept { return ram_base_; }
    [[nodiscard]] std::uint64_t ram_size() const noexcept { return ram_size_; }

    [[nodiscard]] std::uint64_t total_usable_ram() const noexcept;
    [[nodiscard]] std::uint64_t total_reserved_ram() const noexcept;

    [[nodiscard]] bool validate(std::uint8_t address_width_bits, std::string* out_error = nullptr) const;
    [[nodiscard]] std::string serialize_summary() const;

private:
    std::uint64_t ram_base_{0};
    std::uint64_t ram_size_{0};
    std::vector<BootMemoryRange> ranges_;
};

} // namespace winios::memory
