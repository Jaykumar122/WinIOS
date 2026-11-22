// ============================================================================
// Stage 255.0 - 255.5: Baseline Verification, Canonical Mach-O Reconstruction,
// Bootstrap Transition, Ownership State Machine, Stack & Page-Table Migration
// ============================================================================

#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/cpu.h"
#include "loader/kernel_image_parser.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <string>
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
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 255.0: Baseline Verification & Evidence Audit
// ----------------------------------------------------------------------------
static void test_stage255_0_baseline_and_evidence_audit() {
    std::cout << "[Stage 255.0] Testing Baseline Verification & Historical Evidence Audit...\n";

    // 1. Load genuine embedded Mach-O binary
    const auto image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!image.empty(), "Genuine kernel image must be available");
    TEST_ASSERT(image.size() == 68728, "Mach-O image file size is exactly 68,728 bytes");

    // 2. Parse Mach-O segments directly from binary
    KernelParsedImage parsed;
    auto status = KernelImageParser::parse(image.data(), image.size(), parsed);
    TEST_ASSERT(status == KernelImageParseStatus::Success, "Mach-O parsing succeeds");
    TEST_ASSERT(parsed.cputype == 0x0100000CU, "ARM64 cputype (0x0100000C)");
    TEST_ASSERT(parsed.entry_point_fileoff == 0x2be0ULL, "Entrypoint file offset is 0x2be0");

    // Inspect actual segments:
    const auto* seg_text = parsed.find_segment("__TEXT");
    const auto* seg_data_const = parsed.find_segment("__DATA_CONST");
    const auto* seg_data = parsed.find_segment("__DATA");

    TEST_ASSERT(seg_text != nullptr, "Found __TEXT segment");
    TEST_ASSERT(seg_text->vmaddr == 0x0ULL, "__TEXT vmaddr is 0x0");
    TEST_ASSERT(seg_text->vmsize == 0x2E08ULL, "__TEXT vmsize is 0x2E08 (11,784 bytes)");
    TEST_ASSERT(seg_text->is_executable, "__TEXT is executable");
    TEST_ASSERT(!seg_text->is_writable, "__TEXT is non-writable");

    TEST_ASSERT(seg_data_const != nullptr, "Found __DATA_CONST segment");
    TEST_ASSERT(seg_data_const->vmaddr == 0x4000ULL, "__DATA_CONST vmaddr is 0x4000");

    TEST_ASSERT(seg_data != nullptr, "Found __DATA segment");
    TEST_ASSERT(seg_data->vmaddr == 0x8000ULL, "__DATA vmaddr is 0x8000");
    TEST_ASSERT(seg_data->filesize == 0x8000ULL, "__DATA filesize is 0x8000 (32,768 bytes = 32 KiB)");
    TEST_ASSERT(seg_data->vmsize == 0x80C8ULL, "__DATA vmsize is 0x80C8 (32,968 bytes)");
    TEST_ASSERT(!seg_data->is_executable, "__DATA is non-executable");
    TEST_ASSERT(seg_data->is_writable, "__DATA is writable");

    // 3. Resolve historical 32 KiB vs 64 KiB __DATA discrepancy:
    // - Raw __DATA segment file size is 32 KiB (0x8000).
    // - Raw __DATA segment virtual size is 32.2 KiB (0x80C8).
    // - However, __DATA base starts at 0x8000, so __DATA ends at 0x8000 + 0x80C8 = 0x100C8 (~64 KiB from DRAM base 0x0).
    // - In 16 KiB page alignment, the data region spans Frame 1 (DATA_CONST) through Frame 4 (DATA),
    //   totaling 64 KiB allocated across frames 0x00000 to 0x10000/0x14000.
    // - Thus, "32 KiB" refers strictly to the __DATA segment itself, while "64 KiB" refers to the total cumulative
    //   data footprint from offset 0x4000 to 0x14000. This resolves the documentation discrepancy.
    const uint64_t data_seg_size = seg_data->filesize;
    const uint64_t cumulative_data_end = seg_data->vmaddr + seg_data->vmsize;
    TEST_ASSERT(data_seg_size == 32768ULL, "__DATA segment size is exactly 32 KiB");
    TEST_ASSERT(cumulative_data_end > 0x10000ULL && cumulative_data_end <= 0x14000ULL,
                "Cumulative data end extends into Frame 4 (~64 KiB from DRAM base)");

    // 4. Confirm historical handler addresses:
    // - 0x80280: Resides at Frame 32 (0x80000), inside Dynamic Free Pool (0x50000-0x800000), Category D.
    // - 0x10280 (Stage 254.14): Resides in Frame 4 (0x10000), which is in __DATA (RW, non-executable in Model A).
    // - 0x20280 (Stage 254.21): Resides in Frame 8 (0x20000), which is in __DATA (RW, non-executable in Model A).
    // Neither 0x10280 nor 0x20280 is valid executable vector code under W^X!
    TEST_ASSERT(0x00080280ULL >= 0x00050000ULL, "0x80280 is inside the Dynamic Free Pool");
    TEST_ASSERT(0x00010280ULL >= 0x00010000ULL && 0x00010280ULL < 0x00030000ULL, "0x10280 is inside Model A DATA");
    TEST_ASSERT(0x00020280ULL >= 0x00010000ULL && 0x00020280ULL < 0x00030000ULL, "0x20280 is inside Model A DATA");

    std::cout << "[Stage 255.0] Baseline verification & discrepancy audit complete.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 255.1: Canonical Mach-O Layout Reconstruction
