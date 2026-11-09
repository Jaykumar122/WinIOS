#include "runtime/darwin_boot_milestones.h"

#include <iomanip>
#include <sstream>

namespace winios::runtime {

namespace {

std::string to_hex_str(std::uint64_t val) {
    std::ostringstream ss;
    ss << std::hex << val;
    return ss.str();
}

} // anonymous namespace

DarwinBootMilestoneTracker::DarwinBootMilestoneTracker() {
    reset();
}

void DarwinBootMilestoneTracker::reset() {
    milestones_.clear();

    const std::vector<DarwinBootMilestone> all = {
        DarwinBootMilestone::BootArgsParsed,
        DarwinBootMilestone::DeviceTreeParsed,
        DarwinBootMilestone::PlatformExpertInit,
        DarwinBootMilestone::InterruptControllerMatched,
        DarwinBootMilestone::PowerManagerMatched,
        DarwinBootMilestone::DisplayFramebufferMatched,
        DarwinBootMilestone::ConsoleUartMatched,
        DarwinBootMilestone::IOKitCatalogPopulated,
        DarwinBootMilestone::DriverMatchingComplete,
        DarwinBootMilestone::RootFilesystemLocated,
        DarwinBootMilestone::LaunchdSpawnPrepared
    };

    for (auto m : all) {
        DarwinMilestoneRecord r;
        r.milestone = m;
        r.name = milestone_name(m);
        r.achieved = false;
        r.instruction_count = 0;
        r.pc = 0;
        r.current_el = 1;
        milestones_[m] = r;
    }
}

const char* DarwinBootMilestoneTracker::milestone_name(DarwinBootMilestone m) noexcept {
    switch (m) {
        case DarwinBootMilestone::BootArgsParsed:             return "BootArgsParsed";
        case DarwinBootMilestone::DeviceTreeParsed:           return "DeviceTreeParsed";
        case DarwinBootMilestone::PlatformExpertInit:         return "PlatformExpertInit";
        case DarwinBootMilestone::InterruptControllerMatched: return "InterruptControllerMatched";
        case DarwinBootMilestone::PowerManagerMatched:        return "PowerManagerMatched";
        case DarwinBootMilestone::DisplayFramebufferMatched:  return "DisplayFramebufferMatched";
        case DarwinBootMilestone::ConsoleUartMatched:         return "ConsoleUartMatched";
        case DarwinBootMilestone::IOKitCatalogPopulated:      return "IOKitCatalogPopulated";
        case DarwinBootMilestone::DriverMatchingComplete:     return "DriverMatchingComplete";
        case DarwinBootMilestone::RootFilesystemLocated:      return "RootFilesystemLocated";
        case DarwinBootMilestone::LaunchdSpawnPrepared:       return "LaunchdSpawnPrepared";
    }
    return "UnknownMilestone";
}

void DarwinBootMilestoneTracker::record_milestone(
    DarwinBootMilestone milestone,
    std::uint64_t instruction_count,
    std::uint64_t pc,
    std::uint8_t el,
    const std::string& evidence) {

    auto it = milestones_.find(milestone);
    if (it != milestones_.end()) {
        if (!it->second.achieved) {
            it->second.achieved = true;
            it->second.instruction_count = instruction_count;
            it->second.pc = pc;
            it->second.current_el = el;
            it->second.diagnostic_evidence = evidence;
        }
    }
}

bool DarwinBootMilestoneTracker::is_achieved(DarwinBootMilestone milestone) const noexcept {
    auto it = milestones_.find(milestone);
    if (it != milestones_.end()) {
        return it->second.achieved;
    }
    return false;
}

const DarwinMilestoneRecord* DarwinBootMilestoneTracker::get_record(DarwinBootMilestone milestone) const noexcept {
    auto it = milestones_.find(milestone);
    if (it != milestones_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<DarwinMilestoneRecord> DarwinBootMilestoneTracker::get_achieved_milestones() const {
    std::vector<DarwinMilestoneRecord> res;
    for (const auto& [_, rec] : milestones_) {
        if (rec.achieved) {
            res.push_back(rec);
        }
    }
    return res;
}

std::size_t DarwinBootMilestoneTracker::count_achieved() const noexcept {
    std::size_t count = 0;
    for (const auto& [_, rec] : milestones_) {
        if (rec.achieved) count++;
    }
    return count;
}

void DarwinBootMilestoneTracker::scan_console_line(
    const std::string& line,
    std::uint64_t instruction_count,
    std::uint64_t pc,
    std::uint8_t el) {

    if (line.find("Darwin Kernel Version") != std::string::npos ||
        line.find("XNU image") != std::string::npos ||
        line.find("PE_init_platform") != std::string::npos) {
        record_milestone(DarwinBootMilestone::PlatformExpertInit, instruction_count, pc, el, line);
    }

    if (line.find("AppleA16InterruptController") != std::string::npos ||
        line.find("AIC 2:") != std::string::npos) {
        record_milestone(DarwinBootMilestone::InterruptControllerMatched, instruction_count, pc, el, line);
    }

    if (line.find("AppleT8120PMGR") != std::string::npos ||
        line.find("PMGR: power domain") != std::string::npos ||
        line.find("PMGR: clock lock") != std::string::npos) {
        record_milestone(DarwinBootMilestone::PowerManagerMatched, instruction_count, pc, el, line);
    }

    if (line.find("AppleDCP") != std::string::npos ||
        line.find("AppleDisplay") != std::string::npos ||
        line.find("Super Retina XDR") != std::string::npos ||
        line.find("framebuffer at") != std::string::npos) {
        record_milestone(DarwinBootMilestone::DisplayFramebufferMatched, instruction_count, pc, el, line);
    }

    if (line.find("AppleSerialUART") != std::string::npos ||
        line.find("UART0: 0x09000000") != std::string::npos ||
        line.find("serial console active") != std::string::npos) {
        record_milestone(DarwinBootMilestone::ConsoleUartMatched, instruction_count, pc, el, line);
    }

    if (line.find("IOKit: driver matching") != std::string::npos ||
        line.find("IOKit: matching drivers") != std::string::npos ||
        line.find("IOKit catalog loaded") != std::string::npos ||
        line.find("IOKit personality") != std::string::npos) {
        record_milestone(DarwinBootMilestone::IOKitCatalogPopulated, instruction_count, pc, el, line);
    }

    if (line.find("matching drivers complete") != std::string::npos ||
        line.find("IOKit matching completed") != std::string::npos ||
        line.find("drivers started") != std::string::npos) {
        record_milestone(DarwinBootMilestone::DriverMatchingComplete, instruction_count, pc, el, line);
    }

    if (line.find("Root mount:") != std::string::npos ||
        line.find("RAMDisk: image mapped") != std::string::npos ||
        line.find("APFS container mounted") != std::string::npos) {
        record_milestone(DarwinBootMilestone::RootFilesystemLocated, instruction_count, pc, el, line);
    }

    if (line.find("launchd") != std::string::npos ||
        line.find("Userspace bootstrap ready") != std::string::npos ||
        line.find("spawning PID 1") != std::string::npos) {
        record_milestone(DarwinBootMilestone::LaunchdSpawnPrepared, instruction_count, pc, el, line);
    }
}

void DarwinBootMilestoneTracker::scan_console_text(
    const std::string& full_text,
    std::uint64_t instruction_count,
    std::uint64_t pc,
    std::uint8_t el) {

    std::istringstream stream(full_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            scan_console_line(line, instruction_count, pc, el);
        }
    }
}

void DarwinBootMilestoneTracker::scan_mmio_event(
    std::uint64_t pa,
    bool is_write,
    std::uint64_t val,
    std::uint64_t instruction_count,
    std::uint64_t pc,
    std::uint8_t el) {

    (void)val;
    (void)is_write;

    if (pa >= 0x08000000ULL && pa < 0x08100000ULL) {
        record_milestone(DarwinBootMilestone::InterruptControllerMatched,
                         instruction_count, pc, el, "AIC 2 MMIO event at 0x" + to_hex_str(pa));
    } else if (pa >= 0x10000000ULL && pa < 0x10100000ULL) {
        record_milestone(DarwinBootMilestone::PowerManagerMatched,
                         instruction_count, pc, el, "PMGR MMIO event at 0x" + to_hex_str(pa));
    } else if (pa >= 0x09000000ULL && pa < 0x09001000ULL) {
        record_milestone(DarwinBootMilestone::ConsoleUartMatched,
                         instruction_count, pc, el, "UART MMIO event at 0x" + to_hex_str(pa));
    } else if (pa >= 0x4F000000ULL && pa < 0x4FC00000ULL) {
        record_milestone(DarwinBootMilestone::DisplayFramebufferMatched,
                         instruction_count, pc, el, "Framebuffer display access at 0x" + to_hex_str(pa));
    }
}

std::string DarwinBootMilestoneTracker::dump_report() const {
    std::ostringstream ss;
    ss << "# Darwin / XNU Boot Milestone Execution Report\n\n";
    ss << "| Milestone | Status | Instructions | PC | EL | Evidence |\n";
    ss << "| :--- | :---: | :---: | :---: | :---: | :--- |\n";

    for (const auto& [m, rec] : milestones_) {
        ss << "| `" << rec.name << "` | "
           << (rec.achieved ? "**ACHIEVED**" : "PENDING") << " | "
           << rec.instruction_count << " | `0x"
           << std::hex << rec.pc << std::dec << "` | EL"
           << static_cast<unsigned>(rec.current_el) << " | "
           << (rec.diagnostic_evidence.empty() ? "-" : rec.diagnostic_evidence) << " |\n";
    }

    return ss.str();
}

} // namespace winios::runtime
