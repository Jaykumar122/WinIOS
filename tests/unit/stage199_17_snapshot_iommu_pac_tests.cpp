// ============================================================================
// Stage 199.17: Secure Snapshot and Replay Integration Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pointer_auth.h"
#include "cpu/virtual_time.h"
#include "device/virtual_iommu.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_types.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::mmu;
using namespace winios::snapshot;

// 1. Stage-2 translation engine snapshot round-trip
static bool test_stage2_mmu_snapshot_roundtrip() {
    Memory mem(0x200000); // 2 MiB
    const std::uint64_t root_table_pa = 0x10000ULL;

    Stage2TranslationEngine src_s2;
    src_s2.reset();
    src_s2.set_hcr(0x1ULL); // VM = 1 (enable stage 2)
    src_s2.set_vtcr(0x80000000ULL | (0b10ULL << 14u) | (0b01ULL << 6u) | 16u); // TG0=16KB, SL0=1, T0SZ=16
    src_s2.set_vttbr((0x42ULL << 48u) | root_table_pa); // VMID = 0x42

    // Map a 16KB page: IPA 0x40000 -> PA 0x80000 (RWX)
    TEST_ASSERT(Stage2TranslationEngine::map_page_16k(mem, root_table_pa, 0x40000, 0x80000, true, true, true),
                "Map 16KB Stage-2 page must succeed");

    // Serialize Stage-2 state
    auto blob = src_s2.serialize();
    TEST_ASSERT(blob.size() == 24, "Stage-2 serialized size must be 24 bytes");

    // Deserialize into new Stage-2 engine
    Stage2TranslationEngine dst_s2;
    TEST_ASSERT(dst_s2.deserialize(blob.data(), blob.size()), "Deserialization must succeed");

    TEST_ASSERT(dst_s2.is_enabled(), "Stage-2 must remain enabled");
    TEST_ASSERT(dst_s2.current_vmid() == 0x0042u, "VMID must be preserved");
    TEST_ASSERT(dst_s2.vttbr() == src_s2.vttbr(), "VTTBR must match");
    TEST_ASSERT(dst_s2.vtcr() == src_s2.vtcr(), "VTCR must match");
    TEST_ASSERT(dst_s2.hcr() == src_s2.hcr(), "HCR must match");

    // Translate through restored engine
    auto res = dst_s2.translate(mem, 0x40000 + 0x120, Stage2AccessType::Read);
    TEST_ASSERT(res.has_value(), "Translation must succeed through restored Stage-2 engine");
    TEST_ASSERT(res->physical_address == 0x80000 + 0x120, "Physical address translation mismatch");
    TEST_ASSERT(res->readable && res->writable && res->executable, "Permissions mismatch");

    std::cout << "[PASS] test_stage2_mmu_snapshot_roundtrip\n";
    return true;
}

