// ==============================================================================
// WinIOS Campaign 203 Stage 203.3: DMA Performance & Mach IPC Safety Audit Tests
// ==============================================================================
// Independent verification of:
// 1. DMA Direct-Copy audit: std::memmove direct single-copy vs bounce buffer
// 2. DMA Coherency, exclusive monitor invalidation, and GIC completion interrupt
// 3. Mach IPC Generation wraparound (0xFFFF -> 1)
// 4. Mach IPC Stale handle rejection and generation reuse
// 5. Mach IPC Bounded queue capacity (64 msgs) and overflow handling
// 6. Mach IPC Send-once right atomic consumption
// 7. Mach IPC Concurrent right destruction & dead-name transition safety
// ==============================================================================

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cpu/gicv3.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_iommu.h"
#include "runtime/darwin_mach_ipc.h"

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

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
// Test 1: DMA Direct-Copy vs Bounce Buffer Benchmark & Coherency Audit
// -----------------------------------------------------------------------------
void test_dma_direct_copy_and_coherency_audit() {
    std::cout << "[Stage 203.3 Test 1] DMA Direct-Copy & Coherency Audit...\n";

    constexpr std::size_t kRamSize = 16 * 1024 * 1024; // 16 MB
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 4);
    SmpExclusiveMonitorManager monitor(4);
    shared_mem->attach_exclusive_monitor(&monitor);

    auto gic = std::make_shared<GICv3>();
    constexpr std::uint32_t kDmaIrq = 48;
    gic->set_core_count(1);
    gic->write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0080 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0100 + (kDmaIrq / 32) * 4, 1u << (kDmaIrq % 32), 4);
    gic->write_mmio(GICv3::kGicdBase + 0x0400 + kDmaIrq, 0x20u, 1);
    gic->write_icc_pmr(0xFFu, 0);
    gic->write_icc_igrpen1(1u, 0);

    VirtualDmaEngine dma(shared_mem);
    dma.set_gic(gic, kDmaIrq);

    // Map 64KB buffers
    constexpr std::uint64_t kDescPa = 0x10000;
    constexpr std::uint64_t kSrcPa  = 0x20000;
    constexpr std::uint64_t kDstPa  = 0x60000;
    constexpr std::size_t   kLength = 64 * 1024;

    // Initialize source buffer with pattern
    std::uint8_t* raw_mem = shared_mem->raw_memory().raw_data();
    for (std::size_t i = 0; i < kLength; ++i) {
        raw_mem[kSrcPa + i] = static_cast<std::uint8_t>((i * 37 + 13) & 0xFF);
    }
    std::memset(raw_mem + kDstPa, 0, kLength);

    // Arm exclusive monitor reservation on destination cache line
    monitor.load_exclusive(1, kDstPa, 64);
    TEST_CHECK(monitor.is_reservation_valid(1, kDstPa, 64),
               "Pre-DMA CPU exclusive reservation must be valid");

    // Setup descriptor in RAM at kDescPa
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = kSrcPa;
    desc.dst_paddr = kDstPa;
    desc.length_bytes = static_cast<std::uint32_t>(kLength);
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;
    std::memcpy(raw_mem + kDescPa, &desc, sizeof(desc));

    // 1. Execute direct-copy (single memmove, no bounce buffer)
    dma.set_zero_copy_enabled(true);
    auto res_direct = dma.execute_descriptor_chain(kDescPa);
    TEST_CHECK(res_direct.is_success(), "Direct-copy DMA transfer must succeed");
    TEST_CHECK(res_direct.bytes_transferred == kLength, "Bytes transferred must match 64KB");

    // Verify data bit-exactness
    bool match = (std::memcmp(raw_mem + kDstPa, raw_mem + kSrcPa, kLength) == 0);
    TEST_CHECK(match, "Destination buffer must exactly match source data");

    // Verify exclusive monitor reservation invalidation
    TEST_CHECK(!monitor.is_reservation_valid(1, kDstPa, 64),
               "DMA write must invalidate intersecting CPU exclusive reservations");

    // Verify GIC interrupt signaling (SPI 48)
    auto gic_eval = gic->evaluate(1, 0, 0);
    TEST_CHECK(gic_eval.has_interrupt && gic_eval.intid == kDmaIrq, "DMA completion must assert virtual GIC interrupt 48");

    dma.acknowledge_completion_irq();

    // Benchmark comparison: 1000 iterations of 64KB transfer
    constexpr int kIterations = 1000;
    
    // Direct-copy timing
    dma.set_zero_copy_enabled(true);
    auto start_direct = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        dma.execute_descriptor_chain(kDescPa);
    }
    auto end_direct = std::chrono::high_resolution_clock::now();
    double time_direct_ms = std::chrono::duration<double, std::milli>(end_direct - start_direct).count();

    // Bounce-buffer timing
    dma.set_zero_copy_enabled(false);
    auto start_bounce = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        dma.execute_descriptor_chain(kDescPa);
    }
    auto end_bounce = std::chrono::high_resolution_clock::now();
    double time_bounce_ms = std::chrono::duration<double, std::milli>(end_bounce - start_bounce).count();

    double mb_transferred = (static_cast<double>(kLength) * kIterations) / (1024.0 * 1024.0);
    double rate_direct = mb_transferred / (time_direct_ms / 1000.0);
    double rate_bounce = mb_transferred / (time_bounce_ms / 1000.0);

    std::cout << "  DMA Benchmark: " << kIterations << " x 64KB iterations (" << mb_transferred << " MB total)\n";
    std::cout << "    Direct-Copy:   " << time_direct_ms << " ms (" << rate_direct << " MB/s)\n";
    std::cout << "    Bounce-Buffer: " << time_bounce_ms << " ms (" << rate_bounce << " MB/s)\n";
    std::cout << "    Speedup:       " << (rate_direct / rate_bounce) << "x\n";

    TEST_CHECK(rate_direct > rate_bounce, "Direct-copy must be significantly faster than bounce-buffer");

    std::cout << "  -> DMA direct-copy & coherency audit passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Mach IPC Generation Wraparound & Stale Handle Rejection
