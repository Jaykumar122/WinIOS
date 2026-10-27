#include "cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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

    if ((instruction & kMovzMask) == kMovz64) {
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t halfword = (instruction >> 21u) & 0x3u;
        const std::uint64_t immediate = (instruction >> 5u) & 0xFFFFu;
        write_register(destination, immediate << (halfword * 16u));
        program_counter_ = instruction_address + 4u;
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
