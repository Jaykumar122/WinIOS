#include "kernel/user_stack.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace winios::kernel {

UserStackResult UserStackBuilder::build_stack(memory::UserAddressSpace& uas,
                                             memory::PhysicalPageManager& /*ppm*/,
                                             Memory& /*mem*/,
                                             const UserStackConfig& config) {
    UserStackResult res{};

    // 1. Allocate physical pages and map user stack in UserAddressSpace
    if (!uas.allocate_user_stack(config.stack_top_va, config.stack_size, res.stack_bottom_va)) {
        res.success = false;
        return res;
    }

    std::uint64_t sp = config.stack_top_va;

    // 2. Push string contents onto stack (from top downward)
    std::vector<std::uint64_t> envp_ptrs;
    for (const auto& env : config.envp) {
        const std::size_t len = env.size() + 1;
        sp -= len;
        if (!uas.copyin(sp, env.c_str(), len)) {
            res.success = false;
            return res;
        }
        envp_ptrs.push_back(sp);
    }

    std::vector<std::uint64_t> argv_ptrs;
    for (const auto& arg : config.argv) {
        const std::size_t len = arg.size() + 1;
        sp -= len;
        if (!uas.copyin(sp, arg.c_str(), len)) {
            res.success = false;
            return res;
        }
        argv_ptrs.push_back(sp);
    }

    // Align SP to 8 bytes for pointer arrays
    sp &= ~0x7ULL;

    const std::uint64_t null_ptr = 0;

    // 3. Push envp NULL-terminated pointer array
    sp -= sizeof(std::uint64_t);
    uas.copyin(sp, &null_ptr, sizeof(null_ptr));

    for (auto it = envp_ptrs.rbegin(); it != envp_ptrs.rend(); ++it) {
        sp -= sizeof(std::uint64_t);
        const std::uint64_t ptr = *it;
        uas.copyin(sp, &ptr, sizeof(ptr));
    }
    res.envp_va = sp;

    // 4. Push argv NULL-terminated pointer array
    sp -= sizeof(std::uint64_t);
    uas.copyin(sp, &null_ptr, sizeof(null_ptr));

    for (auto it = argv_ptrs.rbegin(); it != argv_ptrs.rend(); ++it) {
        sp -= sizeof(std::uint64_t);
        const std::uint64_t ptr = *it;
        uas.copyin(sp, &ptr, sizeof(ptr));
    }
    res.argv_va = sp;

    // 5. Push argc
    res.argc = config.argv.size();
    sp -= sizeof(std::uint64_t);
    uas.copyin(sp, &res.argc, sizeof(res.argc));

    // 6. AAPCS64 requires stack pointer aligned to 16 bytes at function entry
    sp &= ~0xFULL;

    res.final_sp_el0 = sp;
    res.success = true;
    return res;
}

} // namespace winios::kernel
