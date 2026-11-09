#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/linux_boot_milestones.h"
#include "runtime/linux_boot_session.h"

#define STAGE82_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::runtime;

namespace {

std::vector<std::uint8_t> create_dummy_kernel() {
    std::vector<std::uint8_t> buf(0x10000, 0);
    auto* hdr = reinterpret_cast<loader::LinuxArm64ImageHeader*>(buf.data());
    hdr->code0 = 0x14000010u; // B +64
    hdr->code1 = 0xD503201Fu; // NOP
    hdr->text_offset = 0x80000ULL;
    hdr->image_size = 0x20000ULL;
    hdr->flags = 0x2u;
    hdr->magic = loader::kLinuxArm64ImageMagic;
    for (std::size_t i = 64; i < buf.size(); i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + i) = 0xD503201Fu;
    }
    return buf;
}

// -----------------------------------------------------------------------------
// Suite 1: Milestone Enum and Tracking Lifecycle
// -----------------------------------------------------------------------------
bool test_milestone_lifecycle() {
    std::cout << "[1] test_milestone_lifecycle...\n";

    LinuxBootMilestoneTracker tracker;
    STAGE82_ASSERT(tracker.count_achieved() == 0, "Initial count must be 0");
    STAGE82_ASSERT(!tracker.is_achieved(LinuxBootMilestone::FirmwareHandoff), "FirmwareHandoff pending");
    STAGE82_ASSERT(!tracker.is_achieved(LinuxBootMilestone::UserspaceLaunched), "UserspaceLaunched pending");

    tracker.record_milestone(LinuxBootMilestone::FirmwareHandoff, 100, 0x40080000ULL, 1, "Firmware boot");
    STAGE82_ASSERT(tracker.count_achieved() == 1, "Count must be 1 after recording 1 milestone");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::FirmwareHandoff), "FirmwareHandoff achieved");

    const auto* rec = tracker.get_record(LinuxBootMilestone::FirmwareHandoff);
    STAGE82_ASSERT(rec != nullptr, "Record exists");
    STAGE82_ASSERT(rec->achieved, "Record achieved");
    STAGE82_ASSERT(rec->instruction_count == 100, "Instruction count matches");
    STAGE82_ASSERT(rec->pc == 0x40080000ULL, "PC matches");
    STAGE82_ASSERT(rec->current_el == 1, "EL matches");
    STAGE82_ASSERT(rec->diagnostic_evidence == "Firmware boot", "Evidence matches");

    tracker.reset();
    STAGE82_ASSERT(tracker.count_achieved() == 0, "Count reset to 0");
    STAGE82_ASSERT(!tracker.is_achieved(LinuxBootMilestone::FirmwareHandoff), "Status reset to pending");

    std::cout << "[PASS] Milestone Enum and Tracking Lifecycle\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Firmware Handoff Milestone Recording
// -----------------------------------------------------------------------------
bool test_firmware_handoff_recording() {
    std::cout << "[2] test_firmware_handoff_recording...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_dummy_kernel();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE82_ASSERT(session.initialize(err), "Initialize");

    STAGE82_ASSERT(!session.milestone_tracker().is_achieved(LinuxBootMilestone::FirmwareHandoff),
                   "Milestone not yet achieved before boot");

    STAGE82_ASSERT(session.boot(err), "Boot");

    STAGE82_ASSERT(session.milestone_tracker().is_achieved(LinuxBootMilestone::FirmwareHandoff),
                   "FirmwareHandoff must be achieved after boot call");
    const auto* rec = session.milestone_tracker().get_record(LinuxBootMilestone::FirmwareHandoff);
    STAGE82_ASSERT(rec != nullptr && rec->achieved, "Valid record for FirmwareHandoff");
    STAGE82_ASSERT(rec->pc == 0x40080000ULL, "PC is kernel entry point");

    std::cout << "[PASS] Firmware Handoff Milestone Recording\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Earlycon and Kernel Banner Pattern Matching
// -----------------------------------------------------------------------------
bool test_earlycon_and_kernel_banner_matching() {
    std::cout << "[3] test_earlycon_and_kernel_banner_matching...\n";

    LinuxBootMilestoneTracker tracker;

    // Scan kernel banner line
    tracker.scan_console_line("[    0.000000] Linux version 6.6.0-arm64 (build@win_ios) #1 SMP PREEMPT",
                             1200, 0x40080100ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::KernelEntry), "KernelEntry detected");

    // Scan earlycon banner line
    tracker.scan_console_line("[    0.000000] earlycon: pl011 at MMIO 0x0000000009000000 (ptrval)",
                             1850, 0x40080300ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::EarlyconActive), "EarlyconActive detected");

    STAGE82_ASSERT(tracker.count_achieved() == 2, "2 milestones achieved");

    std::cout << "[PASS] Earlycon and Kernel Banner Pattern Matching\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Device Tree and Memory Layout Milestone Detection
