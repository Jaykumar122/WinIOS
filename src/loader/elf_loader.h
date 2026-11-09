#pragma once

#include "memory/memory.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

struct LoadedElfSegment {
    std::uint64_t address{};
    std::uint64_t size{};
    std::uint32_t flags{};
};

struct LoadedElf {
    std::uint64_t entry_address{};
    std::size_t loaded_bytes{};
    std::vector<LoadedElfSegment> segments;
};

class ElfLoader {
public:
    // Loads a bounded, static, little-endian AArch64 ELF64 ET_EXEC image.
    // Interpreter, dynamic-linking, and TLS program headers are rejected.
    // Informational headers/sections are ignored; relocations are not applied.
    static constexpr std::size_t kDefaultMaxFileSize = 64u * 1024u * 1024u;
    static LoadedElf load(const std::filesystem::path& path, Memory& memory,
                          std::size_t max_file_size = kDefaultMaxFileSize);
};
