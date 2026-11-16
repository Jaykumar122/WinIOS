#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"
#include "loader/xnu_instruction_provenance_tracker.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_provenance_audit_and_export() {
    std::cout << "[Test 1] Stage 209.5: Provenance Classification & Trace Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_209();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Mach-O image loaded");

    const std::string sha256 = XnuGenuineInitBuilder::compute_sha256(kernel_image);
    TEST_CHECK(!sha256.empty(), "Computed image SHA-256");

    auto& tracker = XnuInstructionProvenanceTracker::instance();
    tracker.reset();
    tracker.set_binary_sha256(sha256);

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot coordinator succeeded");

    std::size_t retired = 0;
    for (std::size_t step = 0; step < 10000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;

        const std::uint64_t current_pc = cpu.get_program_counter();
        const std::uint32_t opcode = memory.read_u32(static_cast<std::size_t>(current_pc));

        tracker.record_instruction(current_pc, opcode);
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total instructions tracked in Campaign 209: " << tracker.count() << "\n";
    TEST_CHECK(tracker.count() >= 600, "Tracked retired instructions expanded past baseline");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI");

    // Export VM initialization trace
    const std::string json = tracker.export_json();
    TEST_CHECK(json.find("\"campaign\": 208") != std::string::npos || json.find("\"source_revision\"") != std::string::npos, "JSON contains provenance tracking tags");

    // Write trace to docs/campaign209/209_vm_initialization_trace.json
    std::ofstream out1("docs/campaign209/209_vm_initialization_trace.json", std::ios::binary);
    if (out1.is_open()) {
        out1.write(json.data(), static_cast<std::streamsize>(json.size()));
    } else {
        std::ofstream out2("../docs/campaign209/209_vm_initialization_trace.json", std::ios::binary);
        if (out2.is_open()) {
            out2.write(json.data(), static_cast<std::streamsize>(json.size()));
        }
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.5 Provenance Audit Tests ===\n";
    test_provenance_audit_and_export();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.5: All provenance audit tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.5: " << g_failures << " tests failed.\n";
        return 1;
    }
}
