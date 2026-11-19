#include "security/security_utils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace winios::security {

void secure_zero_memory(void* ptr, std::size_t size) noexcept {
    if (!ptr || size == 0) return;
#if defined(_WIN32)
    SecureZeroMemory(ptr, size);
#else
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#endif
}

bool constant_time_equals(const void* a, const void* b, std::size_t len) noexcept {
    if (!a || !b) {
        return (a == b) && (len == 0);
    }
    const auto* p1 = static_cast<const volatile unsigned char*>(a);
    const auto* p2 = static_cast<const volatile unsigned char*>(b);
    volatile unsigned char diff = 0;
    for (std::size_t i = 0; i < len; ++i) {
        diff |= (p1[i] ^ p2[i]);
    }
    return diff == 0;
}

bool constant_time_equals(const std::string& a, const std::string& b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return constant_time_equals(a.data(), b.data(), a.size());
}

bool constant_time_equals(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return constant_time_equals(a.data(), b.data(), a.size());
}

bool SecureRandom::get_random_bytes(void* dest, std::size_t size) noexcept {
    if (size == 0) return true;
    if (!dest) return false;

#if defined(_WIN32)
    std::size_t remaining = size;
    auto* cur = static_cast<PUCHAR>(dest);
    while (remaining > 0) {
        ULONG chunk = static_cast<ULONG>((remaining > 0xFFFFFFFF) ? 0xFFFFFFFF : remaining);
        NTSTATUS status = BCryptGenRandom(
            nullptr,
            cur,
            chunk,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        );
        if (!BCRYPT_SUCCESS(status)) {
            return false;
        }
        cur += chunk;
        remaining -= chunk;
    }
    return true;
#else
    return false;
#endif
}

std::uint64_t SecureRandom::get_random_u64() {
    std::uint64_t val = 0;
    if (!get_random_bytes(&val, sizeof(val))) {
        throw std::runtime_error("SecureRandom::get_random_u64 failed");
    }
    return val;
}

std::uint32_t SecureRandom::get_random_u32() {
    std::uint32_t val = 0;
    if (!get_random_bytes(&val, sizeof(val))) {
        throw std::runtime_error("SecureRandom::get_random_u32 failed");
    }
    return val;
}

std::string SecureRandom::get_random_hex(std::size_t byte_count) {
    if (byte_count == 0) return "";
    std::vector<std::uint8_t> buf(byte_count);
    if (!get_random_bytes(buf.data(), byte_count)) {
        throw std::runtime_error("SecureRandom::get_random_hex failed");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t b : buf) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    secure_zero_memory(buf.data(), buf.size());
    return oss.str();
}

SecureBuffer::SecureBuffer(std::size_t size) {
    if (size > 0) {
        data_ = new std::uint8_t[size]();
        size_ = size;
    }
}

SecureBuffer::SecureBuffer(const void* src, std::size_t size) {
    if (src && size > 0) {
        data_ = new std::uint8_t[size];
        size_ = size;
        std::memcpy(data_, src, size);
    }
}

SecureBuffer::~SecureBuffer() noexcept {
    clear();
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        clear();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void SecureBuffer::clear() noexcept {
    if (data_) {
        secure_zero_memory(data_, size_);
        delete[] data_;
        data_ = nullptr;
        size_ = 0;
    }
}

void SecureBuffer::resize(std::size_t new_size) {
    if (new_size == size_) return;
    if (new_size == 0) {
        clear();
        return;
    }
    auto* new_data = new std::uint8_t[new_size]();
    if (data_) {
        std::size_t copy_len = (size_ < new_size) ? size_ : new_size;
        std::memcpy(new_data, data_, copy_len);
        secure_zero_memory(data_, size_);
        delete[] data_;
    }
    data_ = new_data;
    size_ = new_size;
}

bool BinaryReader::read_bytes(void* dest, std::size_t count) noexcept {
    if (!has_bytes(count) || (!dest && count > 0)) {
        return false;
    }
    if (count > 0) {
        std::memcpy(dest, data_ + offset_, count);
        offset_ += count;
    }
    return true;
}

bool BinaryReader::read_u8(std::uint8_t& val) noexcept {
    return read_bytes(&val, sizeof(val));
}

bool BinaryReader::read_u16_le(std::uint16_t& val) noexcept {
    std::uint8_t b[2];
    if (!read_bytes(b, 2)) return false;
    val = static_cast<std::uint16_t>(b[0]) |
          (static_cast<std::uint16_t>(b[1]) << 8);
    return true;
}

bool BinaryReader::read_u32_le(std::uint32_t& val) noexcept {
    std::uint8_t b[4];
    if (!read_bytes(b, 4)) return false;
    val = static_cast<std::uint32_t>(b[0]) |
          (static_cast<std::uint32_t>(b[1]) << 8) |
          (static_cast<std::uint32_t>(b[2]) << 16) |
          (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

bool BinaryReader::read_u64_le(std::uint64_t& val) noexcept {
    std::uint8_t b[8];
    if (!read_bytes(b, 8)) return false;
    val = static_cast<std::uint64_t>(b[0]) |
          (static_cast<std::uint64_t>(b[1]) << 8) |
          (static_cast<std::uint64_t>(b[2]) << 16) |
          (static_cast<std::uint64_t>(b[3]) << 24) |
          (static_cast<std::uint64_t>(b[4]) << 32) |
          (static_cast<std::uint64_t>(b[5]) << 40) |
          (static_cast<std::uint64_t>(b[6]) << 48) |
          (static_cast<std::uint64_t>(b[7]) << 56);
    return true;
}

bool BinaryReader::read_string(std::string& out, std::size_t length) noexcept {
    if (!has_bytes(length)) return false;
    out.assign(reinterpret_cast<const char*>(data_ + offset_), length);
    offset_ += length;
    return true;
}

bool BinaryReader::read_slice(const std::uint8_t*& out_ptr, std::size_t count) noexcept {
    if (!has_bytes(count)) {
        out_ptr = nullptr;
        return false;
    }
    out_ptr = data_ + offset_;
    offset_ += count;
    return true;
}

std::span<const std::uint8_t> BinaryReader::read_span(std::size_t count) noexcept {
    const std::uint8_t* ptr = nullptr;
    if (!read_slice(ptr, count)) {
        return {};
    }
    return {ptr, count};
}

} // namespace winios::security
