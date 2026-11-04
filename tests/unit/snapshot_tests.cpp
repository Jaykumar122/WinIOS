#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_validation.h"
#include "snapshot/wsnap_format.h"
#include "trace/trace_serializer.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

void test_format_header_round_trip() {
    std::cout << "[Test] format_header_round_trip...\n";
    winios::snapshot::WSnapHeader hdr{};
    hdr.magic = winios::snapshot::kWSnapMagic;
    hdr.version = 1;
    hdr.core_count = 4;
    hdr.compression_type = winios::snapshot::SnapshotCompressionType::None;
    hdr.flags = 0x0001;
    hdr.timestamp_ns = 1730718000000000000ULL;
    hdr.memory_capacity = 64ULL * 1024 * 1024;
    hdr.section_dir_offset = 1024;
    hdr.section_count = 5;
    hdr.total_file_size = 1224;

    std::uint8_t buffer[64];
    winios::snapshot::encode_wsnap_header(hdr, buffer);

    winios::snapshot::WSnapHeader decoded{};
    const auto res = winios::snapshot::decode_wsnap_header(buffer, sizeof(buffer), decoded);
    assert(res.success);
    assert(decoded.magic == winios::snapshot::kWSnapMagic);
    assert(decoded.version == 1);
    assert(decoded.core_count == 4);
    assert(decoded.memory_capacity == 64ULL * 1024 * 1024);
    assert(decoded.section_dir_offset == 1024);
    assert(decoded.section_count == 5);
    assert(decoded.total_file_size == 1224);

    // Test corrupted CRC
    buffer[60] ^= 0xFF;
    winios::snapshot::WSnapHeader corrupt{};
    const auto fail_res = winios::snapshot::decode_wsnap_header(buffer, sizeof(buffer), corrupt);
    assert(!fail_res.success);
    assert(fail_res.error_code == winios::snapshot::SnapshotErrorCode::HeaderCrcMismatch);

    // Test invalid magic
    buffer[0] = 'X';
    const auto bad_magic = winios::snapshot::decode_wsnap_header(buffer, sizeof(buffer), corrupt);
    assert(!bad_magic.success);
    assert(bad_magic.error_code == winios::snapshot::SnapshotErrorCode::InvalidMagic);
}

void test_section_descriptor_round_trip() {
    std::cout << "[Test] section_descriptor_round_trip...\n";
    winios::snapshot::WSnapSectionDescriptor desc{};
    desc.section_type = winios::snapshot::SnapshotSectionType::CpuState;
    desc.section_id = 2;
    desc.file_offset = 0x1000;
    desc.compressed_size = 1128;
    desc.uncompressed_size = 1128;
    desc.section_crc32c = 0x12345678;
    desc.flags = 0;

    std::uint8_t buffer[40];
    winios::snapshot::encode_section_descriptor(desc, buffer);

    winios::snapshot::WSnapSectionDescriptor decoded{};
    const auto res = winios::snapshot::decode_section_descriptor(buffer, sizeof(buffer), decoded);
    assert(res.success);
    assert(decoded.section_type == winios::snapshot::SnapshotSectionType::CpuState);
    assert(decoded.section_id == 2);
    assert(decoded.file_offset == 0x1000);
    assert(decoded.compressed_size == 1128);
    assert(decoded.section_crc32c == 0x12345678);
}

