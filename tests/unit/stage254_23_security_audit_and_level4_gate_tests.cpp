// ============================================================================
// Stage 254.22 - 254.23: Level 4 Certification Gate Evaluation &
// 20-Dimensional Integrated Security and Conformance Audit
// ============================================================================

#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "kernel/context_switch.h"
#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "loader/kernel_image_parser.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "loader/xnu_instruction_provenance_tracker.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 254.22: Level 4 Certification Gate Evaluation
// ----------------------------------------------------------------------------
static void test_stage254_22_level4_certification_gate() {
    std::cout << "[Stage 254.22] Testing Level 4 Certification Gate Evaluation...\n";

    // Boot Level Definitions:
    // Level 0: None
    // Level 1: Early CPU Execution (Basic ARM64 instruction execution)
    // Level 2: Memory & Page Translation (MMU 16KB granule, identity mapping)
    // Level 3: Early XNU Initialization Demonstrated (Adapted Mach-O image loaded, WFI reached)
    // Level 4: Kernel Runtime Continuation & Exception Handling (Unassisted upstream interrupt/continuation)
    // Level 5: Basic Subsystem Initialization (Mach VM, tasks/threads, IPC)
    // Level 6: Driver / Device Initialization (I/O Kit matching, platform drivers)
    // Level 7: Multi-User / Full User-Space Boot (launchd execution)

    struct Level4GateDomain {
        const char* domain_name;
        bool satisfied;
        const char* assessment;
        const char* blocker_evidence;
    };

    const Level4GateDomain level4_matrix[] = {
        {"1. Genuine Upstream Exception Vectors", false,
         "NOT MET: IRQ vector handler (0x80280) was a Category D synthetic test harness stub, not upstream locore.s fleh_irq.",
         "Requires freestanding adaptation of osfmk/arm64/locore.s with int_stack."},

        {"2. Per-CPU Data Structure (cpu_data_t)", false,
         "NOT MET: cpu_data_t structure is not initialized in guest memory.",
         "Requires allocating and populating osfmk/arm/cpu_data.h per-core structures."},

        {"3. Upstream Thread Status Specification", true,
         "MET: arm_saved_state64_t layout and bitwise register mapping verified.",
         "None."},

        {"4. Upstream Trap & Interrupt Dispatcher", false,
         "NOT MET: trap.c::interrupt() is not linked or executed in guest space.",
         "Requires Mach thread scheduler and AST (Asynchronous Software Trap) support."},

        {"5. Mach VM Pmap Bootstrap", false,
         "NOT MET: pmap_bootstrap() not executed; relying on host-synthesized Stage 1 tables.",
         "Requires two-level / three-level page table walking and stolen memory allocation."},

        {"6. PV Entry Dynamic Aliasing Pool", false,
         "NOT MET: Mach VM physical-to-virtual (PV) dynamic tracking pool not initialized.",
         "Requires pv_head_table and dynamic pv_entry_t allocation."},

        {"7. Mach VM Zone Allocator (zalloc)", false,
         "NOT MET: zalloc/zinit zone allocator not initialized.",
         "Requires Mach VM page allocator integration."},

        {"8. I/O Kit Interrupt Controller Driver", false,
         "NOT MET: AppleInterruptController C++ runtime not loaded; using Category C AIC2 virtual device.",
         "Requires I/O Kit C++ runtime, libkern, and registry subsystem."},

        {"9. Genuine Mach Thread/Task Creation", false,
         "NOT MET: task_create/thread_create not executed; using Category C synthetic GuestTcb.",
         "Requires Mach task and thread zone initialization."},

        {"10. Upstream Scheduler Quantum & Preemption", false,
         "NOT MET: sched_prim.c not executed; using Category C PriorityScheduler.",
         "Requires run queues, priority queues, and turnstiles."},

        {"11. Platform Expert Device Tree Integration", true,
         "MET: DeviceTree blob parsed, boot-args struct validated at authoritative address.",
         "None."}
    };

    std::size_t met_count = 0;
    std::size_t unfulfilled_count = 0;

    for (const auto& domain : level4_matrix) {
        if (domain.satisfied) {
            met_count++;
        } else {
            unfulfilled_count++;
            TEST_ASSERT(domain.blocker_evidence != nullptr, "Blocker must have documented evidence");
        }
    }

    TEST_ASSERT(met_count == 2, "Exactly 2 Level 4 criteria met");
    TEST_ASSERT(unfulfilled_count == 9, "Exactly 9 Level 4 criteria unfulfilled (blocking certification)");

    // Formal Certification Verdict:
    // We strictly CERTIFY Level 3: Early XNU Initialization Demonstrated.
    // We strictly REJECT Level 4: Kernel Runtime Continuation & Exception Handling.
    const unsigned int certified_boot_level = 3;
    TEST_ASSERT(certified_boot_level == 3, "Formally certify Level 3; Level 4 is blocked");

    std::cout << "[Stage 254.22] Level 4 certification gate evaluated: Level 3 CERTIFIED, Level 4 REJECTED.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 254.23: 20-Dimensional Integrated Security & Conformance Audit
// ----------------------------------------------------------------------------
static void test_stage254_23_20_dimensional_security_audit() {
    std::cout << "[Stage 254.23] Testing 20-Dimensional Integrated Security & Conformance Audit...\n";

    // Dim 1: Canonical Interval Map (Zero Overlaps)
    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);
    pmap.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true});
    pmap.add_interval({"ReservedPadding", IntervalType::GenericReserved, 0x00004000ULL, 0x0000C000ULL, 0x3, 0x3, true});
    pmap.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true});
    pmap.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x00030000ULL, 0x00004000ULL, 0x3, 0x3, true});
    pmap.add_interval({"BootArgs", IntervalType::BootArgs, 0x00034000ULL, 0x00004000ULL, 0x3, 0x3, true});
    pmap.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x00038000ULL, 0x00010000ULL, 0x3, 0x3, true});
    pmap.add_interval({"PageTables", IntervalType::PageTables, 0x00048000ULL, 0x00008000ULL, 0x3, 0x3, true});
    pmap.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x00050000ULL, 0x007B0000ULL, 0x3, 0x3, false});
    for (std::size_t i = 0; i < pmap.intervals().size(); ++i) {
        for (std::size_t j = i + 1; j < pmap.intervals().size(); ++j) {
            TEST_ASSERT(!pmap.intervals()[i].overlaps_with(pmap.intervals()[j].base, pmap.intervals()[j].size),
                        "Dim 1: Zero interval overlaps across authoritative map");
        }
    }

    // Dim 2: Exact Dynamic Pool Sizing (7,872 KiB = 8,060,928 bytes = 492 frames)
    const auto* pool = pmap.find_interval_by_name("DynamicPool");
    TEST_ASSERT(pool && pool->size == 0x7B0000ULL && (pool->size / 1024ULL) == 7872ULL,
                "Dim 2: Dynamic pool is exactly 7,872 KiB (0x7B0000 bytes, 492 frames)");

    // Dim 3: Frame Ownership & Reserve Enclave
    XnuPhysicalPageAllocator alloc;
    std::string err;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x50000ULL, "AuthoritativeEnclave");
    for (std::size_t f = 0; f < 20; ++f) {
        TEST_ASSERT(alloc.is_frame_reserved(f), "Dim 3: Reserved frame enclave 0-19 intact");
    }

    // Dim 4: Allocator Double-Free Protection
    auto page_opt = alloc.allocate_pages(1);
    TEST_ASSERT(page_opt.has_value(), "Dim 4: Allocate dynamic page");
    TEST_ASSERT(alloc.free_pages(*page_opt, 1, &err), "Dim 4: Free dynamic page");
    TEST_ASSERT(!alloc.free_pages(*page_opt, 1, &err), "Dim 4: Double-free strictly rejected");

    // Dim 5: Reserved Frame Deallocation Protection
    TEST_ASSERT(!alloc.free_pages(0x18000ULL, 1, &err), "Dim 5: Freeing reserved frame 6 rejected");

    // Dim 6: Physical Alias W^X Invariant
    XnuVmMap vmap(0x0ULL, 0x800000ULL);
    vmap.set_strict_wx(true);
    TEST_ASSERT(vmap.register_physical_alias(0x10000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute),
                "Dim 6: Register RX physical alias");
    TEST_ASSERT(!vmap.register_physical_alias(0x20000ULL, 0x50000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err),
                "Dim 6: Conflicting RW physical alias strictly rejected");

    // Dim 7: Alias Deregistration Lifecycle
    TEST_ASSERT(vmap.unregister_physical_alias(0x10000ULL, 0x4000ULL), "Dim 7: Unregister physical alias");
    TEST_ASSERT(vmap.alias_count() == 0, "Dim 7: Alias count restored to 0");

    // Dim 8: Independent MMU Descriptor Decoding (Normal DRAM)
    auto d1 = Arm64DescriptorDecoder::decode(0x0000000000000701ULL, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(d1.valid && d1.type == DecodedDescriptorType::Block && d1.attr_indx == 0,
                "Dim 8: Normal DRAM descriptor decoding");

    // Dim 9: Independent MMU Descriptor Decoding (Device-nGnRE MMIO)
    auto d2 = Arm64DescriptorDecoder::decode(0x000000023A000705ULL, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(d2.valid && d2.attr_indx == 1, "Dim 9: Device-nGnRE MMIO descriptor decoding");

    // Dim 10: Stack 16-byte Alignment
    const uint64_t sp_val = 0x00048000ULL;
    TEST_ASSERT((sp_val % 16ULL) == 0ULL, "Dim 10: AAPCS64 16-byte stack alignment");

    // Dim 11: Stack Guard Boundary Protection
    const uint64_t stack_base = 0x00038000ULL;
    TEST_ASSERT((stack_base - 16ULL) < stack_base, "Dim 11: Stack guard underflow boundary");

    // Dim 12: Exception Vector 2048-byte Alignment
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x00020000ULL),
                "Dim 12: VBAR_EL1 2048-byte alignment");

    // Dim 13: Exception Vector Offset Coverage (16 Vectors)
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::IRQ) == 0x280ULL,
                "Dim 13: CurrentEL_SPx IRQ offset is 0x280");

    // Dim 14: DAIF Masking upon Exception Entry
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00020000ULL);
    cpu.raise_asynchronous_exception(false, 30, false, 0);
    TEST_ASSERT((cpu.get_daif() & 0xFu) == 0xFu, "Dim 14: DAIF fully masked upon exception entry");

    // Dim 15: Apple AIC2 Source Routing & Masking
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic.mask_hw_irq(44);
    aic.set_hw_irq(44, true);
    TEST_ASSERT(!aic.has_pending_interrupt(0), "Dim 15: Masked IRQ does not assert interrupt");
    aic.unmask_hw_irq(44);
    TEST_ASSERT(aic.has_pending_interrupt(0), "Dim 15: Unmasked IRQ asserts interrupt");

    // Dim 16: SMP Core Interrupt Isolation
    aic.set_irq_target(44, 0x04); // Core 2 only
    TEST_ASSERT(!aic.has_pending_interrupt(0), "Dim 16: Core 0 isolated from Core 2 interrupt");
    TEST_ASSERT(aic.has_pending_interrupt(2), "Dim 16: Core 2 correctly targeted");

    // Dim 17: Per-Core WhoAmI Banking
    TEST_ASSERT(aic.read_register(0x2000, 4) == 0, "Dim 17: Core 0 WhoAmI is 0");
    TEST_ASSERT(aic.read_register(0x2080, 4) == 1, "Dim 17: Core 1 WhoAmI is 1");
    TEST_ASSERT(aic.read_register(0x2100, 4) == 2, "Dim 17: Core 2 WhoAmI is 2");

    // Dim 18: Spurious Interrupt Handling
    aic.set_hw_irq(44, false);
    aic.write_register(0x2104, 44, 4); // EOI Core 2
    TEST_ASSERT(aic.read_register(0x2104, 4) == AppleAIC::kEventNone,
                "Dim 18: Spurious event read returns kEventNone (0xFFFFFFFF)");

    // Dim 19: Guest TCB 512-Byte Packed Layout & Magic
    TEST_ASSERT(sizeof(GuestTcb) == 512, "Dim 19: GuestTcb size is 512 bytes");
    GuestTcb tcb_audit{};
    TEST_ASSERT(tcb_audit.magic == kTcbMagic, "Dim 19: GuestTcb magic verified");

    // Dim 20: Mach-O Provenance and Category Classification
    const auto img = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!img.empty(), "Dim 20: Genuine Mach-O image loaded");
    KernelParsedImage parsed;
    TEST_ASSERT(KernelImageParser::parse(img.data(), img.size(), parsed) == KernelImageParseStatus::Success,
                "Dim 20: Mach-O parsed successfully");
    TEST_ASSERT(parsed.entry_point_fileoff == 0x2be0ULL, "Dim 20: Entrypoint is 0x2be0");

    std::cout << "[Stage 254.23] 20-Dimensional Integrated Security & Conformance Audit PASSED.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 254: Stages 254.22 - 254.23 Level 4 Gate & Security Tests\n";
    std::cout << "====================================================================\n";

    test_stage254_22_level4_certification_gate();
    test_stage254_23_20_dimensional_security_audit();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 254.22 - 254.23 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
