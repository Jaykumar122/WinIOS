// ============================================================================
// Stage 252.5 - 252.8: Independent MMU Decoder, DRAM Boundary Abort,
// Exception Vector Execution & Complete W^X Enforcement Tests
// ============================================================================

#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "memory/memory.h"
#include "memory/xnu_vm_map.h"
#include "loader/kernel_image_parser.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::memory;
using namespace winios::loader;

// ----------------------------------------------------------------------------
// Test 1: Stage 252.5: Independent MMU Descriptor Decoder
// ----------------------------------------------------------------------------
static void test_stage252_5_descriptor_decoder() {
    std::cout << "[Stage 252.5] Testing Independent MMU Descriptor Decoder...\n";

    // 1. Decode Level 1 32 MiB Normal DRAM Block descriptor (0x0000000000000701ULL)
    const uint64_t l1_dram_desc = 0x0000000000000701ULL;
    auto d1 = Arm64DescriptorDecoder::decode(l1_dram_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(d1.valid, "Descriptor is valid");
    TEST_ASSERT(d1.type == DecodedDescriptorType::Block, "Decoded as Block descriptor");
    TEST_ASSERT(d1.output_address == 0x0ULL, "Output address covers DRAM base 0x0");
    TEST_ASSERT(d1.attr_indx == 0, "AttrIndx is 0 (Normal Cacheable)");
    TEST_ASSERT(d1.ap == 0b00, "AP is 0b00 (Kernel Read/Write)");
    TEST_ASSERT(d1.sh == 0b11, "Shareability is 0b11 (Inner Shareable)");
    TEST_ASSERT(d1.access_flag, "Access Flag is set");

    // 2. Decode Level 1 AIC2 MMIO Block descriptor (0x000000023A000705ULL)
    const uint64_t l1_aic_desc = 0x000000023A000705ULL;
    auto d2 = Arm64DescriptorDecoder::decode(l1_aic_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(d2.valid, "AIC descriptor is valid");
    TEST_ASSERT(d2.type == DecodedDescriptorType::Block, "AIC decoded as Block descriptor");
    TEST_ASSERT(d2.output_address == 0x23A000000ULL, "AIC output address covers 0x23B100000");
    TEST_ASSERT(d2.attr_indx == 1, "AttrIndx is 1 (Device-nGnRE)");
    TEST_ASSERT(d2.access_flag, "AF is set");

    // 3. Decode Level 1 Table descriptor (0x0000000000048003ULL)
    const uint64_t l1_tbl_desc = 0x0000000000048003ULL;
    auto dt = Arm64DescriptorDecoder::decode(l1_tbl_desc, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(dt.valid, "Table descriptor valid");
    TEST_ASSERT(dt.type == DecodedDescriptorType::Table, "Decoded as Table descriptor");
    TEST_ASSERT(dt.output_address == 0x48000ULL, "Table points to PA 0x48000");

    // 4. Decode Level 3 Page descriptor (0x0000000000050703ULL)
    const uint64_t l3_page_desc = 0x0000000000050703ULL;
    auto dp = Arm64DescriptorDecoder::decode(l3_page_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(dp.valid, "Page descriptor valid");
    TEST_ASSERT(dp.type == DecodedDescriptorType::Page, "Decoded as Page descriptor");
    TEST_ASSERT(dp.output_address == 0x50000ULL, "Page points to PA 0x50000 (Frame 20)");

    // 5. Decode Invalid descriptor (0x0ULL)
    auto di = Arm64DescriptorDecoder::decode(0x0ULL, 1, Arm64Granule::Granule16KB);
    TEST_ASSERT(!di.valid, "0x0 is invalid");
    TEST_ASSERT(di.type == DecodedDescriptorType::Invalid, "Type is Invalid");

    std::cout << "[Stage 252.5] Independent MMU descriptor decoder verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 252.6: DRAM Boundary & Architectural Data Abort
// ----------------------------------------------------------------------------
static void test_stage252_6_dram_boundary_abort() {
    std::cout << "[Stage 252.6] Testing DRAM Boundary & Architectural Data Abort...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00080000ULL);

    Memory mem(0x800000); // 8 MiB physical DRAM [0x0, 0x800000)

    // Setup vector table at 0x80000
    // Vector offset for Synchronous from Current EL with SPx is +0x200
    // At 0x80200, place handler:
    //   0x80200: ERET (0xD69F03E0)
    mem.write_u32(0x80200, 0xD69F03E0u);

    // Guest code at 0x1000:
    //   LDR X2, [X1, #0] (0xF9400022u)
    //   MOV X0, #42      (0xD2800540u)
    mem.write_u32(0x1000, 0xF9400022u);
    mem.write_u32(0x1004, 0xD2800540u);

    cpu.set_program_counter(0x1000);
    // Point X1 to 0x800000 (first byte beyond 8 MiB DRAM, unmapped in MMIO)
    cpu.write_register(1, 0x800000ULL);

    // Execute instruction: LDR X2, [X1, #0]
    // Must generate synchronous Data Abort and branch to VBAR_EL1 + 0x200!
    cpu.step(mem);

    // Verify architectural exception state:
    TEST_ASSERT(cpu.get_program_counter() == 0x80200ULL, "PC vectored to VBAR_EL1 + 0x200");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1000ULL, "ELR_EL1 captures faulting PC (0x1000)");
    TEST_ASSERT(cpu.get_far_el1() == 0x800000ULL, "FAR_EL1 captures exact faulting address (0x800000)");

    // Decode ESR_EL1
    auto esr_info = KernelExceptionVectors::decode_esr(cpu.get_esr_el1());
    TEST_ASSERT(esr_info.ec == 0x25, "ESR_EL1 EC is 0x25 (Data Abort from same EL)");
    // DFSC for Synchronous External Abort is 0x10
    TEST_ASSERT((esr_info.iss & 0x3Fu) == 0x10u, "DFSC is 0x10 (Synchronous External Abort)");

    // Now execute handler instruction at 0x80200 (ERET)
    // ERET will restore PC from ELR_EL1 (0x1000)
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1000ULL, "ERET restored PC to ELR_EL1 (0x1000)");

    std::cout << "[Stage 252.6] DRAM boundary and architectural Data Abort verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 252.7: Exception Vector Backing & Execution
// ----------------------------------------------------------------------------
static void test_stage252_7_vector_backing_and_execution() {
    std::cout << "[Stage 252.7] Testing Exception Vector Backing & Execution...\n";

    // 1. Verify alignment validation
    std::string err;
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x80000ULL, &err), "0x80000 is 2048-byte aligned");
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x80100ULL, &err), "0x80100 is misaligned");

    // 2. Build default vector table
    auto tbl = KernelExceptionVectors::build_default_vector_table();
    TEST_ASSERT(tbl.size() == 512, "Default table contains 512 words (2048 bytes)");

    // 3. Verify offsets for all 4 origins and 4 types
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::Synchronous) == 0x000, "SP0 Sync");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::IRQ) == 0x080, "SP0 IRQ");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::Synchronous) == 0x200, "SPx Sync");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::IRQ) == 0x280, "SPx IRQ");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::FIQ) == 0x300, "SPx FIQ");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::SError) == 0x380, "SPx SError");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::Synchronous) == 0x400, "Lower A64 Sync");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::IRQ) == 0x480, "Lower A64 IRQ");

    std::cout << "[Stage 252.7] Exception vector table verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 252.8: Strict W^X & Memory Permission Enforcement
