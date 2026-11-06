// =============================================================================
// WinIOS Phase 52 Stage 34: Differential Validation Tests
// =============================================================================
// Tri-way differential verification comparing:
// 1. Direct deterministic execution (Production CPU/Memory/DMA)
// 2. Recorded execution followed by replay (SmpReplayCoordinator)
// 3. Independent reference-model execution (ReferenceExecutor)
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_exclusive_monitor.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "device/virtual_dma_engine.h"
#include "reference/independent_reference_model.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "sndl/sndl_v2_recorder.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::reference;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sndl;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_stage34_diff_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Integer Arithmetic & Flags Differential Validation
// -----------------------------------------------------------------------------
void test_group_1_arithmetic_and_flags_differential() {
    std::cout << "[Group 1] Testing Integer Arithmetic & Flags Differential Validation...\n";

    constexpr std::size_t kRamSize = 1024 * 1024;
    Memory prod_mem(kRamSize);
    ReferenceMemory ref_mem(kRamSize);

    // Build instruction program:
    // 0x1000: add x0, xzr, #42        (0x9100a9e0) -> X0 = 42
    // 0x1004: adds x1, x0, #10        (0xb1002801) -> X1 = 52, NZCV updated
    // 0x1008: sub x2, x1, #20         (0xd1005022) -> X2 = 32
    // 0x100c: subs x3, x2, #32        (0xf1008043) -> X3 = 0, Z flag set!
    // 0x1010: b 0x1010 (halt loop)    (0x14000000)
    const std::vector<std::uint32_t> program = {
        0x9100a9e0u, // add x0, xzr, #42
        0xb1002801u, // adds x1, x0, #10
        0xd1005022u, // sub x2, x1, #20
        0xf1008043u, // subs x3, x2, #32
        0x14000000u  // b .
    };

    for (std::size_t i = 0; i < program.size(); ++i) {
        prod_mem.write_u32(0x1000 + i * 4, program[i]);
        ref_mem.write_u32(0x1000 + i * 4, program[i]);
    }

    // 1. Run Production CPU
    CPU prod_cpu;
    prod_cpu.set_program_counter(0x1000);
    for (std::size_t step = 0; step < 4; ++step) {
        prod_cpu.step(prod_mem);
    }

    // 2. Run Independent Reference Model
    ReferenceCpuState ref_cpu{};
    ref_cpu.pc = 0x1000;
    ReferenceExclusiveMonitor ref_mon{};

    for (std::size_t step = 0; step < 4; ++step) {
        auto step_res = ReferenceExecutor::step_instruction(ref_cpu, ref_mem, ref_mon, 0);
        TEST_ASSERT(step_res.success);
    }

    // 3. Differential Comparison
    std::uint64_t actual_x[31]{};
    for (std::size_t i = 0; i < 31; ++i) actual_x[i] = prod_cpu.read_register(i);

    const std::uint32_t prod_nzcv32 = static_cast<std::uint32_t>(prod_cpu.get_nzcv()) << 28;

    const auto report = DifferentialValidator::compare_cpu_state(
        0, ref_cpu, prod_cpu.get_program_counter(),
        prod_nzcv32, prod_cpu.get_instruction_retirement_count(),
        actual_x);

    if (report.has_divergence) {
        std::cerr << "Divergence in Group 1: " << report.message << " field=" << report.field_name
                  << " expected=0x" << std::hex << report.expected_value
                  << " actual=0x" << report.actual_value << std::dec << "\n";
    }
    TEST_ASSERT(!report.has_divergence);
    TEST_ASSERT(actual_x[0] == 42);
    TEST_ASSERT(actual_x[1] == 52);
    TEST_ASSERT(actual_x[2] == 32);
    TEST_ASSERT(actual_x[3] == 0);
    TEST_ASSERT((prod_cpu.get_nzcv() & (1u << 2)) != 0); // Z flag (bit 2 of 4-bit field) set

    std::cout << "  -> Group 1 Passed. Production and Reference agree bit-for-bit across arithmetic and flags.\n";
}

