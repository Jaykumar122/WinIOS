#include "kernel/syscall_dispatcher.h"

namespace winios::kernel {

KernelSyscallDispatcher::KernelSyscallDispatcher(
    PriorityScheduler& sched,
    KernelThreadManager& thread_mgr,
    Memory& mem)
    : sched_(sched), thread_mgr_(thread_mgr), mem_(mem) {
    register_default_handlers();
}

void KernelSyscallDispatcher::register_default_handlers() {
    register_handler(SyscallId::Yield, [](const SyscallArgs&, PriorityScheduler& sched, KernelThreadManager&, Memory&, CPU& cpu) {
        sched.yield(cpu);
        return SyscallResult{0, true};
    });

    register_handler(SyscallId::Sleep, [](const SyscallArgs&, PriorityScheduler& sched, KernelThreadManager&, Memory&, CPU& cpu) {
        sched.block_current(WaitReason::Sleep, cpu);
        return SyscallResult{0, true};
    });

    register_handler(SyscallId::CreateThread, [](const SyscallArgs& args, PriorityScheduler& sched, KernelThreadManager& tm, Memory&, CPU&) {
        const std::uint64_t entry_point = args.args[0];
        const std::uint64_t arg = args.args[1];
        const std::uint64_t stack_base = args.args[2];
        const std::uint64_t stack_size = args.args[3] != 0 ? args.args[3] : 0x4000;
        const auto prio = static_cast<std::uint32_t>(args.args[4] != 0 ? args.args[4] : kDefaultPriority);

        KernelThread* child = tm.create_thread("user_spawned", entry_point, arg, stack_base, stack_size, prio);
        if (child != nullptr) {
            sched.enqueue_thread(child);
            return SyscallResult{static_cast<std::int64_t>(child->tid()), true};
        }
        return SyscallResult{-1, false};
    });

    register_handler(SyscallId::Exit, [](const SyscallArgs& args, PriorityScheduler& sched, KernelThreadManager&, Memory&, CPU& cpu) {
        KernelThread* cur = sched.current_thread();
        if (cur != nullptr) {
            cur->terminate(static_cast<std::int32_t>(args.args[0]));
        }
        sched.block_current(WaitReason::None, cpu);
        return SyscallResult{0, true};
    });

    register_handler(SyscallId::GetTid, [](const SyscallArgs&, PriorityScheduler& sched, KernelThreadManager&, Memory&, CPU&) {
        KernelThread* cur = sched.current_thread();
        const std::int64_t tid = cur != nullptr ? static_cast<std::int64_t>(cur->tid()) : -1;
        return SyscallResult{tid, true};
    });

    register_handler(SyscallId::TimeTicks, [](const SyscallArgs&, PriorityScheduler& sched, KernelThreadManager&, Memory&, CPU&) {
        return SyscallResult{static_cast<std::int64_t>(sched.stats().total_scheduler_ticks), true};
    });
}

void KernelSyscallDispatcher::register_handler(SyscallId id, SyscallHandler handler) {
    handlers_[static_cast<std::uint64_t>(id)] = std::move(handler);
}

bool KernelSyscallDispatcher::dispatch(CPU& cpu) {
    // Extract syscall number from X8 or X16 (Darwin / standard ABI)
    std::uint64_t sys_num = cpu.read_register(8);
    if (sys_num == 0) {
        sys_num = cpu.read_register(16);
    }

    SyscallArgs args{};
    args.num = sys_num;
    for (std::size_t i = 0; i < 6; ++i) {
        args.args[i] = cpu.read_register(static_cast<std::uint32_t>(i));
    }

    auto it = handlers_.find(sys_num);
    if (it == handlers_.end()) {
        cpu.write_register(0, static_cast<std::uint64_t>(-1)); // ENOSYS
        return false;
    }

    ++total_dispatched_;
    SyscallResult result = it->second(args, sched_, thread_mgr_, mem_, cpu);
    cpu.write_register(0, static_cast<std::uint64_t>(result.return_value));
    return result.success;
}

std::vector<std::uint32_t> KernelSyscallDispatcher::emit_vector_stub_eret() {
    return {
        0xD69F03E0u // ERET (0xD69F03E0u)
    };
}

} // namespace winios::kernel
