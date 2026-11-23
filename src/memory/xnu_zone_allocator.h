#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace winios::memory {

inline constexpr std::uint64_t kZoneMagic = 0x5A4F4E455F4D4147ULL; // "ZONE_MAG"
inline constexpr std::size_t kMaxZoneNameLen = 32;

struct ZoneStats {
    std::string zone_name;
    std::size_t elem_size{0};
    std::size_t count{0};
    std::size_t max_count{0};
    std::size_t free_count{0};
    std::size_t total_alloc_ops{0};
    std::size_t total_free_ops{0};
};

struct ZoneInfo {
    std::string name;
    std::size_t elem_size{0};
    std::size_t max_elements{0};
    std::uint64_t base_address{0};
    std::size_t total_allocated{0};
    std::size_t total_alloc_ops{0};
    std::size_t total_free_ops{0};
    std::deque<std::uint64_t> free_list;
    std::vector<bool> allocation_bitmap;
};

class XnuZoneAllocator {
public:
    explicit XnuZoneAllocator(std::uint64_t zone_pool_base = 0x200000ULL,
                              std::uint64_t zone_pool_size = 0x200000ULL);
    ~XnuZoneAllocator() = default;

    // Create a new zone with specified element size
    bool create_zone(const std::string& name,
                     std::size_t elem_size,
                     std::size_t max_elements,
                     std::string* out_error = nullptr);

    // Allocate an element from the named zone. Returns guest address (or 0 on failure)
    [[nodiscard]] std::uint64_t zalloc(const std::string& name);

    // Free an element previously allocated from the named zone
    bool zfree(const std::string& name, std::uint64_t ptr, std::string* out_error = nullptr);

    // Query zone statistics
    [[nodiscard]] bool get_zone_stats(const std::string& name, ZoneStats& out_stats) const;
    [[nodiscard]] std::vector<ZoneStats> get_all_zone_stats() const;
    [[nodiscard]] std::size_t zone_count() const;

    // Pool capacity and bounds queries
    [[nodiscard]] std::uint64_t pool_base() const noexcept { return zone_pool_base_; }
    [[nodiscard]] std::uint64_t pool_size() const noexcept { return zone_pool_size_; }
    [[nodiscard]] std::uint64_t next_zone_base() const noexcept { return next_zone_base_; }

    // Snapshot serialization & deserialization
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error = nullptr);

private:
    std::uint64_t zone_pool_base_;
    std::uint64_t zone_pool_size_;
    std::uint64_t next_zone_base_;
    std::unordered_map<std::string, ZoneInfo> zones_;
    mutable std::mutex mutex_;
};

} // namespace winios::memory
