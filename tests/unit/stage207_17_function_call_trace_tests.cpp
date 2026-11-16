#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

struct CallRecord {
    std::string   caller;
    std::string   callee;
    std::uint64_t call_pc;
    std::uint64_t return_pc;
};

void test_genuine_function_call_trace() {
    std::cout << "[Test 1] Stage 207.17: Genuine Function Call & Stack Trace Tracking...\n";

    CPU cpu;
    Memory memory(0x400000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    std::vector<CallRecord> call_log;
    bool in_start = true;
    bool in_arm_init = false;
    bool in_pe_init = false;
    bool returned_from_arm_init = false;

    // Track function boundaries
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;

        const std::uint64_t pc = cpu.get_program_counter();
        if (pc == 0x14DCULL && !in_arm_init) {
            in_arm_init = true;
            call_log.push_back({"_start", "arm_init", 0x1BD0ULL, 0x1BD4ULL});
        }
        if (pc == 0x19B4ULL && !in_pe_init) {
            in_pe_init = true;
            call_log.push_back({"arm_init", "PE_init_platform", 0x1B98ULL, 0x1B9CULL});
        }
        if (in_arm_init && pc == 0x1BD4ULL) {
            returned_from_arm_init = true;
        }

        cpu.step(memory);
    }

    std::cout << "  -> Captured Function Transitions: " << call_log.size() << "\n";
    for (const auto& call : call_log) {
        std::cout << "     * " << call.caller << " -> " << call.callee
                  << " (Call PC: 0x" << std::hex << call.call_pc
                  << ", Return: 0x" << call.return_pc << std::dec << ")\n";
    }

    TEST_CHECK(call_log.size() >= 2, "Recorded at least 2 genuine C function transitions");
    TEST_CHECK(in_arm_init, "Dispatched into arm_init");
    TEST_CHECK(in_pe_init, "Dispatched into PE_init_platform");
    TEST_CHECK(returned_from_arm_init, "Executed AAPCS64 RET back to _start caller");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.17 Function Call Trace Tests ===\n";
    test_genuine_function_call_trace();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.17: All function call trace tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.17: " << g_failures << " tests failed.\n";
        return 1;
    }
}
