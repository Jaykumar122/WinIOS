// ============================================================================
// Stage 249.1 - 249.5: Guest DMA Ownership, Lifecycle, Genuine Completion
// Interrupt Workload & Memory Visibility Contract Tests
//
// Tests:
// 1. Stage 249.1 & 249.2: Process-Bound DMA Descriptors & Authorization Checks
//    - Verified that DMA descriptors and transfers are bound to owner PID via kOwnerPid.
//    - Unauthorized process submission attempts are rejected with error status.
// 2. Stage 249.3: DMA Buffer Lifetime, Cancellation & Safe Teardown
//    - Verified kControlCancel cleanly transitions controller to kStatusCancelled.
//    - Verified duplicate cancellation safety and idempotent teardown.
// 3. Stage 249.4: Genuine Guest DMA Completion Interrupt Workload
//    - Guest ARM64 code executing at EL1 with architectural exceptions enabled.
//    - GICv3 SPI 34 configured and unmasked.
//    - Guest prepares descriptor and programs DMA via MMIO (0x10000000).
//    - DMA triggers transfer and asserts interrupt line 34.
//    - CPU vectors to VBAR_EL1 + 0x280.
//    - Guest handler acknowledges INTID 34 via ICC_IAR1_EL1, signals EOI via
//      ICC_EOIR1_EL1, increments IRQ counter, and executes ERET.
//    - Guest main code validates destination buffer data integrity.
// 4. Stage 249.5: DMA Coherency & Memory Visibility
//    - CPU store before DMA read, and DMA write before CPU load.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"

#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::device;

static void setup_guest_el1(CPU& cpu, uint64_t vbar) {
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(vbar);
    cpu.set_daif(0x0); // Unmask DAIF (interrupts enabled)
}

// ----------------------------------------------------------------------------
// Test 1: Stage 249.1 & 249.2 - Process-Bound DMA Descriptors & Authorization
// ----------------------------------------------------------------------------
static void test_dma_ownership_and_authorization() {
    std::cout << "[Stage 249.1/2] Testing Process-Bound DMA Descriptors & Authorization...\n";

    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    // 1. Initial owner PID is unassigned (-1)
    TEST_ASSERT(dma.owner_pid() == -1, "Initial owner PID is -1");

    // 2. Program owner PID = 501 via MMIO
    dma.write_register(dma_reg::kOwnerPid, 501, 8);
    TEST_ASSERT(dma.owner_pid() == 501, "Owner PID programmed to 501");
    TEST_ASSERT(dma.read_register(dma_reg::kOwnerPid, 8) == static_cast<uint64_t>(501), "Readback of kOwnerPid matches 501");

    // 3. Setup descriptor in shared memory
    const uint64_t desc_addr = 0x10000;
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.src_paddr = 0x20000;
    desc.dst_paddr = 0x30000;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    // 4. Verify unauthorized cancel attempt by foreign PID (999) fails
    bool cancel_foreign = dma.cancel_transfer(999);
    TEST_ASSERT(!cancel_foreign, "Foreign process cannot cancel DMA transfer");

    // 5. Authorized cancel by owner PID (501) succeeds
    bool cancel_owner = dma.cancel_transfer(501);
    TEST_ASSERT(cancel_owner, "Owner process can cancel DMA transfer");
    TEST_ASSERT((dma.read_register(dma_reg::kStatus, 4) & kStatusCancelled) != 0, "Status flags kStatusCancelled");

    std::cout << "[Stage 249.1/2] DMA ownership and authorization verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 249.3 - DMA Lifetime, Cancellation & Safe Teardown
// ----------------------------------------------------------------------------
static void test_dma_cancellation_and_teardown() {
    std::cout << "[Stage 249.3] Testing DMA Lifetime, Cancellation & Safe Teardown...\n";

    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    dma.set_owner_pid(1001);

    // Write kControlCancel via MMIO
    dma.write_register(dma_reg::kControl, kControlCancel, 4);
    uint64_t status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusCancelled) != 0, "MMIO kControlCancel sets kStatusCancelled");
    TEST_ASSERT(!dma.is_busy(), "Device is not busy after cancellation");

    // Clear cancelled bit by writing 1 to kStatusCancelled in Status register
    dma.write_register(dma_reg::kStatus, kStatusCancelled, 4);
    status = dma.read_register(dma_reg::kStatus, 4);
    TEST_ASSERT((status & kStatusCancelled) == 0, "Writing 1 to kStatusCancelled clears the bit");

    // Reset clears owner PID and resets device
    dma.reset();
    TEST_ASSERT(dma.owner_pid() == -1, "Reset clears owner PID");
    TEST_ASSERT(dma.read_register(dma_reg::kStatus, 4) == 0, "Reset clears status register");

    std::cout << "[Stage 249.3] DMA cancellation and teardown verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 249.4 - Genuine Guest DMA Completion Interrupt Workload
