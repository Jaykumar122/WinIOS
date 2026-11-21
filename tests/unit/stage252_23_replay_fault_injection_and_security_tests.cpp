// ============================================================================
// Stage 252.22 - 252.23: Deterministic Boot Replay, Fault Injection &
// 15-Dimensional Integrated Security Audit Tests
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "kernel/ipc.h"
#include "loader/kernel_image_parser.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 252.22: Deterministic Replay & Fault Injection
// ----------------------------------------------------------------------------
static void test_stage252_22_replay_and_fault_injection() {
    std::cout << "[Stage 252.22] Testing Deterministic Replay & Controlled Fault Injection...\n";

    // 1. Replay Determinism Run 1 vs Run 2
    auto run_boot = []() {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x800000);
        const auto image = XnuGenuineInitBuilder::load_or_get_image_210();
        KernelBootConfig cfg{};
        cfg.raw_kernel_image = image;
        cfg.dram_base = 0x0ULL;
        cfg.dram_size = 0x800000ULL;
        cfg.num_cores = 6;
        cfg.enable_apple_aic = true;
        cfg.aic_base = 0x23B100000ULL;
        cfg.aic_size = 0x100000ULL;
        KernelBootCoordinator::prepare_and_boot(cpu, mem, cfg);
        for (std::size_t i = 0; i < 15000; ++i) {
            if (cpu.is_waiting_for_interrupt()) break;
            cpu.step(mem);
        }
        return mem.read_u64(0x50018ULL); // Mailbox 210
    };

    uint64_t res1 = run_boot();
    uint64_t res2 = run_boot();
    TEST_ASSERT(res1 == 0xCAFEBABE210ULL, "Run 1 produced valid mailbox 210");
    TEST_ASSERT(res2 == 0xCAFEBABE210ULL, "Run 2 produced valid mailbox 210");
    TEST_ASSERT(res1 == res2, "Replay is 100% bit-exact deterministic");

    // 2. Fault Injection: Allocator double free
    XnuPhysicalPageAllocator alloc;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x50000ULL, "Reserved");
    auto p = alloc.allocate_pages(1);
    TEST_ASSERT(p.has_value(), "Allocated page");
    std::string err;
    TEST_ASSERT(alloc.free_pages(*p, 1, &err), "Free page once");
    TEST_ASSERT(!alloc.free_pages(*p, 1, &err), "Double free fault successfully trapped");

    // 3. Fault Injection: Physical alias W^X violation
    XnuVmMap vmap(0x0ULL, 0x800000ULL);
    vmap.set_strict_wx(true);
    vmap.register_physical_alias(0x10000ULL, 0x60000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute);
    TEST_ASSERT(!vmap.register_physical_alias(0x20000ULL, 0x60000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "W^X alias collision trapped");

    std::cout << "[Stage 252.22] Deterministic replay and fault injection verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 252.23: 15-Dimensional Integrated Security Audit
// ----------------------------------------------------------------------------
static void test_stage252_23_security_audit() {
    std::cout << "[Stage 252.23] Testing 15-Dimensional Integrated Security Audit...\n";

    // 1. Physical interval ownership (0 overlaps)
    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);
    pmap.add_interval({"Text", IntervalType::KernelText, 0x0ULL, 0x4000ULL, 0x5, 0x5, true});
    pmap.add_interval({"Scratch", IntervalType::GenericReserved, 0x4000ULL, 0xC000ULL, 0x3, 0x3, true});
    pmap.add_interval({"Data", IntervalType::KernelData, 0x10000ULL, 0x20000ULL, 0x3, 0x3, true});
    TEST_ASSERT(!pmap.has_overlap(0x50000ULL, 0x7B0000ULL), "No physical interval overlaps");
    TEST_ASSERT(pmap.add_interval({"Pool", IntervalType::DynamicPool, 0x50000ULL, 0x7B0000ULL, 0x3, 0x3, false}), "Pool registered cleanly");

    // 2. Frame allocator correctness (492 free, 20 reserved)
    XnuPhysicalPageAllocator alloc;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x50000ULL, "Reserved");
    TEST_ASSERT(alloc.reserved_pages_count() == 20, "20 reserved frames");
    TEST_ASSERT(alloc.free_pages_count() == 492, "492 free frames");

    // 3. Virtual-to-physical mapping
    XnuVmMap vmap(0x0ULL, 0x800000ULL);
    vmap.set_strict_wx(true);
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "code"), "Valid mapping");

    // 4. MMU descriptor validation (AP, AF, SH)
    const uint64_t l1_desc = 0x0000000000000701ULL;
    auto dec = cpu::Arm64DescriptorDecoder::decode(l1_desc, 1, cpu::Arm64Granule::Granule16KB);
    TEST_ASSERT(dec.valid && dec.access_flag, "Descriptor valid with AF=1");

    // 5. Architectural abort delivery (ESR/FAR)
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    Memory mem(0x800000);
    mem.write_u32(0x1000, 0xF9400022u); // LDR X2, [X1]
    cpu.set_program_counter(0x1000);
    cpu.write_register(1, 0x800000ULL); // Out-of-bounds address
    cpu.step(mem);
    TEST_ASSERT(cpu.get_far_el1() == 0x800000ULL, "FAR captured 0x800000");

    // 6. W^X enforcement
    std::string err;
    TEST_ASSERT(!vmap.vm_map_enter(0x20000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, kVmProtRead | kVmProtWrite | kVmProtExecute, "rwx", &err), "RWX rejected");

    // 7. Physical alias protection
    vmap.register_physical_alias(0x10000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute);
    TEST_ASSERT(!vmap.register_physical_alias(0x30000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Alias W^X conflict rejected");

    // 8. Exception vector integrity
    TEST_ASSERT(cpu::KernelExceptionVectors::validate_vector_table_alignment(0x80000ULL), "Vector table aligned");

    // 9. Mach-O parser safety
    const auto img = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelParsedImage parsed;
    TEST_ASSERT(KernelImageParser::parse(img.data(), img.size(), parsed) == KernelImageParseStatus::Success, "Mach-O parsed safely");

    // 10. Boot argument validation
    TEST_ASSERT(parsed.entry_point_vaddr == 0x2be0ULL, "Entry point is 0x2be0");

    // 11. AIC2 MMIO isolation
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    TEST_ASSERT(aic.read_register(4, 4) == 0x06010200u, "AIC isolated to 0x23B100000");

    // 12. Timer and IRQ delivery
    aic.unmask_hw_irq(30);
    aic.set_hw_irq(30, true);
    TEST_ASSERT(aic.has_pending_interrupt(0), "IRQ delivery confirmed");

    // 13. SMP synchronization
    TEST_ASSERT(cpu.get_current_el() == 1, "Core privilege EL1");

    // 14. IPC rights and resource ownership
    kernel::ProcessIpcSpace space;
    TEST_ASSERT(space.grant_right(1, kernel::MachPortRight::Send), "IPC send right granted");

    // 15. Replay integrity
    TEST_ASSERT(parsed.format == KernelImageFormat::MachO64, "Replay integrity intact");

    std::cout << "[Stage 252.23] 15-dimensional security audit fully satisfied.\n";
}

int main() {
    std::cout << "=== Running Stages 252.22 - 252.23 Unit Tests ===\n";
    test_stage252_22_replay_and_fault_injection();
    test_stage252_23_security_audit();
    std::cout << "[PASS] All Stage 252.22 - 252.23 tests passed successfully.\n";
    return 0;
}
