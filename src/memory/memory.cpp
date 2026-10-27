#include "memory.h"
#include <sstream>
#include <stdexcept>

namespace {
constexpr std::size_t kWord32Width = 4;
constexpr std::size_t kWord64Width = 8;
constexpr unsigned kBitsPerByte = 8;
} // namespace

Memory::Memory(std::size_t size) {
    if (size == 0) {
        throw std::invalid_argument("Memory size must be greater than zero");
    }
    bytes_.assign(size, std::uint8_t{0});
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

std::uint8_t Memory::read_u8(std::size_t offset) const {
    require_range(offset, 1);
    return bytes_[offset];
}

void Memory::write_u8(std::size_t offset, std::uint8_t value) {
    require_range(offset, 1);
    bytes_[offset] = value;
}

std::uint32_t Memory::read_u32(std::size_t offset) const {
    require_range(offset, kWord32Width);

    std::uint32_t value = 0;
    for (std::size_t i = 0; i < kWord32Width; ++i) {
        value |= static_cast<std::uint32_t>(bytes_[offset + i])
                 << static_cast<unsigned>(i * kBitsPerByte);
    }
    return value;
}

void Memory::write_u32(std::size_t offset, std::uint32_t value) {
    require_range(offset, kWord32Width);

    for (std::size_t i = 0; i < kWord32Width; ++i) {
        bytes_[offset + i] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>(i * kBitsPerByte));
    }
}

std::uint64_t Memory::read_u64(std::size_t offset) const {
    require_range(offset, kWord64Width);

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < kWord64Width; ++i) {
        value |= static_cast<std::uint64_t>(bytes_[offset + i])
                 << static_cast<unsigned>(i * kBitsPerByte);
    }
    return value;
}

void Memory::write_u64(std::size_t offset, std::uint64_t value) {
    require_range(offset, kWord64Width);

    for (std::size_t i = 0; i < kWord64Width; ++i) {
        bytes_[offset + i] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>(i * kBitsPerByte));
    }
}
