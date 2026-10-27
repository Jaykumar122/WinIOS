#include "debugger.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
std::string hex_value(std::uint64_t value, unsigned width) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(width)
           << std::setfill('0') << value;
    return stream.str();
}
}

std::string Debugger::inspect_cpu() const {
    std::ostringstream output;
    output << "PC=" << hex_value(cpu_.get_program_counter(), 16)
           << " SP=" << hex_value(cpu_.get_stack_pointer(), 16) << '\n';
    for (std::size_t index = 0; index < CPU::kRegisterCount; ++index) {
        output << "X" << index << "="
               << hex_value(cpu_.read_register(index), 16);
        if (index + 1 != CPU::kRegisterCount) {
            output << '\n';
        }
    }
    return output.str();
}

std::string Debugger::inspect_memory(std::size_t offset, std::size_t width) const {
    if (offset > memory_.size() || width > memory_.size() - offset) {
        throw std::out_of_range("Debugger memory range is outside guest memory");
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < width; ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << hex_value(memory_.read_u8(offset + index), 2);
    }
    return output.str();
}

void Debugger::step() {
    cpu_.step(memory_);
}

std::size_t Debugger::continue_execution(std::size_t max_instructions) {
    return cpu_.run(memory_, max_instructions);
}

void Debugger::set_tracing(bool enabled, CPU::TraceSink sink) {
    if (enabled) {
        if (!sink) {
            throw std::invalid_argument("Tracing requires a non-empty trace sink");
        }
        cpu_.set_trace_sink(std::move(sink));
    } else {
        cpu_.clear_trace_sink();
    }
}
