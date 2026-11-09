#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// LinuxBootMilestone: Canonical progression milestones for Linux ARM64 boot
// -----------------------------------------------------------------------------
enum class LinuxBootMilestone {
    FirmwareHandoff,    // Firmware finishes platform init and branches to kernel entry
    KernelEntry,        // Kernel entry point reached and primary setup commences
    EarlyconActive,     // Early console (PL011) active and emitting characters
    DeviceTreeParsed,   // FDT / DTB successfully traversed and validated by kernel
    MemoryMapped,       // Physical memory regions, page tables, and zones established
    GicInitialized,     // GICv3 distributor, redistributor, and CPU interfaces mapped
    TimerCalibrated,    // Generic timer (arch timer) clocksource calibrated
    InitramfsUnpacked,  // Root filesystem archive extracted into rootfs / ramfs
    UserspaceLaunched   // Init process (/init, /sbin/init) spawned and executed
};

// -----------------------------------------------------------------------------
// MilestoneRecord: Diagnostic evidence and execution context for a milestone
// -----------------------------------------------------------------------------
struct MilestoneRecord {
    LinuxBootMilestone milestone{LinuxBootMilestone::FirmwareHandoff};
    std::string name;
    bool achieved{false};
    std::uint64_t instruction_count{0};
    std::uint64_t pc{0};
    std::uint8_t current_el{1};
    std::string diagnostic_evidence;
};

// -----------------------------------------------------------------------------
// LinuxBootMilestoneTracker: Milestone detection and progress analysis
// -----------------------------------------------------------------------------
class LinuxBootMilestoneTracker {
public:
    LinuxBootMilestoneTracker();
    ~LinuxBootMilestoneTracker() = default;

    // Reset all milestone statuses
    void reset();

    // Explicitly record a milestone
    void record_milestone(LinuxBootMilestone milestone,
                          std::uint64_t instruction_count,
                          std::uint64_t pc,
                          std::uint8_t el,
                          const std::string& evidence);

    // Query status
    [[nodiscard]] bool is_achieved(LinuxBootMilestone milestone) const noexcept;
    [[nodiscard]] const MilestoneRecord* get_record(LinuxBootMilestone milestone) const noexcept;
    [[nodiscard]] std::vector<MilestoneRecord> get_achieved_milestones() const;
    [[nodiscard]] std::size_t count_achieved() const noexcept;
    [[nodiscard]] static const char* milestone_name(LinuxBootMilestone m) noexcept;

    // Pattern matching inspection hooks
    void scan_console_line(const std::string& line,
                           std::uint64_t instruction_count = 0,
                           std::uint64_t pc = 0,
                           std::uint8_t el = 1);

    void scan_console_text(const std::string& full_text,
                           std::uint64_t instruction_count = 0,
                           std::uint64_t pc = 0,
                           std::uint8_t el = 1);

    void scan_mmio_event(std::uint64_t pa,
                         bool is_write,
                         std::uint64_t val,
                         std::uint64_t instruction_count = 0,
                         std::uint64_t pc = 0,
                         std::uint8_t el = 1);

    // Human-readable diagnostic report
    [[nodiscard]] std::string dump_report() const;

private:
    std::map<LinuxBootMilestone, MilestoneRecord> milestones_;
};

} // namespace winios::runtime
