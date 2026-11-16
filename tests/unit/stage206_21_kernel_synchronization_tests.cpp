#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_arm64_exclusive_monitor_and_atomics() {
    std::cout << "[Test 1] ARM64 Exclusive Monitor & Spinlock Operations...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x10000); // 64 KB

    constexpr std::uint64_t kLockAddr = 0x2000ULL;
    memory.write_u64(kLockAddr, 0ULL); // Lock initially free (0)

    cpu.set_program_counter(0x1000ULL);
    cpu.set_stack_pointer(0x8000ULL);
    cpu.set_current_el(1);
    cpu.write_register(0, kLockAddr); // X0 = lock address
    cpu.write_register(3, 1ULL);      // X3 = value 1 (locked)

    // Write spinlock acquisition snippet:
    // Insn 0 (0x1000): LDXR X1, [X0]     (0xC85F7C01) -> Loads current lock val into X1, sets exclusive monitor
    // Insn 1 (0x1004): STXR W2, X3, [X0] (0xC8027C03) -> Stores X3 (1) into [X0], sets W2=0 on success
    // Insn 2 (0x1008): DMB ISH           (0xD5033B5F -> 0xD5033BBF)
    // Insn 3 (0x100C): ISB               (0xD5033FDF)
    memory.write_u32(0x1000, 0xC85F7C01U);
    memory.write_u32(0x1004, 0xC8027C03U);
    memory.write_u32(0x1008, 0xD5033BBFU);
    memory.write_u32(0x100C, 0xD5033FDFU);

    // Step 0: LDXR
    cpu.step(memory);
    TEST_CHECK(cpu.read_register(1) == 0ULL, "LDXR loaded 0 (free lock)");

    // Step 1: STXR
    cpu.step(memory);
    TEST_CHECK(cpu.read_register(2) == 0ULL, "STXR succeeded (W2 == 0)");
    TEST_CHECK(memory.read_u64(kLockAddr) == 1ULL, "Memory now holds lock value 1");

    // Step 2: DMB
    const std::uint64_t dmb_before = cpu.get_dmb_count();
    cpu.step(memory);
    TEST_CHECK(cpu.get_dmb_count() > dmb_before, "DMB instruction executed");

    // Step 3: ISB
    const std::uint64_t isb_before = cpu.get_isb_count();
    cpu.step(memory);
    TEST_CHECK(cpu.get_isb_count() > isb_before, "ISB instruction executed");

    std::cout << "  -> Atomics and barriers verified successfully.\n";
}

void test_exclusive_monitor_loss_on_clrex() {
    std::cout << "[Test 2] Exclusive Monitor Loss on CLREX...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x10000);

    constexpr std::uint64_t kLockAddr = 0x3000ULL;
    memory.write_u64(kLockAddr, 0ULL);

    cpu.set_program_counter(0x1000ULL);
    cpu.set_current_el(1);
    cpu.write_register(0, kLockAddr);
    cpu.write_register(3, 42ULL);

    // Insn 0: LDXR X1, [X0] (0xC85F7C01)
    // Insn 1: CLREX         (0xD5033F5F) -> Clears exclusive monitor!
    // Insn 2: STXR W2, X3, [X0] (0xC8027C03) -> Must fail (W2 = 1)!
    memory.write_u32(0x1000, 0xC85F7C01U);
    memory.write_u32(0x1004, 0xD5033F5FU);
    memory.write_u32(0x1008, 0xC8027C03U);

    cpu.step(memory); // LDXR
    cpu.step(memory); // CLREX
    cpu.step(memory); // STXR

    TEST_CHECK(cpu.read_register(2) == 1ULL, "STXR failed after CLREX (W2 == 1)");
    TEST_CHECK(memory.read_u64(kLockAddr) == 0ULL, "Memory unmodified on failed store");

    std::cout << "  -> Exclusive monitor loss verified.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.21 Kernel Synchronization Tests ===\n";
    test_arm64_exclusive_monitor_and_atomics();
    test_exclusive_monitor_loss_on_clrex();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.21 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.21 Tests FAILED ===\n";
        return 1;
    }
}
