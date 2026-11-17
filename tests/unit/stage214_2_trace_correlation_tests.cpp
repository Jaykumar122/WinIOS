// ============================================================================
// Stage 214.2: Guest Instruction and Syscall Trace Correlation Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"
#include "kernel/bsd_process.h"
#include "kernel/guest_trace_logger.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TRACE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[TRACE_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [TRACE_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_trace_correlation_pipeline() {
    std::cout << "[Stage 214.2] Verifying Guest Trace Correlation Logging...\n";
    auto& logger = GuestTraceLogger::instance();
    logger.reset();

    // 1. Log guest instructions
    logger.log_instruction(1, "test_app", 0x1000, 0xD2800000, 1);
    logger.log_instruction(1, "test_app", 0x1004, 0xD4000001, 2);
    TRACE_CHECK(logger.instruction_records().size() == 2, "Logged 2 instruction samples");

    // 2. Log syscall execution
    std::uint64_t args[6] = {10, 20, 30, 40, 50, 60};
    logger.log_syscall(1, "test_app", 500, "SYS_winios_window_create", args, 0x80, 1, 0, "Window created at (10,20) 30x40");
    TRACE_CHECK(logger.syscall_records().size() == 1, "Logged syscall record");
    TRACE_CHECK(logger.syscall_records()[0].syscall_num == 500, "Syscall num verified");
    TRACE_CHECK(logger.syscall_records()[0].args[0] == 10, "Argument 0 verified");

    // 3. Log input delivery
    logger.log_input(1, 1, 150, 250, 50, 50, 1, 10000ULL);
    TRACE_CHECK(logger.input_records().size() == 1, "Logged input delivery record");
    TRACE_CHECK(logger.input_records()[0].raw_x == 150 && logger.input_records()[0].window_x == 50,
                "Screen to window coordinate mapping logged");

    // 4. Log framebuffer operations
    logger.log_framebuffer(1, 1, 1, "fill", 10, 10, 50, 50);
    logger.log_framebuffer(1, 1, 1, "present", 0, 0, 700, 900, "screenshot_01.bmp", "86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b");
    TRACE_CHECK(logger.framebuffer_records().size() == 2, "Logged framebuffer records");
    TRACE_CHECK(logger.framebuffer_records()[1].screenshot_file == "screenshot_01.bmp", "Screenshot correlation verified");

    // 5. Verify Markdown report generation
    std::string md = logger.export_markdown_correlation();
    TRACE_CHECK(!md.empty(), "Exported markdown trace correlation");
    TRACE_CHECK(md.find("SYS_winios_window_create") != std::string::npos, "Markdown includes syscall name");
    TRACE_CHECK(md.find("screenshot_01.bmp") != std::string::npos, "Markdown includes screenshot artifact");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.2: Trace Correlation Tests        \n";
    std::cout << "====================================================\n";

    test_trace_correlation_pipeline();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TRACE TESTS PASSED" : "TRACE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
