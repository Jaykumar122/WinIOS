#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Flat guest RAM. All multi-byte accesses use little-endian byte order and are
// bounds-checked; unaligned accesses are permitted at this layer.
class Memory {
public:
    // Creates guest RAM of `size` bytes, all initialized to zero.
    // Throws std::invalid_argument when `size` is zero.
    explicit Memory(std::size_t size);

    std::size_t size() const noexcept { return bytes_.size(); }

    std::uint8_t read_u8(std::size_t offset) const;
    void write_u8(std::size_t offset, std::uint8_t value);

    std::uint32_t read_u32(std::size_t offset) const;
    void write_u32(std::size_t offset, std::uint32_t value);

    std::uint64_t read_u64(std::size_t offset) const;
    void write_u64(std::size_t offset, std::uint64_t value);

private:
    // Throws std::out_of_range unless [offset, offset + width) lies inside
    // the buffer. The check cannot overflow: `offset > size` is rejected
    // before `size - offset` is evaluated.
    void require_range(std::size_t offset, std::size_t width) const;

    std::vector<std::uint8_t> bytes_;
};
