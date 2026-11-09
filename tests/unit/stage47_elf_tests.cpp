#include "loader/elf_loader.h"
#include "runtime/guest_runtime.h"
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
class TemporaryElf {
public:
    explicit TemporaryElf(const char* name)
        : path_(std::filesystem::temp_directory_path()
                / (std::string("winios_stage47_") + name + ".elf")) {}
    ~TemporaryElf() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void put16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}
void put64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}
void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    put32(bytes, offset, value);
}

std::vector<std::uint8_t> make_elf(std::vector<std::uint32_t> words,
                                   std::uint64_t entry = 0x1000,
                                   std::uint64_t memory_size = 0x40) {
    constexpr std::size_t header = 64;
    constexpr std::size_t phdr = 56;
    constexpr std::size_t file_offset = 0x100;
    std::vector<std::uint8_t> bytes(file_offset + words.size() * 4u, 0);
    bytes[0] = 0x7F; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    put16(bytes, 16, 2); put16(bytes, 18, 183); put32(bytes, 20, 1);
    put64(bytes, 24, entry); put64(bytes, 32, header);
    put64(bytes, 40, 0); put32(bytes, 48, 0); put16(bytes, 52, 64);
    put16(bytes, 54, phdr); put16(bytes, 56, 1); put16(bytes, 58, 0);
    put16(bytes, 60, 0); put16(bytes, 62, 0);
    put32(bytes, header, 1); put32(bytes, header + 4, 5);
    put64(bytes, header + 8, file_offset); put64(bytes, header + 16, 0x1000);
    put64(bytes, header + 24, 0); put64(bytes, header + 32, words.size() * 4u);
    put64(bytes, header + 40, memory_size); put64(bytes, header + 48, 0x100);
    for (std::size_t i = 0; i < words.size(); ++i) write_word(bytes, file_offset + i * 4u, words[i]);
    return bytes;
}
void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}
RuntimeConfig config() {
    return RuntimeConfig{0x4000, 0, 0, 0x3000, 0x1000, 100, std::nullopt, false};
}

void valid_elf_loads_segments_and_bss() {
    TemporaryElf file("valid");
    write_file(file.path(), make_elf({0xD4400000u}, 0x1000, 0x40));
    Memory memory(0x4000);
    const LoadedElf loaded = ElfLoader::load(file.path(), memory);
    CHECK(loaded.entry_address == 0x1000);
    CHECK(loaded.loaded_bytes == 0x40);
    CHECK(memory.read_u32(0x1000) == 0xD4400000u);
    CHECK(memory.read_u8(0x1000 + 4) == 0);
}

void malformed_and_invalid_elf_inputs_fail_without_writes() {
    TemporaryElf file("malformed");
    write_file(file.path(), {0x7F, 'E', 'L', 'F'});
    Memory memory(0x4000);
    memory.write_u8(0x1000, 0xAA);
    CHECK_INVALID_ARGUMENT(ElfLoader::load(file.path(), memory));
    CHECK(memory.read_u8(0x1000) == 0xAA);

    write_file(file.path(), make_elf({0xD4400000u}, 0x2000));
    std::vector<std::uint8_t> bytes = make_elf({0xD4400000u}, 0x1000);
    put64(bytes, 64 + 40, 0x5000);
    write_file(file.path(), bytes);
    CHECK_INVALID_ARGUMENT(ElfLoader::load(file.path(), memory));

    write_file(file.path(), make_elf({0xD4400000u}, 0x2000));
    CHECK_INVALID_ARGUMENT(ElfLoader::load(file.path(), memory));
}

void elf_executes_arithmetic_memory_write_and_hlt() {
    TemporaryElf file("execute");
    write_file(file.path(), make_elf({
        0xD2800540u, // MOVZ X0, #42
        0x91001400u, // ADD X0, X0, #5
        0xD2804001u, // MOVZ X1, #0x200
        0xF9000020u, // STR X0, [X1]
        0xD4400000u  // HLT #0, isolated runtime exit
    }));
    RuntimeConfig runtime = config();
    const ExecutionResult result = GuestRuntime::execute_elf(file.path(), runtime);
    CHECK(result.status == ExecutionStatus::completed);
    CHECK(result.instructions_executed == 5);
    CHECK(result.final_registers[0] == 47);
    CHECK(result.memory_at_0x100.has_value());
}

void elf_executes_conditional_branch() {
    TemporaryElf file("branch");
    write_file(file.path(), make_elf({
        0xD2800020u, // MOVZ X0, #1
        0xB4000040u, // CBZ X0, +8 (not taken)
        0xD4400000u
    }));
    const ExecutionResult result = GuestRuntime::execute_elf(file.path(), config());
    CHECK(result.status == ExecutionStatus::completed);
    CHECK(result.instructions_executed == 3);
}

void budget_fault_and_unsupported_are_distinct() {
    TemporaryElf budget("budget");
    write_file(budget.path(), make_elf({0x14000000u})); // B .
    RuntimeConfig runtime = config();
    runtime.max_instructions = 3;
    CHECK(GuestRuntime::execute_elf(budget.path(), runtime).status
          == ExecutionStatus::instruction_limit_reached);

    TemporaryElf unsupported("unsupported");
    write_file(unsupported.path(), make_elf({0}));
    runtime = config();
    CHECK(GuestRuntime::execute_elf(unsupported.path(), runtime).status
          == ExecutionStatus::unsupported_instruction);

    TemporaryElf fault("fault");
    write_file(fault.path(), make_elf({0xD2880001u, 0xF9400020u}));
    runtime = config();
    CHECK(GuestRuntime::execute_elf(fault.path(), runtime).status
          == ExecutionStatus::memory_fault);
}

void repeated_elf_execution_is_deterministic() {
    TemporaryElf file("repeat");
    write_file(file.path(), make_elf({0xD2800540u, 0xD4400000u}));
    const ExecutionResult first = GuestRuntime::execute_elf(file.path(), config());
    const ExecutionResult second = GuestRuntime::execute_elf(file.path(), config());
    CHECK(first.status == ExecutionStatus::completed);
    CHECK(second.status == first.status);
    CHECK(second.instructions_executed == first.instructions_executed);
    CHECK(second.final_registers == first.final_registers);
}
}

int main(int argc, char** argv) {
    const bool loader_only = argc > 1 && std::string(argv[1]) == "loader";
    if (loader_only || argc <= 1) {
        valid_elf_loads_segments_and_bss();
        malformed_and_invalid_elf_inputs_fail_without_writes();
    }
    if (!loader_only || argc <= 1) {
        elf_executes_arithmetic_memory_write_and_hlt();
        elf_executes_conditional_branch();
        budget_fault_and_unsupported_are_distinct();
        repeated_elf_execution_is_deterministic();
    }
    return test::summarize("stage47_elf_tests");
}
