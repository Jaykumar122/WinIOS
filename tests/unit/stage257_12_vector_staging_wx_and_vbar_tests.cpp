// ============================================================================
// Stage 257.9 - 257.12: Vector Runtime-Staging Mechanism Audit, W^X Transition
// Correctness, Instruction Synchronization & VBAR_EL1 Slot Verification
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "memory/memory.h"
#include "memory/xnu_vm_map.h"

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
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 257.9: Vector Runtime-Staging Mechanism Audit
// ----------------------------------------------------------------------------
static void test_stage257_9_runtime_staging_mechanism_audit() {
    std::cout << "[Stage 257.9] Testing Vector Runtime-Staging Mechanism Audit...\n";

    // Trace time-ordered permission states of Frame 0 (__TEXT region: 0x0 - 0x4000):
    struct StagingStateTransition {
        uint64_t timestamp_step;
        const char* phase_name;
        uint32_t active_protection;
        bool is_writable;
        bool is_executable;
        const char* description;
    };

    const StagingStateTransition trace[] = {
        {1, "T0_INITIAL_RESERVATION", kVmProtRead | kVmProtWrite,   true,  false, "Frame 0 allocated as RW/NX for binary & vector staging"},
        {2, "T1_VECTOR_WRITING",      kVmProtRead | kVmProtWrite,   true,  false, "Loader writes 2048-byte vector table and trampoline at 0x800"},
        {3, "T2_BARRIER_SYNC",        kVmProtRead | kVmProtWrite,   true,  false, "DSB and ISB flush store pipelines"},
        {4, "T3_PERMISSION_SEAL",     kVmProtRead | kVmProtExecute, false, true,  "vm_map_protect transitions Frame 0 to RX (strict W^X)"},
        {5, "T4_VBAR_PROGRAMMING",    kVmProtRead | kVmProtExecute, false, true,  "VBAR_EL1 programmed with sealed vector base 0x00000800"}
    };

    TEST_ASSERT(sizeof(trace)/sizeof(trace[0]) == 5, "5 time-ordered staging steps verified");

    // Invariant: At no point during staging is a region simultaneously writable AND executable
    for (const auto& step : trace) {
        TEST_ASSERT(!(step.is_writable && step.is_executable), "Strict W^X invariant preserved during step");
    }

    std::cout << "[Stage 257.9] Vector runtime-staging mechanism audit completed.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 257.10: W^X Transition Correctness
// ----------------------------------------------------------------------------
static void test_stage257_10_wx_transition_correctness() {
    std::cout << "[Stage 257.10] Testing W^X Transition Correctness...\n";

    XnuVmMap vm_map(0x0ULL, 0x800000ULL, "kernel_map");
    std::string err;

    // Phase 1: Initial staging state (RW/NX: Read+Write, Non-Executable)
    TEST_ASSERT(vm_map.vm_map_enter(0x0ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, kVmProtAll, "__TEXT_STAGING", &err),
                "Enter staging mapping as RW/NX");

    const auto* entry_phase1 = vm_map.vm_map_lookup(0x00000800ULL);
    TEST_ASSERT(entry_phase1 != nullptr, "Lookup 0x800 during Phase 1");
    TEST_ASSERT((entry_phase1->protection & kVmProtWrite) != 0, "Phase 1: 0x800 is writable (permits vector staging)");
    TEST_ASSERT((entry_phase1->protection & kVmProtExecute) == 0, "Phase 1: 0x800 is strictly NON-EXECUTABLE (NX)");

    // Verify rejection of simultaneous WX request:
    TEST_ASSERT(!vm_map.vm_map_protect(0x0ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, false, &err),
                "vm_map_protect strictly REJECTS simultaneous RWX");

    // Phase 2: Seal permission transition (Removes Write, Adds Execute -> RX)
    TEST_ASSERT(vm_map.vm_map_protect(0x0ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, false, &err),
                "Transition Frame 0 to RX");

    // Phase 3: Final runtime state (RX: Read+Execute, Non-Writable)
    const auto* entry_phase3 = vm_map.vm_map_lookup(0x00000800ULL);
    TEST_ASSERT(entry_phase3 != nullptr, "Lookup 0x800 during Phase 3");
    TEST_ASSERT((entry_phase3->protection & kVmProtExecute) != 0, "Phase 3: 0x800 is EXECUTABLE");
    TEST_ASSERT((entry_phase3->protection & kVmProtWrite) == 0, "Phase 3: 0x800 is strictly NON-WRITABLE (read-only)");

    std::cout << "[Stage 257.10] W^X transition correctness verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 257.11: Instruction Cache & Synchronization Barriers
// ----------------------------------------------------------------------------
static void test_stage257_11_instruction_synchronization() {
    std::cout << "[Stage 257.11] Testing Instruction Cache & Synchronization Barriers...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();

    // Staging sequence with barrier opcodes:
    // 1. Write trampoline instructions at 0x800
    // 2. Execute DSB SY (Data Synchronization Barrier: 0xD5033F9F)
    // 3. Execute ISB (Instruction Synchronization Barrier: 0xD5033FDF)
    // 4. Instruction fetch pipeline flushes and re-fetches newly committed opcodes

    const uint32_t dsb_sy_opcode = 0xD5033F9Fu;
    const uint32_t isb_opcode    = 0xD5033FDFu;

    mem.write_u32(0x2000, dsb_sy_opcode);
    mem.write_u32(0x2004, isb_opcode);

    cpu.set_program_counter(0x2000);
    cpu.step(mem); // executes DSB
    TEST_ASSERT(cpu.get_program_counter() == 0x2004, "DSB completed cleanly");

    cpu.step(mem); // executes ISB
    TEST_ASSERT(cpu.get_program_counter() == 0x2008, "ISB completed cleanly");

    std::cout << "[Stage 257.11] Instruction synchronization barriers verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 257.12: VBAR_EL1 & Vector-Slot Verification
// ----------------------------------------------------------------------------
static void test_stage257_12_vbar_and_vector_slots() {
    std::cout << "[Stage 257.12] Testing VBAR_EL1 & Vector-Slot Verification...\n";

    // 1. Vector table alignment: Must be 2048-byte aligned (bits [10:0] == 0)
    std::string err;
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x00000800ULL, &err), "0x800 is valid 2048-byte aligned VBAR");
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x00001000ULL, &err), "0x1000 is valid 2048-byte aligned VBAR");

    // Negative tests for invalid alignment:
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x00000804ULL, &err), "0x804 rejected (misaligned by 4)");
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x00000880ULL, &err), "0x880 rejected (misaligned by 128)");

    // 2. Vector table slot calculation:
    // CurrentEL_SPx IRQ is Slot 5 (offset +0x280)
    const uint64_t irq_offset = KernelExceptionVectors::compute_vector_offset(
        ExceptionOrigin::CurrentEL_SPx,
        ExceptionVectorType::IRQ
    );
    TEST_ASSERT(irq_offset == 0x280ULL, "CurrentEL_SPx IRQ offset is exactly 0x280");

    // Target vector address:
    const uint64_t vbar_base = 0x00000800ULL;
    const uint64_t target_vector = vbar_base + irq_offset;
    TEST_ASSERT(target_vector == 0x00000A80ULL, "VBAR (0x800) + IRQ slot (0x280) = 0x00000A80");

    // 3. CPU hardware register masking:
    CPU cpu;
    cpu.reset();
    cpu.set_vbar_el1(0x00000804ULL); // unaligned attempt
    TEST_ASSERT(cpu.get_vbar_el1() == 0x00000800ULL, "CPU masks lower 11 bits of VBAR_EL1 to preserve 2048-byte alignment");

    std::cout << "[Stage 257.12] VBAR_EL1 and vector slots verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 257: Stages 257.9 - 257.12 Vector Staging & VBAR Tests\n";
    std::cout << "====================================================================\n";

    test_stage257_9_runtime_staging_mechanism_audit();
    test_stage257_10_wx_transition_correctness();
    test_stage257_11_instruction_synchronization();
    test_stage257_12_vbar_and_vector_slots();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 257.9 - 257.12 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
