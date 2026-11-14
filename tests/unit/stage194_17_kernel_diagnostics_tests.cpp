#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "kernel/panic.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

void test_panic_info_and_formatting() {
    std::cout << "Running test_panic_info_and_formatting...\n";
    PanicInfo panic{};
    panic.reason = PanicReason::FatalKernelException;
    panic.message = "Kernel Data Abort at EL1";
    panic.pc = 0x80001040ULL;
    panic.el = 1;
    panic.esr = 0x96000044ULL;
    panic.far = 0x00000000ULL;
    panic.gpr[0] = 0x12345678ULL;
    panic.gpr[1] = 0xABCDEF01ULL;
    panic.gpr[29] = 0x40100ULL;
    panic.gpr[30] = 0x80002000ULL;

    panic.backtrace.push_back(StackFrame{0x40100ULL, 0x80002000ULL});
    panic.backtrace.push_back(StackFrame{0x40180ULL, 0x80003000ULL});

    std::string report = KernelDiagnostics::format_panic_report(panic);
    EXPECT_TRUE(report.find("FatalKernelException") != std::string::npos);
    EXPECT_TRUE(report.find("Kernel Data Abort at EL1") != std::string::npos);
    EXPECT_TRUE(report.find("80001040") != std::string::npos);
    EXPECT_TRUE(report.find("12345678") != std::string::npos);
    EXPECT_TRUE(report.find("Backtrace:") != std::string::npos);
}

void test_diagnostic_logging_circular_buffer() {
    std::cout << "Running test_diagnostic_logging_circular_buffer...\n";
    KernelDiagnostics diag(3); // Capacity 3
    EXPECT_EQ(diag.log_count(), 0u);

    diag.log(LogLevel::Info, "BOOT", "Initializing kernel", 100);
    diag.log(LogLevel::Info, "MEM", "Page tables configured", 200);
    diag.log(LogLevel::Warn, "SCHED", "Run queue low", 300);

    EXPECT_EQ(diag.log_count(), 3u);
    auto logs = diag.get_logs();
    EXPECT_EQ(logs[0].subsystem, "BOOT");
    EXPECT_EQ(logs[2].subsystem, "SCHED");

    // 4th log should drop the oldest (BOOT)
    diag.log(LogLevel::Error, "IPC", "Port buffer exhausted", 400);
    EXPECT_EQ(diag.log_count(), 3u);

    auto updated = diag.get_logs();
    EXPECT_EQ(updated[0].subsystem, "MEM");
    EXPECT_EQ(updated[1].subsystem, "SCHED");
    EXPECT_EQ(updated[2].subsystem, "IPC");
}

void test_stack_unwinding_valid_frames() {
    std::cout << "Running test_stack_unwinding_valid_frames...\n";
    Memory mem(0x100000);
    const std::uint64_t stack_base = 0x40000ULL;
    const std::uint64_t stack_size = 0x4000ULL;

    // Frame 0 at 0x40100: points to Frame 1 (0x40180), LR = 0x1000
    mem.write_u64(0x40100, 0x40180ULL);
    mem.write_u64(0x40108, 0x1000ULL);

    // Frame 1 at 0x40180: points to Frame 2 (0x40200), LR = 0x2000
    mem.write_u64(0x40180, 0x40200ULL);
    mem.write_u64(0x40188, 0x2000ULL);

    // Frame 2 at 0x40200: points to 0 (end of stack), LR = 0x3000
    mem.write_u64(0x40200, 0ULL);
    mem.write_u64(0x40208, 0x3000ULL);

    auto frames = KernelDiagnostics::unwind_stack(0x40100ULL, stack_base, stack_size, mem);
    EXPECT_EQ(frames.size(), 3u);

    EXPECT_EQ(frames[0].fp, 0x40100ULL);
    EXPECT_EQ(frames[0].lr, 0x1000ULL);

    EXPECT_EQ(frames[1].fp, 0x40180ULL);
    EXPECT_EQ(frames[1].lr, 0x2000ULL);

    EXPECT_EQ(frames[2].fp, 0x40200ULL);
    EXPECT_EQ(frames[2].lr, 0x3000ULL);
}

void test_stack_unwinding_corrupted_frames() {
    std::cout << "Running test_stack_unwinding_corrupted_frames...\n";
    Memory mem(0x100000);
    const std::uint64_t stack_base = 0x40000ULL;
    const std::uint64_t stack_size = 0x4000ULL;

    // 1. Misaligned FP (0x40103)
    auto f_misaligned = KernelDiagnostics::unwind_stack(0x40103ULL, stack_base, stack_size, mem);
    EXPECT_EQ(f_misaligned.size(), 0u);

    // 2. Out of bounds FP (0x10000 < stack_base)
    auto f_oob = KernelDiagnostics::unwind_stack(0x10000ULL, stack_base, stack_size, mem);
    EXPECT_EQ(f_oob.size(), 0u);

    // 3. Circular stack reference (Frame 0 points to itself)
    mem.write_u64(0x40100, 0x40100ULL); // points to itself
    mem.write_u64(0x40108, 0x5000ULL);
    auto f_circular = KernelDiagnostics::unwind_stack(0x40100ULL, stack_base, stack_size, mem);
    EXPECT_EQ(f_circular.size(), 1u); // Must stop immediately after frame 0, no infinite loop!
}

void test_trigger_panic_and_state() {
    std::cout << "Running test_trigger_panic_and_state...\n";
    KernelDiagnostics diag(10);
    EXPECT_TRUE(!diag.has_panicked());

    PanicInfo p{};
    p.reason = PanicReason::DoubleFault;
    p.message = "Double fault in exception handler";
    p.pc = 0x80004000ULL;

    diag.trigger_panic(p);
    EXPECT_TRUE(diag.has_panicked());
    EXPECT_EQ(static_cast<std::uint32_t>(diag.last_panic().reason), static_cast<std::uint32_t>(PanicReason::DoubleFault));
    EXPECT_EQ(diag.last_panic().message, "Double fault in exception handler");

    auto logs = diag.get_logs();
    EXPECT_EQ(logs.size(), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(logs[0].level), static_cast<std::uint32_t>(LogLevel::Panic));
}

int main() {
    std::cout << "=== WINIOS STAGE 194.17: KERNEL DIAGNOSTICS & FAILURE RECOVERY TESTS ===\n";

    test_panic_info_and_formatting();
    test_diagnostic_logging_circular_buffer();
    test_stack_unwinding_valid_frames();
    test_stack_unwinding_corrupted_frames();
    test_trigger_panic_and_state();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
