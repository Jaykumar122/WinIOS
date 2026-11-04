#include "memory.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace {
constexpr std::size_t kWord16Width = 2;
constexpr std::size_t kWord32Width = 4;
constexpr std::size_t kWord64Width = 8;
constexpr unsigned kBitsPerByte = 8;

// An access is at most 16 bytes wide (128-bit SIMD), so it can span at most
// two 4 KiB stripes. The helpers below therefore never need to hold more than
// two locks. Stripes are always acquired in ascending order, which gives a
// total order and rules out lock-order inversion between concurrent writers.
std::pair<std::size_t, std::size_t> stripe_range(std::size_t offset,
                                                 std::size_t width,
                                                 std::size_t stripe_bytes) {
    const std::size_t first = offset / stripe_bytes;
    const std::size_t last = (offset + width - 1u) / stripe_bytes;
    return {first, last};
}
} // namespace

Memory::Memory(std::size_t size) {
    if (size == 0) {
        throw std::invalid_argument("Memory size must be greater than zero");
    }
    bytes_.assign(size, std::uint8_t{0});
    stripe_count_ = std::max<std::size_t>(1, (size + kStripeBytes - 1u) / kStripeBytes);
    stripes_ = std::make_unique<std::shared_mutex[]>(stripe_count_);
}

void Memory::require_range(std::size_t offset, std::size_t width) const {
    if (offset > bytes_.size() || width > bytes_.size() - offset) {
        std::ostringstream message;
        message << "Memory access out of range: offset " << offset
                << ", width " << width
                << ", size " << bytes_.size();
        throw std::out_of_range(message.str());
    }
}

std::uint64_t Memory::read_bytes(std::size_t offset, std::size_t width) const {
    require_range(offset, width);

    const bool locked = concurrent_.load(std::memory_order_acquire);
    std::shared_lock<std::shared_mutex> first_lock;
    std::shared_lock<std::shared_mutex> second_lock;
    if (locked) {
        const auto [first, last] = stripe_range(offset, width, kStripeBytes);
        first_lock = std::shared_lock<std::shared_mutex>(stripes_[first]);
        if (last != first) {
            second_lock = std::shared_lock<std::shared_mutex>(stripes_[last]);
        }
    }

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(bytes_[offset + i])
                 << static_cast<unsigned>(i * kBitsPerByte);
    }
    return value;
}

void Memory::write_bytes(std::size_t offset, std::size_t width,
                         std::uint64_t value) {
    require_range(offset, width);

    // Phase 43 store serialization point. Ordinary stores take the same
    // recursive mutex that a load/store exclusive pair holds across its
    // compare-and-swap, so no store can slip between the compare and the
    // write. The mutex is released after the stripe locks and observer.
    std::unique_lock<std::recursive_mutex> store_lock;
    if (store_mutex_ != nullptr) {
        store_lock = std::unique_lock<std::recursive_mutex>(*store_mutex_);
    }

    const bool locked = concurrent_.load(std::memory_order_acquire);
    std::unique_lock<std::shared_mutex> first_lock;
    std::unique_lock<std::shared_mutex> second_lock;
    if (locked) {
        const auto [first, last] = stripe_range(offset, width, kStripeBytes);
        first_lock = std::unique_lock<std::shared_mutex>(stripes_[first]);
        if (last != first) {
            second_lock = std::unique_lock<std::shared_mutex>(stripes_[last]);
        }
    }

    for (std::size_t i = 0; i < width; ++i) {
        bytes_[offset + i] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>(i * kBitsPerByte));
    }

    // The observer runs while the write lock is still held so that a global
    // exclusive-monitor reservation can never slip in between the store and
    // its invalidation.
    if (store_observer_) {
        store_observer_(offset, width);
    }
}

std::uint8_t Memory::read_u8(std::size_t offset) const {
    return static_cast<std::uint8_t>(read_bytes(offset, 1u));
}

void Memory::write_u8(std::size_t offset, std::uint8_t value) {
    write_bytes(offset, 1u, value);
}

std::uint16_t Memory::read_u16(std::size_t offset) const {
    return static_cast<std::uint16_t>(read_bytes(offset, kWord16Width));
}

void Memory::write_u16(std::size_t offset, std::uint16_t value) {
    write_bytes(offset, kWord16Width, value);
}

std::uint32_t Memory::read_u32(std::size_t offset) const {
    return static_cast<std::uint32_t>(read_bytes(offset, kWord32Width));
}

void Memory::write_u32(std::size_t offset, std::uint32_t value) {
    write_bytes(offset, kWord32Width, value);
}

std::uint64_t Memory::read_u64(std::size_t offset) const {
    return read_bytes(offset, kWord64Width);
}

void Memory::write_u64(std::size_t offset, std::uint64_t value) {
    write_bytes(offset, kWord64Width, value);
}

void Memory::reset_zero() noexcept {
    const bool locked = concurrent_.load(std::memory_order_acquire);
    if (locked) {
        for (std::size_t i = 0; i < stripe_count_; ++i) {
            stripes_[i].lock();
        }
    }

    std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});

    if (locked) {
        for (std::size_t i = 0; i < stripe_count_; ++i) {
            stripes_[i].unlock();
        }
    }
}
