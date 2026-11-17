#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

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

void test_genuine_xnu_campaign210_execution_boundary() {
    std::cout << "[Test 1] Stage 210.3: Genuine XNU Execution Boundary Breakthrough (Campaign 210)...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x800000); // 8 MiB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Campaign 210 Mach-O image loaded");

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
    for (std::size_t step = 0; step < 15000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total instructions retired in Campaign 210 expansion: " << retired << "\n";
    TEST_CHECK(retired >= 650, "Instruction count strictly expanded beyond Campaign 209 baseline (>626)");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI checkpoint");

    // 1. Verify pmap_init milestone reached (state = 2)
    const std::uint32_t pmap_state = memory.read_u32(0x10038ULL);
    TEST_CHECK(pmap_state == 2, "pmap_init completed in guest execution");

    // 2. Verify vm_page_bootstrap milestone reached (state = 1)
    const std::uint32_t vm_page_state = memory.read_u32(0x1003CULL);
    TEST_CHECK(vm_page_state == 1, "vm_page_bootstrap completed in guest execution");

    // 3. Verify Campaign 210 initialization state flags in g_arm_init_state (0x10000)
    const std::uint32_t kmem_state = memory.read_u32(0x10058ULL);
    TEST_CHECK(kmem_state == 1, "kmem_init completed in guest execution (flag == 1)");

    const std::uint32_t zone_state = memory.read_u32(0x1005CULL);
    TEST_CHECK(zone_state == 1, "zone_bootstrap completed in guest execution (flag == 1)");

    const std::uint32_t sched_state = memory.read_u32(0x10060ULL);
    TEST_CHECK(sched_state == 1, "sched_init completed in guest execution (flag == 1)");

    const std::uint32_t task_state = memory.read_u32(0x10064ULL);
    TEST_CHECK(task_state == 1, "task_init completed in guest execution (flag == 1)");

    const std::uint32_t thread_state = memory.read_u32(0x10068ULL);
    TEST_CHECK(thread_state == 1, "thread_init completed in guest execution (flag == 1)");

    const std::uint32_t startup_state = memory.read_u32(0x1006CULL);
    TEST_CHECK(startup_state == 1, "machine_startup completed in guest execution (flag == 1)");

    // 4. Verify Mailboxes 207, 208, 209, and 210
    const std::uint64_t mb207 = memory.read_u64(0x50000ULL);
    const std::uint64_t mb208 = memory.read_u64(0x50008ULL);
    const std::uint64_t mb209 = memory.read_u64(0x50010ULL);
    const std::uint64_t mb210 = memory.read_u64(0x50018ULL);

    TEST_CHECK(mb207 == 0xCAFEBABE207ULL, "Mailbox 207 preserved (0xCAFEBABE207)");
    TEST_CHECK(mb208 == 0xCAFEBABE208ULL, "Mailbox 208 preserved (0xCAFEBABE208)");
    TEST_CHECK(mb209 == 0xCAFEBABE209ULL, "Mailbox 209 preserved (0xCAFEBABE209)");
    TEST_CHECK(mb210 == 0xCAFEBABE210ULL, "Mailbox 210 written (0xCAFEBABE210)");

    // 5. Verify Subsystem In-Memory Structures
    const std::uint32_t kmap_magic = memory.read_u32(0x29200ULL);
    TEST_CHECK(kmap_magic == 0x4B4D4150u, "kernel_map structure magic valid (0x4B4D4150 'KMAP')");

    const std::uint32_t zone_magic = memory.read_u32(0x29300ULL);
    TEST_CHECK(zone_magic == 0x5A4F4E45u, "zone_allocator structure magic valid (0x5A4F4E45 'ZONE')");

    const std::uint32_t sched_magic = memory.read_u32(0x29500ULL);
    TEST_CHECK(sched_magic == 0x53434844u, "scheduler structure magic valid (0x53434844 'SCHD')");

    const std::uint64_t task_magic = memory.read_u64(0x29600ULL);
    TEST_CHECK(task_magic == 0x5441534B5F54534BULL, "kernel_task structure magic valid ('TASK_TSK')");

    const std::uint64_t tcb_magic = memory.read_u64(0x29800ULL);
    TEST_CHECK(tcb_magic == 0x5443425F54485244ULL, "kernel_thread TCB magic valid ('TCB_THRD')");

    // 6. Verify reconciled avail_start is exactly 0x40000
    const std::uint64_t avail_start = memory.read_u64(0x10028ULL);
    TEST_CHECK(avail_start == 0x40000ULL, "Reconciled avail_start is 0x40000 (Frame 16 boundary)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.3 Execution Boundary Breakthrough Tests ===\n";
    test_genuine_xnu_campaign210_execution_boundary();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.3: All execution boundary breakthrough tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.3: " << g_failures << " tests failed.\n";
        return 1;
    }
}
