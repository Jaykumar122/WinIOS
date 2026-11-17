#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::crypto {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t len);
    void finalize(std::uint8_t hash[32]);

    // Convenience one-shot hashing
    static std::string hash_hex(const void* data, std::size_t len);
    static std::string hash_hex(const std::string& str);
    static std::string hash_hex(const std::vector<std::uint8_t>& data);

private:
    void transform(const std::uint8_t block[64]);

    std::uint32_t state_[8];
    std::uint64_t bit_count_{0};
    std::uint8_t  buffer_[64];
};

} // namespace winios::crypto
