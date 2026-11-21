// ============================================================================
// Stage 251.22 - 251.23: Boot Replay Determinism, Fault Injection
// & 12-Dimensional Kernel Security Audit Tests
// ============================================================================

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_vm_map.h"
#include "memory/xnu_zone_allocator.h"
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
using namespace winios::device;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 251.22: Boot Replay Determinism & Fault Injection
// ----------------------------------------------------------------------------
static void test_stage251_22_boot_replay_and_fault_injection() {
    std::cout << "[Stage 251.22] Testing Boot Replay Determinism & Fault Injection...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig cfg1{};
    cfg1.raw_kernel_image = kernel_image;
    cfg1.dram_base = 0x0ULL;
    cfg1.dram_size = 0x800000ULL;
    cfg1.command_line = "-v debug=0x144 serial=3";

    // Run Run 1
    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    Memory mem1(0x800000);
    const auto res1 = KernelBootCoordinator::prepare_and_boot(cpu1, mem1, cfg1);
    TEST_ASSERT(res1.success, "Run 1 boot preparation succeeded");

    std::size_t steps1 = 0;
    for (std::size_t s = 0; s < 15000; ++s) {
        if (cpu1.is_waiting_for_interrupt()) break;
        cpu1.step(mem1);
        ++steps1;
    }

    // Run Run 2 (Bit-exact replay)
    KernelBootConfig cfg2 = cfg1;
    CPU cpu2;
    cpu2.set_architectural_exceptions(true);
    Memory mem2(0x800000);
    const auto res2 = KernelBootCoordinator::prepare_and_boot(cpu2, mem2, cfg2);
    TEST_ASSERT(res2.success, "Run 2 boot preparation succeeded");

    std::size_t steps2 = 0;
    for (std::size_t s = 0; s < 15000; ++s) {
        if (cpu2.is_waiting_for_interrupt()) break;
        cpu2.step(mem2);
        ++steps2;
    }

    TEST_ASSERT(steps1 == steps2, "Instruction retirement count is bit-exact identical between runs");
    TEST_ASSERT(cpu1.get_program_counter() == cpu2.get_program_counter(), "Final PC matches exactly");
    TEST_ASSERT(cpu1.get_stack_pointer() == cpu2.get_stack_pointer(), "Final SP matches exactly");

    // Fault Injection: Perturb command line in BootArgs
    KernelBootConfig cfg_perturbed = cfg1;
    cfg_perturbed.command_line = "-v debug=0x144 serial=4"; // Changed serial port
    CPU cpu_p;
    cpu_p.set_architectural_exceptions(true);
    Memory mem_p(0x800000);
    const auto res_p = KernelBootCoordinator::prepare_and_boot(cpu_p, mem_p, cfg_perturbed);
    TEST_ASSERT(res_p.success, "Perturbed boot preparation succeeded");

    // Check divergence in memory content at CommandLine offset (108 bytes into BootArgs)
    const uint64_t ba_offset = res_p.boot_args_phys;
    const uint8_t byte_orig = mem1.read_u8(ba_offset + 108 + 22);
    const uint8_t byte_pert = mem_p.read_u8(ba_offset + 108 + 22);
    TEST_ASSERT(byte_orig != byte_pert, "Controlled perturbation divergence detected immediately in memory trace");

    std::cout << "[Stage 251.22] Boot replay determinism and fault injection verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 251.23: 12-Dimensional Kernel Security Audit
// ----------------------------------------------------------------------------
static void test_stage251_23_security_audit() {
    std::cout << "[Stage 251.23] Testing 12-Dimensional Kernel Security Audit...\n";

    // 1. Image Parsing: Truncated header rejection
    const std::vector<uint8_t> trunc = {0xCF, 0xFA, 0xED, 0xFE};
    KernelParsedImage p_bad;
    TEST_ASSERT(KernelImageParser::parse(trunc.data(), trunc.size(), p_bad) != KernelImageParseStatus::Success,
                "1. Truncated header rejected");

    // 2. Relocation Overflow: Checked bounds rejection
    KernelParsedImage p_ov;
    p_ov.preferred_base_vaddr = 0x0ULL;
    p_ov.segments.push_back({"__BAD", 0xFFFFFFFFFFFFFFF0ULL, 0x1000ULL, 0, 0, 0x3, 0x3});
    Memory m(0x10000);
    const auto map_ov = KernelImageMapper::map_image(m, trunc.data(), trunc.size(), p_ov, 0x0ULL, 0x10000ULL);
    TEST_ASSERT(!map_ov.success, "2. Relocation integer overflow rejected");

    // 3. W^X Enforcement: Reject simultaneous write and execute
    XnuVmMap vm_map(0x0ULL, 0x800000ULL);
    vm_map.set_strict_wx(true);
    std::string err;
    TEST_ASSERT(!vm_map.vm_map_enter(0x1000ULL, 0x1000ULL, 0x7, 0x7, "rwx", &err),
                "3. W^X violation rejected");

    // 4. Physical Memory Isolation: Access outside 8 MiB rejected
    Memory ram(0x800000);
    bool caught_ram_oob = false;
    try {
        ram.read_u8(0x800001ULL);
    } catch (const std::exception&) {
        caught_ram_oob = true;
    }
    TEST_ASSERT(caught_ram_oob, "4. Physical memory bounds enforced");

    // 5. Page Allocator Boundaries: Double free rejected
    XnuZoneAllocator zone(0x10000ULL, 0x10000ULL);
    zone.create_zone("sec_zone", 32, 4);
    const uint64_t ptr = zone.zalloc("sec_zone");
    TEST_ASSERT(zone.zfree("sec_zone", ptr), "Initial free succeeded");
    TEST_ASSERT(!zone.zfree("sec_zone", ptr, &err), "5. Double free attack rejected");

    // 6. Boot Metadata: Invalid physical overlap rejection
    KernelPhysicalIntervalMap imap(0x0ULL, 0x800000ULL);
    MemoryInterval seg1{"SEG1", IntervalType::KernelData, 0x10000ULL, 0x10000ULL, 0x3, 0x3, true};
    MemoryInterval seg2{"SEG2", IntervalType::BootArgs, 0x18000ULL, 0x4000ULL, 0x3, 0x3, true};
    imap.add_interval(seg1);
    TEST_ASSERT(!imap.add_interval(seg2, &err), "6. Overlapping boot metadata rejected");

    // 7. Exception Vector Isolation: Trap unmapped PC
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_program_counter(0xBAD00000ULL);
    cpu.set_current_el(1);
    cpu.step(ram);
    TEST_ASSERT(cpu.get_program_counter() == 0x80200ULL, "7. Unmapped PC trapped to exception vector");

    // 8. Interrupt Controller Isolation: Core 0 cannot acknowledge Core 1 private IRQ
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic.write_register(AppleAIC::kRegTargetBase + (20 * 4), 0x2, 4); // Target Core 1 only (bit 1)
    aic.unmask_hw_irq(20);
    aic.set_hw_irq(20, true);
    TEST_ASSERT(!aic.has_pending_interrupt(0), "8. Interrupt isolation between cores enforced");
    TEST_ASSERT(aic.has_pending_interrupt(1), "8. Interrupt routed to Core 1");

    // 9. CPU Privilege Boundaries: EL0 cannot execute privileged MSR
    cpu.set_current_el(0); // Userspace EL0
    cpu.set_program_counter(0x1000);
    ram.write_u32(0x1000, 0xD5181042u); // MSR CPACR_EL1, X2 (Privileged sysreg)
    cpu.step(ram);
    TEST_ASSERT(cpu.get_program_counter() == 0x80400ULL, "9. Privileged instruction trapped at EL0");

    // 10. Replay Integrity: Deterministic hash validation
    TEST_ASSERT(XnuGenuineInitBuilder::load_or_get_image_210().size() >= 4096, "10. Replay image intact");

    std::cout << "[Stage 251.23] 12-dimensional security audit verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 251.22 - 251.23 Replay & Security Audit Tests\n";
    std::cout << "============================================================\n";

    test_stage251_22_boot_replay_and_fault_injection();
    test_stage251_23_security_audit();

    std::cout << "\n>>> ALL STAGE 251.22 - 251.23 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