void test_cpu_state_round_trip() {
    std::cout << "[Test] cpu_state_round_trip...\n";
    CPU src_cpu;
    src_cpu.set_core_id(1);
    src_cpu.set_program_counter(0x0000000040100000ULL);

    // Set GPRs
    for (std::size_t i = 0; i < 31; ++i) {
        src_cpu.write_register(i, 0x1000000000000000ULL | (i * 0x11111111ULL));
    }

    // Set Banked SPs
    src_cpu.set_sp_el0(0x000000007FFF0000ULL);
    src_cpu.set_sp_el1(0x000000007FFE0000ULL);
    src_cpu.set_sp_el2(0x000000007FFD0000ULL);
    src_cpu.set_sp_el3(0x000000007FFC0000ULL);

    // PSTATE
    src_cpu.set_current_el(1);
    src_cpu.set_sp_sel(true);
    src_cpu.set_nzcv(0xAu); // N=1, C=1
    src_cpu.set_daif(0x5u); // D=0, A=1, I=0, F=1
    src_cpu.set_il(false);
    src_cpu.set_pstate_ss(true);
    src_cpu.set_architectural_exceptions(true);

    // SIMD
    for (std::size_t i = 0; i < 32; ++i) {
        CPU::VectorRegister v{};
        v.low = 0xAA00000000000000ULL | i;
        v.high = 0x5500000000000000ULL | (i * 2);
        src_cpu.write_vector_register(i, v);
    }
    src_cpu.set_fpcr(0x01000000u);
    src_cpu.set_fpsr(0x00000008u);

    // EL1 sysregs
    src_cpu.set_elr_el1(0x40001000ULL);
    src_cpu.set_spsr_el1(0x3C5u);
    src_cpu.set_esr_el1(0x56000000u);
    src_cpu.set_far_el1(0x10002000ULL);
    src_cpu.set_vbar_el1(0x40008000ULL);
    src_cpu.set_cntkctl_el1(0x103u);

    // EL2 sysregs
    src_cpu.set_elr_el2(0x40002000ULL);
    src_cpu.set_spsr_el2(0x3C9u);
    src_cpu.set_esr_el2(0x92000000u);
    src_cpu.set_far_el2(0x20004000ULL);
    src_cpu.set_hpfar_el2(0x00001000ULL);
    src_cpu.set_vbar_el2(0x40009000ULL);
    src_cpu.set_hcr_el2(0x80000001ULL);
    src_cpu.set_vttbr_el2(0x0000000050000000ULL);
    src_cpu.set_vtcr_el2(0x80003550ULL);
    src_cpu.set_cntvoff_el2(1000ULL);
    src_cpu.set_cnthctl_el2(3ULL);
    src_cpu.set_mdcr_el2(0x00000002ULL);

    // EL3 sysregs
    src_cpu.set_sp_el3(0x7FF00000ULL);
    src_cpu.set_spsr_el3(0x3CDu);
    src_cpu.set_elr_el3(0x40003000ULL);
    src_cpu.set_esr_el3(0x5E000000u);
    src_cpu.set_far_el3(0x30006000ULL);
    src_cpu.set_vbar_el3(0x4000A000ULL);
    src_cpu.set_scr_el3(0x501ULL);
    src_cpu.set_cptr_el3(0x00000001ULL);
    src_cpu.set_mdcr_el3(0x00000004ULL);

    // MMU sysregs
    src_cpu.set_sctlr_el1(0x00C50839ULL);
    src_cpu.set_tcr_el1(0x00000001B5193519ULL);
    src_cpu.set_ttbr0_el1(0x0000000040000000ULL);
    src_cpu.set_ttbr1_el1(0x0000000040010000ULL);
    src_cpu.set_mair_el1(0x00000000004404FFULL);
    src_cpu.set_par_el1(0x0000000000000001ULL);

    // Lifecycle
    src_cpu.set_halted(true);
    src_cpu.set_waiting_for_interrupt(true);
    src_cpu.set_event_register(true);
    src_cpu.set_started(true);
    src_cpu.set_stopped(false);
    src_cpu.set_instruction_trace_sequence(42ULL);

    // Serialize
    const auto payload = winios::snapshot::serialize_cpu_state(src_cpu);
    assert(payload.size() == 1128);

    // Restore into fresh clean CPU instance
    CPU dst_cpu;
    const auto res = winios::snapshot::deserialize_cpu_state(payload.data(), payload.size(), dst_cpu);
    assert(res.success);

    // Compare bit-for-bit
    assert(dst_cpu.get_core_id() == 1);
    assert(dst_cpu.get_program_counter() == 0x0000000040100000ULL);
    for (std::size_t i = 0; i < 31; ++i) {
        assert(dst_cpu.read_register(i) == src_cpu.read_register(i));
    }
    assert(dst_cpu.get_sp_el0() == src_cpu.get_sp_el0());
    assert(dst_cpu.get_sp_el1() == src_cpu.get_sp_el1());
    assert(dst_cpu.get_sp_el2() == src_cpu.get_sp_el2());
    assert(dst_cpu.get_sp_el3() == src_cpu.get_sp_el3());
    assert(dst_cpu.get_current_el() == 1);
    assert(dst_cpu.get_sp_sel() == true);
    assert(dst_cpu.get_nzcv() == 0xAu);
    assert(dst_cpu.get_daif() == 0x5u);
    assert(dst_cpu.get_il() == false);
    assert(dst_cpu.get_pstate_ss() == true);
    assert(dst_cpu.architectural_exceptions() == true);

    for (std::size_t i = 0; i < 32; ++i) {
        assert(dst_cpu.read_vector_register(i) == src_cpu.read_vector_register(i));
    }
    assert(dst_cpu.get_fpcr() == src_cpu.get_fpcr());
    assert(dst_cpu.get_fpsr() == src_cpu.get_fpsr());

    assert(dst_cpu.get_elr_el1() == src_cpu.get_elr_el1());
    assert(dst_cpu.get_spsr_el1() == src_cpu.get_spsr_el1());
    assert(dst_cpu.get_esr_el1() == src_cpu.get_esr_el1());
    assert(dst_cpu.get_far_el1() == src_cpu.get_far_el1());
    assert(dst_cpu.get_vbar_el1() == src_cpu.get_vbar_el1());
    assert(dst_cpu.get_cntkctl_el1() == src_cpu.get_cntkctl_el1());

    assert(dst_cpu.get_elr_el2() == src_cpu.get_elr_el2());
    assert(dst_cpu.get_spsr_el2() == src_cpu.get_spsr_el2());
    assert(dst_cpu.get_esr_el2() == src_cpu.get_esr_el2());
    assert(dst_cpu.get_far_el2() == src_cpu.get_far_el2());
    assert(dst_cpu.get_hpfar_el2() == src_cpu.get_hpfar_el2());
    assert(dst_cpu.get_vbar_el2() == src_cpu.get_vbar_el2());
    assert(dst_cpu.get_hcr_el2() == src_cpu.get_hcr_el2());
    assert(dst_cpu.get_vttbr_el2() == src_cpu.get_vttbr_el2());
    assert(dst_cpu.get_vtcr_el2() == src_cpu.get_vtcr_el2());
    assert(dst_cpu.get_cntvoff_el2() == src_cpu.get_cntvoff_el2());
    assert(dst_cpu.get_cnthctl_el2() == src_cpu.get_cnthctl_el2());
    assert(dst_cpu.get_mdcr_el2() == src_cpu.get_mdcr_el2());

    assert(dst_cpu.get_sp_el3() == src_cpu.get_sp_el3());
    assert(dst_cpu.get_spsr_el3() == src_cpu.get_spsr_el3());
    assert(dst_cpu.get_elr_el3() == src_cpu.get_elr_el3());
    assert(dst_cpu.get_esr_el3() == src_cpu.get_esr_el3());
    assert(dst_cpu.get_far_el3() == src_cpu.get_far_el3());
    assert(dst_cpu.get_vbar_el3() == src_cpu.get_vbar_el3());
    assert(dst_cpu.get_scr_el3() == src_cpu.get_scr_el3());
    assert(dst_cpu.get_cptr_el3() == src_cpu.get_cptr_el3());
    assert(dst_cpu.get_mdcr_el3() == src_cpu.get_mdcr_el3());

    assert(dst_cpu.get_sctlr_el1() == src_cpu.get_sctlr_el1());
    assert(dst_cpu.get_tcr_el1() == src_cpu.get_tcr_el1());
    assert(dst_cpu.get_ttbr0_el1() == src_cpu.get_ttbr0_el1());
    assert(dst_cpu.get_ttbr1_el1() == src_cpu.get_ttbr1_el1());
    assert(dst_cpu.get_mair_el1() == src_cpu.get_mair_el1());
    assert(dst_cpu.get_par_el1() == src_cpu.get_par_el1());

    assert(dst_cpu.is_halted() == true);
    assert(dst_cpu.is_waiting() == true);
    assert(dst_cpu.get_event_register() == true);
    assert(dst_cpu.is_started() == true);
    assert(dst_cpu.is_stopped() == false);
    assert(dst_cpu.get_instruction_trace_sequence() == 42ULL);
}