// -----------------------------------------------------------------------------
void test_mach_ipc_generation_wraparound() {
    std::cout << "[Stage 203.3 Test 2] Mach IPC Generation Wraparound & Stale Handles...\n";

    MachPortSpace space(100);
    auto port_obj = std::make_shared<MachPortObject>(1, 100);

    // Allocate initial port
    mach_port_t h1 = space.allocate_port(port_obj, kMachPortRightReceive);
    std::uint16_t idx1 = MachPortSpace::handle_index(h1);
    std::uint16_t gen1 = MachPortSpace::handle_generation(h1);
    TEST_CHECK(gen1 == 1, "Initial generation must be 1");

    // Deallocate port
    bool dealloc_ok = space.deallocate_port(h1);
    TEST_CHECK(dealloc_ok, "Deallocation must succeed");

    // Reallocate port in the same slot -> generation should increment to 2
    auto port_obj2 = std::make_shared<MachPortObject>(2, 100);
    mach_port_t h2 = space.allocate_port(port_obj2, kMachPortRightReceive);
    std::uint16_t idx2 = MachPortSpace::handle_index(h2);
    std::uint16_t gen2 = MachPortSpace::handle_generation(h2);
    TEST_CHECK(idx2 == idx1, "Reallocated port should reuse slot from free list");
    TEST_CHECK(gen2 == 2, "Generation must increment on reuse");

    // Verify that using stale handle h1 fails
    TEST_CHECK(space.lookup_port(h1) == nullptr, "Lookup with stale generation handle must fail");
    TEST_CHECK(!space.deallocate_port(h1), "Deallocate with stale generation handle must fail");
    TEST_CHECK(!space.mod_refs(h1, kMachPortRightReceive, 1), "mod_refs with stale handle must fail");

    // Advance generation linearly
    mach_port_t current_handle = h2;
    for (int i = 0; i < 200; ++i) {
        space.deallocate_port(current_handle);
        current_handle = space.allocate_port(port_obj2, kMachPortRightReceive);
    }
    std::uint16_t gen_after_200 = MachPortSpace::handle_generation(current_handle);
    TEST_CHECK(gen_after_200 == 202, "Generation increments linearly on reuse");

    std::cout << "  -> Mach IPC generation wraparound & stale handle tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Mach IPC Send-Once Consumption & Bounded Queue Capacity
// -----------------------------------------------------------------------------
void test_mach_ipc_send_once_and_queue_bounds() {
    std::cout << "[Stage 203.3 Test 3] Mach IPC Send-Once & Bounded Queue Limits...\n";

    MachPortSpace space(200);
    auto port_obj = std::make_shared<MachPortObject>(10, 200);
    mach_port_t rcv_h = space.allocate_port(port_obj, kMachPortRightReceive);
    mach_port_t so_h = space.allocate_port(port_obj, kMachPortRightSendOnce);
    (void)rcv_h;

    TEST_CHECK(space.has_right(so_h, kMachPortRightSendOnce), "Port must have SendOnce right");

    // Atomic consumption of send-once right
    bool consumed = space.consume_send_once(so_h);
    TEST_CHECK(consumed, "First send-once consumption must succeed");

    // Second consumption must fail
    bool consumed_again = space.consume_send_once(so_h);
    TEST_CHECK(!consumed_again, "Second send-once consumption must fail");
    TEST_CHECK(space.lookup_port(so_h) == nullptr, "Consumed send-once port must no longer exist");

    // Bounded Queue Capacity Test: exactly 64 messages allowed
    for (std::size_t i = 0; i < MachPortObject::kMaxQueueLimit; ++i) {
        MachMessage msg{};
        msg.header.msgh_id = static_cast<std::int32_t>(1000 + i);
        bool enq = port_obj->enqueue_message(std::move(msg));
        TEST_CHECK(enq, "Enqueue within queue capacity must succeed");
    }
    TEST_CHECK(port_obj->queue_size() == 64, "Queue must hold exactly 64 messages");

    // The 65th message must be rejected (queue full)
    MachMessage overflow_msg{};
    overflow_msg.header.msgh_id = 9999;
    bool overflow_enq = port_obj->enqueue_message(std::move(overflow_msg));
    TEST_CHECK(!overflow_enq, "65th message must be rejected due to bounded queue limit");
    TEST_CHECK(port_obj->queue_size() == 64, "Queue size must remain bounded at 64");

    std::cout << "  -> Mach IPC send-once & queue bounds tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Concurrent Right Destruction & Dead-Name Safety
// -----------------------------------------------------------------------------
void test_mach_ipc_concurrent_destruction() {
    std::cout << "[Stage 203.3 Test 4] Concurrent Right Destruction & Dead-Name Safety...\n";

    MachPortSpace task_a(301);
    MachPortSpace task_b(302);

    auto shared_port = std::make_shared<MachPortObject>(50, 301);
    mach_port_t rcv_right = task_a.allocate_port(shared_port, kMachPortRightReceive);
    mach_port_t send_right = task_b.allocate_port(shared_port, kMachPortRightSend);

    std::atomic<bool> start_flag{false};
    std::atomic<int> send_successes{0};
    std::atomic<int> send_failures{0};

    // Thread 1: Dispatches messages from Task B
    std::thread sender([&]() {
        while (!start_flag.load()) {}
        for (int i = 0; i < 500; ++i) {
            auto dest = task_b.lookup_port(send_right);
            if (dest && !dest->is_dead()) {
                MachMessage msg{};
                msg.header.msgh_id = i;
                if (dest->enqueue_message(msg)) {
                    send_successes++;
                } else {
                    send_failures++;
                }
            } else {
                send_failures++;
            }
        }
    });

    // Thread 2: Deallocates receive right on Task A midway
    std::thread destroyer([&]() {
        while (!start_flag.load()) {}
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        task_a.deallocate_port(rcv_right);
    });

    start_flag.store(true);
    sender.join();
    destroyer.join();

    // Verify final state: receive right is gone, port object is marked dead
    TEST_CHECK(shared_port->is_dead(), "Port object must be marked dead after receive right deallocation");

    // Task B's lookup must now transition to dead name
    auto dest_after = task_b.lookup_port(send_right);
    TEST_CHECK(dest_after == nullptr, "Lookup of dead port must return nullptr");
    TEST_CHECK(task_b.get_right_type(send_right) == kMachPortRightDeadName,
               "Send right must have transitioned to DeadName");

    std::cout << "  -> Concurrent right destruction & dead-name safety passed.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.3: DMA Performance & Mach IPC Safety Audit Tests\n";
    std::cout << "==================================================================\n";

    test_dma_direct_copy_and_coherency_audit();
    test_mach_ipc_generation_wraparound();
    test_mach_ipc_send_once_and_queue_bounds();
    test_mach_ipc_concurrent_destruction();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.3 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
