#include "cpu/cpu.h"
#include "debugger/debugger.h"
#include "loader/binary_loader.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
std::uint64_t parse_integer(std::string_view text, const char* option) {
    std::uint64_t value = 0;
    const int base = text.starts_with("0x") || text.starts_with("0X") ? 16 : 10;
    const auto first = text.data() + ((base == 16) ? 2 : 0);
    const auto last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::invalid_argument(std::string("Invalid value for ") + option);
    }
    return value;
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  win_ios.exe\n"
              << "  win_ios.exe --debug\n"
              << "  win_ios.exe --run-binary <path> --memory-size <bytes>"
                 " --entry <address> --max-instructions <count> [--trace]\n";
}

int run_binary_mode(int argc, char* argv[]) {
    std::string binary_path;
    std::size_t memory_size = 0;
    std::uint64_t entry = 0;
    std::size_t max_instructions = 0;
    bool have_memory_size = false;
    bool have_entry = false;
    bool have_limit = false;
    bool trace = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--run-binary") {
            if (++index >= argc) throw std::invalid_argument("Missing binary path");
            binary_path = argv[index];
        } else if (option == "--memory-size") {
            if (++index >= argc) throw std::invalid_argument("Missing memory size");
            memory_size = static_cast<std::size_t>(
                parse_integer(argv[index], "--memory-size"));
            have_memory_size = true;
        } else if (option == "--entry") {
            if (++index >= argc) throw std::invalid_argument("Missing entry address");
            entry = parse_integer(argv[index], "--entry");
            have_entry = true;
        } else if (option == "--max-instructions") {
            if (++index >= argc) throw std::invalid_argument("Missing instruction limit");
            max_instructions = static_cast<std::size_t>(
                parse_integer(argv[index], "--max-instructions"));
            have_limit = true;
        } else if (option == "--trace") {
            trace = true;
        } else {
            throw std::invalid_argument("Unknown binary-run option: " + std::string(option));
        }
    }
    if (binary_path.empty() || !have_memory_size || !have_entry || !have_limit
        || memory_size == 0) {
        throw std::invalid_argument(
            "--run-binary requires path, nonzero memory-size, entry, and "
            "max-instructions");
    }

    Memory memory(memory_size);
    const LoadedBinary loaded =
        BinaryLoader::load(binary_path, memory, entry);
    CPU cpu;
    cpu.initialize_entry(loaded.entry_address, memory);
    if (trace) {
        cpu.set_trace_sink([](const CPU::TraceRecord& record) {
            std::cout << "PC=0x" << std::hex << record.instruction_address
                      << " WORD=0x" << record.instruction_word << std::dec
                      << " " << record.disassembly << " PC_AFTER=0x"
                      << std::hex << record.program_counter_after << std::dec
                      << '\n';
        });
    }
    const std::size_t executed = cpu.run(memory, max_instructions);
    std::cout << "Loaded bytes: " << loaded.loaded_bytes << '\n'
              << "Instructions executed: " << executed << '\n'
              << "Final PC: 0x" << std::hex << cpu.get_program_counter()
              << std::dec << '\n'
              << "X0: " << cpu.read_register(0) << '\n'
              << "X1: " << cpu.read_register(1) << '\n'
              << "SP: " << cpu.get_stack_pointer() << '\n'
              << "Memory[0x100]: " << memory.read_u64(0x100) << '\n';
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    std::cout << "WinIOS v0.1.0\n";
    std::cout << "ARM64 emulation research prototype\n";

    try {
    if (argc > 1 && std::string_view(argv[1]) == "--run-binary") {
        return run_binary_mode(argc, argv);
    }
    if (argc > 1 && std::string_view(argv[1]) == "--debug") {
        Memory memory(8);
        memory.write_u32(0, 0xD2800540u); // MOVZ X0, #42
        memory.write_u32(4, 0x91001401u); // ADD X1, X0, #5
        CPU cpu;
        Debugger debugger(cpu, memory);
        debugger.set_tracing(true, [](const CPU::TraceRecord& record) {
            std::cout << "PC=0x" << std::hex << record.instruction_address
                      << " WORD=0x" << record.instruction_word << std::dec
                      << " " << record.disassembly << " PC_AFTER=0x"
                      << std::hex << record.program_counter_after << std::dec
                      << '\n';
        });
        std::cout << debugger.inspect_cpu() << '\n';
        debugger.continue_execution(2);
        std::cout << debugger.inspect_cpu() << '\n';
        std::cout << "Memory[0..7]: " << debugger.inspect_memory(0, 8) << '\n';
        return 0;
    }
    if (argc > 1) {
        print_usage();
        return 2;
    }

    CPU cpu;
    cpu.run_demo();

    return 0;
    } catch (const std::exception& error) {
        std::cerr << "Execution error: " << error.what() << '\n';
        return 1;
    }
}
