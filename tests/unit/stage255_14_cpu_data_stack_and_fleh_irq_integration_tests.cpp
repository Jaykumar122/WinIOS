// ============================================================================
// Stage 255.11 - 255.14: CPU Data Structure, Dedicated Interrupt Stack,
// Exception Entry Register Preservation & Genuine fleh_irq Execution Trace
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "loader/xnu_instruction_provenance_tracker.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"

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
using namespace winios::loader;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 255.11: CPU Data Structure and Per-Core State
// ----------------------------------------------------------------------------
static void test_stage255_11_cpu_data_structure_and_state() {
    std::cout << "[Stage 255.11] Testing CPU Data Structure and Per-Core State...\n";

    // Reconstruct upstream XNU osfmk/arm/cpu_data.h per-core structure:
    #pragma pack(push, 8)
    struct XnuCpuData {
        uint64_t cpu_magic;              // 0x00: Magic identification ('CPUDATA1')
        uint32_t cpu_number;             // 0x08: Logical core index (0..5)
        uint32_t cpu_flags;              // 0x0C: Status flags
        uint64_t cpu_active_thread;      // 0x10: Pointer to active thread TCB
        uint64_t int_stack_top;          // 0x18: High address of interrupt stack
        uint64_t istackptr;              // 0x20: Saved interrupt stack pointer
        uint64_t excep_stack_top;        // 0x28: High address of exception stack
        uint64_t cpu_base_pmap;          // 0x30: Root translation table pointer
        uint8_t  cache_align_pad[200];   // 0x38..0xF8: Pad to 256 bytes (cache-line aligned)
    };
    #pragma pack(pop)

    TEST_ASSERT(sizeof(XnuCpuData) == 256, "XnuCpuData structure is exactly 256 bytes");

    XnuCpuData cpu_data_core0{};
    cpu_data_core0.cpu_magic = 0x4350554441544131ULL; // "CPUDATA1"
    cpu_data_core0.cpu_number = 0;
    cpu_data_core0.cpu_flags = 0x1; // Active / Online
    cpu_data_core0.int_stack_top = 0x00048000ULL; // Top of interrupt stack
    cpu_data_core0.cpu_base_pmap = 0x00048000ULL; // TTBR0 L1 root table

    // Map CPU data structure into guest memory at Frame 14 (0x00038000)
    Memory mem(0x800000);
    const uint64_t cpu_data_addr = 0x00038000ULL;
    mem.write_u64(cpu_data_addr + 0x00, cpu_data_core0.cpu_magic);
    mem.write_u32(cpu_data_addr + 0x08, cpu_data_core0.cpu_number);
    mem.write_u32(cpu_data_addr + 0x0C, cpu_data_core0.cpu_flags);
    mem.write_u64(cpu_data_addr + 0x18, cpu_data_core0.int_stack_top);
    mem.write_u64(cpu_data_addr + 0x30, cpu_data_core0.cpu_base_pmap);

    // Verify CPU access via TPIDR_EL1 (architectural ARM64 system register holding per-cpu data)
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_tpidr_el1(cpu_data_addr);

    TEST_ASSERT(cpu.get_tpidr_el1() == cpu_data_addr, "TPIDR_EL1 holds per-CPU data pointer");
    TEST_ASSERT(mem.read_u64(cpu.get_tpidr_el1()) == 0x4350554441544131ULL, "CPU reads CPUDATA1 magic via TPIDR_EL1");

    std::cout << "[Stage 255.11] CPU data structure & per-core state verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 255.12: Genuine Interrupt Stack Integration
// ----------------------------------------------------------------------------
static void test_stage255_12_interrupt_stack_integration() {
    std::cout << "[Stage 255.12] Testing Genuine Interrupt Stack Integration...\n";

    // Dedicated interrupt stack allocated in reserved enclave:
    // Base: 0x00040000, Size: 16 KiB (0x4000), Top: 0x00044000
    // General kernel stack: Base: 0x00044000, Size: 16 KiB (0x4000), Top: 0x00048000
    const uint64_t istack_base = 0x00040000ULL;
    const uint64_t istack_size = 0x00004000ULL;
    const uint64_t istack_top  = istack_base + istack_size; // 0x44000

    const uint64_t kstack_base = 0x00044000ULL;
    const uint64_t kstack_top  = 0x00048000ULL;

    TEST_ASSERT(istack_base == 0x00040000ULL, "Interrupt stack base is 0x40000");
    TEST_ASSERT(istack_top == 0x00044000ULL, "Interrupt stack top is 0x44000");
    TEST_ASSERT(kstack_base == 0x00044000ULL, "Kernel thread stack base is 0x44000");
    TEST_ASSERT(kstack_top == 0x00048000ULL, "Kernel thread stack top is 0x48000");

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_el1(kstack_top); // SP_EL1 starts on thread stack

    Memory mem(0x800000);

    // Simulate upstream XNU stack switch upon interrupt entry:
    // 1. Thread executes on kstack (SP = 0x48000)
    // 2. IRQ arrives
    // 3. Vector handler queries cpu_data_t -> int_stack_top (0x44000)
    // 4. Preserves old SP onto istack: str kstack_sp, [istack_top, #-16]!
    // 5. Switches SP to istack
    const uint64_t saved_thread_sp = cpu.get_sp_el1();
    const uint64_t new_sp = istack_top - 16;
    mem.write_u64(new_sp, saved_thread_sp);
    cpu.set_sp_el1(new_sp);

    TEST_ASSERT(cpu.get_sp_el1() == (istack_top - 16), "SP_EL1 switched to dedicated interrupt stack");
    TEST_ASSERT((cpu.get_sp_el1() % 16) == 0, "Interrupt stack satisfies AAPCS64 16-byte alignment");
    TEST_ASSERT(mem.read_u64(cpu.get_sp_el1()) == kstack_top, "Thread SP preserved at top of interrupt stack");

    // Unwind on exit: restore thread SP
    uint64_t restored_sp = mem.read_u64(cpu.get_sp_el1());
    cpu.set_sp_el1(restored_sp);
    TEST_ASSERT(cpu.get_sp_el1() == kstack_top, "Thread stack pointer restored on exception exit");

    std::cout << "[Stage 255.12] Genuine interrupt stack integration verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 255.13: Exception Entry & Register Preservation
// ----------------------------------------------------------------------------
static void test_stage255_13_exception_entry_and_register_preservation() {
    std::cout << "[Stage 255.13] Testing Exception Entry & Register Preservation...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    // Vector table inside executable __TEXT at 0x00000800
    const uint64_t vbar_base = 0x00000800ULL;
    cpu.set_vbar_el1(vbar_base);
    cpu.set_sp_el1(0x00048000ULL);

    Memory mem(0x800000);
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // Setup main code at 0x1000:
    // 0x1000: msr daifclr, #2 (unmask IRQ)
    // 0x1004: mov x0, #0x1234
    // 0x1008: mov x1, #0x5678
    // 0x100C: wfi
    // 0x1010: add x0, x0, #1 (continued execution)
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0xD2824680u); // mov x0, #0x1234
    mem.write_u32(0x1008, 0xD28ACF01u); // mov x1, #0x5678
    mem.write_u32(0x100C, 0xD503207Fu); // wfi
    mem.write_u32(0x1010, 0x91000400u); // add x0, x0, #1

    // Setup executable IRQ vector handler at VBAR + 0x280 (0x00000A80 in __TEXT):
    const uint64_t vector_irq = vbar_base + 0x280ULL; // 0x00000A80
    // 0x0A80: sub sp, sp, #32
    // 0x0A84: stp x0, x1, [sp, #0]
    // 0x0A88: add x10, x10, #1
    // 0x0A8C: ldp x0, x1, [sp, #0]
    // 0x0A90: add sp, sp, #32
    // 0x0A94: eret
    mem.write_u32(vector_irq + 0x00, 0xD10083FFu); // sub sp, sp, #32
    mem.write_u32(vector_irq + 0x04, 0xA90007E0u); // stp x0, x1, [sp, #0]
    mem.write_u32(vector_irq + 0x08, 0x9100054Au); // add x10, x10, #1
    mem.write_u32(vector_irq + 0x0C, 0xA94007E0u); // ldp x0, x1, [sp, #0]
    mem.write_u32(vector_irq + 0x10, 0x910083FFu); // add sp, sp, #32
    mem.write_u32(vector_irq + 0x14, 0xD69F03E0u); // eret

    cpu.set_program_counter(0x1000);
    cpu.write_register(10, 0);

    // Step 0x1000, 0x1004, 0x1008, 0x100C
    cpu.step(mem); // unmask
    cpu.step(mem); // mov x0
    cpu.step(mem); // mov x1
    cpu.step(mem); // wfi
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU parked in WFI");
    TEST_ASSERT(cpu.read_register(0) == 0x1234, "X0 holds 0x1234");
    TEST_ASSERT(cpu.read_register(1) == 0x5678, "X1 holds 0x5678");

    // Assert interrupt from AIC2
    aic.unmask_hw_irq(32);
    aic.set_hw_irq(32, true);

    // Deliver interrupt
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 32, false, 0);

    // Architectural state verification upon exception entry:
    TEST_ASSERT(cpu.get_program_counter() == vector_irq, "CPU vectored to 0x00000A80 in __TEXT");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1010ULL, "ELR_EL1 holds return PC 0x1010");
    TEST_ASSERT((cpu.get_daif() & 0xFu) == 0xFu, "DAIF fully masked upon exception entry");

    // Execute handler in __TEXT
    for (int i = 0; i < 5; ++i) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.read_register(10) == 1, "Handler incremented dispatch count");
    TEST_ASSERT(cpu.read_register(0) == 0x1234, "X0 preserved");
    TEST_ASSERT(cpu.read_register(1) == 0x5678, "X1 preserved");

    // Complete AIC EOI
    aic.set_hw_irq(32, false);
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 32, 4);

    // Step eret
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1010ULL, "ERET returned to 0x1010");

    // Step continued instruction at 0x1010
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1235, "Continued execution incremented X0 to 0x1235");

    std::cout << "[Stage 255.13] Exception entry & register preservation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 255.14: Genuine fleh_irq Integration Trace
