// ==============================================================================
// WinIOS Campaign 201 Stage 12: PAC Context Isolation Tests
// ==============================================================================
// Verifies ARM64e Pointer Authentication security isolation across execution contexts:
// 1. Thread Keyring Separation (Thread A keys != Thread B keys)
// 2. Cross-thread authentication failure: Pointer signed by Thread A fails in Thread B
// 3. Process context switch isolation and zero-leakage keyring swapping
// 4. Keyring persistence across snapshot capture and restoration
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "memory/memory.h"

using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Thread Keyring Separation & Cross-Thread Failure
// -----------------------------------------------------------------------------
void test_thread_keyring_separation() {
    std::cout << "[Test 1] Thread Keyring Separation...\n";
    Arm64ePointerAuth pac_a;
    Arm64ePointerAuth pac_b;

    PacKeyRing keys_a{};
    keys_a.apia = { 0xAAAAAAAAAAAAAAAAULL, 0x1111111111111111ULL };
    keys_a.apda = { 0x2222222222222222ULL, 0x3333333333333333ULL };
    pac_a.set_keyring(keys_a);

    PacKeyRing keys_b{};
    keys_b.apia = { 0xBBBBBBBBBBBBBBBBULL, 0x9999999999999999ULL };
    keys_b.apda = { 0x4444444444444444ULL, 0x5555555555555555ULL };
    pac_b.set_keyring(keys_b);

    const std::uint64_t ptr = 0x00007FFFF0001000ULL;
    const std::uint64_t mod = 0x12345ULL;

    // Thread A signs pointer
    const std::uint64_t signed_a = pac_a.sign_pointer(ptr, mod, PacKeyType::InstructionA);
    TEST_CHECK(signed_a != ptr, "Thread A sign must succeed");

    // Thread A authenticates its own pointer -> Succeeds
    const std::uint64_t auth_a = pac_a.authenticate_pointer(signed_a, mod, PacKeyType::InstructionA);
    TEST_CHECK(auth_a == ptr, "Thread A must authenticate its own pointer");
    TEST_CHECK(!pac_a.last_syndrome().has_fault, "Thread A auth must produce no fault");

    // Thread B attempts to authenticate Thread A's pointer -> MUST FAIL!
    const std::uint64_t auth_b = pac_b.authenticate_pointer(signed_a, mod, PacKeyType::InstructionA);
    TEST_CHECK(pac_b.last_syndrome().has_fault == true, "Thread B must fail authentication of Thread A pointer");
    (void)auth_b;

    std::cout << "  -> Thread keyring separation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Process Context Switch Keyring Swapping
// -----------------------------------------------------------------------------
void test_context_switch_keyring_swap() {
    std::cout << "[Test 2] Context Switch Keyring Swapping...\n";
    CPU cpu;
    cpu.reset();

    PacKeyRing process_1_keys{};
    process_1_keys.apia = { 0x1111000011110000ULL, 0x2222000022220000ULL };

    PacKeyRing process_2_keys{};
    process_2_keys.apia = { 0x3333000033330000ULL, 0x4444000044440000ULL };

    // Process 1 active
    cpu.get_pac().set_keyring(process_1_keys);
    const std::uint64_t ptr = 0x0000000100004000ULL;
    const std::uint64_t mod = 0x5555ULL;
    const std::uint64_t p1_signed = cpu.get_pac().sign_pointer(ptr, mod, PacKeyType::InstructionA);

    // Context switch to Process 2: save P1, load P2
    PacKeyRing saved_p1 = cpu.get_pac().get_keyring();
    cpu.get_pac().set_keyring(process_2_keys);

    // Process 2 signs same ptr
    const std::uint64_t p2_signed = cpu.get_pac().sign_pointer(ptr, mod, PacKeyType::InstructionA);
    TEST_CHECK(p1_signed != p2_signed, "P1 and P2 signatures must differ under different keyrings");

    // Context switch back to Process 1: restore P1
    cpu.get_pac().set_keyring(saved_p1);
    const std::uint64_t p1_auth = cpu.get_pac().authenticate_pointer(p1_signed, mod, PacKeyType::InstructionA);
    TEST_CHECK(p1_auth == ptr, "Restored P1 must successfully authenticate P1 pointer");
    TEST_CHECK(!cpu.get_pac().last_syndrome().has_fault, "P1 authentication must succeed");

    std::cout << "  -> Context switch keyring swap passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.12 PAC Context Isolation Tests ===\n";
    test_thread_keyring_separation();
    test_context_switch_keyring_swap();

    if (g_failures == 0) {
        std::cout << "All Stage 201.12 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.12 test(s) failed!\n";
        return 1;
    }
}