// -----------------------------------------------------------------------------
bool test_device_tree_and_memory_layout_detection() {
    std::cout << "[4] test_device_tree_and_memory_layout_detection...\n";

    LinuxBootMilestoneTracker tracker;

    tracker.scan_console_line("[    0.000000] OF: fdt: Machine model: linux,dummy-virt",
                             2500, 0x40080500ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::DeviceTreeParsed), "DeviceTreeParsed detected");

    tracker.scan_console_line("[    0.000000] Virtual kernel memory layout:\n",
                             3400, 0x40080800ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::MemoryMapped), "MemoryMapped detected");

    std::cout << "[PASS] Device Tree and Memory Layout Milestone Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: GICv3 MMIO Transition and Initialization Recognition
// -----------------------------------------------------------------------------
bool test_gicv3_initialization_recognition() {
    std::cout << "[5] test_gicv3_initialization_recognition...\n";

    LinuxBootMilestoneTracker tracker;

    // MMIO trigger
    tracker.scan_mmio_event(0x08000000ULL, true, 0x3u, 4500, 0x40081000ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::GicInitialized), "GicInitialized detected via MMIO");

    // Console banner trigger
    LinuxBootMilestoneTracker tracker2;
    tracker2.scan_console_line("[    0.000000] GICv3: 256 SPIs implemented", 4600, 0x40081020ULL, 1);
    STAGE82_ASSERT(tracker2.is_achieved(LinuxBootMilestone::GicInitialized), "GicInitialized detected via banner");

    std::cout << "[PASS] GICv3 MMIO Transition and Initialization Recognition\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Architected Timer Calibration Recognition
// -----------------------------------------------------------------------------
bool test_timer_calibration_recognition() {
    std::cout << "[6] test_timer_calibration_recognition...\n";

    LinuxBootMilestoneTracker tracker;

    tracker.scan_console_line("[    0.000000] clocksource: arch_sys_counter: mask: 0xffffffffffffff max_cycles: 0x1cd42e208c, max_idle_ns: 440795202120 ns",
                             6200, 0x40081500ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::TimerCalibrated), "TimerCalibrated detected via clocksource banner");

    std::cout << "[PASS] Architected Timer Calibration Recognition\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Initramfs Unpacking Detection
// -----------------------------------------------------------------------------
bool test_initramfs_unpacking_detection() {
    std::cout << "[7] test_initramfs_unpacking_detection...\n";

    LinuxBootMilestoneTracker tracker;

    tracker.scan_console_line("[    0.050000] Unpacking initramfs...", 15000, 0x40085000ULL, 1);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::InitramfsUnpacked), "InitramfsUnpacked detected");

    std::cout << "[PASS] Initramfs Unpacking Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Userspace Launch and Interactive Prompt Milestone
// -----------------------------------------------------------------------------
bool test_userspace_launch_detection() {
    std::cout << "[8] test_userspace_launch_detection...\n";

    LinuxBootMilestoneTracker tracker;

    tracker.scan_console_line("[    0.120000] Run /init as init process", 25000, 0x40090000ULL, 0);
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::UserspaceLaunched), "UserspaceLaunched detected via /init banner");

    LinuxBootMilestoneTracker tracker2;
    tracker2.scan_console_line("/ # ", 30000, 0x00401000ULL, 0);
    STAGE82_ASSERT(tracker2.is_achieved(LinuxBootMilestone::UserspaceLaunched), "UserspaceLaunched detected via prompt");

    std::cout << "[PASS] Userspace Launch and Interactive Prompt Milestone\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Out-of-order and Redundant Milestone Robustness
