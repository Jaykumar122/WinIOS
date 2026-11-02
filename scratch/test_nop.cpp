#include <iostream>
#include "cpu/cpu.h"
#include "memory/memory.h"

int main() {
    CPU cpu;
    Memory memory(0x1000);
    cpu.set_program_counter(0x100);
    memory.write_u32(0x100, 0xD503201Fu);
    std::cout << "Before step" << std::endl;
    cpu.step(memory);
    std::cout << "After step: PC = " << std::hex << cpu.get_program_counter() << std::endl;
    return 0;
}
