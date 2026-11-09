#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "app/linux_boot_cli.h"
#include "cpu/cpu.h"
#include "device/fdt_builder.h"
#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"
#include "firmware/virtual_firmware.h"
#include "loader/guest_memory_map.h"
#include "loader/initramfs_loader.h"
#include "loader/linux_image_loader.h"
#include "loader/rootfs_builder.h"
#include "runtime/linux_boot_milestones.h"
#include "runtime/linux_boot_session.h"
#include "runtime/linux_boot_snapshot.h"
#include "runtime/linux_syscall_abi.h"
#include "runtime/userspace_runner.h"

#define STAGE86_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::app;
using namespace winios::device;
using namespace winios::firmware;
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
        *reinterpret_cast<std::uint32_t*>(buf.data() + i) = 0xD503201Fu; // NOP
    }
    return buf;
}

LinuxBootConfig create_audit_config(std::size_t cores = 1) {
    LinuxBootConfig cfg;
    cfg.ram_base = 0;
    cfg.ram_size = 0x00400000; // 4 MB for instant unit testing
    cfg.core_count = cores;
    cfg.boot_args = "console=ttyAMA0 earlycon=pl011,0x09000000 init=/init";
    cfg.kernel_data = create_dummy_kernel();
    return cfg;
}

// -----------------------------------------------------------------------------
// Suite 1: Virtual Firmware Lifecycle & Transition Integrity (Stage 73)
// -----------------------------------------------------------------------------
bool test_firmware_lifecycle_state_machine() {
    std::cout << "[1] test_firmware_lifecycle_state_machine...\n";

    FirmwareConfig cfg;
    cfg.ram_base = 0;
    cfg.ram_size = 0x00400000;
    cfg.core_count = 1;

    VirtualFirmware fw(cfg);
    STAGE86_ASSERT(fw.state() == BootLifecycleState::Uninitialized, "State is Uninitialized");
    STAGE86_ASSERT(std::string(boot_lifecycle_state_to_string(fw.state())) == "Uninitialized", "String name");

    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 1;
    plat_cfg.ram_base = 0;
    plat_cfg.ram_size = 0x00400000;
    VirtualPlatform plat(plat_cfg);

    // Platform init
    STAGE86_ASSERT(fw.initialize_platform(plat), "Platform init");
    STAGE86_ASSERT(fw.state() == BootLifecycleState::PlatformInitialized, "State is PlatformInitialized");

    // Firmware init
    STAGE86_ASSERT(fw.initialize_firmware(), "Firmware init");
    STAGE86_ASSERT(fw.state() == BootLifecycleState::FirmwareInitialized, "State is FirmwareInitialized");

    // Illegal transition check: handoff without prepare
    STAGE86_ASSERT(!fw.handoff_to_guest(), "Handoff without prepare fails");
    STAGE86_ASSERT(fw.state() == BootLifecycleState::Error, "Transitions to Error");

    // Reset firmware for proper lifecycle run
    VirtualFirmware fw2(cfg);
    STAGE86_ASSERT(fw2.initialize_platform(plat), "Platform init 2");
    STAGE86_ASSERT(fw2.initialize_firmware(), "Firmware init 2");
    STAGE86_ASSERT(fw2.prepare_guest(0x80000ULL, 0x3FE000ULL, 0, 0), "Prepare guest");
    STAGE86_ASSERT(fw2.state() == BootLifecycleState::GuestPrepared, "GuestPrepared");

    STAGE86_ASSERT(fw2.handoff_to_guest(), "Handoff to guest");
    STAGE86_ASSERT(fw2.state() == BootLifecycleState::GuestHandoff, "GuestHandoff");

    STAGE86_ASSERT(fw2.start_execution(), "Start execution");
    STAGE86_ASSERT(fw2.state() == BootLifecycleState::Running, "Running");

    STAGE86_ASSERT(fw2.halt_guest(), "Halt guest");
    STAGE86_ASSERT(fw2.state() == BootLifecycleState::Halted, "Halted");

    STAGE86_ASSERT(fw2.shutdown(), "Shutdown");
    STAGE86_ASSERT(fw2.state() == BootLifecycleState::Shutdown, "Shutdown");

    std::cout << "[PASS] Virtual Firmware Lifecycle & Transition Integrity\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Linux ARM64 Boot Protocol Register & CPU Contract (Stage 74)
// -----------------------------------------------------------------------------
bool test_linux_arm64_boot_protocol_contract() {
    std::cout << "[2] test_linux_arm64_boot_protocol_contract...\n";

    auto cfg = create_audit_config(2);
    LinuxBootSession session(cfg);
    std::string err;
    STAGE86_ASSERT(session.initialize(err), "Init");
    STAGE86_ASSERT(session.boot(err), "Boot");

    const auto* dtb_region = session.memory_map().find_region(GuestMemoryRegionType::DeviceTree);
    STAGE86_ASSERT(dtb_region != nullptr, "DTB region registered in memory map");

    const auto& primary = session.platform().get_cpu(0);
    STAGE86_ASSERT(primary.read_register(0) == dtb_region->base_address, "X0 has DTB pointer");
    STAGE86_ASSERT(primary.read_register(1) == 0, "X1 == 0");
    STAGE86_ASSERT(primary.read_register(2) == 0, "X2 == 0");
    STAGE86_ASSERT(primary.read_register(3) == 0, "X3 == 0");
    for (std::size_t r = 4; r < 31; ++r) {
        STAGE86_ASSERT(primary.read_register(r) == 0, "X4..X30 == 0");
    }

    STAGE86_ASSERT(primary.get_program_counter() == 0x80000ULL, "PC is kernel entry");
    STAGE86_ASSERT(primary.get_stack_pointer() == 0, "SP == 0 per protocol");
    STAGE86_ASSERT(primary.get_current_el() == 1, "CurrentEL == EL1");
    STAGE86_ASSERT((primary.get_daif() & 0xFu) == 0xFu, "All interrupts masked (DAIF=0xF)");
    STAGE86_ASSERT(((primary.get_cpacr_el1() >> 20) & 3u) == 3u, "CPACR_EL1 FP/SIMD enabled");
    STAGE86_ASSERT((primary.get_sctlr_el1() & 1u) == 0u, "SCTLR_EL1 MMU disabled at boot");

    // Secondary core 1 held quiescent
    STAGE86_ASSERT(!session.platform().is_core_active(1), "Core 1 quiescent");
    const auto& sec = session.platform().get_cpu(1);
    STAGE86_ASSERT(sec.get_program_counter() == 0, "Secondary PC == 0");

    std::cout << "[PASS] Linux ARM64 Boot Protocol Register & CPU Contract\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Flattened Device Tree (FDT/DTB) Structural Conformance (Stage 75)
// -----------------------------------------------------------------------------
bool test_fdt_dtb_structural_conformance() {
    std::cout << "[3] test_fdt_dtb_structural_conformance...\n";

    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 2;
    plat_cfg.ram_base = 0x40000000ULL;
    plat_cfg.ram_size = 0x08000000;

    auto dtb = FdtBuilder::build_platform_dtb(plat_cfg, "console=ttyAMA0 earlycon", 0x41000000ULL, 0x41200000ULL);
    STAGE86_ASSERT(!dtb.empty(), "DTB blob generated");

    // Standard big-endian magic: 0xD0, 0x0D, 0xFE, 0xED
    STAGE86_ASSERT(dtb.size() >= sizeof(FdtRawHeader), "Size >= header");
    STAGE86_ASSERT(dtb[0] == 0xD0 && dtb[1] == 0x0D && dtb[2] == 0xFE && dtb[3] == 0xED, "FDT magic 0xD00DFEED");

    std::cout << "[PASS] Flattened Device Tree (FDT/DTB) Structural Conformance\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Linux Generic Interrupt & Architectural Timer Subsystem (Stage 76)
// -----------------------------------------------------------------------------
bool test_generic_interrupt_and_timer_integration() {
    std::cout << "[4] test_generic_interrupt_and_timer_integration...\n";

    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 1;
    VirtualPlatform plat(plat_cfg);

    // 1. Virtual Time & Architectural Counter
    auto& vt = plat.virtual_time();
    STAGE86_ASSERT(vt.get_frequency() > 0, "Frequency configured");
    vt.set_physical_counter(1000);
    STAGE86_ASSERT(vt.get_physical_counter() == 1000, "Physical counter set");
    vt.advance_physical_counter(50);
    STAGE86_ASSERT(vt.get_physical_counter() == 1050, "Physical counter advanced");

    // 2. GICv3 Distributor & Priority Masking
    auto& gic = plat.gic();
    gic.write_icc_pmr(0xF0u, 0);
    STAGE86_ASSERT(gic.read_icc_pmr(0) == 0xF0u, "ICC_PMR set");

    std::cout << "[PASS] Linux Generic Interrupt & Architectural Timer Subsystem\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: PL011 UART & Early Console Line Framing (Stage 77)
// -----------------------------------------------------------------------------
bool test_pl011_uart_and_earlycon_compatibility() {
    std::cout << "[5] test_pl011_uart_and_earlycon_compatibility...\n";

    VirtualUartDevice uart(0x55415254u, 0x09000000ULL, 0x1000ULL, 33u);
    STAGE86_ASSERT(uart.get_output_text().empty(), "Initially empty");

    // Transmit character stream
    const std::string banner = "[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x410fd034]\n";
    for (char c : banner) {
        uart.write_register(VirtualUartDevice::kRegDR, static_cast<std::uint64_t>(c), 4);
    }
    STAGE86_ASSERT(uart.get_output_text() == banner, "Transmitted text matches exact banner");

    // Flag register checks
    std::uint32_t fr = static_cast<std::uint32_t>(uart.read_register(VirtualUartDevice::kRegFR, 4));
    STAGE86_ASSERT((fr & VirtualUartDevice::kFR_TXFE) != 0, "Transmit FIFO is empty");
    STAGE86_ASSERT((fr & VirtualUartDevice::kFR_RXFE) != 0, "Receive FIFO is empty initially");

    // Input injection
    uart.inject_input('L');
    fr = static_cast<std::uint32_t>(uart.read_register(VirtualUartDevice::kRegFR, 4));
    STAGE86_ASSERT((fr & VirtualUartDevice::kFR_RXFE) == 0, "Receive FIFO has byte");
    STAGE86_ASSERT(uart.read_register(VirtualUartDevice::kRegDR, 4) == 'L', "Read received byte");

    std::cout << "[PASS] PL011 UART & Early Console Line Framing\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Guest RAM Memory Map & Reserved Regions Collision Guard (Stage 78)
// -----------------------------------------------------------------------------
bool test_guest_ram_layout_and_collision_guard() {
    std::cout << "[6] test_guest_ram_layout_and_collision_guard...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000); // 128 MB RAM
    STAGE86_ASSERT(map.register_kernel_image(0x40080000ULL, 0x00200000), "Register kernel");
    STAGE86_ASSERT(map.register_initramfs(0x40400000ULL, 0x00100000), "Register initramfs");
    STAGE86_ASSERT(map.register_dtb(0x47FE0000ULL, 0x00004000), "Register DTB");

    std::string err;
    STAGE86_ASSERT(map.validate(&err) == MemoryMapValidationResult::Valid, "Layout is valid without collisions");

    // Overlapping registration should fail validation
    STAGE86_ASSERT(map.register_reserved("OverlapReserved", 0x40070000ULL, 0x00020000), "Registered reserved span");
    STAGE86_ASSERT(map.validate(&err) == MemoryMapValidationResult::RegionOverlap, "Collision detected and rejected");

    std::cout << "[PASS] Guest RAM Memory Map & Reserved Regions Collision Guard\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Linux ARM64 Image Header & Kernel Placement Verification (Stage 79)
// -----------------------------------------------------------------------------
bool test_linux_image_loader_and_placement() {
    std::cout << "[7] test_linux_image_loader_and_placement...\n";

    auto kernel_buf = create_dummy_kernel();
    Memory mem(0x01000000); // 16 MB

    auto res = LinuxImageLoader::load_from_buffer(kernel_buf.data(), kernel_buf.size(), mem, 0, 0x01000000);
    STAGE86_ASSERT(res.is_success(), "Kernel loaded successfully");
    STAGE86_ASSERT(res.entry_point == 0x80000ULL, "Entry point is 0x80000");
    STAGE86_ASSERT(res.load_address == 0x80000ULL, "Load address is 0x80000");
    STAGE86_ASSERT(res.effective_size == 0x20000ULL, "Effective size matches header image_size");

    // Verify first instruction copied into memory
    STAGE86_ASSERT(mem.read_u32(0x80000ULL) == 0x14000010u, "First instruction B +64 in memory");

    std::cout << "[PASS] Linux ARM64 Image Header & Kernel Placement Verification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Initramfs CPIO Generation & Load Placement (Stage 80, 84)
// -----------------------------------------------------------------------------
bool test_initramfs_cpio_generation_and_loading() {
    std::cout << "[8] test_initramfs_cpio_generation_and_loading...\n";

    RootfsBuilder builder;
    builder.add_directory("/bin");
    builder.add_directory("/dev");
    builder.add_directory("/proc");

    const std::string init_script = "#!/bin/sh\necho 'WinIOS Linux Ready'\nexit 0\n";
    builder.add_file_text("/init", init_script, 0100755);

    auto cpio = builder.build_cpio();
    STAGE86_ASSERT(!cpio.empty(), "CPIO archive built non-empty");
    STAGE86_ASSERT(RootfsBuilder::has_file(cpio.data(), cpio.size(), "/init"), "Archive contains '/init'");

    std::vector<std::uint8_t> content;
    STAGE86_ASSERT(RootfsBuilder::get_file_content(cpio.data(), cpio.size(), "/init", content), "Extracted content");
    std::string content_str(content.begin(), content.end());
    STAGE86_ASSERT(content_str == init_script, "Content matches script");

    Memory mem(0x01000000);
    GuestMemoryMap map(0, 0x01000000);
    auto load_res = InitramfsLoader::load_from_buffer(cpio.data(), cpio.size(), mem, 0x200000ULL, 0, 0x01000000, &map);
    STAGE86_ASSERT(load_res.is_success(), "Initramfs loaded into RAM");
    STAGE86_ASSERT(load_res.load_address == 0x200000ULL, "Start address 0x200000");
    STAGE86_ASSERT(load_res.size == cpio.size(), "Size matches CPIO archive size");

    std::cout << "[PASS] Initramfs CPIO Generation & Load Placement\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Unified Linux Boot Session & CLI Subsystem (Stage 81)
// -----------------------------------------------------------------------------
bool test_unified_linux_boot_cli_and_session_orchestration() {
    std::cout << "[9] test_unified_linux_boot_cli_and_session_orchestration...\n";

    char arg0[] = "winios_linux";
    char arg1[] = "--kernel";
    char arg2[] = "vmlinuz.dummy";
    char arg3[] = "--memory";
    char arg4[] = "128";
    char arg5[] = "--smp";
    char arg6[] = "2";
    char arg7[] = "--append";
    char arg8[] = "console=ttyAMA0 root=/dev/ram0";
    char arg9[] = "--max-insns";
    char arg10[] = "5000";
    char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10 };
    int argc = sizeof(argv) / sizeof(argv[0]);

    LinuxBootConfig parsed_cfg;
    std::string err;
    STAGE86_ASSERT(LinuxBootCli::parse_arguments(argc, argv, parsed_cfg, err), "Parse args succeeded");
    STAGE86_ASSERT(parsed_cfg.kernel_path == "vmlinuz.dummy", "Kernel path");
    STAGE86_ASSERT(parsed_cfg.ram_size == 128ULL * 1024 * 1024, "RAM size 128MB");
    STAGE86_ASSERT(parsed_cfg.core_count == 2, "Core count 2");
    STAGE86_ASSERT(parsed_cfg.boot_args == "console=ttyAMA0 root=/dev/ram0", "Boot args");
    STAGE86_ASSERT(parsed_cfg.max_instructions == 5000, "Max steps 5000");

    std::cout << "[PASS] Unified Linux Boot Session & CLI Subsystem\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Nine Canonical Boot Milestones Tracking (Stage 82)
// -----------------------------------------------------------------------------
bool test_canonical_boot_milestone_tracking() {
    std::cout << "[10] test_canonical_boot_milestone_tracking...\n";

    LinuxBootMilestoneTracker tracker;
    STAGE86_ASSERT(tracker.count_achieved() == 0, "Initially 0 milestones");

    tracker.record_milestone(LinuxBootMilestone::FirmwareHandoff, 0, 0x80000ULL, 1, "Firmware handoff");
    tracker.record_milestone(LinuxBootMilestone::KernelEntry, 1, 0x80004ULL, 1, "Kernel entry branch");
    STAGE86_ASSERT(tracker.is_achieved(LinuxBootMilestone::FirmwareHandoff), "FirmwareHandoff reached");
    STAGE86_ASSERT(tracker.is_achieved(LinuxBootMilestone::KernelEntry), "KernelEntry reached");
    STAGE86_ASSERT(!tracker.is_achieved(LinuxBootMilestone::UserspaceLaunched), "Userspace not yet reached");

    // Automatic pattern scanner
    tracker.scan_console_line("[    0.000000] earlycon: pl011 at MMIO 0x09000000 (options '38400n8')", 10, 0x80010ULL, 1);
    STAGE86_ASSERT(tracker.is_achieved(LinuxBootMilestone::EarlyconActive), "EarlyconActive detected from console");

    tracker.scan_console_line("[    0.000000] OF: fdt: Machine model: linux,dummy-virt", 20, 0x80020ULL, 1);
    STAGE86_ASSERT(tracker.is_achieved(LinuxBootMilestone::DeviceTreeParsed), "DeviceTreeParsed detected");

    tracker.scan_console_line("Run /init as init process", 100, 0x80100ULL, 0);
    STAGE86_ASSERT(tracker.is_achieved(LinuxBootMilestone::UserspaceLaunched), "UserspaceLaunched detected");

    auto report = tracker.dump_report();
    STAGE86_ASSERT(!report.empty(), "Report generated");

    std::cout << "[PASS] Nine Canonical Boot Milestones Tracking\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 11: Linux Userspace Syscall ABI Compliance (Stage 83)
// -----------------------------------------------------------------------------
bool test_linux_syscall_abi_compliance() {
    std::cout << "[11] test_linux_syscall_abi_compliance...\n";

    STAGE86_ASSERT(LinuxSyscalls::kWrite == 64, "write == 64");
    STAGE86_ASSERT(LinuxSyscalls::kExit == 93, "exit == 93");
    STAGE86_ASSERT(LinuxSyscalls::kGetpid == 172, "getpid == 172");
    STAGE86_ASSERT(LinuxSyscalls::kBrk == 214, "brk == 214");

    CPU cpu;
    cpu.write_register(8, LinuxSyscalls::kWrite);
    cpu.write_register(0, 1);                  // fd = stdout
    cpu.write_register(1, 0x1000ULL);          // buf
    cpu.write_register(2, 12);                 // count = 12
    cpu.set_stack_pointer(0x007FFFF0ULL);      // 16-byte aligned

    auto ctx = LinuxSyscallContext::capture(cpu);
    STAGE86_ASSERT(ctx.syscall_nr == 64, "Syscall nr 64");
    STAGE86_ASSERT(ctx.args[0] == 1 && ctx.args[1] == 0x1000ULL && ctx.args[2] == 12, "Args match");
    STAGE86_ASSERT(LinuxSyscallAbi::is_sp_aligned(cpu.get_stack_pointer()), "SP is aligned");

    ctx.commit_return(cpu, 12);
    STAGE86_ASSERT(cpu.read_register(0) == 12, "Result 12 applied to X0");

    STAGE86_ASSERT(LinuxSyscallAbi::is_error_return(-22), "-22 is error return");

    std::cout << "[PASS] Linux Userspace Syscall ABI Compliance\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 12: Minimal Userspace Execution (`/init`) (Stage 84)
// -----------------------------------------------------------------------------
bool test_minimal_userspace_runner_execution() {
    std::cout << "[12] test_minimal_userspace_runner_execution...\n";

    Memory mem(0x00200000); // 2 MB
    CPU cpu;

    // Load message at 0x1000
    const std::string banner = "Hello from /init userspace!\n";
    for (std::size_t i = 0; i < banner.size(); ++i) {
        mem.write_u8(0x1000ULL + i, static_cast<std::uint8_t>(banner[i]));
    }

    // Write binary program at 0x0000:
    // 0x00: MOVZ X8, #64         (0xD2800808u)
    // 0x04: MOVZ X0, #1          (0xD2800020u)
    // 0x08: MOVZ X1, #0x1000     (0xD2820001u)
    // 0x0C: MOVZ X2, #banner_sz  (0xD2800002u | (banner.size() << 5))
    // 0x10: SVC #0               (0xD4000001u)
    // 0x14: MOVZ X8, #93         (0xD2800BA8u)
    // 0x18: MOVZ X0, #0          (0xD2800000u)
    // 0x1C: SVC #0               (0xD4000001u)
    std::vector<std::uint8_t> code(32, 0);
    auto* p32 = reinterpret_cast<std::uint32_t*>(code.data());
    p32[0] = 0xD2800808u;
    p32[1] = 0xD2800020u;
    p32[2] = 0xD2820001u;
    p32[3] = 0xD2800002u | (static_cast<std::uint32_t>(banner.size()) << 5u);
    p32[4] = 0xD4000001u;
    p32[5] = 0xD2800BA8u;
    p32[6] = 0xD2800000u;
    p32[7] = 0xD4000001u;

    UserspaceConfig ucfg;
    ucfg.entry_point = 0x00ULL;
    ucfg.stack_pointer = 0x001FFF00ULL;
    ucfg.code = code;
    ucfg.max_instructions = 100;

    auto res = UserspaceRunner::run(cpu, mem, ucfg);
    STAGE86_ASSERT(res.exited, "Process exited");
    STAGE86_ASSERT(res.exit_code == 0, "Exit code is 0");
    STAGE86_ASSERT(res.console_output == banner, "Userspace output matches banner");

    std::cout << "[PASS] Minimal Userspace Execution (`/init`)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 13: Deterministic Snapshot Checkpointing & Bit-Exact Replay (Stage 85)
// -----------------------------------------------------------------------------
bool test_deterministic_snapshot_and_replay_verification() {
    std::cout << "[13] test_deterministic_snapshot_and_replay_verification...\n";

    auto cfg = create_audit_config();
    LinuxBootSession baseline(cfg);
    std::string err;
    STAGE86_ASSERT(baseline.initialize(err), "Init baseline");
    STAGE86_ASSERT(baseline.boot(err), "Boot baseline");

    // Capture checkpoint
    auto snap = LinuxBootSnapshotManager::capture(baseline);
    STAGE86_ASSERT(snap.magic == LinuxBootPlatformSnapshot::kMagic, "Valid snapshot magic");

    // Restore into fresh session
    LinuxBootSession restored(cfg);
    STAGE86_ASSERT(restored.initialize(err), "Init target");
    STAGE86_ASSERT(LinuxBootSnapshotManager::restore(restored, snap), "Restore snapshot");

    // Verify lockstep execution for 10 steps
    std::string divergence;
    bool lockstep = LinuxBootSnapshotManager::verify_deterministic_replay(baseline, restored, 10, &divergence);
    STAGE86_ASSERT(lockstep, "Lockstep execution bit-exact without divergence");

    std::cout << "[PASS] Deterministic Snapshot Checkpointing & Bit-Exact Replay\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 14: Full-System Virtual Machine Readiness Certification (Stage 86)
// -----------------------------------------------------------------------------
bool test_full_system_virtual_machine_readiness_certification() {
    std::cout << "[14] test_full_system_virtual_machine_readiness_certification...\n";

    // Build complete boot configuration
    auto cfg = create_audit_config(1);
    RootfsBuilder rootfs_builder;
    rootfs_builder.add_directory("/bin");
    rootfs_builder.add_file_text("/init", "#!/bin/sh\nexit 0\n", 0100755);
    cfg.initramfs_data = rootfs_builder.build_cpio();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE86_ASSERT(session.initialize(err), "Platform + Kernel + Initramfs initialized");
    STAGE86_ASSERT(session.boot(err), "Firmware handoff to Linux ARM64 kernel succeeded");

    // Execute 15 kernel instructions
    std::size_t stepped = session.step(15);
    STAGE86_ASSERT(stepped == 15, "Stepped 15 instructions");
    STAGE86_ASSERT(session.status() == LinuxSessionStatus::Running, "Session status Running");

    // Check milestone tracking
    STAGE86_ASSERT(session.milestone_tracker().is_achieved(LinuxBootMilestone::FirmwareHandoff),
                   "Milestone FirmwareHandoff reached");

    // Check platform state hash stability
    std::uint64_t hash = session.platform().compute_platform_hash();
    STAGE86_ASSERT(hash != 0, "Canonical platform hash computed non-zero");

    std::cout << "[PASS] Full-System Virtual Machine Readiness Certification\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Stage 86: Full Linux ARM64 VM Readiness Audit ===\n\n";

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

    run_test(test_firmware_lifecycle_state_machine, "test_firmware_lifecycle_state_machine");
    run_test(test_linux_arm64_boot_protocol_contract, "test_linux_arm64_boot_protocol_contract");
    run_test(test_fdt_dtb_structural_conformance, "test_fdt_dtb_structural_conformance");
    run_test(test_generic_interrupt_and_timer_integration, "test_generic_interrupt_and_timer_integration");
    run_test(test_pl011_uart_and_earlycon_compatibility, "test_pl011_uart_and_earlycon_compatibility");
    run_test(test_guest_ram_layout_and_collision_guard, "test_guest_ram_layout_and_collision_guard");
    run_test(test_linux_image_loader_and_placement, "test_linux_image_loader_and_placement");
    run_test(test_initramfs_cpio_generation_and_loading, "test_initramfs_cpio_generation_and_loading");
    run_test(test_unified_linux_boot_cli_and_session_orchestration, "test_unified_linux_boot_cli_and_session_orchestration");
    run_test(test_canonical_boot_milestone_tracking, "test_canonical_boot_milestone_tracking");
    run_test(test_linux_syscall_abi_compliance, "test_linux_syscall_abi_compliance");
    run_test(test_minimal_userspace_runner_execution, "test_minimal_userspace_runner_execution");
    run_test(test_deterministic_snapshot_and_replay_verification, "test_deterministic_snapshot_and_replay_verification");
    run_test(test_full_system_virtual_machine_readiness_certification, "test_full_system_virtual_machine_readiness_certification");

    std::cout << "\nStage 86 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}