#include "elf_loader.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::uint32_t kElfMagic = 0x464C457Fu;
constexpr std::uint16_t kElfClass64 = 2;
constexpr std::uint16_t kElfDataLittle = 1;
constexpr std::uint16_t kElfVersionCurrent = 1;
constexpr std::uint16_t kElfTypeExecutable = 2;
constexpr std::uint16_t kElfMachineAArch64 = 183;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPfExecute = 1;

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(bytes[offset + 1]) << 8u;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | static_cast<std::uint32_t>(bytes[offset + 1]) << 8u
        | static_cast<std::uint32_t>(bytes[offset + 2]) << 16u
        | static_cast<std::uint32_t>(bytes[offset + 3]) << 24u;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
    }
    return value;
}

bool add_overflows(std::uint64_t start, std::uint64_t size) {
    return size > std::numeric_limits<std::uint64_t>::max() - start;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                    std::size_t max_file_size) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Unable to open ELF: " + path.string());
    }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end)
        > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("ELF is too large for host memory");
    }
    if (static_cast<std::uintmax_t>(end) > max_file_size
        || static_cast<std::uintmax_t>(end)
            > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::length_error("ELF exceeds configured file-size limit");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty()
        && !input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Unable to read ELF: " + path.string());
    }
    return bytes;
}
}

LoadedElf ElfLoader::load(const std::filesystem::path& path, Memory& memory,
                          std::size_t max_file_size) {
    const std::vector<std::uint8_t> bytes = read_file(path, max_file_size);
    if (bytes.size() < kElfHeaderSize) {
        throw std::invalid_argument("ELF header is truncated");
    }
    if (read_u32(bytes, 0) != kElfMagic
        || bytes[4] != kElfClass64
        || bytes[5] != kElfDataLittle
        || bytes[6] != kElfVersionCurrent) {
        throw std::invalid_argument("Unsupported ELF identification");
    }
    if (read_u16(bytes, 16) != kElfTypeExecutable
        || read_u16(bytes, 18) != kElfMachineAArch64
        || read_u32(bytes, 20) != kElfVersionCurrent
        || read_u16(bytes, 52) != kElfHeaderSize
        || read_u16(bytes, 54) != kProgramHeaderSize) {
        throw std::invalid_argument("Unsupported ELF format");
    }

    const std::uint64_t entry = read_u64(bytes, 24);
    const std::uint64_t phoff = read_u64(bytes, 32);
    const std::uint16_t phentsize = read_u16(bytes, 54);
    const std::uint16_t phnum = read_u16(bytes, 56);
    if (phnum == 0 || phnum == 0xFFFFu || phentsize != kProgramHeaderSize
        || phoff < kElfHeaderSize || phoff > bytes.size()
        || static_cast<std::uint64_t>(phnum) * phentsize
               > bytes.size() - phoff) {
        throw std::invalid_argument("Invalid ELF program-header table");
    }
    if (entry >= memory.size() || entry % 4u != 0u) {
        throw std::out_of_range("ELF entry point is outside guest memory");
    }

    LoadedElf result;
    result.entry_address = entry;
    for (std::uint16_t index = 0; index < phnum; ++index) {
        const std::size_t offset = static_cast<std::size_t>(
            phoff + static_cast<std::uint64_t>(index) * phentsize);
        const std::uint32_t type = read_u32(bytes, offset);
        if (type == 2 || type == 3 || type == 7) { // PT_DYNAMIC, PT_INTERP, PT_TLS
            throw std::invalid_argument("ELF requires unsupported dynamic linking or TLS");
        }
        if (type != kPtLoad) {
            continue;
        }
        const std::uint32_t flags = read_u32(bytes, offset + 4);
        const std::uint64_t file_offset = read_u64(bytes, offset + 8);
        const std::uint64_t virtual_address = read_u64(bytes, offset + 16);
        const std::uint64_t file_size = read_u64(bytes, offset + 32);
        const std::uint64_t memory_size = read_u64(bytes, offset + 40);
        const std::uint64_t alignment = read_u64(bytes, offset + 48);
        if (file_size > memory_size || file_offset > bytes.size()
            || file_size > bytes.size() - file_offset
            || add_overflows(virtual_address, memory_size)
            || virtual_address + memory_size > memory.size()
            || (alignment != 0 && (alignment & (alignment - 1u)) != 0)
            || (alignment > 1
                && (virtual_address % alignment) != (file_offset % alignment))) {
            throw std::invalid_argument("Invalid ELF load segment");
        }
        if (memory_size == 0) {
            continue;
        }
        if ((flags & kPfExecute) != 0 && virtual_address % 4u != 0u) {
            throw std::invalid_argument("ELF load segment is not instruction aligned");
        }
        const std::uint64_t segment_end = virtual_address + memory_size;
        for (const LoadedElfSegment& existing : result.segments) {
            if (virtual_address < existing.address
                + existing.size && existing.address < segment_end) {
                throw std::invalid_argument("Overlapping ELF load segments");
            }
        }
        result.segments.push_back({virtual_address, memory_size, flags});
        if (memory_size > std::numeric_limits<std::size_t>::max() - result.loaded_bytes) {
            throw std::length_error("ELF loaded size overflows host size");
        }
        result.loaded_bytes += static_cast<std::size_t>(memory_size);
    }
    if (result.segments.empty()) {
        throw std::invalid_argument("ELF contains no loadable segments");
    }
    const auto entry_segment = std::find_if(
        result.segments.begin(), result.segments.end(),
        [entry](const LoadedElfSegment& segment) {
            return (segment.flags & kPfExecute) != 0
                && entry >= segment.address
                && entry < segment.address + segment.size
                && segment.address + segment.size - entry >= 4u;
        });
    if (entry_segment == result.segments.end()) {
        throw std::invalid_argument("ELF entry point is not executable");
    }

    // All validation is complete before guest memory is modified.
    for (std::uint16_t index = 0; index < phnum; ++index) {
        const std::size_t offset = static_cast<std::size_t>(
            phoff + static_cast<std::uint64_t>(index) * phentsize);
        if (read_u32(bytes, offset) != kPtLoad) {
            continue;
        }
        const std::uint64_t file_offset = read_u64(bytes, offset + 8);
        const std::uint64_t virtual_address = read_u64(bytes, offset + 16);
        const std::uint64_t file_size = read_u64(bytes, offset + 32);
        const std::uint64_t memory_size = read_u64(bytes, offset + 40);
        if (memory_size == 0) {
            continue;
        }
        for (std::uint64_t byte = 0; byte < file_size; ++byte) {
            memory.write_u8(static_cast<std::size_t>(virtual_address + byte),
                            bytes[static_cast<std::size_t>(file_offset + byte)]);
        }
        for (std::uint64_t byte = file_size; byte < memory_size; ++byte) {
            memory.write_u8(static_cast<std::size_t>(virtual_address + byte), 0);
        }
    }
    return result;
}