// 2. Virtual IOMMU domains and mappings snapshot round-trip
static bool test_virtual_iommu_snapshot_roundtrip() {
    VirtualIommu src_iommu;
    TEST_ASSERT(src_iommu.create_domain(1), "Create domain 1");
    TEST_ASSERT(src_iommu.create_domain(2), "Create domain 2");

    // Domain 1: Device 10
    TEST_ASSERT(src_iommu.attach_device(10, 1), "Attach dev 10 to dom 1");
    TEST_ASSERT(src_iommu.map(1, 0x10000, 0x50000, 0x4000, IommuAccess::Read), "Map IOVA in dom 1");

    // Domain 2: Device 20
    TEST_ASSERT(src_iommu.attach_device(20, 2), "Attach dev 20 to dom 2");
    TEST_ASSERT(src_iommu.map(2, 0x20000, 0x60000, 0x8000, IommuAccess::ReadWrite), "Map IOVA in dom 2");

    // Serialize IOMMU state
    auto blob = src_iommu.serialize();
    TEST_ASSERT(!blob.empty(), "Serialized IOMMU state must not be empty");

    // Deserialize into fresh VirtualIommu
    VirtualIommu dst_iommu;
    TEST_ASSERT(dst_iommu.deserialize(blob.data(), blob.size()), "IOMMU deserialize must succeed");

    // Verify Device 10 translation
    auto pa10 = dst_iommu.translate(10, 0x10000 + 0x40, 4, IommuAccess::Read, 0x100000);
    TEST_ASSERT(pa10.has_value(), "Device 10 translation must succeed");
    TEST_ASSERT(*pa10 == 0x50000 + 0x40, "Device 10 PA mismatch");

    // Verify Device 20 translation
    auto pa20 = dst_iommu.translate(20, 0x20000 + 0x100, 8, IommuAccess::Write, 0x100000);
    TEST_ASSERT(pa20.has_value(), "Device 20 translation must succeed");
    TEST_ASSERT(*pa20 == 0x60000 + 0x100, "Device 20 PA mismatch");

    // Verify unmapped IOVA faults
    auto pa_fault = dst_iommu.translate(10, 0x90000, 4, IommuAccess::Read, 0x100000);
    TEST_ASSERT(!pa_fault.has_value(), "Unmapped IOVA must fail translation");

    std::cout << "[PASS] test_virtual_iommu_snapshot_roundtrip\n";
    return true;
}

// 3. ARM64e PAC Keyring snapshot round-trip
static bool test_pac_keyring_snapshot_roundtrip() {
    Arm64ePointerAuth src_pac(PacFaultMode::TrapException);

    // Setup distinct keys
    PacKeyRing kr{};
    kr.apia = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    kr.apib = {0x1111222233334444ULL, 0x5555666677778888ULL};
    kr.apda = {0xAAAABBBBCCCCDDDDULL, 0x1234123412341234ULL};
    kr.apdb = {0x9999888877776666ULL, 0x5555444433332222ULL};
    kr.apga = {0xCAFEBABEDEADBEEFULL, 0xFEEDFACEDEADF00DULL};
    src_pac.set_keyring(kr);

    // Sign a pointer with APIA
    const std::uint64_t orig_ptr = 0x00007FFF12345000ULL;
    const std::uint64_t modifier = 0x00007FFF00000000ULL;
    const std::uint64_t signed_ptr = src_pac.sign_pointer(orig_ptr, modifier, PacKeyType::InstructionA);
    TEST_ASSERT(signed_ptr != orig_ptr, "Signed pointer must differ from original pointer");

    // Serialize PAC state
    auto blob = src_pac.serialize();
    TEST_ASSERT(blob.size() == 84, "PAC serialized size must be 84 bytes");

    // Deserialize into fresh Arm64ePointerAuth instance
    Arm64ePointerAuth dst_pac;
    TEST_ASSERT(dst_pac.deserialize(blob.data(), blob.size()), "PAC deserialize must succeed");
    TEST_ASSERT(dst_pac.get_keyring().apia == kr.apia, "APIA key must match");
    TEST_ASSERT(dst_pac.get_keyring().apda == kr.apda, "APDA key must match");

    // Authenticate using restored keyring
    const std::uint64_t auth_ptr = dst_pac.authenticate_pointer(signed_ptr, modifier, PacKeyType::InstructionA);
    TEST_ASSERT(!dst_pac.has_fault(), "Authentication must succeed with restored keyring");
    TEST_ASSERT(auth_ptr == orig_ptr, "Authenticated pointer must match original pointer");

    // Verify invalid modifier triggers fault in restored instance
    (void)dst_pac.authenticate_pointer(signed_ptr, modifier + 1, PacKeyType::InstructionA, 0x2000);
    TEST_ASSERT(dst_pac.has_fault(), "Wrong modifier must trigger fault in restored PAC instance");
    TEST_ASSERT(dst_pac.last_syndrome().exception_class == 0x1C, "EC must be 0x1C");

    std::cout << "[PASS] test_pac_keyring_snapshot_roundtrip\n";
    return true;
}

