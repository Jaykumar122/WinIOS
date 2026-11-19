#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::crypto {

class Sha512 {
public:
    Sha512();
    void update(const void* data, std::size_t len);
    void finalize(std::uint8_t hash[64]);

    static void hash(const void* data, std::size_t len, std::uint8_t hash_out[64]);
    static std::string hash_hex(const void* data, std::size_t len);

private:
    void transform(const std::uint8_t block[128]);

    std::uint64_t state_[8];
    std::uint64_t total_bits_low_{0};
    std::uint64_t total_bits_high_{0};
    std::uint8_t  buffer_[128];
    std::size_t   buffer_len_{0};
};

} // namespace winios::crypto
