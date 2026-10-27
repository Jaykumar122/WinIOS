#include "cpu.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string register_index_message(std::size_t index) {
    return "Register index out of range: " + std::to_string(index)
           + " (valid range is 0.." + std::to_string(CPU::kRegisterCount - 1) + ")";
}
} // namespace

CPU::CPU() {
    reset();
}

void CPU::reset() {
    for (auto& reg : registers_) {
        reg = 0;
    }
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

void CPU::run_demo() {
    // Demonstration of register state only: no instruction fetch or decode.
    write_register(0, 10);
    write_register(1, 20);
    write_register(2, read_register(0) + read_register(1));

    std::cout << "Demo calculation: X2 = "
              << read_register(2) << '\n';

    program_counter_ = 0;

    std::cout << "CPU state demonstration complete.\n";
}
