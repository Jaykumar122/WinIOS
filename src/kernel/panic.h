#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"

namespace winios::kernel {

enum class LogLevel : std::uint32_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Panic = 4
};

enum class PanicReason : std::uint32_t {
    Unknown = 0,
    FatalKernelException = 1,
    UnhandledPageFault = 2,
    DoubleFault = 3,
    AssertionFailure = 4,
    Deadlock = 5,
    CorruptedTcb = 6
};

struct StackFrame {
    std::uint64_t fp{0};
    std::uint64_t lr{0};
};

struct PanicInfo {
    PanicReason reason{PanicReason::Unknown};
    std::string message;
    std::uint64_t pc{0};
    std::uint32_t el{1};
    std::uint64_t esr{0};
    std::uint64_t far{0};
    std::uint64_t gpr[31]{0};
    std::vector<StackFrame> backtrace;
};

struct KernelLogEntry {
    LogLevel level{LogLevel::Info};
    std::uint64_t timestamp{0};
    std::string subsystem;
    std::string message;
};

class KernelDiagnostics {
public:
    explicit KernelDiagnostics(std::size_t max_log_entries = 128);
    ~KernelDiagnostics() = default;

    // Logging interface
    void log(LogLevel level, const std::string& subsystem, const std::string& message, std::uint64_t timestamp = 0);
    [[nodiscard]] std::vector<KernelLogEntry> get_logs() const;
    void clear_logs();
    [[nodiscard]] std::size_t log_count() const noexcept { return logs_.size(); }

    // Panic handling
    void trigger_panic(const PanicInfo& panic);
    [[nodiscard]] bool has_panicked() const noexcept { return panicked_; }
    [[nodiscard]] const PanicInfo& last_panic() const noexcept { return last_panic_; }

    // Formatting & stack unwinding
    [[nodiscard]] static std::string format_panic_report(const PanicInfo& panic);
    [[nodiscard]] static std::vector<StackFrame> unwind_stack(
        std::uint64_t fp,
        std::uint64_t stack_base,
        std::uint64_t stack_size,
        const Memory& mem,
        std::size_t max_frames = 32);

private:
    std::size_t max_log_entries_{128};
    std::vector<KernelLogEntry> logs_;
    bool panicked_{false};
    PanicInfo last_panic_{};
};

} // namespace winios::kernel
