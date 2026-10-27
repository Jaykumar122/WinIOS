#include "cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <limits>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>

namespace {
constexpr std::uint32_t kMovzMask = 0xFF800000u;
constexpr std::uint32_t kMovz64 = 0xD2800000u;
constexpr std::uint32_t kAddImmediateMask = 0xFF800000u;
constexpr std::uint32_t kAddImmediate64 = 0x91000000u;
constexpr std::uint32_t kBranchMask = 0xFC000000u;
constexpr std::uint32_t kBranch = 0x14000000u;

std::string register_index_message(std::size_t index) {
    return "Register index out of range: " + std::to_string(index)
           + " (valid range is 0.." + std::to_string(CPU::kRegisterCount - 1) + ")";
}

std::size_t pc_as_memory_offset(std::uint64_t pc) {
    if (pc > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("Instruction fetch address exceeds host memory offset range");
    }
    return static_cast<std::size_t>(pc);
}

std::uint64_t read_add_operand(const CPU& cpu, std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? cpu.get_stack_pointer()
               : cpu.read_register(register_index);
}

void write_add_result(CPU& cpu, std::uint32_t register_index, std::uint64_t value) {
    if (register_index == CPU::kRegisterCount) {
        cpu.set_stack_pointer(value);
    } else {
        cpu.write_register(register_index, value);
    }
}

std::string register_name(std::uint32_t index, bool stack_pointer_allowed) {
    if (index == CPU::kRegisterCount) {
        return stack_pointer_allowed ? "SP" : "XZR";
    }
    return "X" + std::to_string(index);
}

std::string hex_word(std::uint32_t word) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << word;
    return stream.str();
}

std::string decode_disassembly(std::uint32_t instruction) {
    if ((instruction & kMovzMask) == kMovz64) {
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        std::ostringstream stream;
        stream << "MOVZ " << register_name(destination, false) << ", #"
               << immediate;
        if (halfword != 0) {
            stream << ", LSL #" << (halfword * 16u);
        }
        return stream.str();
    }
    if ((instruction & kAddImmediateMask) == kAddImmediate64) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shift = (instruction >> 22u) & 0x1u;
        const auto immediate = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "ADD " << register_name(destination, true) << ", "
               << register_name(source, true) << ", #" << immediate;
        if (shift != 0) {
            stream << ", LSL #12";
        }
        return stream.str();
    }
    if ((instruction & kBranchMask) == kBranch) {
        const auto encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        std::ostringstream stream;
        stream << "B " << (offset * 4);
        return stream.str();
    }
    return "UNKNOWN " + hex_word(instruction);
}

std::vector<std::string> changed_registers(const CPU& cpu,
                                           const std::uint64_t* before,
                                           std::uint64_t before_sp) {
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        if (cpu.read_register(i) != before[i]) {
            changed.push_back("X" + std::to_string(i));
        }
    }
    if (cpu.get_stack_pointer() != before_sp) {
        changed.push_back("SP");
    }
    return changed;
}
} // namespace

CPU::CPU() {
    reset();
}

void CPU::reset() {
    for (auto& reg : registers_) {
        reg = 0;
    }
    stack_pointer_ = 0;
    program_counter_ = 0;
}

std::uint64_t CPU::read_register(std::size_t index) const {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    return registers_[index];
}

void CPU::write_register(std::size_t index, std::uint64_t value) {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    registers_[index] = value;
}

void CPU::step(Memory& memory) {
    const std::uint64_t instruction_address = program_counter_;
    const std::uint32_t instruction =
        memory.read_u32(pc_as_memory_offset(instruction_address));
    std::uint64_t before[kRegisterCount]{};
    for (std::size_t i = 0; i < kRegisterCount; ++i) {
        before[i] = registers_[i];
    }
    const std::uint64_t before_sp = stack_pointer_;
    const auto emit_trace = [&] {
        if (trace_sink_) {
            trace_sink_(TraceRecord{instruction_address, instruction,
                                    decode_disassembly(instruction),
                                    program_counter_,
                                    changed_registers(*this, before, before_sp)});
        }
    };

    if ((instruction & kMovzMask) == kMovz64) {
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t halfword = (instruction >> 21u) & 0x3u;
        const std::uint64_t immediate = (instruction >> 5u) & 0xFFFFu;
        if (destination != CPU::kRegisterCount) {
            write_register(destination, immediate << (halfword * 16u));
        }
        program_counter_ = instruction_address + 4u;
        emit_trace();
        return;
    }

    if ((instruction & kAddImmediateMask) == kAddImmediate64) {
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shift = (instruction >> 22u) & 0x1u;
        const std::uint64_t immediate = (instruction >> 10u) & 0xFFFu;
        if (shift > 1u) {
            throw std::runtime_error("Unsupported ADD (immediate) shift encoding");
        }
        const std::uint64_t shifted_immediate = immediate << (shift * 12u);
        write_add_result(*this, destination,
                         read_add_operand(*this, source) + shifted_immediate);
        program_counter_ = instruction_address + 4u;
        emit_trace();
        return;
    }

    if ((instruction & kBranchMask) == kBranch) {
        const std::uint32_t encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        offset *= 4;
        if (offset >= 0) {
            program_counter_ = instruction_address
                               + static_cast<std::uint64_t>(offset);
        } else {
            program_counter_ = instruction_address
                               - static_cast<std::uint64_t>(-offset);
        }
        emit_trace();
        return;
    }

    throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                             + std::to_string(instruction));
}

std::size_t CPU::run(Memory& memory, std::size_t max_instructions) {
    std::size_t executed = 0;
    while (executed < max_instructions) {
        step(memory);
        ++executed;
    }
    return executed;
}

void CPU::run_demo() {
    write_register(0, 10);
    write_register(1, 20);
    write_register(2, read_register(0) + read_register(1));

    std::cout << "Demo calculation: X2 = "
              << read_register(2) << '\n';

    program_counter_ = 0;

    std::cout << "CPU state demonstration complete.\n";
}