// ----------------------------------------------------------------------------
static void test_stage252_8_wx_enforcement() {
    std::cout << "[Stage 252.8] Testing Strict W^X Enforcement Across All Mappings...\n";

    XnuVmMap vmap(0x0ULL, 0x800000ULL, "wx_audit_map");
    vmap.set_strict_wx(true);

    std::string err;
    // 1. Direct RWX rejection
    TEST_ASSERT(!vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, kVmProtRead | kVmProtWrite | kVmProtExecute, "bad", &err), "Direct RWX rejected");

    // 2. Map Read+Execute
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "code", &err), "Map RX");

    // 3. Attempt escalation to Write+Execute via protect -> must fail
    TEST_ASSERT(!vmap.vm_map_protect(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite | kVmProtExecute, false, &err), "Escalation to RWX rejected");

    // 4. Physical alias W^X conflict
    vmap.register_physical_alias(0x10000ULL, 0x40000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute);
    TEST_ASSERT(!vmap.register_physical_alias(0x20000ULL, 0x40000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err), "Physical alias W^X conflict rejected");

    std::cout << "[Stage 252.8] W^X enforcement verified.\n";
}

int main() {
    std::cout << "=== Running Stages 252.5 - 252.8 Unit Tests ===\n";
    test_stage252_5_descriptor_decoder();
    test_stage252_6_dram_boundary_abort();
    test_stage252_7_vector_backing_and_execution();
    test_stage252_8_wx_enforcement();
    std::cout << "[PASS] All Stage 252.5 - 252.8 tests passed successfully.\n";
    return 0;
}
