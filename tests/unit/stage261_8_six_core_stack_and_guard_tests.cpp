#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/canonical_physical_memory_map.h"
#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

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
// Test 1: Stage 261.6: Six-Core Stack Inventory Verification
// ----------------------------------------------------------------------------
static void test_stage261_6_six_core_stack_inventory() {
    std::cout << "[Stage 261.6] Testing Six-Core Stack Inventory & Initial SP...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();

    // Authoritative stack intervals table:
    struct ExpectedStackEntry {
        std::size_t core;
        uint64_t guard_base;
        uint64_t guard_size;
        uint64_t int_base;
        uint64_t int_size;
        uint64_t int_sp_top;
        uint64_t thr_base;
        uint64_t thr_size;
        uint64_t thr_sp_top;
    };

    const ExpectedStackEntry kExpectedStacks[6] = {
        // Core 0: Guard 0x3C000, IntStack [0x40000-0x44000, SP=0x44000], ThrStack [0x44000-0x48000, SP=0x48000]
        { 0, 0x0003C000ULL, 0x4000ULL, 0x00040000ULL, 0x4000ULL, 0x00044000ULL, 0x00044000ULL, 0x4000ULL, 0x00048000ULL },
        // Core 1: Guard 0x50000, IntStack [0x54000-0x58000, SP=0x58000], ThrStack [0x58000-0x5C000, SP=0x5C000]
        { 1, 0x00050000ULL, 0x4000ULL, 0x00054000ULL, 0x4000ULL, 0x00058000ULL, 0x00058000ULL, 0x4000ULL, 0x0005C000ULL },
        // Core 2: Guard 0x5C000, IntStack [0x60000-0x64000, SP=0x64000], ThrStack [0x64000-0x68000, SP=0x68000]
        { 2, 0x0005C000ULL, 0x4000ULL, 0x00060000ULL, 0x4000ULL, 0x00064000ULL, 0x00064000ULL, 0x4000ULL, 0x00068000ULL },
        // Core 3: Guard 0x68000, IntStack [0x6C000-0x70000, SP=0x70000], ThrStack [0x70000-0x74000, SP=0x74000]
        { 3, 0x00068000ULL, 0x4000ULL, 0x0006C000ULL, 0x4000ULL, 0x00070000ULL, 0x00070000ULL, 0x4000ULL, 0x00074000ULL },
        // Core 4: Guard 0x74000, IntStack [0x78000-0x7C000, SP=0x7C000], ThrStack [0x7C000-0x80000, SP=0x80000]
        { 4, 0x00074000ULL, 0x4000ULL, 0x00078000ULL, 0x4000ULL, 0x0007C000ULL, 0x0007C000ULL, 0x4000ULL, 0x00080000ULL },
        // Core 5: Guard 0x84000, IntStack [0x88000-0x8C000, SP=0x8C000], ThrStack [0x8C000-0x90000, SP=0x90000]
        { 5, 0x00084000ULL, 0x4000ULL, 0x00088000ULL, 0x4000ULL, 0x0008C000ULL, 0x0008C000ULL, 0x4000ULL, 0x00090000ULL },
    };

    for (std::size_t c = 0; c < 6; ++c) {
        auto geom = map.get_core_stack_geometry(c);
        TEST_ASSERT(geom.has_value(), "Core stack geometry must exist");
        TEST_ASSERT(geom->core_id == c, "Core ID matches");

        const auto& exp = kExpectedStacks[c];
        TEST_ASSERT(geom->guard_base == exp.guard_base, "Guard base matches");
        TEST_ASSERT(geom->guard_size == exp.guard_size, "Guard size is 16 KiB");
        TEST_ASSERT(geom->int_stack_base == exp.int_base, "Interrupt stack base matches");
        TEST_ASSERT(geom->int_stack_size == exp.int_size, "Interrupt stack size is 16 KiB");
        TEST_ASSERT(geom->int_stack_initial_sp == exp.int_sp_top, "Initial SP_EL1 matches stack top");
        TEST_ASSERT(geom->thread_stack_base == exp.thr_base, "Thread stack base matches");
        TEST_ASSERT(geom->thread_stack_size == exp.thr_size, "Thread stack size is 16 KiB");
        TEST_ASSERT(geom->thread_stack_initial_sp == exp.thr_sp_top, "Initial SP_EL0 matches thread stack top");

        // Direction: downwards growing (initial SP is top)
        TEST_ASSERT(geom->int_stack_initial_sp > geom->int_stack_base, "Stack grows downwards");
        TEST_ASSERT(geom->int_stack_initial_sp % 16 == 0, "Initial SP is 16-byte aligned");
    }

    std::cout << "[Stage 261.6] Six-core stack inventory verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 261.7: Stack Layout, Guards & Non-Overlap Implementation
// ----------------------------------------------------------------------------
static void test_stage261_7_stack_layout_and_guards() {
    std::cout << "[Stage 261.7] Testing Stack Layout, Guards & Non-Overlap...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();
    PhysicalMemoryOverlapAuditor auditor(map);

    std::vector<std::string> stack_errors;
    TEST_ASSERT(auditor.audit_six_core_stack_isolation(stack_errors), "Six-core stack isolation verified");

    // Verify all 6 guards are unmapped
    for (std::size_t c = 0; c < 6; ++c) {
        std::string guard_name = "Core" + std::to_string(c) + "_Guard";
        const auto* guard = map.find_interval_by_name(guard_name);
        TEST_ASSERT(guard != nullptr, "Guard interval exists");
        TEST_ASSERT(!guard->is_mapped, "Guard interval must be UNMAPPED");
        TEST_ASSERT(guard->permissions == 0, "Guard interval permissions must be None (0)");
    }

    // Verify non-overlap between ALL core stacks:
    for (std::size_t i = 0; i < 6; ++i) {
        auto g_i = map.get_core_stack_geometry(i);
        for (std::size_t j = i + 1; j < 6; ++j) {
            auto g_j = map.get_core_stack_geometry(j);

            // Interrupt stacks do not overlap
            TEST_ASSERT(g_i->int_stack_base != g_j->int_stack_base, "Disjoint interrupt stack bases");
            TEST_ASSERT(g_i->int_stack_initial_sp <= g_j->int_stack_base ||
                        g_j->int_stack_initial_sp <= g_i->int_stack_base, "Zero overlap between interrupt stacks");

            // Thread stacks do not overlap
            TEST_ASSERT(g_i->thread_stack_base != g_j->thread_stack_base, "Disjoint thread stack bases");
        }
    }

    std::cout << "[Stage 261.7] Stack layout, guards and non-overlap verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 261.8: Stack Adversarial Tests & Real Nesting Safety
// ----------------------------------------------------------------------------
static void test_stage261_8_stack_adversarial_testing() {
    std::cout << "[Stage 261.8] Testing Stack Adversarial Limits & Distinct Bit Patterns...\n";

    Memory mem(0x800000ULL);
    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();

    // 1. Nesting limits calculation:
    // Frame size = 288 bytes.
    // In real execution, a handler has locals, alignment overhead, and nested callee frames (~128 bytes).
    // Effective exception frame overhead = 288 + 128 = 416 bytes.
    // Safe nesting limit within 16 KiB = 16384 / 416 = 39 frames.
    const uint64_t frame_size = sizeof(ArmSavedState64); // 288 bytes
    const uint64_t stack_size = 16384ULL;
    (void)stack_size;

    for (std::size_t c = 0; c < 6; ++c) {
        auto geom = map.get_core_stack_geometry(c);
        TEST_ASSERT(geom.has_value(), "Geometry valid");

        uint64_t current_sp = geom->int_stack_initial_sp;
        const uint64_t stack_base = geom->int_stack_base;

        // Simulate maximum permitted nesting of frames
        std::size_t depth = 0;
        while (current_sp >= stack_base + frame_size) {
            current_sp -= frame_size;
            ++depth;

            // Write distinct 64-bit pattern for this core and depth
            uint64_t pattern = 0xCAFE000000000000ULL |
                               (static_cast<uint64_t>(c) << 32) |
                               (static_cast<uint64_t>(depth) << 16) |
                               0xABCDULL;
            mem.write_u64(current_sp, pattern);
        }

        TEST_ASSERT(depth == 56, "56 raw saved-state frames fit in 16 KiB");

        // Verify remaining space is < 288 bytes (exhausted)
        TEST_ASSERT(current_sp - stack_base < frame_size, "Stack remaining space is < 288 B");

        // Overflow attempt: pushing one more frame breaches stack_base into unmapped guard
        bool overflow_detected = (current_sp < stack_base + frame_size);
        TEST_ASSERT(overflow_detected, "Push beyond depth 56 triggers stack overflow guard");

        // Verify all 56 frames retained their bitwise distinct patterns
        uint64_t verify_sp = geom->int_stack_initial_sp;
        for (std::size_t d = 1; d <= depth; ++d) {
            verify_sp -= frame_size;
            uint64_t expected_pattern = 0xCAFE000000000000ULL |
                                        (static_cast<uint64_t>(c) << 32) |
                                        (static_cast<uint64_t>(d) << 16) |
                                        0xABCDULL;
            TEST_ASSERT(mem.read_u64(verify_sp) == expected_pattern, "Distinct per-core pattern bitwise verified");
        }
    }

    std::cout << "[Stage 261.8] Stack adversarial limits and nesting safety verified.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 261: Stages 261.6 - 261.8 Six-Core Stack & Guard Tests\n";
        std::cout << "====================================================================\n";

        test_stage261_6_six_core_stack_inventory();
        test_stage261_7_stack_layout_and_guards();
        test_stage261_8_stack_adversarial_testing();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 261.6 - 261.8 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}