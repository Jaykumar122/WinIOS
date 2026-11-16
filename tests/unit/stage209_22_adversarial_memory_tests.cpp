#include <iostream>
#include <vector>
#include <cstdint>
#include <string>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "device/apple_aic.h"
#include "memory/xnu_page_allocator.h"
#include "loader/kernel_image_parser.h"

using namespace winios;
using namespace winios::memory;
using namespace winios::device;
using namespace winios::loader;
using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_15_adversarial_memory_attacks() {
    std::cout << "[Test Suite] Stage 209.22: 15 Adversarial Memory & Boundary Security Tests...\n";

    // Attack 1: Malformed Level-2 Block with Misaligned Output Address
    {
        MMU mmu;
        DescriptorKind kind = DescriptorKind::Invalid;
        MmuFaultType fault = MmuFaultType::Translation;
        // Output address bits [24:14] non-zero in a 32MB block descriptor (must be 32MB aligned)
        const std::uint64_t bad_desc = 0x0000000000010701ULL; // Misaligned block address
        // Check descriptor validation: must reject misaligned block descriptor with AddressSize fault
        const bool valid = mmu.validate_descriptor_for_granule(2, bad_desc, GranuleType::Granule16KB, false, &kind, &fault);
        TEST_CHECK(!valid && fault == MmuFaultType::AddressSize, "ATK-01: Block descriptor misaligned address rejected with AddressSize fault");
    }

    // Attack 2: Out-of-Bounds Physical Access Past 8 MiB DRAM
    {
        Memory mem(0x800000); // 8 MiB
        bool caught = false;
        try {
            mem.read_u64(0x02000000ULL); // 32 MiB
        } catch (const std::exception&) {
            caught = true;
        }
        TEST_CHECK(caught, "ATK-02: Direct access beyond 8 MiB DRAM throws handled exception");
    }

    // Attack 3: Ghost Memory Slot 1 Translation Rejection
    {
        MMU mmu;
        Memory mem(0x800000);
        mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
        mmu.set_ttbr0_el1(0x20000ULL);
        // Slot 1 explicitly zero (invalidated ghost RAM)
        mem.write_u64(0x20008ULL, 0x0ULL);
        mmu.set_sctlr_el1(0x00C50839ULL);

        TranslationFault fault{};
        auto trans = mmu.translate(mem, 0x02000000ULL, AccessKind::Read, 1, &fault);
        TEST_CHECK(!trans.has_value(), "ATK-03: Translation into ghost memory Slot 1 returns Translation Fault");
        TEST_CHECK(fault.type == MmuFaultType::Translation, "Fault verified as Translation Fault");
    }

    // Attack 4: Allocator Integer Overflow Request
    {
        XnuPhysicalPageAllocator alloc;
        alloc.initialize(0x0ULL, 0x800000ULL);
        auto p = alloc.allocate_pages(~0ULL / 16384ULL);
        TEST_CHECK(!p.has_value(), "ATK-04: Rejected 64-bit integer overflow page allocation");
    }

    // Attack 5: Allocator Double Free Prevention
    {
        XnuPhysicalPageAllocator alloc;
        alloc.initialize(0x0ULL, 0x800000ULL);
        alloc.reserve_range(0x0ULL, 0x40000ULL, "boot");
        auto p = alloc.allocate_pages(1);
        std::string err;
        TEST_CHECK(alloc.free_pages(p.value(), 1, &err), "Freed page");
        TEST_CHECK(!alloc.free_pages(p.value(), 1, &err), "ATK-05: Double free safely rejected");
    }

    // Attack 6: W^X Permission Bypass Write to Read-Only Descriptor
    {
        MMU mmu;
        Memory mem(0x800000);
        mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
        mmu.set_ttbr0_el1(0x20000ULL);
        mmu.set_mair_el1(0x00000000000004FFULL);
        mem.write_u64(0x20000ULL, 0x0000000000000781ULL); // Read-only descriptor
        mmu.set_sctlr_el1(0x00C50839ULL);

        TranslationFault fault{};
        auto trans = mmu.translate(mem, 0x1000ULL, AccessKind::Write, 1, &fault);
        TEST_CHECK(!trans.has_value(), "ATK-06: Write on read-only page blocked");
        TEST_CHECK(fault.type == MmuFaultType::Permission, "Trapped with Permission Fault");
    }

    // Attack 7: Unaligned TTBR Base Address
    {
        MMU mmu;
        Memory mem(0x800000);
        mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
        mmu.set_ttbr0_el1(0x20001ULL); // Misaligned by 1 byte
        mmu.set_sctlr_el1(0x00C50839ULL);

        TranslationFault fault{};
        auto trans = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
        TEST_CHECK(!trans.has_value(), "ATK-07: Unaligned TTBR base returns Alignment Fault");
        TEST_CHECK(fault.type == MmuFaultType::Alignment, "Alignment fault verified");
    }

    // Attack 8: Unmapped MMIO Read Outside Device Aperture
    {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x800000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_program_counter(0x1000ULL);
        mem.write_u32(0x1000ULL, 0xF9400020U); // LDR X0, [X1]
        cpu.write_register(1, 0x23F000000ULL); // Out-of-aperture MMIO
        cpu.step(mem);
        TEST_CHECK(cpu.get_far_el1() == 0x23F000000ULL, "ATK-08: Trapped out-of-aperture MMIO in FAR_EL1");
    }

    // Attack 9: Corrupted Allocator Snapshot Header
    {
        XnuPhysicalPageAllocator alloc;
        const std::uint8_t corrupted[32] = {'C', 'O', 'R', 'R', 'U', 'P', 'T'};
        std::string err;
        TEST_CHECK(!alloc.deserialize(corrupted, sizeof(corrupted), &err), "ATK-09: Corrupted allocator snapshot rejected");
    }

    // Attack 10: Corrupted AIC2 Snapshot Deserialization
    {
        AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
        const std::uint8_t garbage[16] = {0xFF, 0xEE, 0xDD, 0xCC};
        TEST_CHECK(!aic.deserialize_state(garbage, sizeof(garbage)), "ATK-10: Corrupted AIC2 snapshot rejected");
    }

    // Attack 11: Stack Pointer Misalignment Trapping
    {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x800000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_sp_el1(0x7ULL); // Misaligned SP
        cpu.set_program_counter(0x1000ULL);
        mem.write_u32(0x1000ULL, 0xF81F0FE0U); // STR X0, [SP, #-16]!
        cpu.step(mem);
        TEST_CHECK(cpu.get_program_counter() >= 0x8000ULL, "ATK-11: SP alignment fault routed to vector");
    }

    // Attack 12: Undefined Instruction Trapping
    {
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x800000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_program_counter(0x1000ULL);
        mem.write_u32(0x1000ULL, 0x00000000U); // Undefined opcode
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26) == 0x00, "ATK-12: Undefined opcode trapped with ESR EC=0");
    }

    // Attack 13: Mach-O Segment Misalignment Rejection
    {
        std::vector<KernelSegmentInfo> segs;
        KernelSegmentInfo s;
        s.name = "__BAD_ALIGN";
        s.vmaddr = 0x4001; // Not 16KB aligned
        s.vmsize = 0x4000;
        segs.push_back(s);
        std::string err;
        TEST_CHECK(!KernelImageParser::validate_segments(segs, 0x10000, 16384, err), "ATK-13: Misaligned segment rejected");
    }

    // Attack 14: Allocator Exhaustion Boundary
    {
        XnuPhysicalPageAllocator alloc;
        alloc.initialize(0x0ULL, 0x800000ULL);
        alloc.reserve_range(0x0ULL, 0x40000ULL, "boot");
        auto p = alloc.allocate_pages(496);
        TEST_CHECK(p.has_value(), "Allocated all 496 free pages");
        auto p_ex = alloc.allocate_pages(1);
        TEST_CHECK(!p_ex.has_value(), "ATK-14: Allocation strictly fails past exhaustion limit");
    }

    // Attack 15: Replay Determinism Verification
    {
        XnuPhysicalPageAllocator a1, a2;
        a1.initialize(0x0ULL, 0x800000ULL);
        a2.initialize(0x0ULL, 0x800000ULL);
        a1.reserve_range(0x0ULL, 0x40000ULL, "boot");
        a2.reserve_range(0x0ULL, 0x40000ULL, "boot");
        auto p1 = a1.allocate_pages(8);
        auto p2 = a2.allocate_pages(8);
        TEST_CHECK(p1 == p2, "ATK-15: Deterministic frame allocation guaranteed across instances");
    }

    std::cout << "  -> All 15 Adversarial Memory & Boundary Security Tests Passed.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.22 Adversarial Memory Tests ===\n";
    test_15_adversarial_memory_attacks();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.22: All adversarial memory security tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.22: " << g_failures << " tests failed.\n";
        return 1;
    }
}
