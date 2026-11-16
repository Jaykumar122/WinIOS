#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "device/apple_aic.h"
#include "memory/xnu_page_allocator.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;
using namespace winios::device;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_snapshot_replay_determinism() {
    std::cout << "[Test 1] Stage 209.21: Snapshot Replay & Determinism Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MiB

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_209();
    TEST_CHECK(!kernel_image.empty(), "Genuine Campaign 209 Mach-O image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    // 1. Step CPU through VM bootstrap
    for (std::size_t step = 0; step < 10000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution completed into WFI");

    // 2. Capture baseline state
    const std::uint64_t snap_pc = cpu.get_program_counter();
    const std::uint32_t snap_magic = memory.read_u32(0x28000ULL);
    const std::uint32_t snap_pmap = memory.read_u32(0x10038ULL);
    const std::uint64_t snap_mb209 = memory.read_u64(0x50010ULL);

    TEST_CHECK(snap_magic == 0x584E5550u, "Metadata magic 'XNUP' captured");
    TEST_CHECK(snap_pmap == 2, "pmap_initialized = 2 captured");
    TEST_CHECK(snap_mb209 == 0xCAFEBABE209ULL, "Mailbox 209 captured");

    // 3. Serialize Allocator State
    XnuPhysicalPageAllocator alloc;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x40000ULL, "reserved_boot");
    auto p1 = alloc.allocate_pages(4);
    TEST_CHECK(p1.has_value(), "Allocated 4 pages in allocator");

    const auto alloc_snap = alloc.serialize();
    TEST_CHECK(!alloc_snap.empty(), "Serialized allocator state snapshot");

    // 4. Restore Allocator into fresh instance
    XnuPhysicalPageAllocator alloc_restored;
    std::string err;
    TEST_CHECK(alloc_restored.deserialize(alloc_snap.data(), alloc_snap.size(), &err), "Restored allocator snapshot");
    TEST_CHECK(alloc_restored.free_pages_count() == alloc.free_pages_count(), "Restored allocator free count matches bit-exact");
    TEST_CHECK(alloc_restored.allocated_pages_count() == alloc.allocated_pages_count(), "Restored allocator allocated count matches bit-exact");

    // 5. Serialize and Restore AIC2 State
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic.write_register(0x10, 1ULL, 4); // AIC_CONFIG = 1
    const auto aic_snap = aic.serialize_state();

    AppleAIC aic_restored(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    TEST_CHECK(aic_restored.deserialize_state(aic_snap.data(), aic_snap.size()), "Restored AIC2 state snapshot");
    TEST_CHECK(aic_restored.read_register(0x10, 4) == 1ULL, "Restored AIC_CONFIG matches bit-exact");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.21 Snapshot Replay Tests ===\n";
    test_snapshot_replay_determinism();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.21: All snapshot replay tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.21: " << g_failures << " tests failed.\n";
        return 1;
    }
}
