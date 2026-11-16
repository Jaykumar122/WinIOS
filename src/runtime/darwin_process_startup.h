#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

// Configuration for initializing a Darwin process userspace stack frame
struct DarwinProcessStartupConfig {
    std::uint64_t stack_top{0x02000000ULL};
    std::uint64_t entry_point{0x100004000ULL};
    std::string executable_path{"/sbin/launchd"};
    std::vector<std::string> argv;
    std::vector<std::string> envp;
    std::vector<std::string> apple_entries;
    std::uint64_t stack_guard{0x5F3759DFDEADBEEFULL};
    std::uint64_t malloc_entropy{0x1122334455667788ULL};
    bool populate_registers{true};
};

// Information describing the constructed stack frame and initialized registers
struct DarwinProcessStartupInfo {
    std::uint64_t initial_sp{0};
    std::uint64_t initial_pc{0};
    std::uint64_t argc{0};
    std::uint64_t argv_ptr{0};   // Address of argv[0] pointer slot on stack
    std::uint64_t envp_ptr{0};   // Address of envp[0] pointer slot on stack
    std::uint64_t apple_ptr{0};  // Address of apple[0] pointer slot on stack
    std::vector<std::uint64_t> argv_string_ptrs;
    std::vector<std::uint64_t> envp_string_ptrs;
    std::vector<std::uint64_t> apple_string_ptrs;
};

// Darwin Process Startup Manager: Configures userspace stack frame and registers
class DarwinProcessStartup {
public:
    DarwinProcessStartup() = default;

    // Formats default apple[] vector for Darwin (executable_path, stack_guard, malloc_entropy, pfz)
    [[nodiscard]] static std::vector<std::string> build_default_apple_vector(
        const std::string& exec_path,
        std::uint64_t stack_guard,
        std::uint64_t malloc_entropy);

    // Formats stack frame and initializes CPU registers according to Darwin ARM64 ABI
    [[nodiscard]] static bool setup_process(
        CPU& cpu,
        Memory& memory,
        const DarwinProcessStartupConfig& config,
        DarwinProcessStartupInfo& out_info,
        std::string& out_error);

    // Translates virtual address to physical memory buffer offset
    [[nodiscard]] static std::size_t translate_vaddr(
        std::uint64_t vaddr,
        std::size_t mem_size,
        std::uint64_t stack_top) noexcept;
};

} // namespace winios::runtime
