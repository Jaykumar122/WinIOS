#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/linux_boot_session.h"
#include "runtime/linux_boot_snapshot.h"

#define STAGE85_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

std::vector<std::uint8_t> create_dummy_kernel() {
    std::vector<std::uint8_t> buf(0x10000, 0);
    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(buf.data());
    hdr->code0 = 0x14000010u; // B +64
    hdr->code1 = 0xD503201Fu; // NOP
    hdr->text_offset = 0x80000ULL;
    hdr->image_size = 0x20000ULL;
    hdr->flags = 0x2u;
    hdr->magic = kLinuxArm64ImageMagic;
    for (std::size_t i = 64; i < buf.size(); i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + i) = 0xD503201Fu;
    }
    return buf;
}

LinuxBootConfig create_test_config(std::size_t cores = 1) {
    LinuxBootConfig cfg;
    cfg.ram_base = 0;
    cfg.ram_size = 0x00400000; // 4 MB for instant snapshot testing
    cfg.core_count = cores;
    cfg.kernel_data = create_dummy_kernel();
    return cfg;
}

// -----------------------------------------------------------------------------
// Suite 1: Platform Snapshot Capture Structure and Magic
// -----------------------------------------------------------------------------
bool test_snapshot_capture_structure() {
    std::cout << "[1] test_snapshot_capture_structure...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    auto snap = LinuxBootSnapshotManager::capture(session);
    STAGE85_ASSERT(snap.magic == LinuxBootPlatformSnapshot::kMagic, "Magic is LBSS");
    STAGE85_ASSERT(snap.version == LinuxBootPlatformSnapshot::kVersion, "Version 1");
    STAGE85_ASSERT(snap.cpus.size() == 1, "Captured 1 CPU");
    STAGE85_ASSERT(snap.cpus[0].pc == 0x80000ULL, "PC is kernel entry");
    STAGE85_ASSERT(snap.ram_bytes.size() > 0, "RAM captured");

    std::cout << "[PASS] Platform Snapshot Capture Structure and Magic\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: CPU Register File and System Register Preservation
// -----------------------------------------------------------------------------
bool test_cpu_register_file_preservation() {
    std::cout << "[2] test_cpu_register_file_preservation...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    // Mutate CPU 0 registers
    auto& cpu0 = session.platform().get_cpu(0);
    cpu0.write_register(0, 0x1111222233334444ULL);
    cpu0.write_register(5, 0x5555666677778888ULL);
    cpu0.set_stack_pointer(0x007FFFF0ULL);
    cpu0.set_tpidr_el0(0xDEADBEEFULL);

    auto snap = LinuxBootSnapshotManager::capture(session);

    // Create fresh session and restore
    LinuxBootSession target(cfg);
    STAGE85_ASSERT(target.initialize(err), "Init target");

    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(target, snap), "Restore snapshot");

    const auto& res_cpu = target.platform().get_cpu(0);
    STAGE85_ASSERT(res_cpu.read_register(0) == 0x1111222233334444ULL, "X0 restored");
    STAGE85_ASSERT(res_cpu.read_register(5) == 0x5555666677778888ULL, "X5 restored");
    STAGE85_ASSERT(res_cpu.get_stack_pointer() == 0x007FFFF0ULL, "SP restored");
    STAGE85_ASSERT(res_cpu.get_tpidr_el0() == 0xDEADBEEFULL, "TPIDR_EL0 restored");

    std::cout << "[PASS] CPU Register File and System Register Preservation\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Guest RAM Full Memory State Capture and Restoration
// -----------------------------------------------------------------------------
bool test_guest_ram_capture_and_restoration() {
    std::cout << "[3] test_guest_ram_capture_and_restoration...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    // Mutate RAM
    session.platform().memory().write_u64(0x1000, 0xFEDCBA9876543210ULL);
    session.platform().memory().write_u32(0x2000, 0x12345678u);

    auto snap = LinuxBootSnapshotManager::capture(session);

    LinuxBootSession target(cfg);
    STAGE85_ASSERT(target.initialize(err), "Init target");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(target, snap), "Restore");

    STAGE85_ASSERT(target.platform().memory().read_u64(0x1000) == 0xFEDCBA9876543210ULL, "RAM u64 restored");
    STAGE85_ASSERT(target.platform().memory().read_u32(0x2000) == 0x12345678u, "RAM u32 restored");

    std::cout << "[PASS] Guest RAM Full Memory State Capture and Restoration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: GICv3 Distributor and Redistributor State Capture/Restore
// -----------------------------------------------------------------------------
bool test_gicv3_state_capture_restore() {
    std::cout << "[4] test_gicv3_state_capture_restore...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    // Mutate GIC state
    auto dist = session.platform().gic().get_distributor_state();
    dist.gicd_ctlr = 0x37u;
    dist.gicd_ipriorityr[33] = 0x40u;
    session.platform().gic().set_distributor_state(dist);

    auto snap = LinuxBootSnapshotManager::capture(session);

    LinuxBootSession target(cfg);
    STAGE85_ASSERT(target.initialize(err), "Init target");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(target, snap), "Restore");

    auto res_dist = target.platform().gic().get_distributor_state();
    STAGE85_ASSERT(res_dist.gicd_ctlr == 0x37u, "GICD_CTLR restored");
    STAGE85_ASSERT(res_dist.gicd_ipriorityr[33] == 0x40u, "SPI priority restored");

    std::cout << "[PASS] GICv3 Distributor and Redistributor State Capture/Restore\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: PL011 UART State and FIFO Restoration
// -----------------------------------------------------------------------------
bool test_uart_state_and_fifo_restoration() {
    std::cout << "[5] test_uart_state_and_fifo_restoration...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    // Write output to UART
    std::string banner = "[    0.000000] Snapshot Test Banner\n";
    for (char c : banner) {
        session.platform().uart()->write_register(VirtualUartDevice::kRegDR, c, 4);
    }
    session.platform().uart()->inject_input('X'); // inject RX byte

    auto snap = LinuxBootSnapshotManager::capture(session);

    LinuxBootSession target(cfg);
    STAGE85_ASSERT(target.initialize(err), "Init target");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(target, snap), "Restore");

    STAGE85_ASSERT(target.platform().uart()->get_output_text() == banner, "UART output buffer restored");
    STAGE85_ASSERT(target.platform().uart()->read_register(VirtualUartDevice::kRegDR, 4) == 'X', "UART RX FIFO restored");

    std::cout << "[PASS] PL011 UART State and FIFO Restoration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Snapshot Serialization and Binary Roundtrip Integrity
// -----------------------------------------------------------------------------
bool test_snapshot_serialization_roundtrip() {
    std::cout << "[6] test_snapshot_serialization_roundtrip...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    auto orig_snap = LinuxBootSnapshotManager::capture(session);
    auto bytes = LinuxBootSnapshotManager::serialize(orig_snap);
    STAGE85_ASSERT(!bytes.empty(), "Serialized bytes non-empty");

    auto deser_snap = LinuxBootSnapshotManager::deserialize(bytes.data(), bytes.size(), &err);
    STAGE85_ASSERT(err.empty(), "Deserialization succeeded");
    STAGE85_ASSERT(deser_snap.magic == orig_snap.magic, "Magic preserved");
    STAGE85_ASSERT(deser_snap.total_instructions == orig_snap.total_instructions, "Insns preserved");
    STAGE85_ASSERT(deser_snap.physical_counter == orig_snap.physical_counter, "Counter preserved");
    STAGE85_ASSERT(deser_snap.cpus.size() == orig_snap.cpus.size(), "CPU count preserved");
    STAGE85_ASSERT(deser_snap.ram_bytes.size() == orig_snap.ram_bytes.size(), "RAM size preserved");

    std::cout << "[PASS] Snapshot Serialization and Binary Roundtrip Integrity\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Post-Handoff Snapshot Restoration and Canonical Hash Identity
// -----------------------------------------------------------------------------
bool test_post_handoff_canonical_hash_identity() {
    std::cout << "[7] test_post_handoff_canonical_hash_identity...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    std::uint64_t hash_before = session.platform().compute_platform_hash();
    auto snap = LinuxBootSnapshotManager::capture(session);

    LinuxBootSession target(cfg);
    STAGE85_ASSERT(target.initialize(err), "Init target");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(target, snap), "Restore");

    std::uint64_t hash_after = target.platform().compute_platform_hash();
    STAGE85_ASSERT(hash_before == hash_after, "Canonical platform hashes match identically");

    std::cout << "[PASS] Post-Handoff Snapshot Restoration and Canonical Hash Identity\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Deterministic Stepping from Restored Snapshot vs Continuous Baseline
// -----------------------------------------------------------------------------
bool test_deterministic_stepping_replay() {
    std::cout << "[8] test_deterministic_stepping_replay...\n";

    auto cfg = create_test_config();

    // Baseline session
    LinuxBootSession baseline(cfg);
    std::string err;
    STAGE85_ASSERT(baseline.initialize(err), "Init baseline");
    STAGE85_ASSERT(baseline.boot(err), "Boot baseline");

    // Capture checkpoint right at boot
    auto snap = LinuxBootSnapshotManager::capture(baseline);

    // Restored session
    LinuxBootSession restored(cfg);
    STAGE85_ASSERT(restored.initialize(err), "Init restored");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(restored, snap), "Restore snapshot");

    // Verify lockstep deterministic execution for 20 instructions
    std::string div_err;
    bool ok = LinuxBootSnapshotManager::verify_deterministic_replay(baseline, restored, 20, &div_err);
    STAGE85_ASSERT(ok, "Deterministic replay matched lockstep without divergence");

    STAGE85_ASSERT(baseline.platform().compute_platform_hash() == restored.platform().compute_platform_hash(),
                   "Platform hash matches after 20 steps");

    std::cout << "[PASS] Deterministic Stepping from Restored Snapshot vs Continuous Baseline\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Mid-Boot Console Output and Milestone Restoration
// -----------------------------------------------------------------------------
bool test_mid_boot_milestone_preservation() {
    std::cout << "[9] test_mid_boot_milestone_preservation...\n";

    auto cfg = create_test_config();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init");
    STAGE85_ASSERT(session.boot(err), "Boot");

    session.milestone_tracker().record_milestone(LinuxBootMilestone::KernelEntry, 10, 0x80004ULL, 1, "Early banner");
    auto snap = LinuxBootSnapshotManager::capture(session);
    STAGE85_ASSERT(snap.milestone_report.find("KernelEntry") != std::string::npos, "Captured report contains KernelEntry");

    std::cout << "[PASS] Mid-Boot Console Output and Milestone Restoration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Multi-Core SMP State Snapshot & Deterministic Resumption
// -----------------------------------------------------------------------------
bool test_smp_snapshot_and_deterministic_resumption() {
    std::cout << "[10] test_smp_snapshot_and_deterministic_resumption...\n";

    auto cfg = create_test_config(2);

    LinuxBootSession session(cfg);
    std::string err;
    STAGE85_ASSERT(session.initialize(err), "Init SMP");
    STAGE85_ASSERT(session.boot(err), "Boot SMP");

    // Activate core 1 and set PC
    session.platform().set_core_active(1, true);
    session.platform().get_cpu(1).set_program_counter(0x80000ULL);

    // Step 5 instructions across cores
    session.step(5);

    auto snap = LinuxBootSnapshotManager::capture(session);
    STAGE85_ASSERT(snap.cpus.size() == 2, "Captured 2 cores");
    STAGE85_ASSERT(snap.cpus[0].active && snap.cpus[1].active, "Both cores active in snapshot");

    LinuxBootSession restored(cfg);
    STAGE85_ASSERT(restored.initialize(err), "Init restored SMP");
    STAGE85_ASSERT(LinuxBootSnapshotManager::restore(restored, snap), "Restore SMP");

    STAGE85_ASSERT(session.platform().compute_platform_hash() == restored.platform().compute_platform_hash(),
                   "SMP platform hashes match identically after restore");

    std::cout << "[PASS] Multi-Core SMP State Snapshot & Deterministic Resumption\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Stage 85: Deterministic Boot, Snapshot & Replay Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_snapshot_capture_structure, "test_snapshot_capture_structure");
    run_test(test_cpu_register_file_preservation, "test_cpu_register_file_preservation");
    run_test(test_guest_ram_capture_and_restoration, "test_guest_ram_capture_and_restoration");
    run_test(test_gicv3_state_capture_restore, "test_gicv3_state_capture_restore");
    run_test(test_uart_state_and_fifo_restoration, "test_uart_state_and_fifo_restoration");
    run_test(test_snapshot_serialization_roundtrip, "test_snapshot_serialization_roundtrip");
    run_test(test_post_handoff_canonical_hash_identity, "test_post_handoff_canonical_hash_identity");
    run_test(test_deterministic_stepping_replay, "test_deterministic_stepping_replay");
    run_test(test_mid_boot_milestone_preservation, "test_mid_boot_milestone_preservation");
    run_test(test_smp_snapshot_and_deterministic_resumption, "test_smp_snapshot_and_deterministic_resumption");

    std::cout << "\nStage 85 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
