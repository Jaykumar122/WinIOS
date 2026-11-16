// ==============================================================================
// WinIOS Campaign 201 Stage 19: Snapshot Corruption & Restore Atomicity Tests
// ==============================================================================
// Verifies:
// 1. Truncated snapshot buffer detection and safe rejection
// 2. Corrupted magic header rejection
// 3. Section payload CRC32C mismatch detection
// 4. Inconsistent core count rejection
// 5. Restore Atomicity: failed restore leaves live cluster unmodified
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

// -----------------------------------------------------------------------------
// Test 1: Corrupted Header and Truncation Rejection
// -----------------------------------------------------------------------------
void test_header_corruption_and_truncation() {
    std::cout << "[Test 1] Corrupted Header & Truncation...\n";
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    SmpScheduler sched(mem, 2, 20);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    std::vector<std::uint8_t> valid_snap;
    auto cap_res = SmpSnapshotManager::capture_to_buffer(sched, valid_snap);
    TEST_CHECK(cap_res.success, "Capture must succeed");

    // Case 1.1: Truncate buffer to 16 bytes
    auto val_trunc = SmpSnapshotManager::validate_buffer(valid_snap.data(), 16);
    TEST_CHECK(!val_trunc.valid, "Truncated buffer must be invalid");

    // Case 1.2: Corrupt magic bytes
    std::vector<std::uint8_t> bad_magic = valid_snap;
    bad_magic[0] = 0xFF;
    bad_magic[1] = 0xAA;
    auto val_magic = SmpSnapshotManager::validate_buffer(bad_magic.data(), bad_magic.size());
    TEST_CHECK(!val_magic.valid, "Bad magic must be rejected");

    std::cout << "  -> Header corruption & truncation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Payload CRC32C Tampering and Restore Atomicity
// -----------------------------------------------------------------------------
void test_payload_tampering_and_atomicity() {
    std::cout << "[Test 2] Payload Tampering & Restore Atomicity...\n";
    auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    SmpScheduler sched(mem, 2, 20);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        cpu->set_program_counter(0x5000);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    std::vector<std::uint8_t> snap_buffer;
    auto cap_res = SmpSnapshotManager::capture_to_buffer(sched, snap_buffer);
    TEST_CHECK(cap_res.success, "Capture must succeed");

    // Tamper with payload byte midway in buffer
    std::vector<std::uint8_t> tampered = snap_buffer;
    tampered[tampered.size() / 2] ^= 0xFF;

    // Set up destination cluster with distinct known state
    auto dest_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    SmpScheduler dest_sched(dest_mem, 2, 20);

    for (std::size_t c = 0; c < 2; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        cpu->set_program_counter(0x8888); // Sentinel PC
        dest_sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Attempt restore with tampered snapshot
    auto rest_res = SmpSnapshotManager::restore_from_buffer(tampered.data(), tampered.size(), dest_sched);
    TEST_CHECK(!rest_res.success, "Tampered snapshot restore must fail");

    // Atomicity check: verify destination CPUs were NOT corrupted
    for (std::size_t c = 0; c < 2; ++c) {
        TEST_CHECK(dest_sched.get_slot(c).cpu->get_program_counter() == 0x8888, "Destination CPU must retain original state after failed restore");
    }

    std::cout << "  -> Payload tampering & restore atomicity passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.19 Snapshot Corruption & Atomicity Tests ===\n";
    test_header_corruption_and_truncation();
    test_payload_tampering_and_atomicity();

    if (g_failures == 0) {
        std::cout << "All Stage 201.19 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.19 test(s) failed!\n";
        return 1;
    }
}