// -----------------------------------------------------------------------------
bool test_milestone_robustness_and_redundancy() {
    std::cout << "[9] test_milestone_robustness_and_redundancy...\n";

    LinuxBootMilestoneTracker tracker;

    // First arrival
    tracker.record_milestone(LinuxBootMilestone::EarlyconActive, 1000, 0x40080000ULL, 1, "First banner");
    const auto* r1 = tracker.get_record(LinuxBootMilestone::EarlyconActive);
    STAGE82_ASSERT(r1->instruction_count == 1000, "First insn count kept");
    STAGE82_ASSERT(r1->diagnostic_evidence == "First banner", "First evidence kept");

    // Second redundant arrival with higher count
    tracker.record_milestone(LinuxBootMilestone::EarlyconActive, 2000, 0x40080100ULL, 1, "Second banner");
    const auto* r2 = tracker.get_record(LinuxBootMilestone::EarlyconActive);
    STAGE82_ASSERT(r2->instruction_count == 1000, "Earliest insn count preserved");
    STAGE82_ASSERT(r2->diagnostic_evidence == "First banner", "Earliest evidence preserved");

    std::cout << "[PASS] Out-of-order and Redundant Milestone Robustness\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Integrated Linux Boot Session Milestone Extraction and Report
// -----------------------------------------------------------------------------
bool test_integrated_session_milestone_extraction() {
    std::cout << "[10] test_integrated_session_milestone_extraction...\n";

    LinuxBootConfig cfg;
    cfg.kernel_data = create_dummy_kernel();

    LinuxBootSession session(cfg);
    std::string err;
    STAGE82_ASSERT(session.initialize(err), "Init");
    STAGE82_ASSERT(session.boot(err), "Boot");

    // Simulate kernel early boot console output via UART
    std::string mock_kernel_log =
        "[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x410fd034]\n"
        "[    0.000000] Linux version 6.6.0 (win_ios@host) #1 SMP\n"
        "[    0.000000] earlycon: pl011 at MMIO 0x0000000009000000 (ptrval)\n"
        "[    0.000000] Machine model: linux,dummy-virt\n"
        "[    0.000000] Virtual kernel memory layout:\n"
        "[    0.000000] GICv3: 256 SPIs implemented\n"
        "[    0.000000] clocksource: arch_sys_counter\n"
        "[    0.050000] Unpacking initramfs...\n"
        "[    0.120000] Run /init as init process\n";

    for (char c : mock_kernel_log) {
        session.platform().uart()->write_register(device::VirtualUartDevice::kRegDR, c, 4);
    }

    // Step session so it processes console text
    session.step(10);

    const auto& tracker = session.milestone_tracker();
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::FirmwareHandoff), "FirmwareHandoff achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::KernelEntry), "KernelEntry achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::EarlyconActive), "EarlyconActive achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::DeviceTreeParsed), "DeviceTreeParsed achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::MemoryMapped), "MemoryMapped achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::GicInitialized), "GicInitialized achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::TimerCalibrated), "TimerCalibrated achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::InitramfsUnpacked), "InitramfsUnpacked achieved");
    STAGE82_ASSERT(tracker.is_achieved(LinuxBootMilestone::UserspaceLaunched), "UserspaceLaunched achieved");

    STAGE82_ASSERT(tracker.count_achieved() == 9, "All 9 milestones achieved in synthetic session");

    std::string report = tracker.dump_report();
    STAGE82_ASSERT(!report.empty(), "Report non-empty");
    STAGE82_ASSERT(report.find("9 / 9") != std::string::npos, "Report indicates 9 / 9 achieved");

    std::cout << "Report Output Preview:\n" << report << "\n";
    std::cout << "[PASS] Integrated Linux Boot Session Milestone Extraction and Report\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 82: Linux ARM64 Boot Milestone Integration Tests ===\n";

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

    run_test(test_milestone_lifecycle, "test_milestone_lifecycle");
    run_test(test_firmware_handoff_recording, "test_firmware_handoff_recording");
    run_test(test_earlycon_and_kernel_banner_matching, "test_earlycon_and_kernel_banner_matching");
    run_test(test_device_tree_and_memory_layout_detection, "test_device_tree_and_memory_layout_detection");
    run_test(test_gicv3_initialization_recognition, "test_gicv3_initialization_recognition");
    run_test(test_timer_calibration_recognition, "test_timer_calibration_recognition");
    run_test(test_initramfs_unpacking_detection, "test_initramfs_unpacking_detection");
    run_test(test_userspace_launch_detection, "test_userspace_launch_detection");
    run_test(test_milestone_robustness_and_redundancy, "test_milestone_robustness_and_redundancy");
    run_test(test_integrated_session_milestone_extraction, "test_integrated_session_milestone_extraction");

    std::cout << "\nStage 82 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
