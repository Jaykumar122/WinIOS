#include "runtime/darwin_syscall_dispatcher.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace winios::runtime {

DarwinSyscallDispatcher::DarwinSyscallDispatcher() {
    setup_default_bsd_handlers();
    setup_default_mach_handlers();
}

void DarwinSyscallDispatcher::register_bsd_handler(std::uint64_t syscall_nr, BsdHandler handler) {
    bsd_handlers_[syscall_nr] = std::move(handler);
}

void DarwinSyscallDispatcher::register_mach_handler(std::int64_t trap_nr, MachHandler handler) {
    mach_handlers_[trap_nr] = std::move(handler);
}

bool DarwinSyscallDispatcher::has_bsd_handler(std::uint64_t syscall_nr) const noexcept {
    return bsd_handlers_.find(syscall_nr) != bsd_handlers_.end();
}

bool DarwinSyscallDispatcher::has_mach_handler(std::int64_t trap_nr) const noexcept {
    return mach_handlers_.find(trap_nr) != mach_handlers_.end();
}

std::int64_t DarwinSyscallDispatcher::dispatch(CPU& cpu, Memory& memory, std::uint16_t svc_imm) {
    total_syscalls_++;

    if (svc_imm == 0x80) {
        // BSD POSIX Syscall: syscall number in X16
        const std::uint64_t nr = cpu.read_register(16);
        auto it = bsd_handlers_.find(nr);
        if (it != bsd_handlers_.end()) {
            const std::int64_t result = it->second(cpu, memory);
            cpu.write_register(0, static_cast<std::uint64_t>(result));
            return result;
        }

        // Unimplemented BSD syscall: return ENOSYS (78)
        cpu.write_register(0, 78u);
        return -78;
    }

    if (svc_imm == 0x81) {
        // Mach Trap: trap number in X16 as a negative number
        const auto trap_nr = static_cast<std::int64_t>(cpu.read_register(16));
        auto it = mach_handlers_.find(trap_nr);
        if (it != mach_handlers_.end()) {
            const std::int64_t result = it->second(cpu, memory);
            cpu.write_register(0, static_cast<std::uint64_t>(result));
            return result;
        }

        // Unimplemented Mach trap: return KERN_FAILURE (5)
        cpu.write_register(0, 5u);
        return 5;
    }

    return -1;
}

void DarwinSyscallDispatcher::setup_default_bsd_handlers() {
    // 1. SYS_exit (1)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Exit),
        [this](CPU& cpu, Memory&) -> std::int64_t {
            has_exited_ = true;
            exit_code_ = static_cast<int>(cpu.read_register(0));
            return 0;
        });

    // 2. SYS_write (4)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Write),
        [this](CPU& cpu, Memory& mem) -> std::int64_t {
            const auto fd = cpu.read_register(0);
            const auto buf_addr = cpu.read_register(1);
            const auto count = cpu.read_register(2);

            if ((fd == 1 || fd == 2) && count > 0 && buf_addr + count <= mem.size()) {
                const auto* p = reinterpret_cast<const char*>(mem.raw_data() + buf_addr);
                stdout_buffer_.append(p, count);
            }
            return static_cast<std::int64_t>(count);
        });

    // 3. SYS_read (3)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Read),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // EOF
        });

    // 4. SYS_getpid (20)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid),
        [](CPU&, Memory&) -> std::int64_t {
            return 1; // PID 1 (launchd)
        });

    // 5. SYS_getuid (24) & SYS_geteuid (25)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetUid),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // root
        });
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::GetEuid),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // root
        });

    // 6. SYS_bsdthread_register (366)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::BsdThreadRegister),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // Success
        });

    // 7. SYS_kdebug_trace (180)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::KdebugTrace),
        [](CPU&, Memory&) -> std::int64_t {
            return 0;
        });

    // 8. SYS_mmap (197)
    register_bsd_handler(static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap),
        [](CPU& cpu, Memory&) -> std::int64_t {
            const auto target_addr = cpu.read_register(0);
            return static_cast<std::int64_t>(target_addr != 0 ? target_addr : 0x70000000ULL);
        });
}