// -----------------------------------------------------------------------------
// Group 2: Memory Load/Store & Exclusive Monitor Differential Validation
// -----------------------------------------------------------------------------
void test_group_2_load_store_exclusive_differential() {
    std::cout << "[Group 2] Testing Load/Store & Exclusive Monitor Differential Validation...\n";

    constexpr std::size_t kRamSize = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 2);
    auto prod_monitor = std::make_unique<SmpExclusiveMonitorManager>(2);
    shared_mem->attach_exclusive_monitor(prod_monitor.get());

    ReferenceMemory ref_mem(kRamSize);
    ReferenceExclusiveMonitor ref_monitor{};

    // 1. Establish exclusive reservations on both models for Core 0 at 0x20000
    prod_monitor->load_exclusive(0, 0x20000, 64);
    ref_monitor.load_exclusive(0, 0x20000, 64);

    TEST_ASSERT(prod_monitor->is_reservation_valid(0, 0x20000, 64));
    TEST_ASSERT(ref_monitor.is_reservation_valid(0, 0x20000, 64));

    // 2. Perform write from Core 1 into 0x20020 (overlapping reservation)
    std::uint64_t write_val = 0xCAFEBABE12345678ull;
    shared_mem->write_u64(1, 0x20020, write_val);
    ref_mem.write_u64(0x20020, write_val);
    ref_monitor.invalidate_reservations(0x20020, 8);

    // 3. Verify reservation was invalidated on both models
    TEST_ASSERT(!prod_monitor->is_reservation_valid(0, 0x20000, 64));
    TEST_ASSERT(!ref_monitor.is_reservation_valid(0, 0x20000, 64));

    // 4. Differential Memory Compare
    const auto mem_report = DifferentialValidator::compare_memory(
        ref_mem, shared_mem->raw_memory().raw_data(), kRamSize);
    TEST_ASSERT(!mem_report.has_divergence);

    std::cout << "  -> Group 2 Passed. Exclusive monitor invalidations match identically.\n";
}

// -----------------------------------------------------------------------------
// Group 3: Virtual DMA Descriptor Chain Differential Validation
// -----------------------------------------------------------------------------
void test_group_3_virtual_dma_differential() {
    std::cout << "[Group 3] Testing Virtual DMA Descriptor Chain Differential Validation...\n";

    constexpr std::size_t kRamSize = 2 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 1);
    VirtualDmaEngine prod_dma(shared_mem);

    ReferenceMemory ref_mem(kRamSize);
    ReferenceExclusiveMonitor ref_monitor{};

    // Setup source buffer at 0x10000 with recognizable pattern
    for (std::size_t i = 0; i < 256; ++i) {
        const std::uint8_t byte_val = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
        shared_mem->write_u8(0, 0x10000 + i, byte_val);
        ref_mem.write_u8(0x10000 + i, byte_val);
    }

    // Descriptor 1: Memcpy 128 bytes from 0x10000 to 0x30000, chains to Desc 2
    // Descriptor 2: Fill 64 bytes at 0x40000 with 0xA5A5A5A5, end of chain
    VirtualDmaDescriptor d1{};
    d1.descriptor_id = 1;
    d1.src_paddr = 0x10000;
    d1.dst_paddr = 0x30000;
    d1.length_bytes = 128;
    d1.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    d1.flags = kDmaFlagChainNext;
    d1.next_descriptor_paddr = 0x8040;

    VirtualDmaDescriptor d2{};
    d2.descriptor_id = 2;
    d2.src_paddr = 0;
    d2.dst_paddr = 0x40000;
    d2.length_bytes = 64;
    d2.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    d2.fill_pattern = 0xA5A5A5A5u;
    d2.flags = 0;
    d2.next_descriptor_paddr = 0;

    shared_mem->write_bytes(0, 0x8000, reinterpret_cast<const std::uint8_t*>(&d1), sizeof(d1));
    shared_mem->write_bytes(0, 0x8040, reinterpret_cast<const std::uint8_t*>(&d2), sizeof(d2));

    ref_mem.write_bytes(0x8000, reinterpret_cast<const std::uint8_t*>(&d1), sizeof(d1));
    ref_mem.write_bytes(0x8040, reinterpret_cast<const std::uint8_t*>(&d2), sizeof(d2));

    // Execute Production DMA
    auto prod_res = prod_dma.execute_descriptor_chain(0x8000, 0);
    TEST_ASSERT(prod_res.is_success());
    TEST_ASSERT(prod_res.descriptors_processed == 2);

    // Execute Reference DMA
    auto ref_res = ReferenceDmaEngine::execute_descriptor_chain(ref_mem, ref_monitor, 0x8000, 0);
    TEST_ASSERT(ref_res.success);
    TEST_ASSERT(ref_res.descriptors_processed == 2);
    TEST_ASSERT(ref_res.bytes_transferred == 128 + 64);

    // Differential RAM Comparison
    const auto mem_report = DifferentialValidator::compare_memory(
        ref_mem, shared_mem->raw_memory().raw_data(), kRamSize);
    if (mem_report.has_divergence) {
        std::cerr << "DMA RAM Divergence at addr 0x" << std::hex << mem_report.pc
                  << " expected 0x" << mem_report.expected_value
                  << " actual 0x" << mem_report.actual_value << std::dec << "\n";
    }
    TEST_ASSERT(!mem_report.has_divergence);

    std::cout << "  -> Group 3 Passed. Virtual DMA effects agree byte-for-byte with independent reference model.\n";
}