void test_memory_sparse_chunk_round_trip() {
    std::cout << "[Test] memory_sparse_chunk_round_trip...\n";
    constexpr std::size_t kMemSize = 1024 * 1024; // 1 MB = 16 chunks of 64KB
    Memory src_mem(kMemSize);

    // Populate only chunk 0 (offset 0), chunk 3 (offset 196608), and chunk 15 (offset 983040)
    src_mem.write_u32(0, 0x12345678u);
    src_mem.write_u64(100, 0xDEADBEEFCAFEBABEULL);

    src_mem.write_u32(3 * 65536, 0xAABBCCDDu);
    src_mem.write_u64(3 * 65536 + 200, 0x1122334455667788ULL);

    src_mem.write_u32(15 * 65536 + 4096, 0x55AA55AAu);

    // Serialize
    const auto payload = winios::snapshot::serialize_memory_state(src_mem);

    // Validate sparse optimization:
    // 3 non-zero chunks * (16 + 65536) + 32 byte header = ~196,688 bytes (vs 1,048,576 bytes raw)
    // Over 5x reduction via zero-page skipping!
    assert(payload.size() < 200000);

    // Restore into fresh memory
    Memory dst_mem(kMemSize);
    const auto res = winios::snapshot::deserialize_memory_state(payload.data(), payload.size(), dst_mem);
    assert(res.success);

    // Byte-for-byte verification across all 1 MB
    assert(std::memcmp(src_mem.raw_data(), dst_mem.raw_data(), kMemSize) == 0);

    // Test corrupted chunk payload
    std::vector<std::uint8_t> corrupt_payload = payload;
    // Corrupt a byte in the first chunk's data payload (offset > 48)
    corrupt_payload[60] ^= 0x55;
    Memory corrupt_dst(kMemSize);
    const auto fail_res = winios::snapshot::deserialize_memory_state(corrupt_payload.data(), corrupt_payload.size(), corrupt_dst);
    assert(!fail_res.success);
    assert(fail_res.error_code == winios::snapshot::SnapshotErrorCode::CorruptedChunk);
}