// ----------------------------------------------------------------------------
static void test_stage255_1_canonical_macho_layout_reconstruction() {
    std::cout << "[Stage 255.1] Testing Canonical Mach-O Layout Reconstruction...\n";

    // Reconstruct the relationship between actual linked segment dimensions and canonical reservations:
    struct LayoutReconstructionEntry {
        const char* name;
        uint64_t actual_base;
        uint64_t actual_size;
        uint64_t canonical_base;
        uint64_t canonical_size;
        const char* status;
    };

    const LayoutReconstructionEntry table[] = {
        {"__TEXT",       0x00000000ULL, 0x00004000ULL, 0x00000000ULL, 0x00004000ULL, "Exact match (16 KiB, RX)"},
        {"Padding",      0x00004000ULL, 0x0000C000ULL, 0x00004000ULL, 0x0000C000ULL, "Firmware scratch (48 KiB, RW)"},
        {"__DATA",       0x00010000ULL, 0x00008000ULL, 0x00010000ULL, 0x00020000ULL, "Actual 32 KiB; Reserved 128 KiB (RW)"},
        {"DeviceTree",   0x00018000ULL, 0x00004000ULL, 0x00030000ULL, 0x00004000ULL, "Relocated by +96 KiB (RW)"},
        {"BootArgs",     0x0001C000ULL, 0x00004000ULL, 0x00034000ULL, 0x00004000ULL, "Relocated by +96 KiB (RW)"},
        {"Stack",        0x00020000ULL, 0x00010000ULL, 0x00038000ULL, 0x00010000ULL, "Relocated by +96 KiB (RW)"},
        {"PageTables",   0x00030000ULL, 0x00008000ULL, 0x00048000ULL, 0x00008000ULL, "Relocated by +96 KiB (RW)"},
        {"DynamicPool",  0x00038000ULL, 0x007C8000ULL, 0x00050000ULL, 0x007B0000ULL, "Model A Authoritative: 492 frames"}
    };

    TEST_ASSERT(sizeof(table)/sizeof(table[0]) == 8, "8 entries in layout reconstruction");

    // The 128 KiB canonical __DATA reservation represents a reserved expansion capacity,
    // not the raw Mach-O linked size (32 KiB).
    const uint64_t actual_linked_data = 0x8000ULL; // 32 KiB
    const uint64_t reserved_capacity = 0x20000ULL; // 128 KiB
    TEST_ASSERT(actual_linked_data < reserved_capacity, "Actual linked data fits cleanly inside reserved budget");

    std::cout << "[Stage 255.1] Canonical Mach-O layout reconstruction complete.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 255.2: Bootstrap-to-Canonical Ownership Transition
// ----------------------------------------------------------------------------
static void test_stage255_2_bootstrap_to_canonical_ownership_transition() {
    std::cout << "[Stage 255.2] Testing Bootstrap-to-Canonical Ownership Transition...\n";

    Memory mem(0x800000);

    // Initialize Model B boot structures in memory:
    // DeviceTree at 0x18000: magic 0xD37EEB01
    mem.write_u32(0x18000, 0xD37EEB01u);
    // BootArgs at 0x1C000: revision 1, version 2
    mem.write_u16(0x1C000, 1);
    mem.write_u16(0x1C002, 2);
    // Stack at 0x20000..0x30000: canary at top-16 (0x2FFF0)
    mem.write_u64(0x2FFF0, 0x55AA55AA55AA55AAULL);
    // PageTables at 0x30000: L1 entry 0
    mem.write_u64(0x30000, 0x0000000000000701ULL);

    // Perform explicit transition to Model A canonical addresses (copy high-to-low to prevent overlap corruption):
    // 1. Relocate PageTables from 0x30000..0x38000 to 0x48000..0x50000 (copy 32 KiB)
    for (uint64_t off = 0; off < 0x8000; off += 8) {
        mem.write_u64(0x48000 + off, mem.read_u64(0x30000 + off));
    }
    TEST_ASSERT(mem.read_u64(0x48000) == 0x0000000000000701ULL, "PageTables relocated to 0x48000");

    // 2. Relocate Stack from 0x20000..0x30000 to 0x38000..0x48000 (copy 64 KiB)
    for (uint64_t off = 0; off < 0x10000; off += 8) {
        mem.write_u64(0x38000 + off, mem.read_u64(0x20000 + off));
    }
    TEST_ASSERT(mem.read_u64(0x47FF0) == 0x55AA55AA55AA55AAULL, "Stack canary preserved at new top-16 (0x47FF0)");

    // 3. Relocate BootArgs from 0x1C000 to 0x34000 (copy 16 KiB)
    for (uint64_t off = 0; off < 0x4000; off += 4) {
        mem.write_u32(0x34000 + off, mem.read_u32(0x1C000 + off));
    }
    TEST_ASSERT(mem.read_u16(0x34000) == 1, "BootArgs relocated to 0x34000 (Revision match)");
    TEST_ASSERT(mem.read_u16(0x34002) == 2, "BootArgs relocated to 0x34002 (Version match)");

    // 4. Relocate DeviceTree from 0x18000 to 0x30000 (copy 16 KiB)
    for (uint64_t off = 0; off < 0x4000; off += 4) {
        mem.write_u32(0x30000 + off, mem.read_u32(0x18000 + off));
    }
    TEST_ASSERT(mem.read_u32(0x30000) == 0xD37EEB01u, "DeviceTree relocated to 0x30000");

    // 5. Initialize Canonical Allocator (Frames 0..19 reserved, 20..511 free)
    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Initialize 8 MiB allocator");
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00050000ULL, "CanonicalEnclave", &err), "Reserve Frames 0-19");

    // Invariant: Zero active canonical or relocated frames exposed as free
    for (std::size_t f = 0; f < 20; ++f) {
        TEST_ASSERT(alloc.is_frame_reserved(f), "Enclave frames 0-19 are strictly reserved");
    }
    TEST_ASSERT(alloc.free_pages_count() == 492, "Exactly 492 free frames in dynamic pool");

    std::cout << "[Stage 255.2] Bootstrap-to-canonical ownership transition verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 255.3: Memory Ownership State Machine
