#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::memory {

inline constexpr std::uint32_t kVmProtNone    = 0x00u;
inline constexpr std::uint32_t kVmProtRead    = 0x01u;
inline constexpr std::uint32_t kVmProtWrite   = 0x02u;
inline constexpr std::uint32_t kVmProtExecute = 0x04u;
inline constexpr std::uint32_t kVmProtDefault = kVmProtRead | kVmProtWrite;
inline constexpr std::uint32_t kVmProtAll     = kVmProtRead | kVmProtWrite | kVmProtExecute;

inline constexpr std::uint64_t kVmMapMagic    = 0x584E55564D4D4150ULL; // "XNUVMAP"

struct VmMapEntry {
    std::uint64_t start{0};
    std::uint64_t end{0};
    std::uint32_t protection{kVmProtDefault};
    std::uint32_t max_protection{kVmProtAll};
    std::uint32_t inheritance{0};
    std::uint32_t wired_count{0};
    std::string   tag;
    bool          is_submap{false};

    [[nodiscard]] std::uint64_t size() const noexcept {
        return (end > start) ? (end - start) : 0ULL;
    }

    [[nodiscard]] bool contains(std::uint64_t va) const noexcept {
        return va >= start && va < end;
    }

    [[nodiscard]] bool overlaps(std::uint64_t s, std::uint64_t e) const noexcept {
        return start < e && s < end;
    }
};

struct VmMapStats {
    std::uint64_t min_offset{0};
    std::uint64_t max_offset{0};
    std::uint64_t total_mapped_bytes{0};
    std::size_t   entry_count{0};
    std::uint64_t wired_bytes{0};
};

class XnuVmMap {
public:
    XnuVmMap(std::uint64_t min_offset = 0x0ULL,
             std::uint64_t max_offset = 0x800000ULL,
             std::string name = "kernel_map");
    ~XnuVmMap() = default;

    // Enter a new mapping range
    bool vm_map_enter(std::uint64_t start,
                      std::uint64_t size,
                      std::uint32_t prot = kVmProtDefault,
                      std::uint32_t max_prot = kVmProtAll,
                      const std::string& tag = "",
                      std::string* out_error = nullptr);

    // Remove a mapped range
    bool vm_map_remove(std::uint64_t start,
                       std::uint64_t size,
                       std::string* out_error = nullptr);

    // Change protection on a mapped range
    bool vm_map_protect(std::uint64_t start,
                        std::uint64_t size,
                        std::uint32_t new_prot,
                        bool set_max = false,
                        std::string* out_error = nullptr);

    // Lookup entry covering virtual address
    [[nodiscard]] const VmMapEntry* vm_map_lookup(std::uint64_t va) const;

    // Find available virtual address range of specified size
    bool vm_map_find_space(std::uint64_t size,
                           std::uint64_t alignment,
                           std::uint64_t& out_start,
                           std::string* out_error = nullptr) const;

    // Wire / unwire pages in range
    bool vm_map_wire(std::uint64_t start, std::uint64_t size, std::string* out_error = nullptr);
    bool vm_map_unwire(std::uint64_t start, std::uint64_t size, std::string* out_error = nullptr);

    [[nodiscard]] std::uint64_t min_offset() const noexcept { return min_offset_; }
    [[nodiscard]] std::uint64_t max_offset() const noexcept { return max_offset_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<VmMapEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] VmMapStats get_stats() const;

    // Snapshot serialization & deserialization
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error = nullptr);

private:
    std::uint64_t min_offset_;
    std::uint64_t max_offset_;
    std::string   name_;
    std::vector<VmMapEntry> entries_;
    mutable std::mutex mutex_;

    void sort_entries_locked();
};

} // namespace winios::memory