void test_virtual_time_and_timer_round_trip() {
    std::cout << "[Test] virtual_time_and_timer_round_trip...\n";
    winios::VirtualTime src_vt(24000000ULL, 2);
    src_vt.set_physical_counter(150000000ULL);

    winios::TimerState phys{};
    phys.enable = true;
    phys.imask = false;
    phys.istatus = true;
    phys.cval = 140000000ULL;
    src_vt.set_phys_timer(0, phys);

    winios::TimerState virt{};
    virt.enable = true;
    virt.imask = true;
    virt.istatus = false;
    virt.cval = 200000000ULL;
    src_vt.set_virt_timer(0, virt);

    src_vt.set_cntvoff_el2(0, 5000ULL);
    src_vt.set_cntkctl_el1(0, 0x203ULL);
    src_vt.set_cnthctl_el2(0, 3ULL);

    const auto payload = winios::snapshot::serialize_timer_state(src_vt, 2);

    winios::VirtualTime dst_vt(10000000ULL, 2);
    const auto res = winios::snapshot::deserialize_timer_state(payload.data(), payload.size(), dst_vt);
    assert(res.success);

    assert(dst_vt.get_physical_counter() == 150000000ULL);
    assert(dst_vt.get_frequency() == 24000000ULL);

    const auto& dst_phys = dst_vt.get_phys_timer(0);
    assert(dst_phys.enable == true);
    assert(dst_phys.imask == false);
    assert(dst_phys.istatus == true);
    assert(dst_phys.cval == 140000000ULL);

    const auto& dst_virt = dst_vt.get_virt_timer(0);
    assert(dst_virt.enable == true);
    assert(dst_virt.imask == true);
    assert(dst_virt.cval == 200000000ULL);

    assert(dst_vt.get_cntvoff_el2(0) == 5000ULL);
    assert(dst_vt.get_cntkctl_el1(0) == 0x203ULL);
    assert(dst_vt.get_cnthctl_el2(0) == 3ULL);
}

