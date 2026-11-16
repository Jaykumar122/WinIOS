// ==============================================================================
// WinIOS Campaign 201 Stage 20: Bit-Exact Deterministic Replay Tests
// ==============================================================================
// Verifies bit-exact execution replay across a 6-core SMP cluster:
// 1. Initial execution run produces deterministic instruction retirement
// 2. State checkpoint restoration to t=0
// 3. Replay run produces bit-for-bit identical register states and PC values
// 4. Multi-core memory writes match identically across runs
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "snapshot/smp_snapshot_manager.h"

using namespace winios;
using namespace winios::snapshot;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

struct CoreCheckpoint {
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint64_t regs[31]{0};
};

void test_smp_bit_exact_deterministic_replay() {
    std::cout << "[Test 1] 6-Core SMP Bit-Exact Replay...\n";

    constexpr std::size_t kCores = 6;
    constexpr std::size_t kRamSize = 1024 * 1024 * 4;
    auto mem = std::make_shared<SmpSharedMemory>(kRamSize, kCores);
    SmpScheduler sched(mem, kCores, 25);

    // Setup 6 cores running independent deterministic loops with arithmetic and memory writes
    for (std::size_t c = 0; c < kCores; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        const std::uint64_t code_addr = 0x1000 + c * 0x400;
        const std::uint64_t data_addr = 0x4000 + c * 0x100;

        // Loop:
        // 0: MOVZ X0, #(data_addr)
        // 4: ADD X1, X1, #1
        // 8: STR X1, [X0]
        // 12: B .-8 (jumps to +4)
        mem->raw_memory().write_u32(code_addr + 0, 0xD2800000u | ((static_cast<std::uint32_t>(data_addr) & 0xFFFFu) << 5));
        mem->raw_memory().write_u32(code_addr + 4, 0x91000421u); // ADD X1, X1, #1
        mem->raw_memory().write_u32(code_addr + 8, 0xF9000001u); // STR X1, [X0]
        mem->raw_memory().write_u32(code_addr + 12, 0x17FFFFFEu); // B .-8 (jumps to +4)

        cpu->set_program_counter(code_addr);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Capture initial snapshot at t=0
    std::vector<std::uint8_t> t0_snapshot;
    auto cap_t0 = SmpSnapshotManager::capture_to_buffer(sched, t0_snapshot);
    TEST_CHECK(cap_t0.success, "t=0 snapshot capture must succeed");

    // Run execution Pass 1 (50,000 instructions)
    constexpr std::uint64_t kTargetSteps = 50000;
    const std::uint64_t pass1_retired = sched.run_deterministic(kTargetSteps);

    // Record Pass 1 final states
    std::vector<CoreCheckpoint> pass1_states(kCores);
    std::vector<std::uint64_t> pass1_mem(kCores);

    for (std::size_t c = 0; c < kCores; ++c) {
        const auto& cpu = *sched.get_slot(c).cpu;
        pass1_states[c].pc = cpu.get_program_counter();
        pass1_states[c].sp = cpu.get_stack_pointer();
        for (std::uint32_t r = 0; r < 31; ++r) {
            pass1_states[c].regs[r] = cpu.read_register(r);
        }
        pass1_mem[c] = mem->read_u64(0, 0x4000 + c * 0x100);
    }

    // Restore back to t=0 snapshot
    auto rest_t0 = SmpSnapshotManager::restore_from_buffer(t0_snapshot.data(), t0_snapshot.size(), sched);
    TEST_CHECK(rest_t0.success, "Restore to t=0 must succeed");

    // Run execution Pass 2 (exact same target steps)
    const std::uint64_t pass2_retired = sched.run_deterministic(kTargetSteps);
    TEST_CHECK(pass2_retired == pass1_retired, "Retired instruction count must match bit-for-bit");

    // Verify Pass 2 states match Pass 1 states 100%
    for (std::size_t c = 0; c < kCores; ++c) {
        const auto& cpu = *sched.get_slot(c).cpu;
        TEST_CHECK(cpu.get_program_counter() == pass1_states[c].pc, "Replayed PC mismatch");
        TEST_CHECK(cpu.get_stack_pointer() == pass1_states[c].sp, "Replayed SP mismatch");

        for (std::uint32_t r = 0; r < 31; ++r) {
            TEST_CHECK(cpu.read_register(r) == pass1_states[c].regs[r], "Replayed Register mismatch");
        }

        const auto mem_val = mem->read_u64(0, 0x4000 + c * 0x100);
        TEST_CHECK(mem_val == pass1_mem[c], "Replayed memory write mismatch");
    }

    std::cout << "  -> 6-core SMP bit-exact replay passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.20 Deterministic Replay Tests ===\n";
    test_smp_bit_exact_deterministic_replay();

    if (g_failures == 0) {
        std::cout << "All Stage 201.20 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.20 test(s) failed!\n";
        return 1;
    }
}
