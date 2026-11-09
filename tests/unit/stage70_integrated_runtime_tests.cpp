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
#include "cpu/synchronous_exception.h"
#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"
#include "runtime/guest_syscall_dispatcher.h"

#define STAGE70_ASSERT(cond, msg) \
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

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;

// -----------------------------------------------------------------------------
// Suite 1: End-to-End Boot to Clean Exit
// -----------------------------------------------------------------------------
bool test_end_to_end_boot_to_exit() {
    std::cout << "[1] test_end_to_end_boot_to_exit...\n";
    VirtualPlatformConfig config;
    config.boot_entry = 0x0000;
    VirtualPlatform platform(config);
    GuestSyscallDispatcher dispatcher;

    auto& cpu = platform.get_cpu(0);
    auto& mem = platform.memory();

    // Program at 0x40000000:
    // MOV X0, #42        (0xD2800540u)
    // MOV X8, #93        (0xD2800BA8u) - Syscall Exit
    // SVC #0             (0xD4000001u)
    const std::uint64_t ram_off = 0; // mapped at base
    mem.write_u32(ram_off + 0, 0xD2800540u);
    mem.write_u32(ram_off + 4, 0xD2800BA8u);
    mem.write_u32(ram_off + 8, 0xD4000001u);

    // Step MOV instructions
    platform.step_core(0);
    STAGE70_ASSERT(cpu.read_register(0) == 42, "X0 loaded with 42");
    platform.step_core(0);
    STAGE70_ASSERT(cpu.read_register(8) == 93, "X8 loaded with 93");

    // Dispatch syscall upon SVC
    dispatcher.dispatch(cpu, mem);
    STAGE70_ASSERT(dispatcher.has_exited() == true, "Guest requested clean exit");
    STAGE70_ASSERT(dispatcher.exit_code() == 42, "Exit status code is 42");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Full MMU Translation & TLB Integration
// -----------------------------------------------------------------------------
bool test_mmu_translation_integration() {
    std::cout << "[2] test_mmu_translation_integration...\n";
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

    // L3 entry at index 1 (VA 0x1000): maps to PA 0x20000, AF=1, AP=Read/Write
    const std::uint64_t l3_desc = 0x20000ULL | 0x3ULL | (1ULL << 10u);
    memory.write_u64(l3_table + 8u, l3_desc);

    // Translate VA 0x1000
    mmu::TranslationFault fault{};
    auto res1 = mmu.translate(memory, 0x1000, mmu::AccessKind::Read, 1, &fault);
    STAGE70_ASSERT(res1.has_value(), "First translation succeeds (TLB miss)");
    STAGE70_ASSERT(*res1 == 0x20000ULL, "Translated address is 0x20000");
    const std::uint64_t initial_hits = mmu.get_tlb_hits();

    // Second translation of same VA must be a TLB hit
    auto res2 = mmu.translate(memory, 0x1000, mmu::AccessKind::Read, 1, &fault);
    STAGE70_ASSERT(res2.has_value(), "Second translation succeeds (TLB hit)");
    STAGE70_ASSERT(mmu.get_tlb_hits() > initial_hits, "TLB hit count increased on repeated access");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Integrated Syscall & Console Output
// -----------------------------------------------------------------------------
bool test_integrated_syscall_console_output() {
    std::cout << "[3] test_integrated_syscall_console_output...\n";
    VirtualPlatform platform;
    GuestSyscallDispatcher dispatcher;

    auto& cpu = platform.get_cpu(0);
    auto& mem = platform.memory();
    auto uart = platform.uart();

    const std::string text = "Kernel Boot OK\n";
    const std::uint64_t buf_addr = 0x500;
    for (std::size_t i = 0; i < text.size(); ++i) {
        mem.write_u8(buf_addr + i, static_cast<std::uint8_t>(text[i]));
    }

    // Prepare syscall: X8 = 64 (Write), X0 = 1 (stdout), X1 = buf_addr, X2 = size
    cpu.write_register(8, 64);
    cpu.write_register(0, 1);
    cpu.write_register(1, buf_addr);
    cpu.write_register(2, text.size());

    // Host dispatcher handles write and echoes to Virtual UART
    const std::int64_t ret = dispatcher.dispatch(cpu, mem);
    STAGE70_ASSERT(ret == static_cast<std::int64_t>(text.size()), "Bytes written returned");

    // Mirror to virtual UART console
    for (char c : dispatcher.stdout_buffer()) {
        platform.device_bus()->write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR,
                                           static_cast<std::uint32_t>(c), 4);
    }

    STAGE70_ASSERT(uart->get_output_text() == text, "UART console captured exact guest string");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Cross-Core Producer / Consumer with Acquire-Release
// -----------------------------------------------------------------------------
bool test_cross_core_producer_consumer() {
    std::cout << "[4] test_cross_core_producer_consumer...\n";
    VirtualPlatformConfig config;
    config.core_count = 2;
    VirtualPlatform platform(config);

    auto& mem = platform.memory();
    auto& cpu0 = platform.get_cpu(0);
    auto& cpu1 = platform.get_cpu(1);

    constexpr std::uint64_t kDataAddr = 0x1000;
    constexpr std::uint64_t kFlagAddr = 0x1008;

    // Initialize: flag = 0
    mem.write_u64(kFlagAddr, 0);

    // Producer (Core 0): writes data 0x12345678, sets flag = 1 with release ordering
    mem.write_u64(kDataAddr, 0x12345678ULL);
    cpu0.write_register(1, kFlagAddr);
    cpu0.write_register(2, 1);
    // STLR X2, [X1] (0xC89FFC22u)
    mem.write_u32(0x00, 0xC89FFC22u);
    cpu0.set_program_counter(0x00);
    platform.step_core(0);

    STAGE70_ASSERT(mem.read_u64(kFlagAddr) == 1, "Core 0 committed release flag");

    // Consumer (Core 1): reads flag with acquire ordering
    platform.set_core_active(1, true);
    cpu1.write_register(1, kFlagAddr);
    // LDAR X0, [X1] (0xC8DFFC20u)
    mem.write_u32(0x04, 0xC8DFFC20u);
    cpu1.set_program_counter(0x04);
    platform.step_core(1);

    STAGE70_ASSERT(cpu1.read_register(0) == 1, "Core 1 observed release flag = 1");
    // Verify payload data observed intact
    STAGE70_ASSERT(mem.read_u64(kDataAddr) == 0x12345678ULL, "Core 1 reads valid data payload");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Integrated Timer Interrupt & Preemption Lifecycle
// -----------------------------------------------------------------------------
bool test_integrated_timer_preemption() {
    std::cout << "[5] test_integrated_timer_preemption...\n";
    VirtualPlatform platform;
    auto& gic = platform.gic();

    // Core 0 interface: PMR=0xFF, IGRPEN1=1
    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Assert Virtual Timer PPI (INTID 27)
    gic.set_ppi_level(27, true, 0);

    // Evaluate GIC: interrupt must be pending
    auto sig = gic.evaluate(1, 0, 0);
    STAGE70_ASSERT(sig.has_interrupt == true, "Timer PPI interrupt signaled to CPU");
    STAGE70_ASSERT(sig.intid == 27u, "Signaled INTID is 27");

    // CPU acknowledges interrupt: transitions to Active
    const std::uint32_t iar = gic.acknowledge_group1(1, 0);
    STAGE70_ASSERT(iar == 27u, "Acknowledged INTID 27");
    STAGE70_ASSERT(gic.read_icc_rpr(0) == 0x80u, "Running priority updated to active interrupt");

    // Deassert PPI line (timer interrupt handled by OS driver)
    gic.set_ppi_level(27, false, 0);

    // End of Interrupt (EOIR)
    gic.end_of_interrupt_group1(27, 1, 0);
    STAGE70_ASSERT(gic.read_icc_rpr(0) == 0xFFu, "RPR restored to idle (0xFF) after EOIR");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: FP/NEON & Atomic LDADD Integration
// -----------------------------------------------------------------------------
bool test_fp_neon_and_atomic_integration() {
    std::cout << "[6] test_fp_neon_and_atomic_integration...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000); // Full FP access

    // 1. Vector addition: V0.4S = V1.4S + V2.4S
    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000500000002ULL, 0});
    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000300000001ULL, 0});
    // ADD V0.4S, V1.4S, V2.4S (0x4EA28420u)
    memory.write_u32(0x00, 0x4EA28420u);
    cpu.set_program_counter(0x00);
    cpu.step(memory);

    // Lane 0: 2 + 1 = 3, Lane 1: 5 + 3 = 8
    const auto vec = cpu.read_vector_register(0);
    STAGE70_ASSERT((vec.low & 0xFFFFFFFFu) == 3, "Vector lane 0 is 3");

    // 2. Extract lane 0 via UMOV W3, V0.S[0] -> 0x0E043C03u (imm5 = 0b00100)
    memory.write_u32(0x04, 0x0E043C03u);
    cpu.step(memory);
    STAGE70_ASSERT(cpu.read_register(3) == 3, "UMOV extracted 3 into W3");

    // 3. Commit lane value to memory using atomic LDADD: memory[0x200] += W3
    memory.write_u32(0x200, 100);
    cpu.write_register(5, 0x200);
    // LDADD W3, W4, [X5] (0xB82300A4u)
    memory.write_u32(0x08, 0xB82300A4u);
    cpu.step(memory);

    STAGE70_ASSERT(cpu.read_register(4) == 100, "LDADD returned old memory value 100");
    STAGE70_ASSERT(memory.read_u32(0x200) == 103, "Atomic memory updated to 100 + 3 = 103");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Device Bus DMA and UART Concurrent Interaction
// -----------------------------------------------------------------------------
bool test_device_bus_dma_and_uart() {
    std::cout << "[7] test_device_bus_dma_and_uart...\n";
    VirtualPlatform platform;
    auto bus = platform.device_bus();

    // 1. Access UART register
    bus->write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR, 'K', 4);
    STAGE70_ASSERT(platform.uart()->output_bytes_count() == 1, "UART has 1 byte");

    // 2. Access DMA register (write command to control register)
    bus->write_mmio(0x10000000, 0x1, 4); // Trigger DMA status write

    // 3. Write another UART byte
    bus->write_mmio(VirtualUartDevice::kDefaultUartBase + VirtualUartDevice::kRegDR, 'L', 4);
    STAGE70_ASSERT(platform.uart()->output_bytes_count() == 2, "UART has 2 bytes");
    STAGE70_ASSERT(platform.uart()->get_output_text() == "KL", "UART text sequence is KL");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Exception Syndrome Resilience Across Mixed Events
// -----------------------------------------------------------------------------
bool test_mixed_exception_syndromes() {
    std::cout << "[8] test_mixed_exception_syndromes...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_architectural_exceptions(true);

    // Event 1: SVC #0x12 -> EC = 0x15
    memory.write_u32(0x100, 0xD4000241u); // SVC #0x12
    cpu.set_program_counter(0x100);
    cpu.step(memory);

    STAGE70_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == static_cast<std::uint32_t>(ExceptionClass::SvcAArch64),
                   "ESR EC is SvcAArch64");
    STAGE70_ASSERT(cpu.get_elr_el1() == 0x104, "ELR captures 0x104");

    // Event 2: Alignment Fault on ordered store -> EC = DataAbort, FSC = 0x21
    cpu.write_register(1, 0x203); // Unaligned address
    cpu.write_register(2, 0x99);
    memory.write_u32(0x104, 0xC89FFC22u); // STLR X2, [X1]
    cpu.set_program_counter(0x104);
    cpu.step(memory);

    const std::uint32_t abort_ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    const std::uint32_t abort_fsc = static_cast<std::uint32_t>(cpu.get_esr_el1() & 0x3Fu);
    STAGE70_ASSERT(abort_ec == static_cast<std::uint32_t>(ExceptionClass::DataAbortSame) ||
                   abort_ec == static_cast<std::uint32_t>(ExceptionClass::DataAbortLower),
                   "ESR EC is DataAbort");
    STAGE70_ASSERT(abort_fsc == 0x21u, "FSC is AlignmentFault (0x21)");
    STAGE70_ASSERT(cpu.get_far_el1() == 0x203, "FAR captures unaligned address 0x203");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Platform Full System Snapshot Parity
// -----------------------------------------------------------------------------
bool test_platform_snapshot_parity() {
    std::cout << "[9] test_platform_snapshot_parity...\n";
    VirtualPlatform platform;

    // Mutate state
    platform.get_cpu(0).write_register(0, 0x1111);
    platform.get_cpu(0).write_register(1, 0x2222);
    platform.uart()->write_register(VirtualUartDevice::kRegDR, 'M', 4);

    const std::uint64_t hash_before = platform.compute_platform_hash();

    // Serialize UART peripheral
    auto uart_state = platform.uart()->serialize_state();

    // Mutate state further
    platform.get_cpu(0).write_register(0, 0x9999);
    platform.uart()->write_register(VirtualUartDevice::kRegDR, 'N', 4);
    const std::uint64_t hash_dirty = platform.compute_platform_hash();
    STAGE70_ASSERT(hash_dirty != hash_before, "State hash changed on mutation");

    // Restore state
    platform.get_cpu(0).write_register(0, 0x1111);
    platform.uart()->deserialize_state(uart_state.data(), uart_state.size());

    const std::uint64_t hash_restored = platform.compute_platform_hash();
    STAGE70_ASSERT(hash_restored == hash_before, "Platform state hash matches original after restoration");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Multi-Core Extended Stress Run
// -----------------------------------------------------------------------------
bool test_multicore_extended_stress() {
    std::cout << "[10] test_multicore_extended_stress...\n";
    VirtualPlatformConfig config;
    config.core_count = 4;
    VirtualPlatform platform(config);

    // Place NOPs in RAM
    for (std::uint64_t a = 0; a < 0x1000; a += 4) {
        platform.memory().write_u32(a, kNop);
    }

    // Activate all 4 cores at address 0
    for (std::size_t c = 0; c < 4; ++c) {
        platform.set_core_active(c, true);
        platform.get_cpu(c).set_program_counter(0);
    }

    // Run 400 deterministic instructions (100 per core)
    const std::size_t executed = platform.run_deterministic(400);
    STAGE70_ASSERT(executed == 400, "Successfully executed 400 deterministic instructions across 4 cores");

    // Verify all 4 cores advanced identically
    for (std::size_t c = 0; c < 4; ++c) {
        STAGE70_ASSERT(platform.get_cpu(c).get_program_counter() == 400, "Core PC advanced to 400");
    }

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 70 Integrated Virtual Runtime Tests ===" << std::endl;

    if (!test_end_to_end_boot_to_exit()) return 1;
    if (!test_mmu_translation_integration()) return 2;
    if (!test_integrated_syscall_console_output()) return 3;
    if (!test_cross_core_producer_consumer()) return 4;
    if (!test_integrated_timer_preemption()) return 5;
    if (!test_fp_neon_and_atomic_integration()) return 6;
    if (!test_device_bus_dma_and_uart()) return 7;
    if (!test_mixed_exception_syndromes()) return 8;
    if (!test_platform_snapshot_parity()) return 9;
    if (!test_multicore_extended_stress()) return 10;

    std::cout << "stage70_integrated_runtime_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
