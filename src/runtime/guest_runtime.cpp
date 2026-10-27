#include "guest_runtime.h"

#include "loader/binary_loader.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {
ExecutionResult invalid_result(const std::string& message) {
    ExecutionResult result;
    result.status = ExecutionStatus::invalid_configuration;
    result.error = message;
    return result;
}

bool range_overflows(std::uint64_t start, std::uint64_t size) {
    return size > std::numeric_limits<std::uint64_t>::max() - start;
}
}

const char* execution_status_name(ExecutionStatus status) noexcept {
    switch (status) {
    case ExecutionStatus::completed: return "completed";
    case ExecutionStatus::instruction_limit_reached: return "instruction_limit_reached";
    case ExecutionStatus::load_error: return "load_error";
    case ExecutionStatus::invalid_configuration: return "invalid_configuration";
    case ExecutionStatus::memory_fault: return "memory_fault";
    case ExecutionStatus::unsupported_instruction: return "unsupported_instruction";
    }
    return "unknown";
}

ExecutionResult GuestRuntime::execute(const std::filesystem::path& path,
                                      const RuntimeConfig& config,
                                      CPU::TraceSink trace_sink) {
    if (config.memory_size == 0 || config.max_instructions == 0
        || config.stack_size == 0) {
        return invalid_result("Runtime memory, stack, and instruction limit must be nonzero");
    }
    if (config.load_address > config.memory_size
        || config.entry_address > config.memory_size
        || config.stack_base > config.memory_size
        || config.stack_size > config.memory_size
        || range_overflows(config.stack_base, config.stack_size)
        || config.stack_base + config.stack_size > config.memory_size) {
        return invalid_result("Runtime address or stack range is outside guest memory");
    }
    if (config.entry_address % 4u != 0 || config.load_address % 4u != 0
        || (config.completion_sentinel.has_value()
            && (*config.completion_sentinel % 4u != 0
                || *config.completion_sentinel >= config.memory_size))) {
        return invalid_result("Runtime entry, load, or sentinel address is unaligned or invalid");
    }

    try {
        Memory memory(config.memory_size);
        const LoadedBinary loaded =
            BinaryLoader::load(path, memory, config.load_address);
        if (config.entry_address < loaded.entry_address
            || config.entry_address >= loaded.entry_address + loaded.loaded_bytes) {
            return invalid_result("Runtime entry address is outside the loaded program");
        }
        if (range_overflows(loaded.entry_address, loaded.loaded_bytes)
            || loaded.entry_address + loaded.loaded_bytes > config.stack_base) {
            return invalid_result("Loaded program overlaps the configured stack");
        }
        if (config.completion_sentinel.has_value()
            && *config.completion_sentinel >= loaded.entry_address
            && *config.completion_sentinel
               < loaded.entry_address + loaded.loaded_bytes) {
            return invalid_result("Completion sentinel overlaps the loaded program");
        }
        if (config.completion_sentinel.has_value()
            && *config.completion_sentinel >= config.stack_base) {
            return invalid_result("Completion sentinel overlaps the configured stack");
        }

        CPU cpu;
        cpu.initialize_entry(config.entry_address, memory);
        cpu.set_stack_pointer(config.stack_base + config.stack_size);
        if (config.tracing) {
            if (!trace_sink) {
                return invalid_result("Tracing requires a trace sink");
            }
            cpu.set_trace_sink(std::move(trace_sink));
        }

        ExecutionResult result;
        result.loaded_bytes = loaded.loaded_bytes;
        const auto capture_state = [&] {
            result.final_pc = cpu.get_program_counter();
            result.final_stack_pointer = cpu.get_stack_pointer();
            for (std::size_t index = 0; index < CPU::kRegisterCount; ++index) {
                result.final_registers[index] = cpu.read_register(index);
            }
            if (memory.size() >= 0x108u) {
                result.memory_at_0x100 = memory.read_u64(0x100u);
            }
        };
        for (std::size_t count = 0; count < config.max_instructions; ++count) {
            try {
                cpu.step(memory);
            } catch (const std::out_of_range& error) {
                result.status = ExecutionStatus::memory_fault;
                result.error = error.what();
                result.instructions_executed = count;
                capture_state();
                return result;
            } catch (const std::runtime_error& error) {
                result.status = ExecutionStatus::unsupported_instruction;
                result.error = error.what();
                result.instructions_executed = count;
                capture_state();
                return result;
            }
            result.instructions_executed = count + 1;
            if (config.completion_sentinel.has_value()
                && cpu.get_program_counter() == *config.completion_sentinel) {
                result.status = ExecutionStatus::completed;
                capture_state();
                return result;
            }
        }
        result.status = ExecutionStatus::instruction_limit_reached;
        capture_state();
        return result;
    } catch (const std::out_of_range& error) {
        ExecutionResult result;
        result.status = ExecutionStatus::load_error;
        result.error = error.what();
        return result;
    } catch (const std::exception& error) {
        ExecutionResult result;
        result.status = ExecutionStatus::load_error;
        result.error = error.what();
        return result;
    }
}