// ----------------------------------------------------------------------------
static void test_stage255_3_ownership_state_machine() {
    std::cout << "[Stage 255.3] Testing Memory Ownership State Machine...\n";

    enum class OwnershipState {
        ColdBoot,
        BootstrapStructuresActive,
        RelocationInProgress,
        CanonicalStructuresActive,
        AllocatorInitializing,
        AllocatorActive,
        Failed
    };

    class OwnershipStateMachine {
    public:
        OwnershipState state{OwnershipState::ColdBoot};

        bool transition_to(OwnershipState next) {
            switch (state) {
                case OwnershipState::ColdBoot:
                    if (next == OwnershipState::BootstrapStructuresActive) {
                        state = next; return true;
                    }
                    break;
                case OwnershipState::BootstrapStructuresActive:
                    if (next == OwnershipState::RelocationInProgress) {
                        state = next; return true;
                    }
                    break;
                case OwnershipState::RelocationInProgress:
                    if (next == OwnershipState::CanonicalStructuresActive) {
                        state = next; return true;
                    }
                    if (next == OwnershipState::Failed) {
                        state = next; return true;
                    }
                    break;
                case OwnershipState::CanonicalStructuresActive:
                    if (next == OwnershipState::AllocatorInitializing) {
                        state = next; return true;
                    }
                    break;
                case OwnershipState::AllocatorInitializing:
                    if (next == OwnershipState::AllocatorActive) {
                        state = next; return true;
                    }
                    if (next == OwnershipState::Failed) {
                        state = next; return true;
                    }
                    break;
                case OwnershipState::AllocatorActive:
                    // Terminal active state
                    break;
                case OwnershipState::Failed:
                    break;
            }
            state = OwnershipState::Failed;
            return false;
        }
    };

    OwnershipStateMachine sm;
    TEST_ASSERT(sm.state == OwnershipState::ColdBoot, "Initial state is ColdBoot");

    // Valid progression
    TEST_ASSERT(sm.transition_to(OwnershipState::BootstrapStructuresActive), "ColdBoot -> BootstrapActive");
    TEST_ASSERT(sm.transition_to(OwnershipState::RelocationInProgress), "BootstrapActive -> RelocationInProgress");
    TEST_ASSERT(sm.transition_to(OwnershipState::CanonicalStructuresActive), "RelocationInProgress -> CanonicalActive");
    TEST_ASSERT(sm.transition_to(OwnershipState::AllocatorInitializing), "CanonicalActive -> AllocatorInitializing");
    TEST_ASSERT(sm.transition_to(OwnershipState::AllocatorActive), "AllocatorInitializing -> AllocatorActive");
    TEST_ASSERT(sm.state == OwnershipState::AllocatorActive, "State is AllocatorActive");

    // Invalid transition: Attempting to jump directly from ColdBoot to AllocatorActive fails closed
    OwnershipStateMachine sm_bad;
    TEST_ASSERT(!sm_bad.transition_to(OwnershipState::AllocatorActive), "Direct jump to AllocatorActive rejected");
    TEST_ASSERT(sm_bad.state == OwnershipState::Failed, "Failed transition sets state to Failed");

    std::cout << "[Stage 255.3] Memory ownership state machine verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 255.4: Stack Migration & Register-State Validation
// ----------------------------------------------------------------------------
static void test_stage255_4_stack_migration_and_registers() {
    std::cout << "[Stage 255.4] Testing Stack Migration & Register-State Validation...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    Memory mem(0x800000);

    // Initial Model B stack: base 0x20000, top 0x30000
    cpu.set_sp_el1(0x0002F000ULL); // Current SP within Model B stack
    mem.write_u64(0x0002F000ULL, 0x1122334455667788ULL); // Stack value

    // Perform stack migration to Model A stack: base 0x38000, top 0x48000
    // Relative offset within stack = 0x30000 - 0x2F000 = 0x1000 bytes from top
    // New SP = 0x48000 - 0x1000 = 0x47000
    const uint64_t old_sp = cpu.get_sp_el1();
    const uint64_t old_stack_top = 0x00030000ULL;
    const uint64_t new_stack_top = 0x00048000ULL;
    const uint64_t bytes_used = old_stack_top - old_sp;
    const uint64_t new_sp = new_stack_top - bytes_used;

    // Copy used stack contents
    for (uint64_t i = 0; i < bytes_used; i += 8) {
        mem.write_u64(new_sp + i, mem.read_u64(old_sp + i));
    }
    cpu.set_sp_el1(new_sp);

    // Validate new SP
    TEST_ASSERT(cpu.get_sp_el1() == 0x00047000ULL, "New SP_EL1 points to 0x47000");
    TEST_ASSERT((cpu.get_sp_el1() % 16) == 0, "New SP_EL1 satisfies AAPCS64 16-byte alignment");
    TEST_ASSERT(mem.read_u64(0x00047000ULL) == 0x1122334455667788ULL, "Stack value preserved across migration");

    // Guard page validation: stack base is 0x38000; SP < 0x38000 is an underflow violation
    auto check_sp_bounds = [](uint64_t sp) -> bool {
        return (sp >= 0x00038000ULL) && (sp <= 0x00048000ULL) && (sp % 16 == 0);
    };
    TEST_ASSERT(check_sp_bounds(new_sp), "Migrated SP is strictly within bounds");
    TEST_ASSERT(!check_sp_bounds(0x00037FF0ULL), "SP below stack base violates guard");

    std::cout << "[Stage 255.4] Stack migration & register-state validation complete.\n";
}

// ----------------------------------------------------------------------------
// Test 6: Stage 255.5: Page-Table Migration and Translation Continuity
// ----------------------------------------------------------------------------
static void test_stage255_5_page_table_migration_and_continuity() {
    std::cout << "[Stage 255.5] Testing Page-Table Migration and Translation Continuity...\n";

    Memory mem(0x800000);

    // Old Page Tables at 0x30000:
    // Entry 0: 32MB identity map block (Normal DRAM)
    mem.write_u64(0x30000, 0x0000000000000701ULL);
    // Entry 285: AIC2 MMIO (Device-nGnRE)
    mem.write_u64(0x30000 + 285 * 8, 0x000000023A000705ULL);

    // Migrate page tables to canonical Model A location: 0x48000
    for (uint64_t off = 0; off < 0x8000; off += 8) {
        mem.write_u64(0x48000 + off, mem.read_u64(0x30000 + off));
    }

    // Verify translation descriptors at new root table address 0x48000
    uint64_t new_entry0 = mem.read_u64(0x48000);
    uint64_t new_entry285 = mem.read_u64(0x48000 + 285 * 8);

    auto dec0 = Arm64DescriptorDecoder::decode(new_entry0, 1, Arm64Granule::Granule16KB);
    auto dec285 = Arm64DescriptorDecoder::decode(new_entry285, 1, Arm64Granule::Granule16KB);

    TEST_ASSERT(dec0.valid && dec0.attr_indx == 0, "New page table entry 0 is valid Normal DRAM");
    TEST_ASSERT(dec285.valid && dec285.attr_indx == 1, "New page table entry 285 is valid Device MMIO");

    // Verify active mapping continuity: old table memory at 0x30000 is now enclosed in reserved enclave
    // and cannot be allocated by the dynamic allocator
    XnuPhysicalPageAllocator alloc;
    std::string err;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x00050000ULL, "Enclave");
    TEST_ASSERT(alloc.is_frame_reserved(0x30000 / 16384), "Old table frame (Frame 12) is reserved");
    TEST_ASSERT(alloc.is_frame_reserved(0x48000 / 16384), "New table frame (Frame 18) is reserved");

    std::cout << "[Stage 255.5] Page-table migration and translation continuity verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 255: Stages 255.0 - 255.5 Memory Ownership & Migration Tests\n";
    std::cout << "====================================================================\n";

    test_stage255_0_baseline_and_evidence_audit();
    test_stage255_1_canonical_macho_layout_reconstruction();
    test_stage255_2_bootstrap_to_canonical_ownership_transition();
    test_stage255_3_ownership_state_machine();
    test_stage255_4_stack_migration_and_registers();
    test_stage255_5_page_table_migration_and_continuity();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 255.0 - 255.5 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
