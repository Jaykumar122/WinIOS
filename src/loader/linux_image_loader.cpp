#include "loader/linux_image_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::loader {

namespace {

// -----------------------------------------------------------------------------
// Standard Self-Contained SHA-256 Implementation (FIPS 180-4)
// -----------------------------------------------------------------------------
inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}
inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}
inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}
inline std::uint32_t sig0(std::uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}
inline std::uint32_t sig1(std::uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}
inline std::uint32_t theta0(std::uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
inline std::uint32_t theta1(std::uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

const std::uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_transform(std::uint32_t state[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t t1 = h + sig1(e) + ch(e, f, g) + kSha256K[i] + w[i];
        std::uint32_t t2 = sig0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string compute_sha256_internal(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    std::size_t cur = 0;
    while (cur + 64 <= size) {
        sha256_transform(state, p + cur);
        cur += 64;
    }

    std::uint8_t pad[128] = {0};
    std::size_t rem = size - cur;
    if (rem > 0) {
        std::memcpy(pad, p + cur, rem);
    }
    pad[rem] = 0x80;

    std::size_t pad_len = (rem < 56) ? 64 : 128;
    std::uint64_t total_bits = static_cast<std::uint64_t>(size) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        pad[pad_len - 1 - i] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xFFu);
    }

    sha256_transform(state, pad);
    if (pad_len == 128) {
        sha256_transform(state, pad + 64);
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        ss << std::setw(8) << state[i];
    }
    return ss.str();
}

} // anonymous namespace

std::string LinuxImageLoader::compute_sha256(const void* data, std::size_t size) {
    if (!data || size == 0) return "";
    return compute_sha256_internal(data, size);
}

LinuxKernelLoadResult LinuxImageLoader::load_from_buffer(
    const void* data,
    std::size_t size,
    Memory& guest_mem,
    std::uint64_t ram_base,
    std::uint64_t ram_size) {

    LinuxKernelLoadResult result{};

    if (!data || size < sizeof(LinuxArm64ImageHeader)) {
        result.status = LinuxImageLoadStatus::TruncatedHeader;
        result.error_message = "Buffer too small for Linux ARM64 image header";
        return result;
    }

    // 1. Parse header
    std::string err;
    const auto* src = static_cast<const std::uint8_t*>(data);
    if (!LinuxBootProtocolValidator::parse_header(src, size, result.header, err)) {
        if (err.find("magic") != std::string::npos) {
            result.status = LinuxImageLoadStatus::InvalidMagic;
        } else if (err.find("big-endian") != std::string::npos || err.find("Big-endian") != std::string::npos) {
            result.status = LinuxImageLoadStatus::BigEndianUnsupported;
        } else {
            result.status = LinuxImageLoadStatus::TruncatedHeader;
        }
        result.error_message = err;
        return result;
    }

    // 2. Validate placement against physical RAM
    std::uint64_t load_address = ram_base + result.header.text_offset;
    if (!LinuxBootProtocolValidator::validate_placement(result.header, ram_base, ram_size, load_address, err)) {
        if (err.find("aligned") != std::string::npos) {
            result.status = LinuxImageLoadStatus::MisalignedRamBase;
        } else {
            result.status = LinuxImageLoadStatus::RamCapacityExceeded;
        }
        result.error_message = err;
        return result;
    }

    // 3. Determine memory write offset (supports both flat 1:1 and base-relative RAM models)
    std::uint64_t mem_offset = load_address;
    if (guest_mem.size() < load_address + result.header.image_size) {
        if (load_address >= ram_base && (load_address - ram_base + result.header.image_size) <= guest_mem.size()) {
            mem_offset = load_address - ram_base;
        } else {
            result.status = LinuxImageLoadStatus::MemoryWriteFailed;
            result.error_message = "Guest memory capacity too small for kernel image";
            return result;
        }
    }

    // 4. Write kernel image bytes to guest RAM
    for (std::size_t i = 0; i < size; ++i) {
        guest_mem.write_u8(mem_offset + i, src[i]);
    }

    // 5. Zero-fill BSS up to image_size if image_size > size
    if (result.header.image_size > size) {
        std::uint64_t bss_start = mem_offset + size;
        std::uint64_t bss_end = mem_offset + result.header.image_size;
        for (std::uint64_t addr = bss_start; addr < bss_end; ++addr) {
            guest_mem.write_u8(addr, 0);
        }
    }

    // 5. Populate success result
    result.status = LinuxImageLoadStatus::Success;
    result.load_address = load_address;
    result.entry_point = load_address;
    result.effective_size = result.header.image_size;
    result.file_bytes_loaded = size;
    result.sha256_hex = compute_sha256_internal(data, size);

    return result;
}

LinuxKernelLoadResult LinuxImageLoader::load_from_file(
    const std::string& path,
    Memory& guest_mem,
    std::uint64_t ram_base,
    std::uint64_t ram_size) {

    LinuxKernelLoadResult result{};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.status = LinuxImageLoadStatus::FileNotFound;
        result.error_message = "Cannot open kernel image file: " + path;
        return result;
    }

    std::streamsize file_size = file.tellg();
    if (file_size < static_cast<std::streamsize>(sizeof(LinuxArm64ImageHeader))) {
        result.status = LinuxImageLoadStatus::TruncatedHeader;
        result.error_message = "Kernel file too small for header";
        return result;
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        result.status = LinuxImageLoadStatus::FileReadError;
        result.error_message = "Failed to read kernel file: " + path;
        return result;
    }

    return load_from_buffer(buffer.data(), buffer.size(), guest_mem, ram_base, ram_size);
}

} // namespace winios::loader
