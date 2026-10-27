#include "cpu/cpu.h"
#include <iostream>

int main() {
    std::cout << "WinIOS v0.1.0\n";
    std::cout << "ARM64 emulation research prototype\n";

    CPU cpu;
    cpu.run_demo();

    return 0;
}
