#pragma once

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <optional>
#include <string>

enum class ExecutionStatus {
    completed,
    instruction_limit_reached,
    load_error,
    invalid_configuration,
    memory_fault,
    unsupported_instruction
};

struct RuntimeConfig {
    std::size_t memory_size{};
    std::uint64_t load_address{};
    std::uint64_t entry_address{};
    std::uint64_t stack_base{};
    std::size_t stack_size{};
    std::size_t max_instructions{};
    std::optional<std::uint64_t> completion_sentinel{};
    bool tracing{false};
};

struct ExecutionResult {
    ExecutionStatus status{ExecutionStatus::invalid_configuration};
    std::size_t instructions_executed{};
    std::uint64_t final_pc{};
    std::uint64_t final_stack_pointer{};
    std::array<std::uint64_t, CPU::kRegisterCount> final_registers{};
    std::optional<std::uint64_t> memory_at_0x100{};
    std::size_t loaded_bytes{};
    std::string error;
};

class GuestRuntime {
public:
    static ExecutionResult execute(const std::filesystem::path& path,
                                   const RuntimeConfig& config,
                                   CPU::TraceSink trace_sink = {});
};

const char* execution_status_name(ExecutionStatus status) noexcept;