void test_gic_state_round_trip() {
    std::cout << "[Test] gic_state_round_trip...\n";
    winios::GICv3 src_gic;
    src_gic.set_core_count(2);

    auto dist = src_gic.get_distributor_state();
    dist.gicd_ctlr = 0x53u;
    dist.gicd_isenabler[1] = 0x0000FFFFu;
    dist.gicd_ispendr[1]   = 0x000000F0u;
    dist.gicd_ipriorityr[35] = 0x40u;
    src_gic.set_distributor_state(dist);

    auto& rd = src_gic.get_redistributor(0);
    rd.isenabler0 = 0x80000000u;
    rd.ispendr0   = 0x40000000u;
    rd.ipriorityr[30] = 0x20u;

    auto& icc = src_gic.get_cpu_interface(0);
    icc.icc_pmr_el1 = 0xA0u;
    icc.icc_bpr0_el1 = 3u;
    icc.icc_igrpen1_el1 = 1u;
    icc.active_priorities.push_back(0x20u);

    const auto payload = winios::snapshot::serialize_gic_state(src_gic);

    winios::GICv3 dst_gic;
    const auto res = winios::snapshot::deserialize_gic_state(payload.data(), payload.size(), dst_gic);
    assert(res.success);
    assert(dst_gic.get_core_count() == 2);

    const auto dst_dist = dst_gic.get_distributor_state();
    assert(dst_dist.gicd_ctlr == 0x53u);
    assert(dst_dist.gicd_isenabler[1] == 0x0000FFFFu);
    assert(dst_dist.gicd_ispendr[1] == 0x000000F0u);
    assert(dst_dist.gicd_ipriorityr[35] == 0x40u);

    const auto& dst_rd = dst_gic.get_redistributor(0);
    assert(dst_rd.isenabler0 == 0x80000000u);
    assert(dst_rd.ispendr0 == 0x40000000u);
    assert(dst_rd.ipriorityr[30] == 0x20u);

    const auto& dst_icc = dst_gic.get_cpu_interface(0);
    assert(dst_icc.icc_pmr_el1 == 0xA0u);
    assert(dst_icc.icc_bpr0_el1 == 3u);
    assert(dst_icc.icc_igrpen1_el1 == 1u);
    assert(dst_icc.active_priorities.size() == 1);
    assert(dst_icc.active_priorities[0] == 0x20u);
}

void test_pmu_and_debug_round_trip() {
    std::cout << "[Test] pmu_and_debug_round_trip...\n";
    // PMU
    winios::PMU src_pmu;
    src_pmu.write_pmcr(0x41u);
    src_pmu.write_pmcntenset(0x80000007u);
    src_pmu.write_pmccntr(123456789ULL);
    src_pmu.write_pmevcntr(0, 500u);
    src_pmu.write_pmevtyper(0, 0x08u);

    const auto pmu_payload = winios::snapshot::serialize_pmu_state(src_pmu, 0);
    winios::PMU dst_pmu;
    const auto pmu_res = winios::snapshot::deserialize_pmu_state(pmu_payload.data(), pmu_payload.size(), dst_pmu);
    assert(pmu_res.success);
    assert(dst_pmu.read_pmcr() == src_pmu.read_pmcr());
    assert(dst_pmu.read_pmcntenset() == src_pmu.read_pmcntenset());
    assert(dst_pmu.read_pmccntr() == src_pmu.read_pmccntr());
    assert(dst_pmu.read_pmevcntr(0) == src_pmu.read_pmevcntr(0));
    assert(dst_pmu.read_pmevtyper(0) == src_pmu.read_pmevtyper(0));

    // Debug
    winios::DebugArchitecture src_dbg;
    src_dbg.set_mdscr_el1(1ULL << 15u); // MDE
    src_dbg.set_dbgbvr(0, 0x40105000ULL);
    src_dbg.set_dbgbcr(0, 0x1u); // enable
    src_dbg.set_dbgwvr(0, 0x50000000ULL);
    src_dbg.set_dbgwcr(0, 0x5u);

    const auto dbg_payload = winios::snapshot::serialize_debug_state(src_dbg, 0);
    winios::DebugArchitecture dst_dbg;
    const auto dbg_res = winios::snapshot::deserialize_debug_state(dbg_payload.data(), dbg_payload.size(), dst_dbg);
    assert(dbg_res.success);
    assert(dst_dbg.get_mdscr_el1() == (1ULL << 15u));
    assert(dst_dbg.get_dbgbvr(0) == 0x40105000ULL);
    assert(dst_dbg.get_dbgbcr(0) == 0x1u);
    assert(dst_dbg.get_dbgwvr(0) == 0x50000000ULL);
    assert(dst_dbg.get_dbgwcr(0) == 0x5u);
}