void DarwinSyscallDispatcher::setup_default_mach_handlers() {
    // 1. MACH_task_self_trap (-28)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::TaskSelf),
        [](CPU&, Memory&) -> std::int64_t {
            return 0x103; // Mach task port name
        });

    // 2. MACH_host_self_trap (-29)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::HostSelf),
        [](CPU&, Memory&) -> std::int64_t {
            return 0x101; // Mach host port name
        });

    // 3. MACH_mach_reply_port (-26)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::MachReplyPort),
        [](CPU&, Memory&) -> std::int64_t {
            return 0x203; // Mach reply port
        });

    // 3b. MACH_thread_self_trap (-27)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::ThreadSelf),
        [](CPU&, Memory&) -> std::int64_t {
            return 0x303; // Mach thread port
        });

    // 4. MACH_mach_msg_trap (-31)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::MachMsg),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // MACH_MSG_SUCCESS / KERN_SUCCESS
        });

    // 5. MACH_semaphore_signal_trap (-33)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SemaphoreSignal),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // KERN_SUCCESS
        });

    // 6. MACH_semaphore_wait_trap (-36)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SemaphoreWait),
        [](CPU&, Memory&) -> std::int64_t {
            return 0; // KERN_SUCCESS
        });

    // 7. MACH_swtch_pri_trap (-59) & MACH_swtch_trap (-60)
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::SwtchPri),
        [](CPU&, Memory&) -> std::int64_t {
            return 1; // THREAD_AWAKENED
        });
    register_mach_handler(static_cast<std::int64_t>(DarwinMachTrap::Swtch),
        [](CPU&, Memory&) -> std::int64_t {
            return 1; // THREAD_AWAKENED
        });
}

bool DarwinSyscallDispatcher::initialize_commpage(
    Memory& memory,
    std::uint64_t commpage_base,
    std::uint32_t num_cpus) {

    // If commpage_base is outside memory, offset into guest RAM
    const std::size_t offset = (commpage_base < memory.size())
                                   ? static_cast<std::size_t>(commpage_base)
                                   : static_cast<std::size_t>(commpage_base % memory.size());

    if (offset + commpage::kCommpageSize > memory.size()) {
        return false;
    }

    auto* p = memory.raw_data() + offset;
    std::memset(p, 0, commpage::kCommpageSize);

    // 1. Signature ("Apple System\0")
    const char sig[] = "Apple System";
    std::memcpy(p + commpage::kOffSignature, sig, sizeof(sig));

    // 2. CPU Capabilities (64-bit)
    const std::uint64_t caps = commpage::kCapFp |
                              commpage::kCapNeon |
                              commpage::kCapAes |
                              commpage::kCapPmull |
                              commpage::kCapSha1 |
                              commpage::kCapSha256 |
                              commpage::kCapCrc32 |
                              commpage::kCapAtomic;
    std::memcpy(p + commpage::kOffCpuCapabilities64, &caps, 8);

    // 3. Version (16)
    const std::uint16_t ver = 16u;
    std::memcpy(p + commpage::kOffVersion, &ver, 2);

    // 4. Number of CPUs
    const auto cpus = static_cast<std::uint8_t>(num_cpus & 0xFFu);
    std::memcpy(p + commpage::kOffNCpus, &cpus, 1);

    // 5. User Page Shift (14 = 16 KB pages)
    const std::uint8_t page_shift = 14u;
    std::memcpy(p + commpage::kOffUserPageShift, &page_shift, 1);

    // 6. Timebase (24 MHz)
    const std::uint64_t tb = 24000000ULL;
    std::memcpy(p + commpage::kOffTimebase, &tb, 8);

    // 7. Monotonic approximate time
    const std::uint64_t approx_time = 1000ULL;
    std::memcpy(p + commpage::kOffApproxTime, &approx_time, 8);

    return true;
}

} // namespace winios::runtime