// -----------------------------------------------------------------------------
// Group 4: Tri-Way Execution, Replay & Reference Differential Validation
// -----------------------------------------------------------------------------
void test_group_4_tri_way_execution_replay_reference() {
    std::cout << "[Group 4] Testing Tri-Way Execution, Replay & Reference Differential Validation...\n";
    setup_test_dir();

    const auto snap_path = kTestDir / "g4_diff.wsnap";
    const auto sndl_path = kTestDir / "g4_diff.sndl";

    constexpr std::size_t kRamSize = 2 * 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 2);
    auto sched = std::make_unique<SmpScheduler>(shared_mem, 2);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x1000, 0x40, 16);
    dma->set_shared_memory(shared_mem);
    bus->register_device(dma);
    sched->attach_device_bus(bus);

    // Setup initial program on Core 0:
    // add x0, xzr, #100
    // add x1, xzr, #200
    // add x2, x0, x1 (X2 = 300)
    // b .
    const std::vector<std::uint32_t> code0 = {
        0x910191e0u, // add x0, xzr, #100
        0x910323e1u, // add x1, xzr, #200
        0x8b010002u, // add x2, x0, x1
        0x14000000u  // b .
    };
    for (std::size_t i = 0; i < code0.size(); ++i) {
        shared_mem->raw_memory().write_u32(0x2000 + i * 4, code0[i]);
    }

    auto cpu0 = std::make_shared<CPU>();
    cpu0->set_core_id(0);
    cpu0->set_program_counter(0x2000);
    sched->register_core(0, cpu0, CoreLifecycleState::Runnable);

    auto cpu1 = std::make_shared<CPU>();
    cpu1->set_core_id(1);
    cpu1->set_program_counter(0x4000);
    shared_mem->raw_memory().write_u32(0x4000, 0x14000000u);
    sched->register_core(1, cpu1, CoreLifecycleState::Runnable);

    // 1. Initial Snapshot
    const auto snap_res = SmpSnapshotManager::capture_to_file(*sched, snap_path);
    TEST_ASSERT(snap_res.success);

    snapshot::SmpSnapshotInspection snap_info{};
    (void)SmpSnapshotManager::inspect_file(snap_path, snap_info);

    // 2. Direct Recorded Execution
    SndlV2RecorderOptions rec_opts{};
    rec_opts.core_count = 2;
    rec_opts.base_snapshot_hash = snap_info.integrity_data.canonical_cluster_hash;
    rec_opts.memory_capacity = kRamSize;

    SndlV2Recorder recorder(rec_opts);
    dma->attach_sndl_recorder(&recorder);
    sched->attach_sndl_recorder(&recorder);

    TEST_ASSERT(recorder.start_recording(sndl_path).is_success());
    sched->step_quantum();
    sched->step_quantum();
    TEST_ASSERT(recorder.stop_recording(SndlV2CompletionStatus::Completed).is_success());

    const std::uint64_t direct_exec_hash = sched->compute_canonical_multicore_hash();
    const std::uint64_t direct_x2 = cpu0->read_register(2);
    TEST_ASSERT(direct_x2 == 300);

    // 3. Replay Execution
    SmpReplayCapabilityConfig cfg{};
    cfg.allow_experimental_smp = true;
    cfg.max_supported_cores = 2;
    cfg.allow_virtual_devices = true;
    cfg.allow_virtual_dma = true;
    cfg.allow_dma_recording = true;
    cfg.allow_dma_replay = true;

    SmpReplayCoordinator coord(cfg);
    TEST_ASSERT(coord.prepare(snap_path, sndl_path).is_success());
    auto replay_run = coord.run();
    if (!replay_run.is_success()) {
        std::cerr << "Replay run failed: " << replay_run.message << "\n";
    }
    TEST_ASSERT(replay_run.is_success());

    const std::uint64_t replay_hash = coord.scheduler()->compute_canonical_multicore_hash();
    TEST_ASSERT(replay_hash == direct_exec_hash);

    // 4. Independent Reference Execution
    ReferenceMemory ref_mem(kRamSize);
    for (std::size_t i = 0; i < code0.size(); ++i) {
        ref_mem.write_u32(0x2000 + i * 4, code0[i]);
    }
    ReferenceCpuState ref_cpu{};
    ref_cpu.pc = 0x2000;
    ReferenceExclusiveMonitor ref_mon{};

    for (std::size_t step = 0; step < 3; ++step) {
        auto r = ReferenceExecutor::step_instruction(ref_cpu, ref_mem, ref_mon, 0);
        TEST_ASSERT(r.success);
    }

    TEST_ASSERT(ref_cpu.x[2] == 300);
    TEST_ASSERT(ref_cpu.x[2] == direct_x2);

    std::cout << "  -> Group 4 Passed. Tri-way parity confirmed between Direct (0x" << std::hex
              << direct_exec_hash << "), Replay (0x" << replay_hash << ") and Reference (X2=300)"
              << std::dec << ".\n";
}

} // namespace

int main() {
    try {
        std::cout << "=================================================================\n";
        std::cout << "WinIOS Phase 52 Stage 34: Differential Validation Tests\n";
        std::cout << "=================================================================\n";

        test_group_1_arithmetic_and_flags_differential();
        test_group_2_load_store_exclusive_differential();
        test_group_3_virtual_dma_differential();
        test_group_4_tri_way_execution_replay_reference();

        std::cout << "All Differential Validation Tests PASSED successfully.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        return 1;
    }
}
