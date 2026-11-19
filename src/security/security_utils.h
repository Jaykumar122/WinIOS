#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <type_traits>
#include <limits>
#include <span>
#include <memory>
#include <utility>

namespace winios::security {

// 1. Secure wipe
void secure_zero_memory(void* ptr, std::size_t size) noexcept;

// 2. Constant-time comparison for digests, tokens, signatures
bool constant_time_equals(const void* a, const void* b, std::size_t len) noexcept;
bool constant_time_equals(const std::string& a, const std::string& b) noexcept;
bool constant_time_equals(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) noexcept;

// 3. Cryptographically Secure Pseudo-Random Number Generator (CSPRNG)
class SecureRandom {
public:
    // Fills dest with cryptographically secure random bytes. Returns true on success, false on failure.
    static bool get_random_bytes(void* dest, std::size_t size) noexcept;

    // Generates a 64-bit unsigned random integer. Throws std::runtime_error on failure.
    static std::uint64_t get_random_u64();

    // Generates a 32-bit unsigned random integer. Throws std::runtime_error on failure.
    static std::uint32_t get_random_u32();

    // Generates a random byte sequence formatted as a lowercase hex string.
    static std::string get_random_hex(std::size_t byte_count);
};

// 4. RAII SecureBuffer (guarantees wipe on destruction/move/clear)
class SecureBuffer {
public:
    SecureBuffer() noexcept : data_(nullptr), size_(0) {}
    explicit SecureBuffer(std::size_t size);
    SecureBuffer(const void* src, std::size_t size);
    ~SecureBuffer() noexcept;

    // Non-copyable to prevent accidental copies of sensitive secrets
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    // Move construct and move assign
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    std::uint8_t* data() noexcept { return data_; }
    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    void resize(std::size_t new_size);
    void clear() noexcept;

    std::span<std::uint8_t> span() noexcept { return {data_, size_}; }
    std::span<const std::uint8_t> span() const noexcept { return {data_, size_}; }

private:
    std::uint8_t* data_{nullptr};
    std::size_t size_{0};
};

// 5. Checked arithmetic
template<typename T>
requires std::is_integral_v<T>
inline bool checked_add(T a, T b, T& result) noexcept {
    if constexpr (std::is_unsigned_v<T>) {
        if (std::numeric_limits<T>::max() - a < b) {
            return false;
        }
        result = a + b;
        return true;
    } else {
        if ((b > 0 && a > std::numeric_limits<T>::max() - b) ||
            (b < 0 && a < std::numeric_limits<T>::min() - b)) {
            return false;
        }
        result = a + b;
        return true;
    }
}

template<typename T>
requires std::is_integral_v<T>
inline std::optional<T> checked_add(T a, T b) noexcept {
    T res{};
    if (checked_add(a, b, res)) {
        return res;
    }
    return std::nullopt;
}

template<typename T>
requires std::is_integral_v<T>
inline bool checked_sub(T a, T b, T& result) noexcept {
    if constexpr (std::is_unsigned_v<T>) {
        if (a < b) {
            return false;
        }
        result = a - b;
        return true;
    } else {
        if ((b < 0 && a > std::numeric_limits<T>::max() + b) ||
            (b > 0 && a < std::numeric_limits<T>::min() + b)) {
            return false;
        }
        result = a - b;
        return true;
    }
}

template<typename T>
requires std::is_integral_v<T>
inline std::optional<T> checked_sub(T a, T b) noexcept {
    T res{};
    if (checked_sub(a, b, res)) {
        return res;
    }
    return std::nullopt;
}

template<typename T>
requires std::is_integral_v<T>
inline bool checked_mul(T a, T b, T& result) noexcept {
    if constexpr (std::is_unsigned_v<T>) {
        if (a != 0 && b > std::numeric_limits<T>::max() / a) {
            return false;
        }
        result = a * b;
        return true;
    } else {
        if (a > 0) {
            if (b > 0) {
                if (a > std::numeric_limits<T>::max() / b) return false;
            } else {
                if (b < std::numeric_limits<T>::min() / a) return false;
            }
        } else {
            if (b > 0) {
                if (a < std::numeric_limits<T>::min() / b) return false;
            } else {
                if (a != 0 && b < std::numeric_limits<T>::max() / a) return false;
            }
        }
        result = a * b;
        return true;
    }
}

template<typename T>
requires std::is_integral_v<T>
inline std::optional<T> checked_mul(T a, T b) noexcept {
    T res{};
    if (checked_mul(a, b, res)) {
        return res;
    }
    return std::nullopt;
}

// 6. BinaryReader with strict bounds checking
class BinaryReader {
public:
    BinaryReader(const void* data, std::size_t size) noexcept
        : data_(static_cast<const std::uint8_t*>(data)), size_(size), offset_(0) {}

    std::size_t remaining() const noexcept {
        return (offset_ <= size_) ? (size_ - offset_) : 0;
    }

    std::size_t offset() const noexcept { return offset_; }
    std::size_t total_size() const noexcept { return size_; }

    bool has_bytes(std::size_t count) const noexcept {
        return remaining() >= count;
    }

    bool seek(std::size_t offset) noexcept {
        if (offset > size_) return false;
        offset_ = offset;
        return true;
    }

    bool skip(std::size_t count) noexcept {
        if (!has_bytes(count)) return false;
        offset_ += count;
        return true;
    }

    bool read_bytes(void* dest, std::size_t count) noexcept;
    bool read_u8(std::uint8_t& val) noexcept;
    bool read_u16_le(std::uint16_t& val) noexcept;
    bool read_u32_le(std::uint32_t& val) noexcept;
    bool read_u64_le(std::uint64_t& val) noexcept;
    bool read_string(std::string& out, std::size_t length) noexcept;
    bool read_slice(const std::uint8_t*& out_ptr, std::size_t count) noexcept;
    std::span<const std::uint8_t> read_span(std::size_t count) noexcept;

private:
    const std::uint8_t* data_{nullptr};
    std::size_t size_{0};
    std::size_t offset_{0};
};

} // namespace winios::security