void test_full_system_integration_round_trip() {
    std::cout << "[Test] full_system_integration_round_trip...\n";
    const std::filesystem::path snap_path = "test_system.wsnap";

    CPU src_cpu;
    src_cpu.set_program_counter(0x40001000ULL);
    src_cpu.write_register(0, 42ULL);
    src_cpu.write_register(1, 100ULL);
    src_cpu.write_register(30, 0x40002000ULL); // LR
    src_cpu.set_sp_el1(0x7FFF0000ULL);
    src_cpu.set_nzcv(0x4u); // Z flag

    Memory src_mem(512 * 1024); // 512 KB
    src_mem.write_u32(0x1000, 0xD503201Fu); // NOP
    src_mem.write_u64(0x2000, 0xCAFEBABE11223344ULL);

    winios::VirtualTime src_vt;
    src_vt.set_physical_counter(9999ULL);

    winios::GICv3 src_gic;
    src_gic.write_icc_pmr(0x80u, 0);

    winios::snapshot::SnapshotRestoreContext src_ctx{&src_cpu, &src_mem, &src_vt, &src_gic, {}};
    winios::snapshot::SnapshotCaptureOptions opts{};
    opts.description = "Integration Test Snapshot";
    opts.creator = "Test Harness";

    const auto cap_res = winios::snapshot::SnapshotManager::capture_to_file(src_ctx, snap_path, opts);
    assert(cap_res.success);

    // Validate the snapshot file
    const auto val_res = winios::snapshot::SnapshotManager::validate_file(snap_path);
    assert(val_res.valid);

    // Inspect the snapshot file
    winios::snapshot::SnapshotInspection insp{};
    const auto insp_res = winios::snapshot::SnapshotManager::inspect_file(snap_path, insp);
    assert(insp_res.success);
    assert(insp.header.core_count == 1);
    assert(insp.header.memory_capacity == 512 * 1024);
    assert(insp.metadata_description == "Integration Test Snapshot");

    // Restore into completely clean instances
    CPU dst_cpu;
    Memory dst_mem(512 * 1024);
    winios::VirtualTime dst_vt;
    winios::GICv3 dst_gic;

    winios::snapshot::SnapshotRestoreContext dst_ctx{&dst_cpu, &dst_mem, &dst_vt, &dst_gic, {}};
    const auto rest_res = winios::snapshot::SnapshotManager::restore_from_file(snap_path, dst_ctx);
    assert(rest_res.success);

    // Exact state verification
    assert(dst_cpu.get_program_counter() == 0x40001000ULL);
    assert(dst_cpu.read_register(0) == 42ULL);
    assert(dst_cpu.read_register(1) == 100ULL);
    assert(dst_cpu.read_register(30) == 0x40002000ULL);
    assert(dst_cpu.get_sp_el1() == 0x7FFF0000ULL);
    assert(dst_cpu.get_nzcv() == 0x4u);

    assert(dst_mem.read_u32(0x1000) == 0xD503201Fu);
    assert(dst_mem.read_u64(0x2000) == 0xCAFEBABE11223344ULL);

    assert(dst_vt.get_physical_counter() == 9999ULL);
    assert(dst_gic.read_icc_pmr(0) == 0x80u);

    // Clean up temporary test file
    std::filesystem::remove(snap_path);
}

