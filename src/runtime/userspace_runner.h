#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

struct UserspaceConfig {
    std::uint64_t entry_point{0x00400000ULL};
    std::uint64_t stack_pointer{0x007FFFF0ULL};
    std::vector<std::uint8_t> code;
    std::size_t max_instructions{100000};
};

struct UserspaceExecutionResult {
    bool exited{false};
    std::int32_t exit_code{0};
    std::size_t instructions_executed{0};
    std::string console_output;
    std::string error_message;
};

class UserspaceRunner {
public:
    static UserspaceExecutionResult run(CPU& cpu, Memory& memory, const UserspaceConfig& config);
};

} // namespace winios::runtime
