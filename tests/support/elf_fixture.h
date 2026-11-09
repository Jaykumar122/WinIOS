#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace elf_test {
inline void put(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8u * i));
    }
}
struct Segment {
    std::uint64_t address{0x1000};
    std::uint64_t size{0x40};
    std::uint32_t flags{5};
    std::vector<std::uint32_t> words{0xD4400000u};
    std::uint32_t type{1};
};
inline std::vector<std::uint8_t> image(const std::vector<Segment>& segments = {Segment{}},
                                       std::uint64_t entry = 0x1000) {
    const std::size_t data_start = 64 + segments.size() * 56;
    std::vector<std::uint8_t> bytes(data_start, 0);
    put(bytes, 0, 0x464C457F, 4);
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    put(bytes, 16, 2, 2); put(bytes, 18, 183, 2); put(bytes, 20, 1, 4);
    put(bytes, 24, entry, 8); put(bytes, 32, 64, 8);
    put(bytes, 52, 64, 2); put(bytes, 54, 56, 2);
    put(bytes, 56, segments.size(), 2);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const std::size_t header = 64 + i * 56;
        const std::size_t offset = bytes.size();
        bytes.resize(offset + segment.words.size() * 4, 0);
        put(bytes, header, segment.type, 4); put(bytes, header + 4, segment.flags, 4);
        put(bytes, header + 8, offset, 8); put(bytes, header + 16, segment.address, 8);
        put(bytes, header + 32, segment.words.size() * 4, 8);
        put(bytes, header + 40, segment.size, 8); put(bytes, header + 48, 1, 8);
        for (std::size_t j = 0; j < segment.words.size(); ++j) {
            put(bytes, offset + j * 4, segment.words[j], 4);
        }
    }
    return bytes;
}
// An atomically created private directory avoids collisions across CTest processes.
class File {
public:
    File() {
        static std::atomic<unsigned> sequence{0};
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory_ = std::filesystem::temp_directory_path()
                / ("winios_elf_" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(sequence++));
            if (std::filesystem::create_directory(directory_)) return;
        }
        throw std::runtime_error("Unable to create ELF test directory");
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { std::error_code ignored; std::filesystem::remove_all(directory_, ignored); }
    std::filesystem::path path() const { return directory_ / "fixture with spaces.elf"; }
    void write(const std::vector<std::uint8_t>& bytes) const {
        std::ofstream output(path(), std::ios::binary);
        if (!output.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("Unable to write ELF fixture");
        }
    }
private:
    std::filesystem::path directory_;
};
}
