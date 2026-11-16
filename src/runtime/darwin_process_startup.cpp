#include "runtime/darwin_process_startup.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace winios::runtime {

std::size_t DarwinProcessStartup::translate_vaddr(
    std::uint64_t vaddr,
    std::size_t mem_size,
    std::uint64_t stack_top) noexcept {
    if (mem_size == 0) {
        return static_cast<std::size_t>(-1);
    }
    if (vaddr < mem_size) {
        return static_cast<std::size_t>(vaddr);
    }
    // High-address or virtual stack region mapped to the top of allocated memory
    if (stack_top >= mem_size && vaddr <= stack_top) {
        std::uint64_t dist = stack_top - vaddr;
        if (dist < mem_size) {
            return mem_size - static_cast<std::size_t>(dist);
        }
    }
    return static_cast<std::size_t>(-1);
}

std::vector<std::string> DarwinProcessStartup::build_default_apple_vector(
    const std::string& exec_path,
    std::uint64_t stack_guard,
    std::uint64_t malloc_entropy) {
    std::vector<std::string> apple;
    // 1. executable_path
    apple.push_back("executable_path=" + exec_path);
    // 2. stack_guard
    char guard_buf[64];
    std::snprintf(guard_buf, sizeof(guard_buf), "stack_guard=0x%llx", static_cast<unsigned long long>(stack_guard));
    apple.emplace_back(guard_buf);
    // 3. malloc_entropy
    char entropy_buf[64];
    std::snprintf(entropy_buf, sizeof(entropy_buf), "malloc_entropy=0x%llx", static_cast<unsigned long long>(malloc_entropy));
    apple.emplace_back(entropy_buf);
    // 4. pfz (pre-faulted zero page)
    apple.emplace_back("pfz=0x100000000");
    return apple;
}

bool DarwinProcessStartup::setup_process(
    CPU& cpu,
    Memory& memory,
    const DarwinProcessStartupConfig& config,
    DarwinProcessStartupInfo& out_info,
    std::string& out_error) {

    if (config.stack_top == 0 || memory.size() == 0) {
        out_error = "Stack top or memory size is zero";
        return false;
    }
    if (config.stack_top >= 0x8000000000000000ULL) {
        out_error = "Stack top is in kernel address space (high bit set)";
        return false;
    }

    // 16-byte align the initial stack top
    std::uint64_t curr_vaddr = config.stack_top & ~0xFULL;

    auto write_byte = [&](std::uint64_t vaddr, std::uint8_t val) -> bool {
        std::size_t off = translate_vaddr(vaddr, memory.size(), config.stack_top);
        if (off == static_cast<std::size_t>(-1) || off >= memory.size()) {
            out_error = "Stack byte write out of bounds";
            return false;
        }
        memory.write_u8(off, val);
        return true;
    };

    auto write_u64 = [&](std::uint64_t vaddr, std::uint64_t val) -> bool {
        std::size_t off = translate_vaddr(vaddr, memory.size(), config.stack_top);
        if (off == static_cast<std::size_t>(-1) || off + 8 > memory.size()) {
            out_error = "Stack u64 write out of bounds";
            return false;
        }
        memory.write_u64(off, val);
        return true;
    };

    // Determine effective argv and apple vector
    std::vector<std::string> effective_argv = config.argv;
    if (effective_argv.empty()) {
        effective_argv.push_back(config.executable_path.empty() ? "/sbin/launchd" : config.executable_path);
    }

    std::vector<std::string> effective_apple = config.apple_entries;
    if (effective_apple.empty()) {
        effective_apple = build_default_apple_vector(
            config.executable_path.empty() ? effective_argv[0] : config.executable_path,
            config.stack_guard,
            config.malloc_entropy);
    }

    out_info.argv_string_ptrs.clear();
    out_info.envp_string_ptrs.clear();
    out_info.apple_string_ptrs.clear();

    // 1. Write apple strings onto stack (high addresses)
    std::vector<std::uint64_t> apple_addrs;
    for (auto it = effective_apple.rbegin(); it != effective_apple.rend(); ++it) {
        std::size_t len = it->size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < it->size()) ? static_cast<std::uint8_t>((*it)[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        apple_addrs.push_back(curr_vaddr);
    }
    std::reverse(apple_addrs.begin(), apple_addrs.end());
    out_info.apple_string_ptrs = apple_addrs;

    // 2. Write envp strings onto stack
    std::vector<std::uint64_t> envp_addrs;
    for (auto it = config.envp.rbegin(); it != config.envp.rend(); ++it) {
        std::size_t len = it->size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < it->size()) ? static_cast<std::uint8_t>((*it)[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        envp_addrs.push_back(curr_vaddr);
    }
    std::reverse(envp_addrs.begin(), envp_addrs.end());
    out_info.envp_string_ptrs = envp_addrs;

    // 3. Write argv strings onto stack
    std::vector<std::uint64_t> argv_addrs;
    for (auto it = effective_argv.rbegin(); it != effective_argv.rend(); ++it) {
        std::size_t len = it->size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < it->size()) ? static_cast<std::uint8_t>((*it)[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        argv_addrs.push_back(curr_vaddr);
    }
    std::reverse(argv_addrs.begin(), argv_addrs.end());
    out_info.argv_string_ptrs = argv_addrs;

    // 4. Align down to 8 bytes for pointer arrays
    curr_vaddr &= ~0x7ULL;

    // Slots: argc (1) + argv (N) + NULL (1) + envp (M) + NULL (1) + apple (K) + NULL (1)
    std::size_t total_slots = 1 + argv_addrs.size() + 1 + envp_addrs.size() + 1 + apple_addrs.size() + 1;
    curr_vaddr -= (total_slots * sizeof(std::uint64_t));

    // Align stack pointer to 16 bytes for ARM64 ABI
    curr_vaddr &= ~0xFULL;

    std::uint64_t sp_walker = curr_vaddr;

    // Write argc
    out_info.argc = effective_argv.size();
    if (!write_u64(sp_walker, out_info.argc)) return false;
    sp_walker += 8;

    // Write argv pointers
    out_info.argv_ptr = sp_walker;
    for (std::uint64_t ptr : argv_addrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator
    sp_walker += 8;

    // Write envp pointers
    out_info.envp_ptr = sp_walker;
    for (std::uint64_t ptr : envp_addrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator
    sp_walker += 8;

    // Write apple pointers
    out_info.apple_ptr = sp_walker;
    for (std::uint64_t ptr : apple_addrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator
    sp_walker += 8;

    out_info.initial_sp = curr_vaddr;
    out_info.initial_pc = config.entry_point;

    // Configure CPU registers according to ARM64 Darwin userspace convention
    if (config.populate_registers) {
        cpu.set_stack_pointer(out_info.initial_sp);
        cpu.set_program_counter(out_info.initial_pc);
        cpu.write_register(0, out_info.argc);
        cpu.write_register(1, out_info.argv_ptr);
        cpu.write_register(2, out_info.envp_ptr);
        cpu.write_register(3, out_info.apple_ptr);
    }

    return true;
}

} // namespace winios::runtime
