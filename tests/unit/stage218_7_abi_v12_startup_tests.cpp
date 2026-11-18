#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>

#include "runtime/guest_app_contract.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_stack.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::runtime;

void test_argblock_memory_mapping_and_guest_read() {
    std::cout << "[Test 1] Stage 218.7: ABI v1.2 GuestAppArgBlock memory mapping and guest CPU read...\n";

    // 1. Verify struct size and field offsets
    static_assert(sizeof(GuestAppArgBlock) == 96, "GuestAppArgBlock must be exactly 96 bytes");
    TEST_ASSERT(offsetof(GuestAppArgBlock, contract_version) == 0);
    TEST_ASSERT(offsetof(GuestAppArgBlock, process_id) == 4);
    TEST_ASSERT(offsetof(GuestAppArgBlock, heap_base) == 32);
    TEST_ASSERT(offsetof(GuestAppArgBlock, heap_limit) == 40);

    // 2. Setup virtual platform
    Memory mem(0x800000); // 8 MB
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "abi_startup_vm");

    // 3. Map Code page at 0x100000000
    uint64_t code_va = 0x100000000ULL;
    uint64_t code_pa = ppm.allocate_page();
    uas.map_user_page(code_va, code_pa, memory::PagePermissions::UserExecute);

    // 4. Map Arg Block page at 0x100020000
    uint64_t arg_va = 0x100020000ULL;
    uint64_t arg_pa = ppm.allocate_page();
    uas.map_user_page(arg_va, arg_pa, memory::PagePermissions::UserReadWrite);

    // 5. Populate GuestAppArgBlock in guest memory
    GuestAppArgBlock args = GuestAppContract::CreateDefault(
        42,                 // pid
        code_va,            // app_base
        0x100040000ULL,     // stack_base (16-byte aligned)
        0x10000ULL,         // stack_size (64 KB)
        0x200000000ULL,     // heap_base
        0x100000ULL         // heap_size (1 MB)
    );

    // Write args directly to guest physical RAM at arg_pa
    for (size_t b = 0; b < sizeof(args); ++b) {
        mem.write_u8(static_cast<size_t>(arg_pa + b), reinterpret_cast<const uint8_t*>(&args)[b]);
    }

    // 6. Write ARM64 guest instructions at code_pa:
    // Instruction 1: LDR W1, [X0, #0]   ; Load contract_version into W1  -> 0xB9400001
    // Instruction 2: LDR W2, [X0, #4]   ; Load process_id into W2       -> 0xB9400402
    // Instruction 3: LDR X3, [X0, #32]  ; Load heap_base into X3        -> 0xF9401003
    // Instruction 4: LDR X4, [X0, #40]  ; Load heap_limit into X4       -> 0xF9401404
    // Instruction 5: HLT #0             ; Halt                          -> 0xD4400000
    mem.write_u32(static_cast<size_t>(code_pa + 0),  0xB9400001u);
    mem.write_u32(static_cast<size_t>(code_pa + 4),  0xB9400402u);
    mem.write_u32(static_cast<size_t>(code_pa + 8),  0xF9401003u);
    mem.write_u32(static_cast<size_t>(code_pa + 12), 0xF9401404u);
    mem.write_u32(static_cast<size_t>(code_pa + 16), 0xD4400000u);

    // 7. Setup CPU
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    // Setup MMU for 16KB translation
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    // Initialize register state:
    // X0 = arg_va
    // SP_EL0 = 16-byte aligned
    // PC = code_va
    cpu.set_current_el(0);
    cpu.write_register(0, arg_va);
    cpu.set_sp_el0(0x100040000ULL);
    cpu.set_program_counter(code_va);

    TEST_ASSERT(GuestAppContract::ValidateStackAlignment(cpu.get_sp_el0()));

    // 8. Execute 4 instructions on CPU engine
    for (int i = 0; i < 4; ++i) {
        cpu.step(mem);
    }

    // 9. Verify CPU registers loaded directly from guest memory!
    TEST_ASSERT(cpu.read_register(1) == kGuestAppContractVersion);
    TEST_ASSERT(cpu.read_register(2) == 42); // pid
    TEST_ASSERT(cpu.read_register(3) == 0x200000000ULL); // heap_base
    TEST_ASSERT(cpu.read_register(4) == 0x200100000ULL); // heap_limit (heap_base + heap_sz)

    std::cout << "[PASS] test_argblock_memory_mapping_and_guest_read\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.7 ABI v1.2 Startup & Conformance Tests ===\n";
    test_argblock_memory_mapping_and_guest_read();
    std::cout << "[ALL PASS] Stage 218.7 ABI v1.2 Startup tests succeeded.\n";
    return 0;
}
