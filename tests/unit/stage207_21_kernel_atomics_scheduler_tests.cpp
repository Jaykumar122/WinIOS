#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_exclusive_atomics_and_barriers() {
    std::cout << "[Test 1] Stage 207.21: Kernel Synchronization Primitives & Atomics...\n";

    CPU cpu;
    Memory memory(0x100000);

    const std::uint64_t lock_addr = 0x20000ULL;
    memory.write_u64(static_cast<std::size_t>(lock_addr), 0ULL); // Unlocked

    // Test 1: Exclusive Load (LDXR X0, [X1])
    // 0xC85F7C20 (LDXR X0, [X1])
    memory.write_u32(0x1000ULL, 0xC85F7C20U);
    cpu.write_register(1, lock_addr);
    cpu.set_program_counter(0x1000ULL);
    cpu.step(memory);

    TEST_CHECK(cpu.read_register(0) == 0ULL, "LDXR loaded 0 (unlocked state)");

    // Test 2: Exclusive Store (STXR W2, X3, [X1])
    // 0xC8027C23 (STXR W2, X3, [X1])
    memory.write_u32(0x1004ULL, 0xC8027C23U);
    cpu.write_register(3, 1ULL); // Lock value
    cpu.step(memory);

    TEST_CHECK(cpu.read_register(2) == 0ULL, "STXR succeeded (returned status 0)");
    TEST_CHECK(memory.read_u64(static_cast<std::size_t>(lock_addr)) == 1ULL, "Lock acquired in memory");

    // Test 3: Clear Exclusive (CLREX)
    // 0xD5033F5F (CLREX)
    memory.write_u32(0x1008ULL, 0xD5033F5FU);
    cpu.step(memory);
    TEST_CHECK(cpu.get_program_counter() == 0x100CULL, "CLREX executed cleanly");

    // Test 4: Memory Barriers (DMB ISH: 0xD5033BBF, DSB ISH: 0xD5033B9F, ISB: 0xD5033FDF)
    memory.write_u32(0x100CULL, 0xD5033BBFU);
    memory.write_u32(0x1010ULL, 0xD5033B9FU);
    memory.write_u32(0x1014ULL, 0xD5033FDFU);

    cpu.step(memory); // DMB
    TEST_CHECK(cpu.get_program_counter() == 0x1010ULL, "DMB ISH executed");
    cpu.step(memory); // DSB
    TEST_CHECK(cpu.get_program_counter() == 0x1014ULL, "DSB ISH executed");
    cpu.step(memory); // ISB
    TEST_CHECK(cpu.get_program_counter() == 0x1018ULL, "ISB executed");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.21 Kernel Synchronization Tests ===\n";
    test_exclusive_atomics_and_barriers();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.21: All kernel synchronization tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.21: " << g_failures << " tests failed.\n";
        return 1;
    }
}
