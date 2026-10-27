#include "cpu/cpu.h"
#include "loader/binary_loader.h"
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

class TemporaryFile {
public:
    explicit TemporaryFile(const std::string& suffix)
        : path_(std::filesystem::temp_directory_path()
                / ("winios_loader_test_" + suffix + ".bin")) {}
    ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void valid_binary_loads_exact_bytes() {
    TemporaryFile file("valid");
    const std::vector<std::uint8_t> bytes{0x40, 0x05, 0x80, 0xD2, 0x00, 0x14, 0x00, 0x91};
    write_file(file.path(), bytes);
    Memory memory(32);

    const LoadedBinary loaded = BinaryLoader::load(file.path(), memory, 8);

    CHECK(loaded.entry_address == 8);
    CHECK(loaded.loaded_bytes == bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        CHECK(memory.read_u8(8 + index) == bytes[index]);
    }
    CHECK(memory.read_u8(0) == 0);
}

void missing_and_empty_files_fail() {
    Memory memory(16);
    CHECK_RUNTIME_ERROR(BinaryLoader::load("does-not-exist.bin", memory, 0));
    TemporaryFile file("empty");
    write_file(file.path(), {});
    CHECK_INVALID_ARGUMENT(BinaryLoader::load(file.path(), memory, 0));
}

void oversized_and_invalid_loads_do_not_modify_memory() {
    TemporaryFile file("oversized");
    write_file(file.path(), {1, 2, 3, 4, 5});
    Memory memory(8);
    memory.write_u8(0, 0xAA);

    CHECK_OUT_OF_RANGE(BinaryLoader::load(file.path(), memory, 4));
    CHECK(memory.read_u8(0) == 0xAA);
    CHECK(memory.read_u8(4) == 0);
}

void invalid_address_and_entry_are_rejected() {
    TemporaryFile file("address");
    write_file(file.path(), {1, 2, 3, 4});
    Memory memory(16);

    CHECK_INVALID_ARGUMENT(BinaryLoader::load(file.path(), memory, 2));
    CHECK_OUT_OF_RANGE(BinaryLoader::load(file.path(), memory, 14));
    CHECK_OUT_OF_RANGE(BinaryLoader::load(
        file.path(), memory, std::numeric_limits<std::uint64_t>::max()));
    CPU cpu;
    CHECK_INVALID_ARGUMENT(cpu.initialize_entry(2, memory));
    CHECK_INVALID_ARGUMENT(cpu.initialize_entry(16, memory));
}

void loading_at_memory_end_is_checked() {
    TemporaryFile file("end");
    write_file(file.path(), {1, 2, 3, 4});
    Memory memory(8);

    const LoadedBinary loaded = BinaryLoader::load(file.path(), memory, 4);
    CHECK(loaded.entry_address == 4);
    CHECK(loaded.loaded_bytes == 4);
    CHECK(memory.read_u32(4) == 0x04030201u);

    CHECK_OUT_OF_RANGE(BinaryLoader::load(file.path(), memory, 8));
    CHECK_INVALID_ARGUMENT(BinaryLoader::load(file.path(), memory, 1));
}

void loaded_program_starts_at_entry_and_executes() {
    TemporaryFile file("program");
    const std::vector<std::uint8_t> bytes{
        0x40, 0x05, 0x80, 0xD2, // MOVZ X0, #42
        0x00, 0x14, 0x00, 0x91, // ADD X0, X0, #5
        0x01, 0x20, 0x80, 0xD2, // MOVZ X1, #0x100
        0x20, 0x00, 0x00, 0xF9, // STR X0, [X1, #0]
        0x00, 0x00, 0x00, 0x14  // B .
    };
    write_file(file.path(), bytes);
    Memory memory(512);
    const LoadedBinary loaded = BinaryLoader::load(file.path(), memory, 0);
    CPU cpu;
    cpu.write_register(0, 999);
    cpu.set_stack_pointer(123);
    cpu.initialize_entry(loaded.entry_address, memory);

    CHECK(cpu.read_register(0) == 0);
    CHECK(cpu.get_stack_pointer() == 0);
    CHECK(cpu.get_program_counter() == 0);
    CHECK(cpu.run(memory, 5) == 5);
    CHECK(cpu.read_register(0) == 47);
    CHECK(cpu.read_register(1) == 0x100);
    CHECK(memory.read_u64(0x100) == 47);
    CHECK(cpu.get_program_counter() == 16);
}

} // namespace

int main() {
    valid_binary_loads_exact_bytes();
    missing_and_empty_files_fail();
    oversized_and_invalid_loads_do_not_modify_memory();
    invalid_address_and_entry_are_rejected();
    loading_at_memory_end_is_checked();
    loaded_program_starts_at_entry_and_executes();
    return test::summarize("loader_tests");
}
