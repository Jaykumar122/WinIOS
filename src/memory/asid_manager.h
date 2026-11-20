#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace winios::memory {

// Emulated Address Space ID (ASID) Manager.
// Models architectural ASID lifecycle: allocation, generation tracking, rollover,
// retirement, and TLB invalidation epochs to prevent stale guest translations.
class AsidManager {
public:
    static constexpr std::uint16_t kReservedAsid = 0; // 0 is reserved for global/kernel mappings
    static constexpr std::uint16_t kDefaultMaxAsid = 255; // 8-bit ASID range (1..255)

    struct AsidAllocation {
        std::uint16_t asid{0};
        std::uint64_t generation{0};
        bool valid{false};
    };

    explicit AsidManager(std::uint16_t max_asid = kDefaultMaxAsid)
        : max_asid_(max_asid > 1 ? max_asid : kDefaultMaxAsid),
          next_candidate_(1),
          generation_(1),
          in_use_(max_asid_ + 1, false),
          allocated_generations_(max_asid_ + 1, 0)
    {
        in_use_[kReservedAsid] = true; // Reserve ASID 0
    }

    ~AsidManager() = default;

    // Allocate an ASID for a process. Returns the ASID and its epoch generation.
    // When the ASID pool is exhausted, generation is incremented, and needs_tlb_flush is set to true.
    AsidAllocation allocate_asid(bool& out_needs_tlb_flush) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_needs_tlb_flush = false;

        // Search for an unused slot starting from next_candidate_
        for (std::size_t i = 0; i < max_asid_; ++i) {
            std::uint16_t candidate = next_candidate_;
            next_candidate_ = static_cast<std::uint16_t>((next_candidate_ % max_asid_) + 1);
            if (!in_use_[candidate]) {
                in_use_[candidate] = true;
                allocated_generations_[candidate] = generation_;
                return AsidAllocation{candidate, generation_, true};
            }
        }

        // Exhaustion: advance generation epoch, roll over, and require TLB flush
        ++generation_;
        out_needs_tlb_flush = true;
        // All non-reserved slots become free for reallocation in the new generation
        std::fill(in_use_.begin() + 1, in_use_.end(), false);

        std::uint16_t chosen = 1;
        in_use_[chosen] = true;
        allocated_generations_[chosen] = generation_;
        next_candidate_ = 2;
        return AsidAllocation{chosen, generation_, true};
    }

    // Release an ASID upon process termination
    bool release_asid(std::uint16_t asid) {
        if (asid == kReservedAsid || asid > max_asid_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!in_use_[asid]) {
            return false;
        }
        in_use_[asid] = false;
        return true;
    }

    // Verify whether an (asid, generation) pair is currently valid in the active epoch
    [[nodiscard]] bool is_valid_in_epoch(std::uint16_t asid, std::uint64_t gen) const {
        if (asid == kReservedAsid || asid > max_asid_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return in_use_[asid] && (allocated_generations_[asid] == gen);
    }

    [[nodiscard]] std::uint64_t current_generation() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    [[nodiscard]] std::size_t active_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        for (std::size_t i = 1; i <= max_asid_; ++i) {
            if (in_use_[i]) ++count;
        }
        return count;
    }

    [[nodiscard]] std::uint16_t max_asid() const noexcept { return max_asid_; }

private:
    const std::uint16_t max_asid_;
    std::uint16_t next_candidate_{1};
    std::uint64_t generation_{1};
    std::vector<bool> in_use_;
    std::vector<std::uint64_t> allocated_generations_;
    mutable std::mutex mutex_;
};

} // namespace winios::memory
