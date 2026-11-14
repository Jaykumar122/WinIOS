#include "kernel/panic.h"

#include <iomanip>
#include <sstream>

namespace winios::kernel {

KernelDiagnostics::KernelDiagnostics(std::size_t max_log_entries)
    : max_log_entries_(max_log_entries > 0 ? max_log_entries : 128) {}

void KernelDiagnostics::log(LogLevel level, const std::string& subsystem, const std::string& message, std::uint64_t timestamp) {
    if (logs_.size() >= max_log_entries_) {
        logs_.erase(logs_.begin());
    }
    logs_.push_back(KernelLogEntry{level, timestamp, subsystem, message});
}

std::vector<KernelLogEntry> KernelDiagnostics::get_logs() const {
    return logs_;
}

void KernelDiagnostics::clear_logs() {
    logs_.clear();
}

void KernelDiagnostics::trigger_panic(const PanicInfo& panic) {
    panicked_ = true;
    last_panic_ = panic;
    log(LogLevel::Panic, "PANIC", panic.message);
}

std::vector<StackFrame> KernelDiagnostics::unwind_stack(
    std::uint64_t fp,
    std::uint64_t stack_base,
    std::uint64_t stack_size,
    const Memory& mem,
    std::size_t max_frames) {
    std::vector<StackFrame> frames;
    std::uint64_t cur_fp = fp;

    while (cur_fp != 0 && frames.size() < max_frames) {
        // Alignment check (8-byte alignment required)
        if ((cur_fp & 0x7ULL) != 0) {
            break;
        }

        // Stack bounds check
        if (cur_fp < stack_base || cur_fp + 16ULL > stack_base + stack_size) {
            break;
        }

        const std::uint64_t next_fp = mem.read_u64(static_cast<std::size_t>(cur_fp));
        const std::uint64_t lr = mem.read_u64(static_cast<std::size_t>(cur_fp + 8));

        frames.push_back(StackFrame{cur_fp, lr});

        // Ensure stack grows strictly upwards to avoid circular references
        if (next_fp <= cur_fp) {
            break;
        }
        cur_fp = next_fp;
    }

    return frames;
}

std::string KernelDiagnostics::format_panic_report(const PanicInfo& panic) {
    std::ostringstream ss;
    ss << "==================== KERNEL PANIC ====================\n";
    ss << "Reason: ";
    switch (panic.reason) {
        case PanicReason::FatalKernelException: ss << "FatalKernelException"; break;
        case PanicReason::UnhandledPageFault:   ss << "UnhandledPageFault"; break;
        case PanicReason::DoubleFault:          ss << "DoubleFault"; break;
        case PanicReason::AssertionFailure:     ss << "AssertionFailure"; break;
        case PanicReason::Deadlock:             ss << "Deadlock"; break;
        case PanicReason::CorruptedTcb:         ss << "CorruptedTcb"; break;
        default:                                ss << "Unknown"; break;
    }
    ss << "\n";
    ss << "Message: " << panic.message << "\n";
    ss << "Exception State: EL" << panic.el << " | PC=0x"
       << std::hex << std::setfill('0') << std::setw(16) << panic.pc
       << " | ESR=0x" << std::setw(16) << panic.esr
       << " | FAR=0x" << std::setw(16) << panic.far << "\n";

    ss << "Registers:\n";
    for (std::size_t i = 0; i < 31; ++i) {
        ss << "  X" << std::dec << std::setfill(' ') << std::setw(2) << i << "=0x"
           << std::hex << std::setfill('0') << std::setw(16) << panic.gpr[i];
        if (i % 2 == 1 || i == 30) {
            ss << "\n";
        }
    }

    ss << "Backtrace:\n";
    if (panic.backtrace.empty()) {
        ss << "  <no backtrace available>\n";
    } else {
        for (std::size_t i = 0; i < panic.backtrace.size(); ++i) {
            ss << "  #" << std::dec << std::setfill('0') << std::setw(2) << i
               << " FP=0x" << std::hex << std::setw(16) << panic.backtrace[i].fp
               << " LR=0x" << std::setw(16) << panic.backtrace[i].lr << "\n";
        }
    }
    ss << "======================================================\n";
    return ss.str();
}

} // namespace winios::kernel