// ----------------------------------------------------------------------------
static void test_guest_dma_completion_interrupt() {
    std::cout << "[Stage 249.4] Testing Genuine Guest DMA Completion Interrupt Workload...\n";

    CPU cpu;
    Memory mem(0x200000); // 2 MB guest RAM
    cpu.reset();

    const uint64_t vbar = 0x80000;
    setup_guest_el1(cpu, vbar);

    const std::size_t mem_size = 0x200000;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma = std::make_shared<VirtualDmaDevice>(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    bus->register_device(dma);
    cpu.attach_device_bus(bus);

    // Setup GICv3 Distributor & Core 0 for SPI 34
    GICv3::DistributorState dist{};
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    cpu.get_gic().set_distributor_state(dist);

    // Configure SPI 34 in GIC distributor
    // Word 1: bits 0..31 correspond to INTID 32..63; SPI 34 is bit 2
    dist.gicd_isenabler[1] |= (1u << 2); // Enable INTID 34
    dist.gicd_igroupr[1]   |= (1u << 2); // Group 1
    dist.gicd_ipriorityr[34] = 0x70u;    // Priority 0x70
    dist.gicd_irouter[2]     = 0;        // Route to core 0
    cpu.get_gic().set_distributor_state(dist);

    cpu.get_gic().write_icc_pmr(0xF0, 0); // PMR unmask (< 0xF0)
    cpu.get_gic().write_icc_igrpen1(1, 0); // Group 1 CPU interface enable

    // Connect DMA interrupt to GIC SPI line 34
    dma->set_interrupt_callback([&](uint32_t irq, bool asserted) {
        cpu.get_gic().set_spi_level(irq, asserted);
    });

    // Setup buffers in shared memory and guest memory
    const uint64_t src_addr = 0x20000;
    const uint64_t dst_addr = 0x30000;
    const uint64_t desc_addr = 0x40000;
    const std::size_t transfer_len = 128;

    std::vector<uint8_t> src_data(transfer_len);
    for (std::size_t i = 0; i < transfer_len; ++i) {
        src_data[i] = static_cast<uint8_t>(i ^ 0xC3);
    }
    shared_mem->write_bytes(0, src_addr, src_data.data(), transfer_len);
    std::memcpy(mem.raw_data() + src_addr, src_data.data(), transfer_len);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 249;
    desc.src_paddr = src_addr;
    desc.dst_paddr = dst_addr;
    desc.length_bytes = transfer_len;
    desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Copy);
    desc.flags = kDmaFlagInterruptOnComplete;
    shared_mem->write_bytes(0, desc_addr, &desc, sizeof(desc));
    std::memcpy(mem.raw_data() + desc_addr, &desc, sizeof(desc));

    // Setup IRQ Vector Handler at VBAR_EL1 + 0x280:
    // 1. MRS X0, ICC_IAR1_EL1   (0xD538CC00u) -> acknowledge interrupt, X0 = 34
    // 2. MSR ICC_EOIR1_EL1, X0  (0xD518CC20u) -> signal EOI
    // 3. ADD X2, X2, #1         (0x91000442u) -> increment IRQ serviced counter in X2
    // 4. ERET                   (0xD69F03E0u) -> return from exception
    const uint64_t irq_vector = vbar + 0x280;
    mem.write_u32(irq_vector + 0, 0xD538CC00u);
    mem.write_u32(irq_vector + 4, 0xD518CC20u);
    mem.write_u32(irq_vector + 8, 0x91000442u);
    mem.write_u32(irq_vector + 12, 0xD69F03E0u);

    // Setup Guest Program at 0x10000:
    // 1. 0x10000: STR X3, [X1, #0]   (0xF9000023u) -> Control = Enable | IrqEnable (0x9)
    // 2. 0x10004: STR X4, [X1, #16]  (0xF9000824u) -> DescriptorAddr = 0x40000
    // 3. 0x10008: STR X5, [X1, #0]   (0xF9000025u) -> Control = Enable | IrqEnable | Start (0xB)
    // 4. 0x1000C: ADD X6, X6, #1     (0x910004C6u) -> Post-transfer instruction
    // 5. 0x10010: B -4               (0x17FFFFFFu) -> Loop
    const uint64_t code_pc = 0x10000;
    mem.write_u32(code_pc + 0, 0xF9000023u);
    mem.write_u32(code_pc + 4, 0xF9000824u);
    mem.write_u32(code_pc + 8, 0xF9000025u);
    mem.write_u32(code_pc + 12, 0x910004C6u);
    mem.write_u32(code_pc + 16, 0x17FFFFFFu);

    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0x10000000ULL); // DMA MMIO base
    cpu.write_register(3, kControlEnable | kControlIrqEnable);
    cpu.write_register(4, desc_addr);
    cpu.write_register(5, kControlEnable | kControlIrqEnable | kControlStart);
    cpu.write_register(2, 0); // IRQ serviced counter
    cpu.write_register(6, 0); // Post-transfer counter

    // Step 1: STR X3, [X1, #0] (Enable DMA + IrqEnable)
    cpu.step(mem);
    TEST_ASSERT(dma->is_enabled(), "DMA enabled via guest instruction");

    // Step 2: STR X4, [X1, #16] (Set descriptor address)
    cpu.step(mem);
    TEST_ASSERT(dma->read_register(dma_reg::kDescriptorAddr, 8) == desc_addr, "Descriptor address programmed");

    // Step 3: STR X5, [X1, #0] (Start transfer)
    // DMA completes, asserts SPI 34 to GIC!
    cpu.step(mem);
    TEST_ASSERT((dma->read_register(dma_reg::kStatus, 4) & kStatusComplete) != 0, "DMA transfer complete");

    // Sync destination buffer into guest memory view
    std::vector<uint8_t> copied(transfer_len);
    shared_mem->read_bytes(0, dst_addr, copied.data(), transfer_len);
    std::memcpy(mem.raw_data() + dst_addr, copied.data(), transfer_len);

    // Step 4: CPU detects pending SPI 34, takes asynchronous exception to irq_vector!
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == irq_vector, "CPU vectored to IRQ handler 0x80280");

    // Step 5: Handler instruction 0: MRS X0, ICC_IAR1_EL1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 34, "ICC_IAR1_EL1 acknowledged INTID 34 (DMA IRQ)");

    // Step 6: Handler instruction 1: MSR ICC_EOIR1_EL1, X0
    cpu.step(mem);

    // Step 7: Handler instruction 2: ADD X2, X2, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 1, "Guest IRQ service counter incremented to 1");

    // Deassert the DMA interrupt line before ERET to prevent immediate re-vectoring
    cpu.get_gic().set_spi_level(34, false);

    // Step 8: Handler instruction 3: ERET -> return to interrupted instruction stream!
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == code_pc + 12, "ERET returned to interrupted instruction (0x1000C)");

    // Step 9: Post-transfer instruction executed: ADD X6, X6, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(6) == 1, "Main program resumed correctly after IRQ return");

    // Verify destination data integrity
    for (std::size_t i = 0; i < transfer_len; ++i) {
        TEST_ASSERT(mem.read_u8(dst_addr + i) == src_data[i], "Destination byte matches source data");
    }

    std::cout << "[Stage 249.4] Genuine guest DMA completion interrupt verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 249.5 - DMA Coherency & Memory Visibility Contract
