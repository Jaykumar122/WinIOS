#include "cpu/cpu.h"
#include "debugger/debugger.h"
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    std::cout << "WinIOS v0.1.0\n";
    std::cout << "ARM64 emulation research prototype\n";

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

    CPU cpu;
    cpu.run_demo();

    return 0;
}
