#include "runtime/linux_boot_milestones.h"

#include <iomanip>
#include <sstream>

namespace winios::runtime {

const char* LinuxBootMilestoneTracker::milestone_name(LinuxBootMilestone m) noexcept {
    switch (m) {
    case LinuxBootMilestone::FirmwareHandoff:   return "FirmwareHandoff";
    case LinuxBootMilestone::KernelEntry:       return "KernelEntry";
    case LinuxBootMilestone::EarlyconActive:    return "EarlyconActive";
    case LinuxBootMilestone::DeviceTreeParsed:  return "DeviceTreeParsed";
    case LinuxBootMilestone::MemoryMapped:      return "MemoryMapped";
    case LinuxBootMilestone::GicInitialized:    return "GicInitialized";
    case LinuxBootMilestone::TimerCalibrated:   return "TimerCalibrated";
    case LinuxBootMilestone::InitramfsUnpacked: return "InitramfsUnpacked";
    case LinuxBootMilestone::UserspaceLaunched:  return "UserspaceLaunched";
    default:                                    return "Unknown";
    }
}

LinuxBootMilestoneTracker::LinuxBootMilestoneTracker() {
    reset();
}

void LinuxBootMilestoneTracker::reset() {
    milestones_.clear();

    const LinuxBootMilestone all_m[] = {
        LinuxBootMilestone::FirmwareHandoff,
        LinuxBootMilestone::KernelEntry,
        LinuxBootMilestone::EarlyconActive,
        LinuxBootMilestone::DeviceTreeParsed,
        LinuxBootMilestone::MemoryMapped,
        LinuxBootMilestone::GicInitialized,
        LinuxBootMilestone::TimerCalibrated,
        LinuxBootMilestone::InitramfsUnpacked,
        LinuxBootMilestone::UserspaceLaunched
    };

    for (auto m : all_m) {
        MilestoneRecord rec;
        rec.milestone = m;
        rec.name = milestone_name(m);
        rec.achieved = false;
        rec.instruction_count = 0;
        rec.pc = 0;
        rec.current_el = 1;
        rec.diagnostic_evidence = "";
        milestones_[m] = rec;
    }
}

void LinuxBootMilestoneTracker::record_milestone(LinuxBootMilestone milestone,
                                                std::uint64_t instruction_count,
                                                std::uint64_t pc,
                                                std::uint8_t el,
                                                const std::string& evidence) {
    auto& rec = milestones_[milestone];
    if (!rec.achieved) {
        rec.achieved = true;
        rec.instruction_count = instruction_count;
        rec.pc = pc;
        rec.current_el = el;
        rec.diagnostic_evidence = evidence;
    }
}

bool LinuxBootMilestoneTracker::is_achieved(LinuxBootMilestone milestone) const noexcept {
    auto it = milestones_.find(milestone);
    if (it != milestones_.end()) {
        return it->second.achieved;
    }
    return false;
}

const MilestoneRecord* LinuxBootMilestoneTracker::get_record(LinuxBootMilestone milestone) const noexcept {
    auto it = milestones_.find(milestone);
    if (it != milestones_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<MilestoneRecord> LinuxBootMilestoneTracker::get_achieved_milestones() const {
    std::vector<MilestoneRecord> result;
    for (const auto& [_, rec] : milestones_) {
        if (rec.achieved) {
            result.push_back(rec);
        }
    }
    return result;
}

std::size_t LinuxBootMilestoneTracker::count_achieved() const noexcept {
    std::size_t c = 0;
    for (const auto& [_, rec] : milestones_) {
        if (rec.achieved) {
            c++;
        }
    }
    return c;
}

void LinuxBootMilestoneTracker::scan_console_line(const std::string& line,
                                                std::uint64_t instruction_count,
                                                std::uint64_t pc,
                                                std::uint8_t el) {
    if (line.empty()) return;

    // 1. KernelEntry: early Linux banner
    if (line.find("Booting Linux on physical CPU") != std::string::npos ||
        line.find("Linux version ") != std::string::npos) {
        record_milestone(LinuxBootMilestone::KernelEntry, instruction_count, pc, el, line);
    }

    // 2. EarlyconActive: earlycon banner or registration
    if (line.find("earlycon: ") != std::string::npos ||
        line.find("earlycon: pl011") != std::string::npos ||
        line.find("console [ttyAMA0]") != std::string::npos ||
        line.find("bootconsole [") != std::string::npos) {
        record_milestone(LinuxBootMilestone::EarlyconActive, instruction_count, pc, el, line);
    }

    // 3. DeviceTreeParsed: Machine model or OF root
    if (line.find("Machine model:") != std::string::npos ||
        line.find("OF: fdt:") != std::string::npos ||
        line.find("Machine: ") != std::string::npos) {
        record_milestone(LinuxBootMilestone::DeviceTreeParsed, instruction_count, pc, el, line);
    }

    // 4. MemoryMapped: Zone ranges or kernel memory layout
    if (line.find("Zone ranges:") != std::string::npos ||
        line.find("Virtual kernel memory layout:") != std::string::npos ||
        line.find("Memory: ") != std::string::npos ||
        line.find("kasan:") != std::string::npos) {
        record_milestone(LinuxBootMilestone::MemoryMapped, instruction_count, pc, el, line);
    }

    // 5. GicInitialized: GICv3 driver output
    if (line.find("GICv3:") != std::string::npos ||
        line.find("GIC: using split EOI/Deactivate mode") != std::string::npos ||
        line.find("irqchip: GICv3") != std::string::npos ||
        line.find("Root IRQ handler: gic_handle_irq") != std::string::npos) {
        record_milestone(LinuxBootMilestone::GicInitialized, instruction_count, pc, el, line);
    }

    // 6. TimerCalibrated: arch_timer or clocksource registration
    if (line.find("Architected cp15 and arch-timer") != std::string::npos ||
        line.find("arch_timer: cp15 timer(s)") != std::string::npos ||
        line.find("clocksource: arch_sys_counter") != std::string::npos) {
        record_milestone(LinuxBootMilestone::TimerCalibrated, instruction_count, pc, el, line);
    }

    // 7. InitramfsUnpacked: rootfs unpacking progress or completion
    if (line.find("Unpacking initramfs...") != std::string::npos ||
        line.find("Freeing initrd memory:") != std::string::npos ||
        line.find("rootfs image is not initramfs") != std::string::npos ||
        line.find("Initramfs unpacking complete") != std::string::npos) {
        record_milestone(LinuxBootMilestone::InitramfsUnpacked, instruction_count, pc, el, line);
    }

    // 8. UserspaceLaunched: init process invocation or userspace prompt
    if (line.find("Run /init as init process") != std::string::npos ||
        line.find("Starting init: /init") != std::string::npos ||
        line.find("Welcome to Linux") != std::string::npos ||
        line.find("/bin/sh:") != std::string::npos ||
        line.find("/ #") != std::string::npos) {
        record_milestone(LinuxBootMilestone::UserspaceLaunched, instruction_count, pc, el, line);
    }
}

void LinuxBootMilestoneTracker::scan_console_text(const std::string& full_text,
                                                std::uint64_t instruction_count,
                                                std::uint64_t pc,
                                                std::uint8_t el) {
    std::istringstream stream(full_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        scan_console_line(line, instruction_count, pc, el);
    }
}

void LinuxBootMilestoneTracker::scan_mmio_event(std::uint64_t pa,
                                              bool is_write,
                                              std::uint64_t val,
                                              std::uint64_t instruction_count,
                                              std::uint64_t pc,
                                              std::uint8_t el) {
    // Detect early UART register access
    if (pa >= 0x09000000ULL && pa < 0x09001000ULL) {
        if (is_write && (pa == 0x09000000ULL)) { // UARTDR write
            std::string ev = "UART0 Data Register write: 0x" + std::to_string(val);
            record_milestone(LinuxBootMilestone::EarlyconActive, instruction_count, pc, el, ev);
        }
    }

    // Detect GICD / GICR MMIO enable
    if (pa >= 0x08000000ULL && pa < 0x08010000ULL) {
        if (is_write && pa == 0x08000000ULL && (val & 0x3u) != 0) { // GICD_CTLR enable
            std::string ev = "GICD_CTLR write: 0x" + std::to_string(val);
            record_milestone(LinuxBootMilestone::GicInitialized, instruction_count, pc, el, ev);
        }
    }
}

std::string LinuxBootMilestoneTracker::dump_report() const {
    std::ostringstream ss;
    ss << "=== WinIOS Linux ARM64 Boot Milestone Report ===\n";
    ss << "Total Milestones Achieved: " << count_achieved() << " / " << milestones_.size() << "\n\n";

    ss << std::left << std::setw(22) << "Milestone"
       << std::setw(12) << "Status"
       << std::setw(16) << "Insns"
       << std::setw(18) << "PC"
       << std::setw(6)  << "EL"
       << "Evidence" << "\n";
    ss << std::string(90, '-') << "\n";

    for (const auto& [_, rec] : milestones_) {
        ss << std::left << std::setw(22) << rec.name
           << std::setw(12) << (rec.achieved ? "ACHIEVED" : "PENDING")
           << std::setw(16) << rec.instruction_count;

        std::ostringstream pc_ss;
        pc_ss << "0x" << std::hex << rec.pc << std::dec;
        ss << std::setw(18) << pc_ss.str()
           << std::setw(6)  << static_cast<int>(rec.current_el)
           << rec.diagnostic_evidence << "\n";
    }

    return ss.str();
}

} // namespace winios::runtime
