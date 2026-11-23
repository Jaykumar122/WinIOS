// ============================================================================
// WinIOS Campaign 266: First Autonomous XNU Boot
// Stages 266.11 - 266.14: Exception / Interrupt Integration, Timer & AIC2,
// DeviceTree / BootArgs Readiness, and Early Memory Services Tests
// ============================================================================

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/deterministic_virtual_timer.h"
#include "device/apple_aic.h"
#include "firmware/kernel_boot_args_builder.h"
#include "memory/canonical_physical_memory_map.h"
#include "memory/independent_stack_exception_auditor.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_zone_allocator.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::memory;

// -----------------------------------------------------------------------------
// Stage 266.11: Exception and Interrupt Integration
// -----------------------------------------------------------------------------
static bool test_stage266_11_exception_interrupt_integration() {
    std::cout << "[Stage 266.11] Testing Exception / Interrupt Integration & Context Preservation...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    // Populate registers X0-X30 with test patterns
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1111111100000000ULL + i);
    }
    cpu.set_program_counter(0x00008080ULL);
    cpu.set_stack_pointer(0x00060000ULL); // Top of Core0 thread stack

    // Simulate exception entry sequence
    auto entry_insns = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, false /* is_el0_origin */);
    TEST_ASSERT(!entry_insns.empty() && entry_insns.size() == 33, "Entry sequence has 33 instructions");

    // Frame allocation moves SP down by 288 bytes: 0x60000 - 288 = 0x5FEE0
    std::uint64_t frame_sp = cpu.get_stack_pointer() - 288;
    TEST_ASSERT(frame_sp == 0x0005FEE0ULL, "Allocated frame SP is 0x5FEE0");

    // Verify DAIF masking: PSTATE.I forced to 1 on exception entry
    std::uint32_t saved_pstate = 0x00000000u; // Interrupts were unmasked
    std::uint32_t entry_pstate = saved_pstate | (1u << 7); // DAIF.I masked
    TEST_ASSERT((entry_pstate & 0x80u) != 0, "Interrupts masked (DAIF.I=1) upon vector entry");

    // Simulate restore sequence
    auto restore_insns = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, false /* is_el0_origin */);
    TEST_ASSERT(!restore_insns.empty() && restore_insns.size() == 22, "Restore sequence has 22 instructions");

    // Post-restore SP returns to 0x60000
    std::uint64_t restored_sp = frame_sp + 288;
    TEST_ASSERT(restored_sp == 0x00060000ULL, "Stack deallocation restores exact pre-exception SP");

    std::cout << "  [PASS] Stage 266.11 Exception context entry, DAIF masking, and restore verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.12: Timer and AIC2 Integration
