#include "crypto/sha512.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::crypto {

namespace {

inline std::uint64_t rotr(std::uint64_t x, unsigned int n) noexcept {
    return (x >> n) | (x << (64 - n));
}

inline std::uint64_t ch(std::uint64_t x, std::uint64_t y, std::uint64_t z) noexcept {
    return (x & y) ^ (~x & z);
}

inline std::uint64_t maj(std::uint64_t x, std::uint64_t y, std::uint64_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline std::uint64_t sigma0(std::uint64_t x) noexcept {
    return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39);
}

inline std::uint64_t sigma1(std::uint64_t x) noexcept {
    return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41);
}

inline std::uint64_t gamma0(std::uint64_t x) noexcept {
    return rotr(x, 1) ^ rotr(x, 8) ^ (x >> 7);
}

inline std::uint64_t gamma1(std::uint64_t x) noexcept {
    return rotr(x, 19) ^ rotr(x, 61) ^ (x >> 6);
}

const std::uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

} // namespace

Sha512::Sha512() {
    state_[0] = 0x6a09e667f3bcc908ULL;
    state_[1] = 0xbb67ae8584caa73bULL;
    state_[2] = 0x3c6ef372fe94f82bULL;
    state_[3] = 0xa54ff53a5f1d36f1ULL;
    state_[4] = 0x510e527fade682d1ULL;
    state_[5] = 0x9b05688c2b3e6c1fULL;
    state_[6] = 0x1f83d9abfb41bd6bULL;
    state_[7] = 0x5be0cd19137e2179ULL;
    total_bits_low_ = 0;
    total_bits_high_ = 0;
    buffer_len_ = 0;
}

void Sha512::transform(const std::uint8_t block[128]) {
    std::uint64_t w[80];
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint64_t>(block[i * 8 + 0]) << 56) |
               (static_cast<std::uint64_t>(block[i * 8 + 1]) << 48) |
               (static_cast<std::uint64_t>(block[i * 8 + 2]) << 40) |
               (static_cast<std::uint64_t>(block[i * 8 + 3]) << 32) |
               (static_cast<std::uint64_t>(block[i * 8 + 4]) << 24) |
               (static_cast<std::uint64_t>(block[i * 8 + 5]) << 16) |
               (static_cast<std::uint64_t>(block[i * 8 + 6]) << 8)  |
               (static_cast<std::uint64_t>(block[i * 8 + 7]));
    }
    for (std::size_t i = 16; i < 80; ++i) {
        w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
    }

    std::uint64_t a = state_[0];
    std::uint64_t b = state_[1];
    std::uint64_t c = state_[2];
    std::uint64_t d = state_[3];
    std::uint64_t e = state_[4];
    std::uint64_t f = state_[5];
    std::uint64_t g = state_[6];
    std::uint64_t h = state_[7];

    for (std::size_t i = 0; i < 80; ++i) {
        std::uint64_t t1 = h + sigma1(e) + ch(e, f, g) + K512[i] + w[i];
        std::uint64_t t2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha512::update(const void* data, std::size_t len) {
    if (!data || len == 0) return;
    const auto* p = static_cast<const std::uint8_t*>(data);

    std::uint64_t bits = static_cast<std::uint64_t>(len) * 8ULL;
    total_bits_low_ += bits;
    if (total_bits_low_ < bits) {
        total_bits_high_++;
    }
    total_bits_high_ += (static_cast<std::uint64_t>(len) >> 61);

    std::size_t offset = 0;
    if (buffer_len_ > 0) {
        std::size_t to_copy = std::min(len, 128 - buffer_len_);
        std::memcpy(buffer_ + buffer_len_, p, to_copy);
        buffer_len_ += to_copy;
        offset += to_copy;
        if (buffer_len_ == 128) {
            transform(buffer_);
            buffer_len_ = 0;
        }
    }

    while (offset + 128 <= len) {
        transform(p + offset);
        offset += 128;
    }

    if (offset < len) {
        std::memcpy(buffer_, p + offset, len - offset);
        buffer_len_ = len - offset;
    }
}

void Sha512::finalize(std::uint8_t hash[64]) {
    buffer_[buffer_len_++] = 0x80;
    if (buffer_len_ > 112) {
        std::memset(buffer_ + buffer_len_, 0, 128 - buffer_len_);
        transform(buffer_);
        buffer_len_ = 0;
    }
    std::memset(buffer_ + buffer_len_, 0, 112 - buffer_len_);

    // 128-bit length big-endian
    for (int i = 0; i < 8; ++i) {
        buffer_[112 + i] = static_cast<std::uint8_t>((total_bits_high_ >> (56 - i * 8)) & 0xFF);
        buffer_[120 + i] = static_cast<std::uint8_t>((total_bits_low_ >> (56 - i * 8)) & 0xFF);
    }
    transform(buffer_);

    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t j = 0; j < 8; ++j) {
            hash[i * 8 + j] = static_cast<std::uint8_t>((state_[i] >> (56 - j * 8)) & 0xFF);
        }
    }
}

void Sha512::hash(const void* data, std::size_t len, std::uint8_t hash_out[64]) {
    Sha512 ctx;
    ctx.update(data, len);
    ctx.finalize(hash_out);
}

std::string Sha512::hash_hex(const void* data, std::size_t len) {
    std::uint8_t h[64];
    hash(data, len, h);
    std::ostringstream oss;
    for (std::size_t i = 0; i < 64; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(h[i]);
    }
    return oss.str();
}

} // namespace winios::crypto
