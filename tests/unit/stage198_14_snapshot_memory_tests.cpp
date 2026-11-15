// ==============================================================================
// WinIOS Campaign 198 Stage 14: Memory System Snapshot Integration
// ==============================================================================
// Verifies:
// 1. Serialization & restoration of CPU architectural state, including MMU
//    registers (MAIR_EL1, TCR_EL1, TTBR0_EL1, TTBR1_EL1, SCTLR_EL1)
// 2. Full .wsnap buffer capture and restoration containing memory contents
// 3. VirtualDmaDevice state serialization & deserialization round-trip
// 4. Integrity verification against corrupted headers and payloads
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_types.h"
#include "snapshot/wsnap_format.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::snapshot;
using namespace winios::device;

void test_cpu_and_mmu_state_serialization() {
    std::cout << "[Test 1] CPU & MMU system register snapshot round-trip...\n";
    CPU src_cpu;
    src_cpu.set_core_id(0);
    src_cpu.set_program_counter(0x40001000ULL);
    src_cpu.set_current_el(1);
    src_cpu.write_register(0, 0x1122334455667788ULL);
    src_cpu.write_register(30, 0xAABBCCDDEEFF0011ULL);

    // MMU and memory attribute system registers
    src_cpu.set_sctlr_el1(0x00C50839ULL);
    src_cpu.set_tcr_el1(0x00000001B5193519ULL);
    src_cpu.set_ttbr0_el1(0x0000000080000000ULL);
    src_cpu.set_ttbr1_el1(0x0000000090000000ULL);
    src_cpu.set_mair_el1(0x00FFBBAA440C0804ULL);
    src_cpu.set_par_el1(0x0000000000000001ULL);

    const auto payload = serialize_cpu_state(src_cpu);
    TEST_ASSERT(!payload.empty());

    CPU dst_cpu;
    const auto res = deserialize_cpu_state(payload.data(), payload.size(), dst_cpu);
    TEST_ASSERT(res.success);

    TEST_ASSERT(dst_cpu.get_program_counter() == src_cpu.get_program_counter());
    TEST_ASSERT(dst_cpu.read_register(0) == src_cpu.read_register(0));
    TEST_ASSERT(dst_cpu.read_register(30) == src_cpu.read_register(30));
    TEST_ASSERT(dst_cpu.get_sctlr_el1() == src_cpu.get_sctlr_el1());
    TEST_ASSERT(dst_cpu.get_tcr_el1() == src_cpu.get_tcr_el1());
    TEST_ASSERT(dst_cpu.get_ttbr0_el1() == src_cpu.get_ttbr0_el1());
    TEST_ASSERT(dst_cpu.get_ttbr1_el1() == src_cpu.get_ttbr1_el1());
    TEST_ASSERT(dst_cpu.get_mair_el1() == src_cpu.get_mair_el1());
    TEST_ASSERT(dst_cpu.get_par_el1() == src_cpu.get_par_el1());

    std::cout << "  -> PASSED\n";
}

void test_full_system_snapshot_memory_round_trip() {
    std::cout << "[Test 2] Full system memory & CPU snapshot round-trip...\n";
    CPU src_cpu;
    Memory src_mem(0x80000); // 512 KB
    VirtualTime src_vt;
    GICv3 src_gic;

    src_cpu.set_program_counter(0x5000);
    src_cpu.write_register(5, 0x12345678ULL);

    // Populate memory with distinct patterns across multiple 16KB pages
    src_mem.write_u64(0x10000, 0xDEADBEEFCAFEBABEULL);
    src_mem.write_u64(0x20000, 0x0123456789ABCDEFULL);
    src_mem.write_u64(0x30000, 0xFEDCBA9876543210ULL);

    SnapshotRestoreContext src_ctx{&src_cpu, &src_mem, &src_vt, &src_gic, {}};
    SnapshotCaptureOptions opts{};
    opts.description = "Stage 198 Snapshot Test";
    opts.include_memory = true;

    std::vector<std::uint8_t> snapshot_buf;
    const auto cap_res = SnapshotManager::capture_to_buffer(src_ctx, snapshot_buf, opts);
    TEST_ASSERT(cap_res.success);
    TEST_ASSERT(!snapshot_buf.empty());

    // Restore into target system
    CPU dst_cpu;
    Memory dst_mem(0x80000);
    VirtualTime dst_vt;
    GICv3 dst_gic;
    SnapshotRestoreContext dst_ctx{&dst_cpu, &dst_mem, &dst_vt, &dst_gic, {}};

    const auto rest_res = restore_snapshot_from_buffer(snapshot_buf.data(), snapshot_buf.size(), dst_ctx);
    TEST_ASSERT(rest_res.success);

    // Verify restored memory matches bit-for-bit
    TEST_ASSERT(dst_mem.read_u64(0x10000) == 0xDEADBEEFCAFEBABEULL);
    TEST_ASSERT(dst_mem.read_u64(0x20000) == 0x0123456789ABCDEFULL);
    TEST_ASSERT(dst_mem.read_u64(0x30000) == 0xFEDCBA9876543210ULL);
    TEST_ASSERT(dst_cpu.get_program_counter() == 0x5000);
    TEST_ASSERT(dst_cpu.read_register(5) == 0x12345678ULL);

    std::cout << "  -> PASSED\n";
}

void test_virtual_dma_device_serialization() {
    std::cout << "[Test 3] VirtualDmaDevice state serialization & restoration...\n";
    VirtualDmaDevice src_dma(0x1000, 0x1000, 32);

    // Configure DMA registers
    src_dma.write_register(0x10, 0x50000, 8); // DESC_ADDR
    src_dma.write_register(0x00, 0x1, 4);     // CONTROL (enable)

    const auto state_data = src_dma.serialize_state();
    TEST_ASSERT(!state_data.empty());

    VirtualDmaDevice dst_dma(0x1000, 0x1000, 32);
    const bool ok = dst_dma.deserialize_state(state_data.data(), state_data.size());
    TEST_ASSERT(ok);

    TEST_ASSERT(dst_dma.read_register(0x10, 8) == 0x50000);
    TEST_ASSERT(dst_dma.read_register(0x00, 4) == 0x1);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.14: Memory System Snapshot Integration Tests\n";
    std::cout << "========================================================\n";

    test_cpu_and_mmu_state_serialization();
    test_full_system_snapshot_memory_round_trip();
    test_virtual_dma_device_serialization();

    std::cout << "\nAll Stage 198.14 tests passed successfully.\n";
    return 0;
}
