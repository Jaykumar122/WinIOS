#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Darwin BSD POSIX Syscall Numbers (passed in X16 with SVC 0x80)
// -----------------------------------------------------------------------------
enum class DarwinBsdSyscall : std::uint64_t {
    Exit                = 1,
    Fork                = 2,
    Read                = 3,
    Write               = 4,
    Open                = 5,
    Close               = 6,
    GetPid              = 20,
    GetUid              = 24,
    GetEuid             = 25,
    Munmap              = 73,
    Mprotect            = 74,
    Pread               = 153,
    Mmap                = 197,
    KdebugTrace         = 180,
    BsdThreadRegister   = 366
};

// -----------------------------------------------------------------------------
// Darwin Mach Trap Numbers (passed as negative integers in X16 with SVC 0x80/0x81)
// Aligned with XNU osfmk/kern/syscall_sw.c
// -----------------------------------------------------------------------------
enum class DarwinMachTrap : std::int64_t {
    MachReplyPort       = -26,
    ThreadSelf          = -27,
    TaskSelf            = -28,
    HostSelf            = -29,
    MachMsg             = -31,
    MachMsgOverwrite    = -32,
    SemaphoreSignal     = -33,
    SemaphoreWait       = -36,
    SwtchPri            = -59,
    Swtch               = -60,
    MapFd               = -89
};

// -----------------------------------------------------------------------------
// Darwin 64-bit Commpage Constants
// Base address: 0x0000000FFFFFC000ULL in Apple Darwin ARM64 ABI
// -----------------------------------------------------------------------------
namespace commpage {
    constexpr std::uint64_t kDefaultCommpageBase   = 0x0000000FFFFFC000ULL;
    constexpr std::size_t   kCommpageSize          = 0x4000; // 16 KB

    // Commpage Byte Offsets
    constexpr std::size_t kOffSignature            = 0x0000; // "Apple System\0"
    constexpr std::size_t kOffCpuCapabilities64    = 0x0010; // CPU feature flags
    constexpr std::size_t kOffVersion              = 0x001E; // Commpage version (uint16_t)
    constexpr std::size_t kOffNCpus                = 0x0022; // Active CPU count (uint8_t)
    constexpr std::size_t kOffUserPageShift        = 0x0024; // Page shift (14 = 16KB)
    constexpr std::size_t kOffTimebase             = 0x0058; // 24 MHz timebase frequency
    constexpr std::size_t kOffApproxTime           = 0x0060; // Monotonic approximate time

    // Apple Silicon ARM64 Capability Flags
    constexpr std::uint64_t kCapFp                 = (1ULL << 0);
    constexpr std::uint64_t kCapNeon               = (1ULL << 1);
    constexpr std::uint64_t kCapAes                = (1ULL << 2);
    constexpr std::uint64_t kCapPmull              = (1ULL << 3);
    constexpr std::uint64_t kCapSha1               = (1ULL << 4);
    constexpr std::uint64_t kCapSha256             = (1ULL << 5);
    constexpr std::uint64_t kCapCrc32              = (1ULL << 6);
    constexpr std::uint64_t kCapAtomic             = (1ULL << 7);
} // namespace commpage

// -----------------------------------------------------------------------------
// DarwinSyscallDispatcher: Intercepts and executes Darwin BSD and Mach traps
// -----------------------------------------------------------------------------
class DarwinSyscallDispatcher {
public:
    using BsdHandler  = std::function<std::int64_t(CPU& cpu, Memory& memory)>;
    using MachHandler = std::function<std::int64_t(CPU& cpu, Memory& memory)>;

    DarwinSyscallDispatcher();

    // Handler Registration
    void register_bsd_handler(std::uint64_t syscall_nr, BsdHandler handler);
    void register_mach_handler(std::int64_t trap_nr, MachHandler handler);

    [[nodiscard]] bool has_bsd_handler(std::uint64_t syscall_nr) const noexcept;
    [[nodiscard]] bool has_mach_handler(std::int64_t trap_nr) const noexcept;

    // Dispatcher Entry: Handles SVC 0x80 (BSD) and SVC 0x81 (Mach)
    std::int64_t dispatch(CPU& cpu, Memory& memory, std::uint16_t svc_imm = 0x80);

    // Commpage Initialization
    static bool initialize_commpage(
        Memory& memory,
        std::uint64_t commpage_base = commpage::kDefaultCommpageBase,
        std::uint32_t num_cpus = 6);

    // Diagnostics & Inspection
    [[nodiscard]] const std::string& stdout_buffer() const noexcept { return stdout_buffer_; }
    void clear_stdout() noexcept { stdout_buffer_.clear(); }
    [[nodiscard]] bool has_exited() const noexcept { return has_exited_; }
    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] std::uint64_t total_syscalls_dispatched() const noexcept { return total_syscalls_; }

private:
    void setup_default_bsd_handlers();
    void setup_default_mach_handlers();

    std::unordered_map<std::uint64_t, BsdHandler> bsd_handlers_;
    std::unordered_map<std::int64_t, MachHandler> mach_handlers_;

    std::string stdout_buffer_;
    bool has_exited_{false};
    int exit_code_{0};
    std::uint64_t total_syscalls_{0};
};

} // namespace winios::runtime
