#pragma once

#include "memory/memory.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

struct LoadedBinary {
    std::uint64_t entry_address{};
    std::size_t loaded_bytes{};
};

class BinaryLoader {
public:
    // Loads a raw little-endian AArch64 instruction binary into guest memory.
    // The input is not an ELF, Mach-O, IPSW, or application package.
    static LoadedBinary load(const std::filesystem::path& path,
                             Memory& memory,
                             std::uint64_t guest_address);
};
