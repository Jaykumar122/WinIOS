// ==============================================================================
// WinIOS Campaign 200 Stage 21: Snapshot Integrity & Recovery Tests
// ==============================================================================
// Verifies:
// 1. Snapshot round-trip fidelity for Stage-2 translation engine
// 2. Snapshot round-trip fidelity for Virtual IOMMU domains and mappings
// 3. Snapshot round-trip fidelity for ARM64e PAC keyring and mode
// 4. Post-recovery operational verification (translations succeed after restore)
// 5. Corrupted snapshot rejection (truncated buffer, invalid size)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "cpu/pointer_auth.h"
#include "device/virtual_iommu.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::mmu;

static void test_stage2_mmu_recovery_and_translation() {
    Memory mem(1024 * 1024 * 4);
    const std::uint64_t root_table_pa = 0x10000ULL;

    Stage2TranslationEngine src_s2;
    src_s2.reset();
    src_s2.set_hcr(0x1ULL);
    src_s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u);
    src_s2.set_vttbr((0x42ULL << 48u) | root_table_pa);

    const bool mapped = Stage2TranslationEngine::map_page_16k(mem, root_table_pa, 0x40000, 0x80000, true, true, true);
    assert(mapped);
    (void)mapped;

    // Serialize
    auto blob = src_s2.serialize();
    assert(blob.size() == 24);

    // Restore into fresh engine
    Stage2TranslationEngine dst_s2;
    assert(dst_s2.deserialize(blob.data(), blob.size()));
    assert(dst_s2.is_enabled());
    assert(dst_s2.current_vmid() == 0x0042u);
    assert(dst_s2.vttbr() == src_s2.vttbr());
    assert(dst_s2.vtcr() == src_s2.vtcr());
    assert(dst_s2.hcr() == src_s2.hcr());

    // Translate through restored engine
    auto res = dst_s2.translate(mem, 0x40000 + 0x200, Stage2AccessType::Read);
    assert(res.has_value());
    assert(res->physical_address == 0x80000 + 0x200);

    // Corrupted buffer rejection
    assert(!dst_s2.deserialize(blob.data(), blob.size() - 4)); // Truncated

    std::cout << "[PASS] test_stage2_mmu_recovery_and_translation\n";
}

static void test_virtual_iommu_recovery_and_isolation() {
    VirtualIommu src_iommu;
    assert(src_iommu.create_domain(1));
    assert(src_iommu.attach_device(10, 1));
    assert(src_iommu.map(1, 0x10000, 0x50000, 0x4000, IommuAccess::ReadWrite));

    auto blob = src_iommu.serialize();
    assert(!blob.empty());

    VirtualIommu dst_iommu;
    assert(dst_iommu.deserialize(blob.data(), blob.size()));
    assert(dst_iommu.has_domain(1));
    assert(dst_iommu.is_device_attached(10));

    // Translation succeeds on restored IOMMU
    const std::size_t mem_limit = 1024 * 1024 * 16;
    auto pa = dst_iommu.translate(10, 0x10000 + 0x50, 4, IommuAccess::Read, mem_limit);
    assert(pa.has_value());
    assert(pa.value() == 0x50000 + 0x50);

    // Unmapped IOVA fails
    auto pa_fault = dst_iommu.translate(10, 0x90000, 4, IommuAccess::Read, mem_limit);
    assert(!pa_fault.has_value());

    // Truncated buffer rejection
    assert(!dst_iommu.deserialize(blob.data(), 4));

    std::cout << "[PASS] test_virtual_iommu_recovery_and_isolation\n";
}

static void test_pac_keyring_recovery_and_verification() {
    Arm64ePointerAuth src_pac(PacFaultMode::TrapException);
    PacKeyRing kr{};
    kr.apia = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    kr.apib = {0x1111222233334444ULL, 0x5555666677778888ULL};
    kr.apda = {0xAAAABBBBCCCCDDDDULL, 0x1234123412341234ULL};
    kr.apdb = {0x9999888877776666ULL, 0x5555444433332222ULL};
    kr.apga = {0xCAFEBABEDEADBEEFULL, 0xFEEDFACEDEADF00DULL};
    src_pac.set_keyring(kr);

    const std::uint64_t ptr = 0x00007FFF12345000ULL;
    const std::uint64_t mod = 0x00007FFF00000000ULL;
    const std::uint64_t signed_ptr = src_pac.sign_pointer(ptr, mod, PacKeyType::InstructionA);

    auto blob = src_pac.serialize();
    assert(blob.size() == 84);

    Arm64ePointerAuth dst_pac;
    assert(dst_pac.deserialize(blob.data(), blob.size()));
    assert(dst_pac.get_keyring().apia == kr.apia);

    // Verify pointer authentication succeeds with restored keyring
    const std::uint64_t auth_ptr = dst_pac.authenticate_pointer(signed_ptr, mod, PacKeyType::InstructionA);
    (void)auth_ptr;
    assert(!dst_pac.has_fault());
    assert(auth_ptr == ptr);

    // Wrong modifier fails on restored keyring
    (void)dst_pac.authenticate_pointer(signed_ptr, mod + 1, PacKeyType::InstructionA, 0x2000);
    assert(dst_pac.has_fault());

    // Truncated buffer rejection
    assert(!dst_pac.deserialize(blob.data(), blob.size() - 8));

    std::cout << "[PASS] test_pac_keyring_recovery_and_verification\n";
}

int main() {
    std::cout << "=== Running Stage 200.21 Snapshot Integrity & Recovery Tests ===\n";
    test_stage2_mmu_recovery_and_translation();
    test_virtual_iommu_recovery_and_isolation();
    test_pac_keyring_recovery_and_verification();
    std::cout << "All Stage 200.21 tests passed successfully!\n";
    return 0;
}
