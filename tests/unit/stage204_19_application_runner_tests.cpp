#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include "loader/macho_fixture_corpus.h"
#include "runtime/darwin_application_runner.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAILED] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failed_tests++; \
        } \
    } while (0)

static void test_minimal_exit42_binary() {
    std::cout << "[RUN] test_minimal_exit42_binary\n";
    auto bin = PublicMachOCorpus::create_minimal_executable().binary_data;
    TEST_CHECK(!bin.empty(), "minimal binary created");

    // Patch entry point code at 0x1000 to exit with code 42
    // mov x0, #42 (0xD2800540), mov x16, #1 (0xD2800030), svc #0x80 (0xD4001001)
    if (bin.size() >= 0x100C) {
        const std::uint32_t patch[] = { 0xD2800540u, 0xD2800030u, 0xD4001001u };
        std::memcpy(bin.data() + 0x1000, patch, sizeof(patch));
    }

    ApplicationConfig config{};
    config.executable_path = "/usr/bin/exit42";
    DarwinApplicationRunner runner(config);

    ApplicationRunResult res = runner.run_binary(bin.data(), bin.size());
    TEST_CHECK(res.terminated, "process terminated");
    TEST_CHECK(res.exit_code == 42, "exit code is 42");
    TEST_CHECK(res.termination_reason == "SYS_exit", "termination reason is SYS_exit");
    TEST_CHECK(res.syscalls_handled >= 1, "at least 1 syscall handled");
    TEST_CHECK(res.success, "run reported success");
}

static void test_multi_segment_write_and_exit() {
    std::cout << "[RUN] test_multi_segment_write_and_exit\n";
    auto bin = PublicMachOCorpus::create_multisegment_executable().binary_data;
    TEST_CHECK(!bin.empty(), "multi segment binary created");

    // Patch code at 0x1000 to call SYS_write(1, "Hello, Darwin!\n", 15) then SYS_exit(0)
    // 0x1000: mov x0, #1       (0xD2800020)
    // 0x1004: adr x1, #28      (0x100000E1) -> points to 0x1020
    // 0x1008: mov x2, #15      (0xD28001E2)
    // 0x100C: mov x16, #4      (0xD2800090) -> SYS_write
    // 0x1010: svc #0x80        (0xD4001001)
    // 0x1014: mov x0, #0       (0xD2800000)
    // 0x1018: mov x16, #1      (0xD2800030) -> SYS_exit
    // 0x101C: svc #0x80        (0xD4001001)
    // 0x1020: "Hello, Darwin!\n"
    if (bin.size() >= 0x1030) {
        const std::uint32_t patch[] = {
            0xD2800020u,
            0x100000E1u,
            0xD28001E2u,
            0xD2800090u,
            0xD4001001u,
            0xD2800000u,
            0xD2800030u,
            0xD4001001u
        };
        std::memcpy(bin.data() + 0x1000, patch, sizeof(patch));
        const char msg[] = "Hello, Darwin!\n";
        std::memcpy(bin.data() + 0x1020, msg, sizeof(msg));
    }

    ApplicationConfig config{};
    config.executable_path = "/usr/bin/multiseg";
    DarwinApplicationRunner runner(config);

    ApplicationRunResult res = runner.run_binary(bin.data(), bin.size());
    TEST_CHECK(res.terminated, "process terminated");
    TEST_CHECK(res.exit_code == 0, "exit code is 0");
    TEST_CHECK(res.syscalls_handled >= 2, "SYS_write and SYS_exit handled");
    TEST_CHECK(res.stdout_output.find("Hello") != std::string::npos, "stdout output captured");
}

static void test_stack_arguments_and_environment() {
    std::cout << "[RUN] test_stack_arguments_and_environment\n";
    auto bin = PublicMachOCorpus::create_minimal_executable().binary_data;

    // Clean exit with code 0
    ApplicationConfig config{};
    config.executable_path = "/usr/bin/argtest";
    config.argv = { "/usr/bin/argtest", "-v", "--input=data.txt" };
    config.envp = { "PATH=/bin:/usr/bin", "HOME=/var/root", "SHELL=/bin/sh" };

    DarwinApplicationRunner runner(config);
    ApplicationRunResult res = runner.run_binary(bin.data(), bin.size());
    TEST_CHECK(res.terminated, "process terminated");
    TEST_CHECK(res.exit_code == 0, "exit code is 0");
    TEST_CHECK(res.termination_reason == "SYS_exit", "SYS_exit");
}

static void test_max_instructions_bounded_execution() {
    std::cout << "[RUN] test_max_instructions_bounded_execution\n";
    auto bin = PublicMachOCorpus::create_minimal_executable().binary_data;
    if (bin.size() >= 0x1004) {
        std::uint32_t b_self = 0x14000000u; // b . (infinite loop)
        std::memcpy(bin.data() + 0x1000, &b_self, sizeof(b_self));
    }

    ApplicationConfig config{};
    config.executable_path = "/usr/bin/loopapp";
    config.max_instructions = 100;

    DarwinApplicationRunner runner(config);
    ApplicationRunResult res = runner.run_binary(bin.data(), bin.size());
    TEST_CHECK(!res.terminated, "infinite loop should not terminate cleanly");
    TEST_CHECK(res.termination_reason == "MaxInstructionsReached", "bounded by max instructions");
    TEST_CHECK(res.instructions_retired == 100, "retired exactly max_instructions");
}

static void test_cpu_fault_out_of_bounds() {
    std::cout << "[RUN] test_cpu_fault_out_of_bounds\n";
    auto bin = PublicMachOCorpus::create_minimal_executable().binary_data;
    if (bin.size() >= 0x1008) {
        std::uint32_t movz = 0xD29FFFE1u; // movz x1, 0xFFFF
        std::uint32_t br = 0xD61F0020u;   // br x1 (misaligned jump target)
        std::memcpy(bin.data() + 0x1000, &movz, sizeof(movz));
        std::memcpy(bin.data() + 0x1004, &br, sizeof(br));
    }

    ApplicationConfig config{};
    config.executable_path = "/usr/bin/faultapp";
    config.max_instructions = 100;
    config.memory_size = 0x100000; // 1 MB RAM

    DarwinApplicationRunner runner(config);
    ApplicationRunResult res = runner.run_binary(bin.data(), bin.size());
    TEST_CHECK(res.terminated, "fault terminated");
    TEST_CHECK(res.termination_reason.find("OutOfBoundsPC") != std::string::npos ||
               res.termination_reason.find("CPU_Fault") != std::string::npos,
               "meaningful fault reason recorded");
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE 204.19: Application Runtime Entry and Exit\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / A16 T8120)\n";
    std::cout << "====================================================\n";

    test_minimal_exit42_binary();
    test_multi_segment_write_and_exit();
    test_stack_arguments_and_environment();
    test_max_instructions_bounded_execution();
    test_cpu_fault_out_of_bounds();

    std::cout << "====================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.19: All Application Runner Lifecycle Tests PASSED!\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.19: " << g_failed_tests << " test(s) FAILED!\n";
        return 1;
    }
}