// 4. Full system snapshot with Stage-2 Sysregs and RAM preservation
static bool test_full_system_snapshot_with_stage2_sysregs() {
    CPU src_cpu;
    Memory src_mem(0x80000); // 512 KB
    VirtualTime src_vt;
    GICv3 src_gic;

    src_cpu.set_program_counter(0x10000);
    src_cpu.set_current_el(2); // EL2 Hypervisor mode
    src_cpu.write_register(0, 0xCAFEBABE11223344ULL);

    // Stage-2 Sysregs
    src_cpu.set_hcr_el2(0x80000001ULL); // VM = 1, RW = 1
    src_cpu.set_vttbr_el2((0x00FFULL << 48u) | 0x40000ULL);
    src_cpu.set_vtcr_el2(0x80003519ULL);

    // Write RAM pattern
    src_mem.write_u64(0x20000, 0x1122334455667788ULL);
    src_mem.write_u64(0x20008, 0x99AABBCCDDEEFF00ULL);

    SnapshotRestoreContext src_ctx{&src_cpu, &src_mem, &src_vt, &src_gic, {}};
    SnapshotCaptureOptions opts{};
    opts.description = "Stage 199 System Snapshot";
    opts.include_memory = true;

    std::vector<std::uint8_t> snapshot_buf;
    const auto cap_res = SnapshotManager::capture_to_buffer(src_ctx, snapshot_buf, opts);
    TEST_ASSERT(cap_res.success, "Snapshot capture must succeed");
    TEST_ASSERT(!snapshot_buf.empty(), "Snapshot buffer must not be empty");

    // Restore into target system
    CPU dst_cpu;
    Memory dst_mem(0x80000);
    VirtualTime dst_vt;
    GICv3 dst_gic;
    SnapshotRestoreContext dst_ctx{&dst_cpu, &dst_mem, &dst_vt, &dst_gic, {}};

    const auto rest_res = restore_snapshot_from_buffer(snapshot_buf.data(), snapshot_buf.size(), dst_ctx);
    TEST_ASSERT(rest_res.success, "Snapshot restore must succeed");

    // Verify CPU & Stage-2 sysregs
    TEST_ASSERT(dst_cpu.get_program_counter() == 0x10000, "PC must match");
    TEST_ASSERT(dst_cpu.get_current_el() == 2, "Current EL must be 2");
    TEST_ASSERT(dst_cpu.read_register(0) == 0xCAFEBABE11223344ULL, "X0 must match");
    TEST_ASSERT(dst_cpu.get_hcr_el2() == src_cpu.get_hcr_el2(), "HCR_EL2 must match");
    TEST_ASSERT(dst_cpu.get_vttbr_el2() == src_cpu.get_vttbr_el2(), "VTTBR_EL2 must match");
    TEST_ASSERT(dst_cpu.get_vtcr_el2() == src_cpu.get_vtcr_el2(), "VTCR_EL2 must match");

    // Verify RAM contents
    TEST_ASSERT(dst_mem.read_u64(0x20000) == 0x1122334455667788ULL, "RAM data at 0x20000 must match");
    TEST_ASSERT(dst_mem.read_u64(0x20008) == 0x99AABBCCDDEEFF00ULL, "RAM data at 0x20008 must match");

    std::cout << "[PASS] test_full_system_snapshot_with_stage2_sysregs\n";
    return true;
}

int main() {
    std::cout << "--- Stage 199.17: Secure Snapshot and Replay Integration Tests ---\n";
    if (!test_stage2_mmu_snapshot_roundtrip()) return 1;
    if (!test_virtual_iommu_snapshot_roundtrip()) return 1;
    if (!test_pac_keyring_snapshot_roundtrip()) return 1;
    if (!test_full_system_snapshot_with_stage2_sysregs()) return 1;

    std::cout << "All Stage 199.17 tests passed successfully!\n";
    return 0;
}