void test_corruption_handling_and_safety() {
    std::cout << "[Test] corruption_handling_and_safety...\n";
    CPU cpu;
    Memory mem(65536);
    winios::snapshot::SnapshotRestoreContext ctx{&cpu, &mem, nullptr, nullptr, {}};

    std::vector<std::uint8_t> valid_buffer;
    const auto cap_res = winios::snapshot::SnapshotManager::capture_to_buffer(ctx, valid_buffer);
    assert(cap_res.success);

    // 1. Truncated file test
    std::vector<std::uint8_t> truncated(valid_buffer.begin(), valid_buffer.begin() + 30);
    const auto trunc_res = winios::snapshot::validate_snapshot_buffer(truncated.data(), truncated.size());
    assert(!trunc_res.valid);
    assert(trunc_res.error_code == winios::snapshot::SnapshotErrorCode::TruncatedFile);

    // 2. Corrupt Magic test
    auto bad_magic = valid_buffer;
    bad_magic[0] = 'B';
    bad_magic[1] = 'A';
    bad_magic[2] = 'D';
    const auto magic_res = winios::snapshot::validate_snapshot_buffer(bad_magic.data(), bad_magic.size());
    assert(!magic_res.valid);
    assert(magic_res.error_code == winios::snapshot::SnapshotErrorCode::InvalidMagic);

    // 3. Corrupt Section CRC test
    auto corrupt_sec = valid_buffer;
    // Mutate a byte in CPU state payload (offset 64..1192)
    corrupt_sec[100] ^= 0xAA;
    const auto crc_res = winios::snapshot::validate_snapshot_buffer(corrupt_sec.data(), corrupt_sec.size());
    assert(!crc_res.valid);
    assert(crc_res.error_code == winios::snapshot::SnapshotErrorCode::SectionCrcMismatch);

    // 4. Overlapping sections test
    auto overlap_buf = valid_buffer;
    // Decode header, corrupt directory to point to overlapping offset
    winios::snapshot::WSnapHeader hdr{};
    (void)winios::snapshot::decode_wsnap_header(overlap_buf.data(), overlap_buf.size(), hdr);
    // Find second section and point it over first section
    if (hdr.section_count >= 2) {
        const std::size_t sec1_desc_offset = static_cast<std::size_t>(hdr.section_dir_offset) + 40;
        winios::trace::encode_u64_le(overlap_buf.data() + sec1_desc_offset + 8, 64ULL); // point offset to 64
        // Recompute section CRC for modified payload
        const std::uint32_t c = winios::trace::calculate_crc32c(overlap_buf.data() + 64, winios::trace::decode_u64_le(overlap_buf.data() + sec1_desc_offset + 16));
        winios::trace::encode_u32_le(overlap_buf.data() + sec1_desc_offset + 32, c);
        const auto over_res = winios::snapshot::validate_snapshot_buffer(overlap_buf.data(), overlap_buf.size());
        assert(!over_res.valid);
        assert(over_res.error_code == winios::snapshot::SnapshotErrorCode::OverlappingSections);
    }
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "WinIOS Phase 52 Stage 17: Snapshot Tests\n";
    std::cout << "========================================\n";

    test_format_header_round_trip();
    test_section_descriptor_round_trip();
    test_cpu_state_round_trip();
    test_memory_sparse_chunk_round_trip();
    test_virtual_time_and_timer_round_trip();
    test_gic_state_round_trip();
    test_pmu_and_debug_round_trip();
    test_full_system_integration_round_trip();
    test_corruption_handling_and_safety();

    std::cout << "========================================\n";
    std::cout << "ALL SNAPSHOT TESTS PASSED (100% SUCCESS)\n";
    std::cout << "========================================\n";
    return 0;
}
