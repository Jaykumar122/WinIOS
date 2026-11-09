#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

// Flat guest RAM. All multi-byte accesses use little-endian byte order and are
// bounds-checked; unaligned accesses are permitted at this layer.
//
// Phase 43: the backing store can be switched into a concurrent mode in which
// accesses are protected by a fixed set of striped shared mutexes. Reads take
// the stripe(s) shared and writes take them exclusively, so disjoint stripes
// progress in parallel. The default mode is unlocked (the historical
// deterministic path). A store observer is invoked while the write lock is
// held, which lets the cluster invalidate global exclusive-monitor
// reservations atomically with the store.
class Memory {
public:
    using StoreObserver = std::function<void(std::size_t offset, std::size_t width)>;
    using LoadObserver = std::function<void(std::size_t offset, std::size_t width, std::uint64_t value)>;

    // Creates guest RAM of `size` bytes, all initialized to zero.
    // Throws std::invalid_argument when `size` is zero.
    explicit Memory(std::size_t size);

    std::size_t size() const noexcept { return bytes_.size(); }

    // Direct raw memory buffer accessors for snapshot capture, restore, and high-performance I/O.
    const std::uint8_t* raw_data() const noexcept { return bytes_.data(); }
    std::uint8_t* raw_data() noexcept { return bytes_.data(); }

    // Clears all guest RAM to zero under write lock.
    void reset_zero() noexcept;

    std::uint8_t read_u8(std::size_t offset) const;
    void write_u8(std::size_t offset, std::uint8_t value);

    std::uint16_t read_u16(std::size_t offset) const;
    void write_u16(std::size_t offset, std::uint16_t value);

    std::uint32_t read_u32(std::size_t offset) const;
    void write_u32(std::size_t offset, std::uint32_t value);

    std::uint64_t read_u64(std::size_t offset) const;
    void write_u64(std::size_t offset, std::uint64_t value);

    // Untraced forwards for monitor reservation checks
    std::uint8_t read_u8_untraced(std::size_t offset) const { return read_u8(offset); }
    std::uint16_t read_u16_untraced(std::size_t offset) const { return read_u16(offset); }
    std::uint32_t read_u32_untraced(std::size_t offset) const { return read_u32(offset); }
    std::uint64_t read_u64_untraced(std::size_t offset) const { return read_u64(offset); }

    // Phase 43: concurrency controls.
    // Enabling concurrent mode is safe at any time; accesses already in flight
    // use whichever mode they observed at entry.
    void set_concurrent_mode(bool enabled) noexcept {
        concurrent_.store(enabled, std::memory_order_release);
    }
    bool concurrent_mode() const noexcept {
        return concurrent_.load(std::memory_order_acquire);
    }

    // Installs the store observer. Must be called before concurrent execution
    // begins; the observer itself runs under the affected stripe's write lock.
    void set_store_observer(StoreObserver observer) {
        store_observer_ = std::move(observer);
    }
    void clear_store_observer() { store_observer_ = {}; }
    bool has_store_observer() const noexcept {
        return static_cast<bool>(store_observer_);
    }

    // Installs the load observer.
    void set_load_observer(LoadObserver observer) {
        load_observer_ = std::move(observer);
    }
    void clear_load_observer() { load_observer_ = {}; }
    bool has_load_observer() const noexcept {
        return static_cast<bool>(load_observer_);
    }

    // Optional cluster-wide store serialization mutex. When set, every write
    // holds it before acquiring its memory stripe(s). This is the point that
    // makes an exclusive load/store pair (or an LSE RMW) atomic with respect
    // to ordinary guest stores. Recursive, because the exclusive path already
    // holds it when it reaches Memory.
    void set_store_mutex(std::recursive_mutex* mutex) noexcept {
        store_mutex_ = mutex;
    }

    // Number of independent lock stripes (exposed for the concurrency audit).
    std::size_t stripe_count() const noexcept { return stripe_count_; }

private:
    static constexpr std::size_t kStripeBytes = 4096;

    // Throws std::out_of_range unless [offset, offset + width) lies inside
    // the buffer. The check cannot overflow: `offset > size` is rejected
    // before `size - offset` is evaluated.
    void require_range(std::size_t offset, std::size_t width) const;

    std::uint64_t read_bytes(std::size_t offset, std::size_t width) const;
    void write_bytes(std::size_t offset, std::size_t width, std::uint64_t value);

    std::vector<std::uint8_t> bytes_;

    // Fixed lock stripes. Never resized after construction, so a reference is
    // stable for the whole lifetime of the Memory object.
    std::unique_ptr<std::shared_mutex[]> stripes_;
    std::size_t stripe_count_{0};

    std::atomic<bool> concurrent_{false};
    StoreObserver store_observer_{};
    LoadObserver load_observer_{};
    std::recursive_mutex* store_mutex_{nullptr};
};
