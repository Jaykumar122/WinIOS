#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <limits>
#include <string>

namespace winios::memory {

// Emulated Address Space ID (ASID) Manager V3.
// Models architectural ASID lifecycle: allocation, generation tracking, rollover,
// retirement, overflow prevention, fail-closed exhaustion, and TLB invalidation callbacks
// to guarantee translation coherence across address-space recycling.
class AsidManager {
public:
    static constexpr std::uint16_t kReservedAsid = 0; // 0 is reserved for global/kernel mappings
    static constexpr std::uint16_t kDefaultMaxAsid = 255; // 8-bit ASID range (1..255)

    using InvalidationCallback = std::function<void(std::uint16_t asid, bool all_asids)>;

    struct AsidAllocation {
        std::uint16_t asid{0};
        std::uint64_t generation{0};
        bool valid{false};
    };

    explicit AsidManager(std::uint16_t max_asid = kDefaultMaxAsid,
                         std::uint64_t max_generation = std::numeric_limits<std::uint64_t>::max())
        : max_asid_(max_asid > 1 ? max_asid : kDefaultMaxAsid),
          max_generation_(max_generation > 0 ? max_generation : std::numeric_limits<std::uint64_t>::max()),
          next_candidate_(1),
          generation_(1),
          in_use_(max_asid_ + 1, false),
          allocated_generations_(max_asid_ + 1, 0)
    {
        in_use_[kReservedAsid] = true; // Reserve ASID 0
    }

    ~AsidManager() = default;

    // Register an invalidation hook (e.g. connecting to MMU::invalidate_tlb_asid or invalidate_tlb_all)
    void set_invalidation_callback(InvalidationCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        inval_cb_ = std::move(cb);
    }

    // Allocate an ASID for a process. Returns the ASID and its epoch generation.
    // When the ASID pool is exhausted, generation is incremented, and needs_tlb_flush is set to true.
    // If maximum generation is exceeded, fails closed to prevent stale token resurrection.
    AsidAllocation allocate_asid(bool& out_needs_tlb_flush) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_needs_tlb_flush = false;

        if (is_exhausted_) {
            return AsidAllocation{0, 0, false};
        }

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

        // Pool exhaustion: verify generation limit before rollover
        if (generation_ >= max_generation_) {
            is_exhausted_ = true;
            if (inval_cb_) {
                inval_cb_(0, true); // Flush all on permanent retirement
            }
            return AsidAllocation{0, 0, false};
        }

        ++generation_;
        out_needs_tlb_flush = true;

        if (inval_cb_) {
            inval_cb_(0, true); // Flush all TLB translations on epoch rollover
        }

        // All non-reserved slots become free for reallocation in the new generation
        std::fill(in_use_.begin() + 1, in_use_.end(), false);

        std::uint16_t chosen = 1;
        in_use_[chosen] = true;
        allocated_generations_[chosen] = generation_;
        next_candidate_ = 2;
        return AsidAllocation{chosen, generation_, true};
    }

    // Release an ASID upon process termination and trigger invalidation
    bool release_asid(std::uint16_t asid) {
        if (asid == kReservedAsid || asid > max_asid_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!in_use_[asid]) {
            return false;
        }
        in_use_[asid] = false;
        if (inval_cb_) {
            inval_cb_(asid, false); // Invalidate translations for retired ASID
        }
        return true;
    }

    // Verify whether an (asid, generation) pair is currently valid in the active epoch
    [[nodiscard]] bool is_valid_in_epoch(std::uint16_t asid, std::uint64_t gen) const {
        if (asid == kReservedAsid || asid > max_asid_) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_exhausted_) {
            return false;
        }
        return in_use_[asid] && (allocated_generations_[asid] == gen);
    }

    [[nodiscard]] bool is_exhausted() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_exhausted_;
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
    [[nodiscard]] std::uint64_t max_generation() const noexcept { return max_generation_; }

private:
    const std::uint16_t max_asid_;
    const std::uint64_t max_generation_;
    std::uint16_t next_candidate_{1};
    std::uint64_t generation_{1};
    bool is_exhausted_{false};
    std::vector<bool> in_use_;
    std::vector<std::uint64_t> allocated_generations_;
    InvalidationCallback inval_cb_{nullptr};
    mutable std::mutex mutex_;
};

} // namespace winios::memory
