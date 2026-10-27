#include "cpu.h"
#include <iostream>

CPU::CPU() = default;

void CPU::run_demo() {
    registers_[0] = 10;
    registers_[1] = 20;
    registers_[2] = registers_[0] + registers_[1];

    std::cout << "Demo calculation: X2 = "
              << registers_[2] << '\n';

    program_counter_ = 0;

    std::cout << "CPU state demonstration complete.\n";
}
