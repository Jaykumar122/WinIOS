#include "binary_loader.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

LoadedBinary BinaryLoader::load(const std::filesystem::path& path,
                                Memory& memory,
                                std::uint64_t guest_address) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Unable to open raw binary: " + path.string());
    }

    const std::streampos end = input.tellg();
    const std::streamoff file_size = static_cast<std::streamoff>(end);
    if (file_size <= 0) {
        throw std::invalid_argument("Raw binary is empty: " + path.string());
    }
    if (static_cast<std::uintmax_t>(file_size)
        > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("Raw binary is too large for host memory");
    }
    const std::size_t byte_count = static_cast<std::size_t>(file_size);
    std::vector<std::uint8_t> bytes(byte_count);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Unable to read raw binary: " + path.string());
    }

    if (guest_address > memory.size()
        || byte_count > memory.size() - static_cast<std::size_t>(guest_address)) {
        throw std::out_of_range("Raw binary does not fit in guest memory");
    }
    if (guest_address % 4u != 0 || byte_count < 4u || byte_count % 4u != 0) {
        throw std::invalid_argument(
            "Raw binary load address and size must contain 32-bit instructions");
    }

    const std::size_t destination = static_cast<std::size_t>(guest_address);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        memory.write_u8(destination + index, bytes[index]);
    }
    return LoadedBinary{guest_address, byte_count};
}
