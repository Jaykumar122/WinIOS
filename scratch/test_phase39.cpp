#include <iostream>
#include "cpu/cpu.h"

int main() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    cpu.set_program_counter(0x200);
    memory.write_u32(0x200, 0xD51BE020u);
    cpu.step(memory);
    std::cout << "PC: 0x" << std::hex << cpu.get_program_counter()
              << " ESR_EL1: 0x" << cpu.get_esr_el1()
              << " ESR_EL2: 0x" << cpu.get_esr_el2()
              << " EL: " << cpu.get_current_el()
              << std::dec << std::endl;
    return 0;
}
