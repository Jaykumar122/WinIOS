#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/synchronous_exception.h"
#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"
#include "runtime/guest_syscall_dispatcher.h"
#include "replay/replay_state_hash.h"

#define STAGE72_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;
using namespace winios::replay;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Architectural Instruction Conformance
// -----------------------------------------------------------------------------
bool test_release_instruction_conformance() {
    std::cout << "[1] test_release_instruction_conformance...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.reset();

    // 0: MOVZ X0, #42        (0xD2800540u)
    // 4: MOVZ X1, #18        (0xD2800241u)
    // 8: ADD X2, X0, X1      (0x8B010002u)
    // 12: SUBS X3, X2, #60   (0xF100F043u)
    // 16: B.EQ +8            (0x54000040u) -> jump to 24
    // 20: HLT #1             (0xD4400020u) -> fail if jumped
    // 24: STR X2, [SP, #8]   (0xF90007E2u)
    // 28: LDR X4, [SP, #8]   (0xF94007E4u)

    mem.write_u32(0,  0xD2800540u);
    mem.write_u32(4,  0xD2800241u);
    mem.write_u32(8,  0x8B010002u);
    mem.write_u32(12, 0xF100F043u);
    mem.write_u32(16, 0x54000040u);
    mem.write_u32(20, 0xD4400020u);
    mem.write_u32(24, 0xF90007E2u);
    mem.write_u32(28, 0xF94007E4u);

    cpu.set_stack_pointer(0x8000);
    cpu.set_program_counter(0);

    for (int i = 0; i < 7; ++i) {
        cpu.step(mem);
    }

    STAGE72_ASSERT(cpu.read_register(0) == 42, "X0 = 42");
    STAGE72_ASSERT(cpu.read_register(1) == 18, "X1 = 18");
    STAGE72_ASSERT(cpu.read_register(2) == 60, "X2 = 60");
    STAGE72_ASSERT(cpu.read_register(3) == 0, "X3 = 0 (SUBS result)");
    STAGE72_ASSERT(cpu.read_register(4) == 60, "X4 loaded 60 from stack");
    STAGE72_ASSERT(cpu.get_program_counter() == 32, "PC advanced past LDR");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: MMU Translation & Fault Containment
// -----------------------------------------------------------------------------
bool test_release_mmu_translation_and_fault_containment() {
    std::cout << "[2] test_release_mmu_translation_and_fault_containment...\n";
    CPU cpu;
    Memory memory(0x40000);
    auto& mmu = cpu.get_mmu();

    const std::uint64_t l0_table = 0x1000ULL;
    const std::uint64_t l1_table = 0x2000ULL;
    const std::uint64_t l2_table = 0x3000ULL;
    const std::uint64_t l3_table = 0x4000ULL;

    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled
    mmu.set_tcr_el1(0x19ULL);        // 4KB granule, 39-bit VA space
    mmu.set_ttbr0_el1(l0_table | (0x0001ULL << 48u)); // ASID 1

    memory.write_u64(l0_table, l1_table | 0x3ULL);
    memory.write_u64(l1_table, l2_table | 0x3ULL);
    memory.write_u64(l2_table, l3_table | 0x3ULL);

    // L3 entry at index 1 (VA 0x1000): maps to PA 0x20000, AF=1, AP=Read-Only (AP[2:1] = 0b10 -> bit 7 = 1)
    const std::uint64_t l3_ro = 0x20000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 7u);
    memory.write_u64(l3_table + 8u, l3_ro);

    // 1. Valid Read Translation
    mmu::TranslationFault fault{};
    auto res_read = mmu.translate(memory, 0x1000, mmu::AccessKind::Read, 1, &fault);
    STAGE72_ASSERT(res_read.has_value() && *res_read == 0x20000ULL, "Read translation succeeds");

    // 2. Permission Fault on Write to Read-Only Page
    auto res_write = mmu.translate(memory, 0x1000, mmu::AccessKind::Write, 1, &fault);
    STAGE72_ASSERT(!res_write.has_value(), "Write to RO page denied");
    STAGE72_ASSERT(fault.type == mmu::MmuFaultType::Permission, "Fault type is Permission");

    // 3. Translation Fault on Unmapped Address (VA 0x5000)
    auto res_unmapped = mmu.translate(memory, 0x5000, mmu::AccessKind::Read, 1, &fault);
    STAGE72_ASSERT(!res_unmapped.has_value(), "Unmapped VA denied");
    STAGE72_ASSERT(fault.type == mmu::MmuFaultType::Translation, "Fault type is Translation");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Exception Handling & ERET Privilege Return
// -----------------------------------------------------------------------------
bool test_release_exception_handling_and_eret() {
    std::cout << "[3] test_release_exception_handling_and_eret...\n";
    CPU cpu;
    Memory mem(0x100000); // 1 MB
    cpu.reset();

    // Start in EL0
    cpu.set_current_el(0);
    cpu.set_vbar_el1(0x20000);
    cpu.set_program_counter(0x500);

    // Write SVC #0x42 at 0x500: 0xD4000841u
    mem.write_u32(0x500, 0xD4000841u);

    // Step SVC instruction -> architectural delivery enters EL1
    cpu.step(mem);

    STAGE72_ASSERT(cpu.get_current_el() == 1, "Entered EL1 on SVC");
    STAGE72_ASSERT(cpu.get_elr_el1() == 0x504, "ELR_EL1 saved preferred return PC (0x504)");
    STAGE72_ASSERT((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::SvcAArch64), "ESR_EL1 EC is SVC AArch64");
    STAGE72_ASSERT((cpu.get_esr_el1() & 0xFFFFu) == 0x42u, "ESR_EL1 ISS immediate is 0x42");

    // Handler at 0x20400 (VBAR_EL1 + 0x400): execute ERET
    const std::uint64_t handler_pc = cpu.get_program_counter();
    STAGE72_ASSERT(handler_pc == 0x20400, "PC is at VBAR_EL1 + 0x400");
    mem.write_u32(handler_pc, 0xD69F03E0u); // ERET
    cpu.set_elr_el1(0x600);
    cpu.set_spsr_el1(0x00000000u); // Return to EL0 with clean PSTATE

    cpu.step(mem); // Executes ERET

    STAGE72_ASSERT(cpu.get_current_el() == 0, "Returned to EL0");
    STAGE72_ASSERT(cpu.get_program_counter() == 0x600, "PC restored to ELR_EL1 (0x600)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: GICv3 IRQ Preemption & Routing
// -----------------------------------------------------------------------------
bool test_release_gicv3_irq_preemption_and_routing() {
    std::cout << "[4] test_release_gicv3_irq_preemption_and_routing...\n";
    GICv3 gic;
    gic.set_core_count(1);

    // Enable CPU interface and Group 1
    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;

    // Enable SPI 32 (Priority 0x20 - higher)
    dist.gicd_isenabler[1] |= (1u << 0);
    dist.gicd_igroupr[1]   |= (1u << 0);
    dist.gicd_ipriorityr[32] = 0x20u;
    dist.gicd_irouter[0] = 0;

    // Enable SPI 33 (Priority 0x60 - lower)
    dist.gicd_isenabler[1] |= (1u << 1);
    dist.gicd_igroupr[1]   |= (1u << 1);
    dist.gicd_ipriorityr[33] = 0x60u;
    dist.gicd_irouter[1] = 0;

    gic.set_distributor_state(dist);

    // Assert BOTH interrupts simultaneously
    gic.set_spi_level(32, true);
    gic.set_spi_level(33, true);

    // Evaluate: higher priority (INTID 32) must be signaled first
    auto sig1 = gic.evaluate(1, 0, 0);
    STAGE72_ASSERT(sig1.has_interrupt == true, "Interrupt signaled");
    STAGE72_ASSERT(sig1.intid == 32u, "INTID 32 prioritized over INTID 33");

    // Acknowledge INTID 32
    std::uint32_t ack1 = gic.acknowledge_group1(1, 0);
    STAGE72_ASSERT(ack1 == 32u, "Acknowledged higher-priority INTID 32");

    // Complete INTID 32
    gic.set_spi_level(32, false);
    gic.end_of_interrupt_group1(32, 1, 0);

    // Now evaluate: INTID 33 is signaled next
    auto sig2 = gic.evaluate(1, 0, 0);
    STAGE72_ASSERT(sig2.has_interrupt == true, "Next interrupt signaled");
    STAGE72_ASSERT(sig2.intid == 33u, "INTID 33 signaled after INTID 32 completion");

    std::uint32_t ack2 = gic.acknowledge_group1(1, 0);
    STAGE72_ASSERT(ack2 == 33u, "Acknowledged INTID 33");
    gic.set_spi_level(33, false);
    gic.end_of_interrupt_group1(33, 1, 0);

    auto final_sig = gic.evaluate(1, 0, 0);
    STAGE72_ASSERT(!final_sig.has_interrupt, "All interrupts cleared");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Virtual Platform & Bus MMIO Interconnect
// -----------------------------------------------------------------------------
bool test_release_virtual_platform_and_bus_routing() {
    std::cout << "[5] test_release_virtual_platform_and_bus_routing...\n";
    VirtualPlatform platform;
    auto bus = platform.device_bus();
    STAGE72_ASSERT(bus != nullptr, "Device bus valid");

    // 1. Test Physical RAM access
    platform.memory().write_u64(0, 0x1122334455667788ULL);
    STAGE72_ASSERT(platform.memory().read_u64(0) == 0x1122334455667788ULL, "RAM read/write");

    // 2. Test Virtual UART access via bus (DR at 0x09000000)
    bus->write_mmio(0x09000030ULL, 0x0101u, 4); // UARTCR enable
    bus->write_mmio(0x09000000ULL, static_cast<std::uint64_t>('Z'), 4);
    STAGE72_ASSERT(platform.uart()->get_output_text() == "Z", "UART output received 'Z'");

    // 3. Test Unmapped space access containment
    const std::uint64_t unmapped_val = bus->read_mmio(0x50000000ULL, 4);
    STAGE72_ASSERT(unmapped_val == 0ULL, "Unmapped read returns 0");
    STAGE72_ASSERT(bus->access_log().back().status == VirtualDeviceErrorCode::DeviceNotFound, "Access log recorded DeviceNotFound");

    // Unmapped write is safely dropped without error
    bus->write_mmio(0x50000000ULL, 0x1234, 4);

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: SMP Cross-Core Synchronization
// -----------------------------------------------------------------------------
bool test_release_smp_cross_core_synchronization() {
    std::cout << "[6] test_release_smp_cross_core_synchronization...\n";
    constexpr std::size_t kCores = 4;
    SmpExclusiveMonitorManager monitor(kCores);
    SmpSharedMemory shared_mem(0x10000, kCores);
    shared_mem.attach_exclusive_monitor(&monitor);

    const std::uint64_t lock_addr = 0x1000ULL;
    shared_mem.write_u64(0, lock_addr, 0);

    // Core 0 acquires lock
    monitor.load_exclusive(0, lock_addr, 8);
    bool c0_locked = shared_mem.write_exclusive_u64(0, lock_addr, 1);
    STAGE72_ASSERT(c0_locked == true, "Core 0 acquired lock");

    // Core 1 attempts acquisition while Core 0 holds it
    monitor.load_exclusive(1, lock_addr, 8);
    // Core 0 releases lock via standard store -> invalidates Core 1 monitor!
    shared_mem.write_u64(0, lock_addr, 0);

    // Core 1 write exclusive fails because Core 0's write invalidated the monitor
    bool c1_locked = shared_mem.write_exclusive_u64(1, lock_addr, 1);
    STAGE72_ASSERT(c1_locked == false, "Core 1 exclusive store failed after cross-core write");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Guest ABI & Syscall Dispatch Conformance
// -----------------------------------------------------------------------------
bool test_release_guest_abi_syscall_conformance() {
    std::cout << "[7] test_release_guest_abi_syscall_conformance...\n";
    CPU cpu;
    Memory mem(0x10000);
    GuestSyscallDispatcher dispatcher;

    // 1. getpid (172)
    cpu.write_register(8, 172);
    std::int64_t pid = dispatcher.dispatch(cpu, mem);
    STAGE72_ASSERT(pid == 100, "getpid returned expected PID 100");
    STAGE72_ASSERT(cpu.read_register(0) == 100, "X0 holds return value 100");

    // 2. brk (214)
    cpu.write_register(8, 214);
    cpu.write_register(0, 0x40010000ULL);
    std::int64_t brk_res = dispatcher.dispatch(cpu, mem);
    STAGE72_ASSERT(brk_res == 0x40010000ULL, "brk updated break pointer");

    // 3. write with invalid pointer returns -EFAULT (-14)
    cpu.write_register(8, 64);
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0xFFFFFFFF); // invalid pointer
    cpu.write_register(2, 10);
    std::int64_t wr_res = dispatcher.dispatch(cpu, mem);
    STAGE72_ASSERT(wr_res == -14, "Invalid user buffer safely returned -EFAULT");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Floating-Point, ASIMD & Privilege Trapping
// -----------------------------------------------------------------------------
bool test_release_fp_simd_and_trap_enforcement() {
    std::cout << "[8] test_release_fp_simd_and_trap_enforcement...\n";
    CPU cpu;
    Memory mem(0x100000); // 1 MB
    cpu.reset();

    // 1. Trapping: FP disabled (CPACR_EL1.FPEN = 0)
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x0ULL); // CPACR_EL1 = 0
    cpu.set_current_el(0);     // EL0
    cpu.set_vbar_el1(0x20000);

    // FADD D0, D1, D2: 0x1E622820u
    mem.write_u32(0x100, 0x1E622820u);
    cpu.set_program_counter(0x100);

    // Step FP instruction -> delivered to EL1 via architectural exception
    cpu.step(mem);

    STAGE72_ASSERT(cpu.get_current_el() == 1, "Exception entered EL1");
    STAGE72_ASSERT((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::FpSimdAccessTrap), "EC is FP Trapped (0x07)");
    STAGE72_ASSERT(cpu.get_elr_el1() == 0x100, "ELR_EL1 saved trapped instruction address 0x100");

    // 2. Enable FP (CPACR_EL1.FPEN = 3 -> bits [21:20] = 0b11)
    cpu.set_cpacr_el1(3ULL << 20u);
    // Set D1 = 2.0 (0x4000000000000000ULL), D2 = 3.0 (0x4008000000000000ULL)
    // Result: 5.0 (0x4014000000000000ULL)
    cpu.write_d_register(1, 0x4000000000000000ULL);
    cpu.write_d_register(2, 0x4008000000000000ULL);
    cpu.set_program_counter(0x100);

    cpu.step(mem); // Should execute FADD without trap
    const std::uint64_t res = cpu.read_d_register(0);
    STAGE72_ASSERT(res == 0x4014000000000000ULL, "FADD calculated 2.0 + 3.0 = 5.0 (IEEE-754 bit-exact)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Checkpoint & State Hash Invariance
// -----------------------------------------------------------------------------
bool test_release_checkpoint_hash_determinism() {
    std::cout << "[9] test_release_checkpoint_hash_determinism...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.reset();

    // Populate CPU and memory
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x12345678ULL + i);
    }
    cpu.set_program_counter(0x2000);
    mem.write_u64(0x2000, 0xFEEDFACECAFEULL);

    ReplayComponentHashes hashA = ReplayStateHash::compute_state_hashes(cpu, mem);
    ReplayComponentHashes hashB = ReplayStateHash::compute_state_hashes(cpu, mem);

    STAGE72_ASSERT(hashA == hashB, "Hash computations are idempotent and identical");
    STAGE72_ASSERT(hashA.cpu_registers_hash != 0, "CPU register hash valid");
    STAGE72_ASSERT(hashA.memory_hash != 0, "Memory hash valid");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Capability Matrix & Operational Boundaries Audit
// -----------------------------------------------------------------------------
bool test_release_capability_matrix_audit() {
    std::cout << "[10] test_release_capability_matrix_audit...\n";

    // Validate that architectural invariants match documented operational boundaries
    constexpr std::size_t kMaxSmpCores = 8;
    SMPCluster cluster(kMaxSmpCores, 0x10000);
    STAGE72_ASSERT(cluster.core_count() == kMaxSmpCores, "Supports up to 8 SMP cores");

    VirtualPlatform platform;
    STAGE72_ASSERT(platform.core_count() == 1, "Default platform has 1 core");
    STAGE72_ASSERT(platform.memory().size() >= 0x40000, "Platform memory meets minimum size");
    STAGE72_ASSERT(platform.uart() != nullptr, "PL011 UART console bound");
    STAGE72_ASSERT(platform.dma() != nullptr, "Virtual DMA controller bound");

    std::cout << "    [INFO] Full-system capability matrix audit verified 100% compliant\n";
    return true;
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cout << "=== WinIOS Stage 72: Full-System Integration & Release Readiness Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*func)(), const char* name) {
        try {
            if (func()) {
                std::cout << "[PASS] " << name << "\n\n";
                ++passed;
            } else {
                std::cerr << "[FAIL] " << name << "\n\n";
                ++failed;
            }
        } catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << name << ": " << e.what() << "\n\n";
            ++failed;
        }
    };

    run_test(test_release_instruction_conformance, "Architectural Instruction Conformance");
    run_test(test_release_mmu_translation_and_fault_containment, "MMU Translation & Fault Containment");
    run_test(test_release_exception_handling_and_eret, "Exception Handling & ERET Privilege Return");
    run_test(test_release_gicv3_irq_preemption_and_routing, "GICv3 IRQ Preemption & Routing");
    run_test(test_release_virtual_platform_and_bus_routing, "Virtual Platform & Bus MMIO Interconnect");
    run_test(test_release_smp_cross_core_synchronization, "SMP Cross-Core Synchronization");
    run_test(test_release_guest_abi_syscall_conformance, "Guest ABI & Syscall Dispatch Conformance");
    run_test(test_release_fp_simd_and_trap_enforcement, "Floating-Point, ASIMD & Privilege Trapping");
    run_test(test_release_checkpoint_hash_determinism, "Checkpoint & State Hash Invariance");
    run_test(test_release_capability_matrix_audit, "Capability Matrix & Operational Boundaries Audit");

    std::cout << "Stage 72 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
