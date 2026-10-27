#pragma once

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <cstddef>
#include <functional>
#include <string>

class Debugger {
public:
    Debugger(CPU& cpu, Memory& memory) noexcept : cpu_(cpu), memory_(memory) {}

    std::string inspect_cpu() const;
    std::string inspect_memory(std::size_t offset, std::size_t width) const;

    void step();
    std::size_t continue_execution(std::size_t max_instructions);

    void set_tracing(bool enabled, CPU::TraceSink sink = {});

private:
    CPU& cpu_;
    Memory& memory_;
};
