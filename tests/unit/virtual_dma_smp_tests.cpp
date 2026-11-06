// =============================================================================
// WinIOS Phase 52 Stage 31: Virtual DMA SMP & Capability Gate Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "device/virtual_dma_types.h"
#include "replay/smp_replay_coordinator.h"
#include "replay/smp_replay_types.h"

#define TEST_ASSERT(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::device;
using namespace winios::replay;

namespace {

// -----------------------------------------------------------------------------
// Group 1: DMA Execution Across 1, 2, 4, 8 Cores Under Scheduler
// -----------------------------------------------------------------------------
void test_group_1_smp_topologies() {
    std::cout << "[Group 1] Testing DMA Across 1, 2, 4, 8 Virtual Cores...\n";

    for (std::size_t cores : {1ULL, 2ULL, 4ULL, 8ULL}) {
        const std::size_t ram = 1024 * 1024;
        auto mem = std::make_shared<SmpSharedMemory>(ram, cores);
        SmpScheduler sched(mem, cores, 50);

        for (std::size_t c = 0; c < cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            cpu->set_core_id(c);
            sched.register_core(c, cpu, CoreLifecycleState::Runnable);
        }

        auto dma_dev = std::make_shared<VirtualDmaDevice>(0x444D4131, 0x10000000, 0x1000, mem, 48);

        // Core (cores - 1) submits a DMA copy
        const std::size_t sub_core = cores - 1;
        const std::uint64_t src = 0x10000 + sub_core * 0x1000;
        const std::uint64_t dst = 0x20000 + sub_core * 0x1000;
        const std::uint64_t desc_addr = 0x5000 + sub_core * 0x100;

        std::vector<std::uint8_t> data(128, static_cast<std::uint8_t>(0x30 + sub_core));
        mem->write_bytes(sub_core, src, data.data(), data.size());

        VirtualDmaDescriptor desc{};
        desc.descriptor_id = 1000 + sub_core;
        desc.src_paddr = src;
        desc.dst_paddr = dst;
        desc.length_bytes = data.size();
        desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
        mem->write_bytes(sub_core, desc_addr, &desc, sizeof(desc));

        auto res = dma_dev->trigger_transfer(desc_addr, sub_core);
        assert(res.is_success());

        std::vector<std::uint8_t> read_back(data.size(), 0);
        mem->read_bytes(sub_core, dst, read_back.data(), data.size());
        assert(read_back == data);

        std::cout << "  -> " << cores << "-core topology PASS\n";
    }
}

// -----------------------------------------------------------------------------
// Group 2: Core-Tagged Memory Telemetry
// -----------------------------------------------------------------------------
void test_group_2_core_telemetry() {
    std::cout << "[Group 2] Testing Core-Tagged Memory Telemetry on DMA...\n";
    const std::size_t ram = 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram, 8);
    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x444D4131, 0x10000000, 0x1000, mem, 48);

    mem->reset_telemetry();

    // Core 5 triggers a 256-byte copy
    const std::size_t sub_core = 5;
    const std::uint64_t desc_addr = 0x6000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 777;
    desc.src_paddr = 0x10000;
    desc.dst_paddr = 0x20000;
    desc.length_bytes = 256;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    mem->write_bytes(sub_core, desc_addr, &desc, sizeof(desc));

    auto res = dma_dev->trigger_transfer(desc_addr, sub_core);
    assert(res.is_success());

    auto telem5 = mem->get_telemetry(sub_core);
    assert(telem5.bytes_read >= 256);
    assert(telem5.bytes_written >= 256);

    // Other cores should have 0 bytes
    for (std::size_t c = 0; c < 8; ++c) {
        if (c == sub_core) continue;
        auto telem = mem->get_telemetry(c);
        assert(telem.bytes_read == 0);
        assert(telem.bytes_written == 0);
    }

    std::cout << "  -> Group 2 PASS (Telemetry accurately attributed to core 5)\n";
}

// -----------------------------------------------------------------------------
// Group 3: Interrupt Router Integration
// -----------------------------------------------------------------------------
void test_group_3_interrupt_routing() {
    std::cout << "[Group 3] Testing Interrupt Router Integration...\n";
    const std::size_t ram = 1024 * 1024;
    auto mem = std::make_shared<SmpSharedMemory>(ram, 8);
    SmpInterruptRouter router(8);
    VirtualDeviceBus bus;
    bus.attach_interrupt_router(&router);

    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x444D4131, 0x10000000, 0x1000, mem, 48);
    assert(bus.register_device(dma_dev));

    // Enable DMA IRQ in control register (bit 3)
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlIrqEnable, 8);

    // Trigger DMA copy
    const std::uint64_t desc_addr = 0x7000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 888;
    desc.src_paddr = 0x10000;
    desc.dst_paddr = 0x20000;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    dma_dev->write_register(dma_reg::kDescriptorAddr, desc_addr, 8);
    dma_dev->write_register(dma_reg::kControl, kControlEnable | kControlStart | kControlIrqEnable, 8);

    // Verify interrupt router has pending request for Core 0 with INTID 48
    assert(router.has_pending(0));
    auto opt_req = router.peek_pending(0);
    assert(opt_req.has_value());
    assert(opt_req->intid == 48);
    assert(opt_req->type == InterruptType::SPI);

    std::cout << "  -> Group 3 PASS (INTID 48 asserted and queued)\n";
}

// -----------------------------------------------------------------------------
// Group 4: Capability Gate Enforcement & Replay Rejection
// -----------------------------------------------------------------------------
void test_group_4_capability_gates() {
    std::cout << "[Group 4] Testing Capability Gate Security Invariants...\n";

    // 1. allow_uncaptured_dma == true is strictly rejected in SmpReplayCoordinator
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_uncaptured_dma = true; // Illegal
        SmpReplayCoordinator coord(cfg);
        auto prep = coord.prepare("dummy.wsnap", "dummy.sndl");
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 2. allow_dma_replay == true without allow_dma_recording is rejected
    {
        SmpReplayCapabilityConfig cfg;
        cfg.allow_dma_replay = true;
        cfg.allow_dma_recording = false; // Missing prerequisite
        SmpReplayCoordinator coord(cfg);
        auto prep = coord.prepare("dummy.wsnap", "dummy.sndl");
        assert(prep.is_failure());
        assert(prep.code == ReplayErrorCode::UnsupportedConfiguration);
    }

    // 3. Defaults check: all DMA flags are false by default
    {
        SmpReplayCapabilityConfig default_cfg;
        assert(!default_cfg.allow_virtual_devices);
        assert(!default_cfg.allow_virtual_dma);
        assert(!default_cfg.allow_dma_recording);
        assert(!default_cfg.allow_dma_replay);
        assert(!default_cfg.allow_uncaptured_dma);
    }

    std::cout << "  -> Group 4 PASS (Security gates verified)\n";
}

} // namespace

int main() {
    std::cout << "===================================================================\n";
    std::cout << " WinIOS Phase 52 Stage 31: Virtual DMA SMP & Security Tests         \n";
    std::cout << "===================================================================\n\n";

    test_group_1_smp_topologies();
    test_group_2_core_telemetry();
    test_group_3_interrupt_routing();
    test_group_4_capability_gates();

    std::cout << "\n>>> ALL VIRTUAL DMA SMP TEST GROUPS PASSED <<<\n";
    return 0;
}