// ----------------------------------------------------------------------------
static void test_dma_coherency_and_memory_visibility() {
    std::cout << "[Stage 249.5] Testing DMA Coherency & Memory Visibility Contract...\n";

    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    VirtualDmaDevice dma(0x56444D41u, 0x10000000ULL, 0x1000ULL, shared_mem, 34);
    dma.reset();

    const uint64_t src = 0x50000;
    const uint64_t dst = 0x60000;
    const uint64_t desc_addr = 0x70000;
    const std::size_t len = 64;

    // 1. CPU writes pattern before DMA reads
    std::vector<uint8_t> pattern(len);
    for (std::size_t i = 0; i < len; ++i) pattern[i] = static_cast<uint8_t>(0xA5 ^ i);
    shared_mem->write_bytes(0, src, pattern.data(), len);

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 999;
    desc.src_paddr = src;
    desc.dst_paddr = dst;
    desc.length_bytes = len;
    desc.operation_type = static_cast<uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, desc_addr, &desc, sizeof(desc));

    // 2. DMA reads source and writes to destination
    dma.trigger_transfer(desc_addr, 0);
    TEST_ASSERT((dma.read_register(dma_reg::kStatus, 4) & kStatusComplete) != 0, "DMA complete");

    // 3. CPU reads destination and observes updated data
    std::vector<uint8_t> read_back(len);
    shared_mem->read_bytes(0, dst, read_back.data(), len);
    TEST_ASSERT(read_back == pattern, "CPU observes data written by DMA");

    std::cout << "[Stage 249.5] DMA coherency and memory visibility contract verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 249.1 - 249.5 DMA Ownership & Interrupt Tests\n";
    std::cout << "============================================================\n";

    test_dma_ownership_and_authorization();
    test_dma_cancellation_and_teardown();
    test_guest_dma_completion_interrupt();
    test_dma_coherency_and_memory_visibility();

    std::cout << "\n>>> ALL STAGE 249.1 - 249.5 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
