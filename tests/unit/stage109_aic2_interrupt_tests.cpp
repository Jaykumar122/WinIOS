#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_aic.h"
#include "device/apple_devicetree.h"
#include "device/virtual_device_bus.h"
#include "runtime/darwin_platform_expert.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

#define AIC_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Test 1: AIC2 Hardware Information & Identification Registers
// -----------------------------------------------------------------------------
bool test_aic2_hw_info_and_config() {
    std::cout << "[RUN] Test 1: AIC2 Hardware Information & Identification ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    AIC_TEST_ASSERT(aic.core_count() == 6, "AIC2 must support 6 CPU cores for iPhone 15 / A16 Bionic");
    AIC_TEST_ASSERT(aic.mmio_base() == 0x08000000ULL, "MMIO base must match Apple Silicon standard 0x08000000");
    AIC_TEST_ASSERT(aic.mmio_size() == 0x00100000ULL, "MMIO aperture size must be 1 MB");

    // Read AIC_INFO (0x0004): [31:24]=nr_cpus, [23:16]=nr_dies, [15:0]=nr_irqs
    const std::uint64_t info = aic.read_register(AppleAIC::kRegInfo, 4);
    const auto nr_cpus = static_cast<std::uint32_t>((info >> 24) & 0xFFu);
    const auto nr_dies = static_cast<std::uint32_t>((info >> 16) & 0xFFu);
    const auto nr_irqs = static_cast<std::uint32_t>(info & 0xFFFFu);

    AIC_TEST_ASSERT(nr_cpus == 6, "AIC_INFO must report 6 CPUs");
    AIC_TEST_ASSERT(nr_dies == 1, "AIC_INFO must report 1 die");
    AIC_TEST_ASSERT(nr_irqs == 512, "AIC_INFO must report 512 IRQ lines");

    // Global Config Register (0x0010)
    std::uint64_t cfg = aic.read_register(AppleAIC::kRegConfig, 4);
    AIC_TEST_ASSERT(cfg == 1, "AIC must be enabled on reset");

    aic.write_register(AppleAIC::kRegConfig, 0, 4);
    AIC_TEST_ASSERT(aic.read_register(AppleAIC::kRegConfig, 4) == 0, "AIC must be disabled when config cleared");

    aic.write_register(AppleAIC::kRegConfig, 1, 4);
    AIC_TEST_ASSERT(aic.read_register(AppleAIC::kRegConfig, 4) == 1, "AIC must re-enable");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Per-Core Banked Register Banking & WHOAMI Strides
// -----------------------------------------------------------------------------
bool test_aic2_per_core_banking() {
    std::cout << "[RUN] Test 2: Per-Core Banked Register Stride & WHOAMI ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    for (std::size_t c = 0; c < 6; ++c) {
        const std::uint64_t core_reg = AppleAIC::kRegCoreBase + (c * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI;
        const std::uint64_t whoami = aic.read_register(core_reg, 4);
        AIC_TEST_ASSERT(whoami == c, "WHOAMI register must reflect physical core index");
    }

    // Dynamic core switching for unindexed core access
    for (std::size_t c = 0; c < 6; ++c) {
        aic.set_current_core(c);
        AIC_TEST_ASSERT(aic.current_core() == c, "current_core() must track set value");
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Hardware IRQ Masking (Set/Clear Registers)
// -----------------------------------------------------------------------------
bool test_aic2_mask_registers() {
    std::cout << "[RUN] Test 3: Hardware IRQ Masking (Set & Clear Semantics) ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Initial state: all 512 IRQs must be masked (1)
    for (std::uint32_t irq = 0; irq < 512; ++irq) {
        AIC_TEST_ASSERT(aic.is_hw_irq_masked(irq), "All IRQs must be masked by default on reset");
    }

    // Unmask IRQ 33 (UART) by writing 1 to MASK_CLR (offset 0x8080 + 4*(33/32))
    const std::uint64_t clr_offset = AppleAIC::kRegMaskClrBase + (33 / 32) * 4;
    const std::uint32_t clr_bit = 1u << (33 % 32);
    aic.write_register(clr_offset, clr_bit, 4);

    AIC_TEST_ASSERT(!aic.is_hw_irq_masked(33), "IRQ 33 should be unmasked after writing MASK_CLR");
    AIC_TEST_ASSERT(aic.is_hw_irq_masked(32), "IRQ 32 should remain masked");
    AIC_TEST_ASSERT(aic.is_hw_irq_masked(34), "IRQ 34 should remain masked");

    // Re-mask IRQ 33 by writing 1 to MASK_SET (offset 0x8000 + 4*(33/32))
    const std::uint64_t set_offset = AppleAIC::kRegMaskSetBase + (33 / 32) * 4;
    aic.write_register(set_offset, clr_bit, 4);

    AIC_TEST_ASSERT(aic.is_hw_irq_masked(33), "IRQ 33 should be masked after writing MASK_SET");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Priority Arbitration (Timer > IPI > HW IRQ)
// -----------------------------------------------------------------------------
bool test_aic2_priority_arbitration() {
    std::cout << "[RUN] Test 4: Priority Arbitration (Timer > IPI > HW IRQ) ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Unmask IRQ 100 and route to Core 0
    aic.unmask_hw_irq(100);
    aic.set_irq_target(100, 0x01); // Target Core 0

    // Assert HW IRQ 100
    aic.set_hw_irq(100, true);
    AIC_TEST_ASSERT(aic.has_pending_interrupt(0), "Core 0 must have pending interrupt for HW IRQ 100");

    // Verify event is HW type
    std::uint32_t ev = aic.pop_event(0);
    std::uint32_t ev_type = (ev >> 16) & 0xFFFFu;
    std::uint32_t ev_vec = ev & 0xFFFFu;
    AIC_TEST_ASSERT(ev_type == AppleAIC::kEventTypeHw, "Event should be HW IRQ");
    AIC_TEST_ASSERT(ev_vec == 100, "Event vector should be 100");

    // Assert IPI while HW IRQ is still pending
    aic.send_ipi(0x01); // Core 0
    ev = aic.pop_event(0);
    ev_type = (ev >> 16) & 0xFFFFu;
    AIC_TEST_ASSERT(ev_type == AppleAIC::kEventTypeIpi, "IPI must preempt HW IRQ");

    // Assert Timer IRQ while both IPI and HW IRQ are pending
    aic.set_timer_irq(0, true);
    ev = aic.pop_event(0);
    ev_type = (ev >> 16) & 0xFFFFu;
    AIC_TEST_ASSERT(ev_type == AppleAIC::kEventTypeTimer, "Timer must preempt both IPI and HW IRQ (Highest Priority)");

    // EOI Timer -> next should be IPI
    aic.acknowledge_eoi(0, ev);
    ev = aic.pop_event(0);
    ev_type = (ev >> 16) & 0xFFFFu;
    AIC_TEST_ASSERT(ev_type == AppleAIC::kEventTypeIpi, "After Timer EOI, next highest must be IPI");

    // Clear IPI -> next should be HW IRQ 100
    aic.acknowledge_eoi(0, ev);
    ev = aic.pop_event(0);
    ev_type = (ev >> 16) & 0xFFFFu;
    ev_vec = ev & 0xFFFFu;
    AIC_TEST_ASSERT(ev_type == AppleAIC::kEventTypeHw, "After IPI EOI, next highest must be HW IRQ");
    AIC_TEST_ASSERT(ev_vec == 100, "HW IRQ vector must be 100");

    // Deassert HW IRQ 100 -> no pending interrupts left
    aic.set_hw_irq(100, false);
    aic.acknowledge_eoi(0, ev);
    AIC_TEST_ASSERT(!aic.has_pending_interrupt(0), "No pending interrupts should remain on Core 0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Target Core Routing & Affinity Isolation
// -----------------------------------------------------------------------------
bool test_aic2_target_core_routing() {
    std::cout << "[RUN] Test 5: Multi-Core IRQ Affinity Routing & Isolation ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Unmask IRQ 50
    aic.unmask_hw_irq(50);

    // Route IRQ 50 strictly to Core 3 (bitmask = 1 << 3 = 0x08)
    const std::uint64_t target_offset = AppleAIC::kRegTargetBase + (50 * 4);
    aic.write_register(target_offset, 0x08, 4);
    AIC_TEST_ASSERT(aic.get_irq_target(50) == 0x08, "Target register readback must be 0x08");

    // Assert IRQ 50
    aic.set_hw_irq(50, true);

    // Verify Core 3 has pending interrupt, and all other cores (0, 1, 2, 4, 5) do NOT
    for (std::size_t c = 0; c < 6; ++c) {
        if (c == 3) {
            AIC_TEST_ASSERT(aic.has_pending_interrupt(c), "Core 3 must have pending interrupt");
            std::uint32_t ev = aic.pop_event(c);
            AIC_TEST_ASSERT((ev & 0xFFFFu) == 50, "Core 3 event vector must be 50");
        } else {
            AIC_TEST_ASSERT(!aic.has_pending_interrupt(c), "Other cores must NOT receive IRQ 50");
        }
    }

    // Now re-route IRQ 50 to broadcast (0x3F = Cores 0..5)
    aic.write_register(target_offset, 0x3F, 4);
    for (std::size_t c = 0; c < 6; ++c) {
        AIC_TEST_ASSERT(aic.has_pending_interrupt(c), "All cores must receive broadcast IRQ 50");
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Inter-Processor Interrupt (IPI) Lifecycle
// -----------------------------------------------------------------------------
bool test_aic2_ipi_lifecycle() {
    std::cout << "[RUN] Test 6: Inter-Processor Interrupt (IPI) Send & Ack ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Send IPI targeting Cores 1, 2, and 5 (mask = 0b100110 = 0x26)
    aic.write_register(AppleAIC::kRegIpiSend, 0x26, 4);

    AIC_TEST_ASSERT(!aic.has_pending_interrupt(0), "Core 0 was not targeted");
    AIC_TEST_ASSERT(aic.has_pending_interrupt(1), "Core 1 must have pending IPI");
    AIC_TEST_ASSERT(aic.has_pending_interrupt(2), "Core 2 must have pending IPI");
    AIC_TEST_ASSERT(!aic.has_pending_interrupt(3), "Core 3 was not targeted");
    AIC_TEST_ASSERT(!aic.has_pending_interrupt(4), "Core 4 was not targeted");
    AIC_TEST_ASSERT(aic.has_pending_interrupt(5), "Core 5 must have pending IPI");

    // Acknowledge Core 1 IPI via IPI_ACK register
    const std::uint64_t ipi_ack_c1 = AppleAIC::kRegCoreBase + (1 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffIpiAck;
    aic.write_register(ipi_ack_c1, 0x01, 4);
    AIC_TEST_ASSERT(!aic.has_pending_interrupt(1), "Core 1 IPI must be cleared after ACK write");

    // Core 2 and 5 must still have their IPIs
    AIC_TEST_ASSERT(aic.has_pending_interrupt(2), "Core 2 IPI must still be active");
    AIC_TEST_ASSERT(aic.has_pending_interrupt(5), "Core 5 IPI must still be active");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Software Interrupt (SW_SET / SW_CLR) Triggering
// -----------------------------------------------------------------------------
bool test_aic2_software_interrupts() {
    std::cout << "[RUN] Test 7: Software Interrupt Triggering (SW_SET / SW_CLR) ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Unmask IRQ 200
    aic.unmask_hw_irq(200);
    aic.set_irq_target(200, 0x01);

    // Trigger SW IRQ 200 via SW_SET register (offset 0x8200 + 4*(200/32))
    const std::uint64_t sw_set_offset = AppleAIC::kRegSwSetBase + (200 / 32) * 4;
    const std::uint32_t sw_bit = 1u << (200 % 32);
    aic.write_register(sw_set_offset, sw_bit, 4);

    AIC_TEST_ASSERT(aic.has_pending_interrupt(0), "Core 0 must see SW IRQ 200");
    std::uint32_t ev = aic.pop_event(0);
    AIC_TEST_ASSERT((ev & 0xFFFFu) == 200, "Event vector should be 200");

    // Clear SW IRQ 200 via SW_CLR register (offset 0x8280 + 4*(200/32))
    const std::uint64_t sw_clr_offset = AppleAIC::kRegSwClrBase + (200 / 32) * 4;
    aic.write_register(sw_clr_offset, sw_bit, 4);

    AIC_TEST_ASSERT(!aic.has_pending_interrupt(0), "SW IRQ 200 must be cleared");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: End-Of-Interrupt (EOI) Event Code Decoding
// -----------------------------------------------------------------------------
bool test_aic2_eoi_lifecycle() {
    std::cout << "[RUN] Test 8: End-Of-Interrupt (EOI) Event Code Lifecycle ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Set Timer IRQ on Core 4
    aic.set_timer_irq(4, true);
    AIC_TEST_ASSERT(aic.has_pending_interrupt(4), "Core 4 should have pending timer interrupt");

    const std::uint64_t ev_reg_c4 = AppleAIC::kRegCoreBase + (4 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent;
    const std::uint64_t ev_val = aic.read_register(ev_reg_c4, 4);
    AIC_TEST_ASSERT(((ev_val >> 16) & 0xFFFFu) == AppleAIC::kEventTypeTimer, "Event read must be Timer type");

    // Write EOI to event register
    aic.write_register(ev_reg_c4, ev_val, 4);
    AIC_TEST_ASSERT(!aic.has_pending_interrupt(4), "Core 4 interrupt should be cleared after EOI");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: DeviceTree Node Representation & Driver Binding
// -----------------------------------------------------------------------------
bool test_aic2_devicetree_binding() {
    std::cout << "[RUN] Test 9: DeviceTree Representation & Driver Binding ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size(), &err);
    AIC_TEST_ASSERT(root != nullptr, "DTB root node must exist");

    const auto* armio = root->find_child("arm-io");
    AIC_TEST_ASSERT(armio != nullptr, "DTB /arm-io node must exist");

    const auto* aic_node = armio->find_child("aic");
    AIC_TEST_ASSERT(aic_node != nullptr, "DTB /arm-io/aic node must exist");

    // Check compatible string
    const auto* comp = aic_node->find_property("compatible");
    AIC_TEST_ASSERT(comp != nullptr, "AIC node must have compatible property");
    AIC_TEST_ASSERT(!comp->data.empty(), "Compatible property must have data");

    // Verify compatible contains "apple,aic-2"
    const char* str = reinterpret_cast<const char*>(comp->data.data());
    AIC_TEST_ASSERT(std::string(str) == "apple,aic-2", "First compatible string must be apple,aic-2");

    // Check interrupt-controller empty property and #interrupt-cells = 3
    AIC_TEST_ASSERT(aic_node->find_property("interrupt-controller") != nullptr, "interrupt-controller property required");
    const auto* cells = aic_node->find_property("#interrupt-cells");
    AIC_TEST_ASSERT(cells != nullptr, "#interrupt-cells property required");
    std::uint32_t cell_val = 0;
    std::memcpy(&cell_val, cells->data.data(), sizeof(cell_val));
    AIC_TEST_ASSERT(cell_val == 3u, "#interrupt-cells must be 3");

    // Check reg property: [0x08000000, 0x100000]
    const auto* reg = aic_node->find_property("reg");
    AIC_TEST_ASSERT(reg != nullptr, "reg property required");
    AIC_TEST_ASSERT(reg->data.size() >= 16, "reg must contain 64-bit base and size");
    std::uint64_t reg_vals[2] = {0, 0};
    std::memcpy(reg_vals, reg->data.data(), sizeof(reg_vals));
    AIC_TEST_ASSERT(reg_vals[0] == 0x08000000ULL, "AIC MMIO base in DTB must be 0x08000000");
    AIC_TEST_ASSERT(reg_vals[1] == 0x00100000ULL, "AIC MMIO size in DTB must be 0x100000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Darwin Platform Expert Driver Matching
// -----------------------------------------------------------------------------
bool test_aic2_driver_matching() {
    std::cout << "[RUN] Test 10: Darwin Platform Expert Driver Personality Match ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size(), &err);
    AIC_TEST_ASSERT(root != nullptr, "DTB root node must exist");

    AIC_TEST_ASSERT(pe.probe_and_match(*root, err), "probe_and_match should succeed");

    const auto* matched = pe.find_driver_by_path("/arm-io/aic");
    AIC_TEST_ASSERT(matched != nullptr, "Platform expert must find driver match for AIC");
    AIC_TEST_ASSERT(matched->personality.io_class == "AppleA16InterruptController", "Driver IOClass must be AppleA16InterruptController");
    AIC_TEST_ASSERT(matched->personality.probe_score == 50000, "Probe score must be 50000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: State Serialization & Snapshot Round-Trip
// -----------------------------------------------------------------------------
bool test_aic2_state_serialization() {
    std::cout << "[RUN] Test 11: AIC2 State Serialization & Snapshot Restore ... ";

    AppleAIC aic1(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);

    // Configure distinct state
    aic1.unmask_hw_irq(42);
    aic1.set_hw_irq(42, true);
    aic1.set_irq_target(42, 0x04); // Core 2
    aic1.send_ipi(0x02); // Core 1
    aic1.set_timer_irq(5, true); // Core 5

    const auto state = aic1.serialize_state();
    AIC_TEST_ASSERT(!state.empty(), "Serialized state must not be empty");

    AppleAIC aic2(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);
    AIC_TEST_ASSERT(aic2.deserialize_state(state.data(), state.size()), "Deserialization must succeed");

    // Verify restored state
    AIC_TEST_ASSERT(!aic2.is_hw_irq_masked(42), "Restored IRQ 42 must be unmasked");
    AIC_TEST_ASSERT(aic2.is_hw_irq_asserted(42), "Restored IRQ 42 must be asserted");
    AIC_TEST_ASSERT(aic2.get_irq_target(42) == 0x04, "Restored IRQ 42 target must be 0x04");
    AIC_TEST_ASSERT(aic2.has_pending_interrupt(2), "Core 2 must have pending interrupt");
    AIC_TEST_ASSERT(aic2.has_pending_interrupt(1), "Core 1 must have pending IPI");
    AIC_TEST_ASSERT(aic2.has_pending_interrupt(5), "Core 5 must have pending timer");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: CPU Exception Core Line Assertion
// -----------------------------------------------------------------------------
bool test_aic2_cpu_irq_line_integration() {
    std::cout << "[RUN] Test 12: AIC2 Core Interrupt to CPU Exception Line ... ";

    AppleAIC aic(0x41494332u, AppleAIC::kDefaultAicBase, AppleAIC::kDefaultAicSize, 6);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000000ULL);
    cpu.set_program_counter(0x2000ULL);
    cpu.set_current_el(1);
    cpu.set_sp_sel(1);
    cpu.set_daif(0x0u); // Unmask IRQ (I=0)

    Memory mem(0x200000);
    mem.write_u32(0x2000ULL, 0xD503201Fu); // NOP

    // Unmask IRQ 33 and route to Core 0
    aic.unmask_hw_irq(33);
    aic.set_irq_target(33, 0x01);
    aic.set_hw_irq(33, true);

    AIC_TEST_ASSERT(aic.has_pending_interrupt(0), "AIC must assert interrupt on Core 0");

    // When AIC asserts interrupt on Core 0, trigger asynchronous IRQ on CPU
    cpu.raise_asynchronous_exception(false /* is_fiq */, 33, false /* is_virtual */, 0);

    // Check that PC took the IRQ vector at VBAR + 0x280 (Current EL with SP_ELx)
    AIC_TEST_ASSERT(cpu.get_program_counter() == 0x80000280ULL, "CPU must dispatch to VBAR + 0x280 on AIC IRQ");
    AIC_TEST_ASSERT(cpu.get_elr_el1() == 0x2000ULL, "ELR_EL1 must hold return PC");
    AIC_TEST_ASSERT(cpu.get_daif() == 0xFu, "DAIF interrupts must be masked on vector entry");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 109: Apple Interrupt Controller (AIC2) Test Suite        \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 12;

    if (test_aic2_hw_info_and_config()) ++passed;
    if (test_aic2_per_core_banking()) ++passed;
    if (test_aic2_mask_registers()) ++passed;
    if (test_aic2_priority_arbitration()) ++passed;
    if (test_aic2_target_core_routing()) ++passed;
    if (test_aic2_ipi_lifecycle()) ++passed;
    if (test_aic2_software_interrupts()) ++passed;
    if (test_aic2_eoi_lifecycle()) ++passed;
    if (test_aic2_devicetree_binding()) ++passed;
    if (test_aic2_driver_matching()) ++passed;
    if (test_aic2_state_serialization()) ++passed;
    if (test_aic2_cpu_irq_line_integration()) ++passed;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
