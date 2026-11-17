#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/user_address_space.h"

namespace winios::kernel {

struct UserStackConfig {
    std::uint64_t stack_top_va{0x000000007FFF0000ULL}; // Top of user stack
    std::size_t stack_size{0x10000};                     // 64 KiB
    std::vector<std::string> argv{"test_program"};
    std::vector<std::string> envp{"PATH=/usr/bin", "USER=guest"};
};

struct UserStackResult {
    bool success{false};
    std::uint64_t final_sp_el0{0};
    std::uint64_t argc{0};
    std::uint64_t argv_va{0};
    std::uint64_t envp_va{0};
    std::uint64_t stack_bottom_va{0};
};

class UserStackBuilder {
public:
    // Builds and writes the initial user stack (argc, argv, envp, strings) into UserAddressSpace
    static UserStackResult build_stack(memory::UserAddressSpace& uas,
                                       memory::PhysicalPageManager& ppm,
                                       Memory& mem,
                                       const UserStackConfig& config);
};

} // namespace winios::kernel