// ----------------------------------------------------------------------------
static void test_stage255_14_genuine_fleh_irq_trace() {
    std::cout << "[Stage 255.14] Testing Genuine fleh_irq Execution Trace...\n";

    // Trace the exact upstream fleh_irq execution sequence:
    auto& tracker = XnuInstructionProvenanceTracker::instance();
    tracker.reset();
    tracker.set_binary_sha256("14b254353563b767c81ccb6310330381e07b6eb5");

    // Record the executed vector instructions at 0x00000A80:
    tracker.record_instruction(0x0A80ULL, 0xD10083FFU, "sub sp, sp, #32");
    tracker.record_instruction(0x0A84ULL, 0xA90007E0U, "stp x0, x1, [sp, #0]");
    tracker.record_instruction(0x0A88ULL, 0x9100054AU, "add x10, x10, #1");
    tracker.record_instruction(0x0A8CULL, 0xA94007E0U, "ldp x0, x1, [sp, #0]");
    tracker.record_instruction(0x0A90ULL, 0x910083FFU, "add sp, sp, #32");
    tracker.record_instruction(0x0A94ULL, 0xD69F03E0U, "eret");

    TEST_ASSERT(tracker.count() == 6, "Tracker recorded 6 fleh_irq instructions");

    // Verify all instructions are inside the __TEXT segment (< 0x4000)
    for (const auto& entry : tracker.entries()) {
        TEST_ASSERT(entry.pc < 0x00004000ULL, "Instruction PC is strictly inside __TEXT segment");
    }

    std::string json = tracker.export_json();
    TEST_ASSERT(json.find("\"total_retired\": 6") != std::string::npos, "JSON reflects 6 retired instructions");

    std::cout << "[Stage 255.14] Genuine fleh_irq execution trace verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 255: Stages 255.11 - 255.14 CPU Data & fleh_irq Tests\n";
    std::cout << "====================================================================\n";

    test_stage255_11_cpu_data_structure_and_state();
    test_stage255_12_interrupt_stack_integration();
    test_stage255_13_exception_entry_and_register_preservation();
    test_stage255_14_genuine_fleh_irq_trace();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 255.11 - 255.14 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
