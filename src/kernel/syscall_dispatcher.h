#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

enum class SyscallId : std::uint64_t {
    Yield = 1,
    Sleep = 2,
    CreateThread = 3,
    Exit = 4,
    IpcSend = 5,
    IpcRecv = 6,
    GetTid = 7,
    TimeTicks = 8
};

struct SyscallArgs {
    std::uint64_t num{0};
    std::uint64_t args[6]{0};
};

struct SyscallResult {
    std::int64_t return_value{0};
    bool success{true};
};

using SyscallHandler = std::function<SyscallResult(const SyscallArgs&, PriorityScheduler&, KernelThreadManager&, Memory&, CPU&)>;

class KernelSyscallDispatcher {
public:
    KernelSyscallDispatcher(PriorityScheduler& sched, KernelThreadManager& thread_mgr, Memory& mem);
    ~KernelSyscallDispatcher() = default;

    // Registers built-in standard synthetic kernel syscall handlers
    void register_default_handlers();

    // Register or override a specific syscall handler
    void register_handler(SyscallId id, SyscallHandler handler);

    // Dispatches syscall based on CPU register state (X8/X16 syscall #, X0..X5 args, X0 return)
    bool dispatch(CPU& cpu);

    // Emits the ARM64 guest vector entry routine for installation at VBAR_EL1 + 0x400
    // Standard lower EL synchronous handler ending in ERET
    [[nodiscard]] static std::vector<std::uint32_t> emit_vector_stub_eret();

    [[nodiscard]] std::uint64_t total_syscalls_dispatched() const noexcept { return total_dispatched_; }

private:
    PriorityScheduler& sched_;
    KernelThreadManager& thread_mgr_;
    Memory& mem_;
    std::unordered_map<std::uint64_t, SyscallHandler> handlers_;
    std::uint64_t total_dispatched_{0};
};

} // namespace winios::kernel