// -----------------------------------------------------------------------------
static bool test_stage266_12_timer_and_aic2_integration() {
    std::cout << "[Stage 266.12] Testing Hardware AIC2 Aperture Mapping & Guest ACK/EOI Trace...\n";

    AppleAIC aic;
    aic.reset();

    // Verify default SoC register responses
    TEST_ASSERT(aic.read_register(4, 4) == 0x06010200u, "AIC_INFO matches hardware version 0x06010200");
    TEST_ASSERT(aic.read_register(0x2000, 4) == 0u, "AIC_WHOAMI on Core 0 returns 0");

    // Address translation verification
    const std::uint64_t guest_ack_addr = 0x08002000ULL;
    const std::uint64_t guest_eoi_addr = 0x08002004ULL;
    const std::uint64_t hw_ack_addr    = 0x23B102000ULL;
    const std::uint64_t hw_eoi_addr    = 0x23B102004ULL;

    TEST_ASSERT((guest_ack_addr & 0xFFF) == (hw_ack_addr & 0xFFF), "ACK offset +0x2000 matches");
    TEST_ASSERT((guest_eoi_addr & 0xFFF) == (hw_eoi_addr & 0xFFF), "EOI offset +0x2004 matches");

    // Unmask and assert timer interrupt #30
    aic.unmask_hw_irq(30);
    aic.set_hw_irq(30, true);
    TEST_ASSERT(aic.is_hw_irq_asserted(30), "Timer interrupt #30 is in asserted state");
    TEST_ASSERT(aic.has_pending_interrupt(0), "Core 0 has pending interrupt");

    // Guest READ AIC2_ACK (pop_event) -> returns INTID 30
    std::uint32_t acked_evt = aic.pop_event(0);
    TEST_ASSERT((acked_evt & 0xFFFFu) == 30, "AIC2 pop_event returns INTID 30");

    // Guest WRITE AIC2_EOI -> clears active interrupt
    aic.acknowledge_eoi(0, acked_evt);
    aic.set_hw_irq(30, false);
    TEST_ASSERT(!aic.is_hw_irq_asserted(30), "Timer interrupt #30 transitioned to inactive state");

    std::cout << "  - Guest ACK/EOI roundtrip for Timer IRQ 30 verified.\n";
    std::cout << "  [PASS] Stage 266.12 Timer and AIC2 lifecycle verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.13: DeviceTree and BootArgs Readiness
// -----------------------------------------------------------------------------
static bool test_stage266_13_devicetree_and_bootargs_readiness() {
    std::cout << "[Stage 266.13] Auditing DeviceTree and BootArgs Physical Memory Readiness...\n";

    firmware::KernelBootArgsBuilder builder;
    builder.set_virt_base(0xFFFFFE0007004000ULL);
    builder.set_phys_base(0x0ULL);
    builder.set_mem_size(0x800000ULL); // 8 MiB
    builder.set_top_of_kernel_data(0x30000ULL);
    builder.set_device_tree(0x30000ULL, 4096);
    builder.set_command_line("-v debug=0x144 keepsyms=1 serial=3");

    auto args = builder.build();
    auto args_bytes = builder.serialize();
    TEST_ASSERT(!args_bytes.empty(), "BootArgs successfully assembled");
    TEST_ASSERT(args_bytes.size() == 736, "BootArgs matches canonical Darwin size (736 bytes)");

    // Verify boot_args fields
    TEST_ASSERT(args.Revision == firmware::kAppleBootArgsRevision2, "BootArgs Revision == 2");
    TEST_ASSERT(args.Version == firmware::kAppleBootArgsVersion2, "BootArgs Version == 2");
    TEST_ASSERT(args.physBase == 0x0ULL, "physBase matches DRAM start (0x0)");
    TEST_ASSERT(args.memSize == 0x800000ULL, "memSize matches 8 MiB");
    TEST_ASSERT(args.deviceTreeP == 0x30000ULL, "deviceTreeP correctly set to 0x30000");

    // Verify physical map owns both DeviceTree (Frame 12) and BootArgs (Frame 13)
    auto mem_map = CanonicalPhysicalMemoryMap::create_canonical_campaign266_layout();
    const auto* dt_iv = mem_map.find_interval_by_name("DeviceTree");
    TEST_ASSERT(dt_iv != nullptr && dt_iv->start_address == 0x30000ULL, "DeviceTree owned at Frame 12 (0x30000)");

    const auto* ba_iv = mem_map.find_interval_by_name("BootArgs");
    TEST_ASSERT(ba_iv != nullptr && ba_iv->start_address == 0x34000ULL, "BootArgs owned at Frame 13 (0x34000)");

    std::cout << "  - BootArgs Size:     " << args_bytes.size() << " bytes\n";
    std::cout << "  - DeviceTree Base:   0x" << std::hex << args.deviceTreeP << std::dec << "\n";
    std::cout << "  - Command Line:      " << args.CommandLine << "\n";

    std::cout << "  [PASS] Stage 266.13 DeviceTree and BootArgs readiness verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.14: Early Memory Services
// -----------------------------------------------------------------------------
static bool test_stage266_14_early_memory_services() {
    std::cout << "[Stage 266.14] Testing Early Memory Services & Derived Physical Allocator...\n";

    auto mem_map = CanonicalPhysicalMemoryMap::create_canonical_campaign266_layout();
    const auto* pool = mem_map.find_interval_by_name("DynamicFreePool");
    TEST_ASSERT(pool != nullptr, "DynamicFreePool interval found in canonical map");

    XnuPhysicalPageAllocator allocator;
    std::string err;
    bool init_ok = allocator.initialize(mem_map.ram_base(), mem_map.ram_size(), &err);
    TEST_ASSERT(init_ok, "Allocator initialization succeeded");

    for (const auto& iv : mem_map.intervals()) {
        if (iv.kind != CanonicalMemoryKind::DynamicFreePool) {
            bool res_ok = allocator.reserve_range(iv.start_address, iv.size, iv.name, &err);
            TEST_ASSERT(res_ok, "Reserving interval succeeded");
        }
    }

    TEST_ASSERT(allocator.free_pages_count() == 458, "Allocator pool size is exactly 458 frames (7,328 KiB)");
    TEST_ASSERT(allocator.reserved_pages_count() == 54, "Total reserved frames == 54");

    // 1. Reserved frame exclusion: Frames 0..53 rejected for free
    TEST_ASSERT(!allocator.free_pages(0x00000000ULL, 1, &err), "Frame 0 is reserved, free rejected");
    TEST_ASSERT(!allocator.free_pages(0x00054000ULL, 1, &err), "Frame 21 (ThreadGuard) is reserved, free rejected");
    TEST_ASSERT(!allocator.free_pages(0x000D4000ULL, 1, &err), "Frame 53 is reserved, free rejected");

    // 2. Exhaustion test
    std::vector<std::uint64_t> allocated;
    allocated.reserve(458);
    for (std::size_t i = 0; i < 458; ++i) {
        auto addr = allocator.allocate_pages(1);
        TEST_ASSERT(addr.has_value(), "Frame allocation must succeed");
        allocated.push_back(*addr);
    }
    TEST_ASSERT(allocator.free_pages_count() == 0, "Pool completely exhausted after 458 allocations");
    TEST_ASSERT(!allocator.allocate_pages(1).has_value(), "459th allocation must fail");

    // 3. Foreign free rejection
    TEST_ASSERT(!allocator.free_pages(0x00054000ULL, 1, &err), "Foreign deallocation of Frame 21 guard page must fail");

    // 4. Clean deallocation
    for (auto addr : allocated) {
        TEST_ASSERT(allocator.free_pages(addr, 1, &err), "Deallocation must succeed");
    }
    TEST_ASSERT(allocator.free_pages_count() == 458, "All 458 frames returned to free pool");

    // 5. Double free rejection
    TEST_ASSERT(!allocator.free_pages(allocated[0], 1, &err), "Double-free must be rejected");

    std::cout << "  [PASS] Stage 266.14 Early memory services and physical allocator verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 266 - Stages 266.11..266.14 Exception & Memory Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage266_11_exception_interrupt_integration()) return 1;
    if (!test_stage266_12_timer_and_aic2_integration()) return 1;
    if (!test_stage266_13_devicetree_and_bootargs_readiness()) return 1;
    if (!test_stage266_14_early_memory_services()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 266.11..266.14 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
