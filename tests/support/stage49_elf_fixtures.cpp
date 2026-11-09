#include "elf_fixture.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto write = [&](const char* name, const std::vector<std::uint8_t>& bytes) {
            std::ofstream output(directory / name, std::ios::binary);
            if (!output.write(reinterpret_cast<const char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()))) {
                throw std::runtime_error("Unable to write CLI fixture");
            }
        };
        elf_test::Segment code;
        code.words = {0xD2800540u, 0xD2802001u, 0xF9000020u, 0xD4400000u};
        write("valid with spaces.elf", elf_test::image({code}));
        code.words = {0x14000000u};
        write("loop.elf", elf_test::image({code}));
        code.words = {0};
        write("unsupported.elf", elf_test::image({code}));
        code.words = {0xD2900001u, 0xF9400020u}; // X1=0x8000, outside CLI test RAM
        write("fault.elf", elf_test::image({code}));
        code = elf_test::Segment{}; code.address = 0x7000;
        write("high.elf", elf_test::image({code}, 0x7000));
        write("malformed.elf", {0x7F, 'E', 'L', 'F'});
        write("raw.bin", {0x00, 0x00, 0x00, 0x14}); // B . for raw budget regression
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
