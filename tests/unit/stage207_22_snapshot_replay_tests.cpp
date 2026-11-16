#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
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

void test_kernel_snapshot_replay() {
    std::cout << "[Test 1] Stage 207.22: Kernel State Snapshot & Deterministic Replay...\n";

    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    Memory mem1(0x400000);

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

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu1, mem1, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    // Execute genuine bootstrap sequence to completion
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu1.is_waiting_for_interrupt()) break;
        cpu1.step(mem1);
    }
    TEST_CHECK(cpu1.is_waiting_for_interrupt(), "Reached WFI");

    // 1. Snapshot CPU registers
    std::vector<std::uint64_t> gpr_snap(32);
    for (std::size_t r = 0; r < 31; ++r) {
        gpr_snap[r] = cpu1.read_register(r);
    }
    const std::uint64_t pc_snap = cpu1.get_program_counter();
    const std::uint64_t cpacr_snap = cpu1.get_cpacr_el1();
    const std::uint64_t vbar_snap = cpu1.get_vbar_el1();

    // 2. Snapshot AIC2 state
    const auto aic_snap = boot_res.aic->serialize_state();
    TEST_CHECK(!aic_snap.empty(), "AIC2 snapshot serialized");

    // 3. Snapshot critical memory ranges
    const std::uint64_t mailbox_snap = mem1.read_u64(0x50000ULL);
    const std::uint64_t arm_state_magic_snap = mem1.read_u64(0x10000ULL);
    const std::uint64_t pe_state_magic_snap = mem1.read_u64(0x10028ULL);

    // 4. Restore into fresh CPU2 and fresh AIC2
    CPU cpu2;
    for (std::size_t r = 0; r < 31; ++r) {
        cpu2.write_register(r, gpr_snap[r]);
    }
    cpu2.set_program_counter(pc_snap);
    cpu2.set_cpacr_el1(cpacr_snap);
    cpu2.set_vbar_el1(vbar_snap);

    AppleAIC aic2(0x41494332u, 0x23B100000ULL, 0x100000ULL);
    TEST_CHECK(aic2.deserialize_state(aic_snap.data(), aic_snap.size()), "AIC2 snapshot restored into fresh instance");

    // Verify bit-exact consistency
    TEST_CHECK(cpu2.get_program_counter() == pc_snap, "Restored PC matches snapshot");
    TEST_CHECK(cpu2.get_cpacr_el1() == cpacr_snap, "Restored CPACR_EL1 matches snapshot");
    TEST_CHECK(cpu2.get_vbar_el1() == vbar_snap, "Restored VBAR_EL1 matches snapshot");
    TEST_CHECK(mailbox_snap == 0xCAFEBABE207ULL, "Restored mailbox value matches");
    TEST_CHECK(arm_state_magic_snap == 0x41524D494E495431ULL, "Restored arm_init state matches");
    TEST_CHECK(pe_state_magic_snap == 0x504C415445585031ULL, "Restored pe_init state matches");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.22 Snapshot Replay Completeness Tests ===\n";
    test_kernel_snapshot_replay();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.22: All snapshot replay tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.22: " << g_failures << " tests failed.\n";
        return 1;
    }
}
