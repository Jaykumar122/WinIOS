#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace mmu;

static void setup_cpu_16kb_mmu(CPU& cpu) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    // TCR_EL1: TG0 = 16KB (0b10 in bits 15:14), T0SZ = 28, IPS = 48-bit
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL); // Normal memory + Device
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL); // MMU enabled
}

static void test_root_alignment_and_granule() {
    std::cout << "[Test 1] 16KB translation granule & root table alignment...\n";
    TEST_ASSERT(MMU::validate_table_alignment(0x1000000ULL, GranuleType::Granule16KB));
    TEST_ASSERT(MMU::validate_table_alignment(0x1004000ULL, GranuleType::Granule16KB));
    TEST_ASSERT(!MMU::validate_table_alignment(0x1001000ULL, GranuleType::Granule16KB)); // 4KB aligned, not 16KB
    TEST_ASSERT(!MMU::validate_table_alignment(0x1002000ULL, GranuleType::Granule16KB));
}

static void test_asid_tlb_isolation() {
    std::cout << "[Test 2] ASID tagging & TLB multi-process isolation...\n";
    Memory mem(0x20000000ULL); // 512 MiB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    CPU cpu;
    setup_cpu_16kb_mmu(cpu);

    const uint64_t root1 = ppm.allocate_page();
    const uint64_t root2 = ppm.allocate_page();
    UserAddressSpace uas1(mem, ppm, root1, "uas_p1");
    UserAddressSpace uas2(mem, ppm, root2, "uas_p2");
    uas1.set_asid(11);
    uas2.set_asid(22);

    const uint64_t va = 0x100010000ULL;
    const uint64_t pa1 = ppm.allocate_page();
    const uint64_t pa2 = ppm.allocate_page();

    TEST_ASSERT(uas1.map_user_page(va, pa1, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas2.map_user_page(va, pa2, PagePermissions::UserReadWrite));

    mem.write_u32(static_cast<size_t>(pa1), 0x11111111);
    mem.write_u32(static_cast<size_t>(pa2), 0x22222222);

    // Activate Process 1 (ASID 11)
    cpu.get_mmu().set_ttbr0_el1(uas1.root_table_pa_with_asid());
    TranslationFault fault{};
    auto trans1 = cpu.get_mmu().translate(mem, va, AccessKind::Read, 0, &fault);
    TEST_ASSERT(trans1.has_value());
    TEST_ASSERT(trans1.value() == pa1);
    TEST_ASSERT(mem.read_u32(static_cast<size_t>(trans1.value())) == 0x11111111);

    // Activate Process 2 (ASID 22) - must NOT use cached ASID 11 TLB entry!
    cpu.get_mmu().set_ttbr0_el1(uas2.root_table_pa_with_asid());
    auto trans2 = cpu.get_mmu().translate(mem, va, AccessKind::Read, 0, &fault);
    TEST_ASSERT(trans2.has_value());
    TEST_ASSERT(trans2.value() == pa2);
    TEST_ASSERT(mem.read_u32(static_cast<size_t>(trans2.value())) == 0x22222222);
}

static void test_asid_invalidation_and_reuse() {
    std::cout << "[Test 3] ASID selective invalidation and reuse...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    CPU cpu;
    setup_cpu_16kb_mmu(cpu);

    const uint64_t root1 = ppm.allocate_page();
    UserAddressSpace uas1(mem, ppm, root1, "uas_p1");
    uas1.set_asid(5);

    const uint64_t va = 0x100020000ULL;
    const uint64_t pa = ppm.allocate_page();
    TEST_ASSERT(uas1.map_user_page(va, pa, PagePermissions::UserReadWrite));

    cpu.get_mmu().set_ttbr0_el1(uas1.root_table_pa_with_asid());
    TranslationFault fault{};
    auto trans = cpu.get_mmu().translate(mem, va, AccessKind::Read, 0, &fault);
    TEST_ASSERT(trans.has_value());

    // Invalidate ASID 5
    cpu.get_mmu().invalidate_tlb_asid(5);
    TEST_ASSERT(cpu.get_mmu().get_tlb_invalidations() > 0);

    // Unmap page
    TEST_ASSERT(uas1.unmap_user_page(va));
    auto trans_after = cpu.get_mmu().translate(mem, va, AccessKind::Read, 0, &fault);
    TEST_ASSERT(!trans_after.has_value());
    TEST_ASSERT(fault.type == MmuFaultType::Translation);
}

static void test_wxn_and_permission_boundaries() {
    std::cout << "[Test 4] Permission boundaries: W^X enforcement & EL0 isolation...\n";
    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    CPU cpu;
    setup_cpu_16kb_mmu(cpu);

    const uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "uas_perms");
    uas.set_asid(9);

    const uint64_t ro_va = 0x100040000ULL;
    const uint64_t rw_va = 0x100080000ULL;
    const uint64_t exec_va = 0x1000C0000ULL;

    TEST_ASSERT(uas.map_user_page(ro_va, ppm.allocate_page(), PagePermissions::UserRead));
    TEST_ASSERT(uas.map_user_page(rw_va, ppm.allocate_page(), PagePermissions::UserReadWrite));
    TEST_ASSERT(uas.map_user_page(exec_va, ppm.allocate_page(), PagePermissions::UserExecute));

    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa_with_asid());

    // Write to Read-Only page -> Permission Fault
    TranslationFault fault{};
    auto ro_write = cpu.get_mmu().translate(mem, ro_va, AccessKind::Write, 0, &fault);
    TEST_ASSERT(!ro_write.has_value());
    TEST_ASSERT(fault.type == MmuFaultType::Permission);

    // Execute from Read-Write page (W^X violation) -> Permission Fault
    auto rw_exec = cpu.get_mmu().translate(mem, rw_va, AccessKind::Execute, 0, &fault);
    TEST_ASSERT(!rw_exec.has_value());
    TEST_ASSERT(fault.type == MmuFaultType::Permission);

    // Read and Execute from Executable page -> OK
    auto exec_fetch = cpu.get_mmu().translate(mem, exec_va, AccessKind::Execute, 0, &fault);
    TEST_ASSERT(exec_fetch.has_value());
}

int main() {
    std::cout << "=== WinIOS Stage 220.4 MMU Translation, TLB and ASID Tests ===\n";
    test_root_alignment_and_granule();
    test_asid_tlb_isolation();
    test_asid_invalidation_and_reuse();
    test_wxn_and_permission_boundaries();
    std::cout << "[PASS] All Stage 220.4 MMU Translation, TLB and ASID tests passed.\n";
    return 0;
}
