#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// DarwinBootMilestone: Canonical progression milestones for native Darwin / iOS boot
// -----------------------------------------------------------------------------
enum class DarwinBootMilestone {
    BootArgsParsed,             // Apple struct boot_args validated and mapped
    DeviceTreeParsed,           // Apple DeviceTree (ADTS format) parsed and registered
    PlatformExpertInit,         // AppleARMPE Platform Expert verified for iPhone 15
    InterruptControllerMatched, // Apple AIC 2 interrupt controller matched
    PowerManagerMatched,        // Apple PMGR power manager matched and lock verified
    DisplayFramebufferMatched,  // Super Retina XDR framebuffer display driver matched
    ConsoleUartMatched,         // Apple Serial UART console matched and IRQ wired
    IOKitCatalogPopulated,      // IOKit driver catalog loaded with driver personalities
    DriverMatchingComplete,     // All required platform controllers probed and active
    RootFilesystemLocated,      // Ramdisk / APFS root volume attached and located
    LaunchdSpawnPrepared        // Darwin userspace / Commpage / launchd bootstrap prepared
};

// -----------------------------------------------------------------------------
// DarwinMilestoneRecord: Diagnostic evidence and execution context for a milestone
// -----------------------------------------------------------------------------
struct DarwinMilestoneRecord {
    DarwinBootMilestone milestone{DarwinBootMilestone::BootArgsParsed};
    std::string name;
    bool achieved{false};
    std::uint64_t instruction_count{0};
    std::uint64_t pc{0};
    std::uint8_t current_el{1};
    std::string diagnostic_evidence;
};

// -----------------------------------------------------------------------------
// DarwinBootMilestoneTracker: Milestone detection and progress analysis
// -----------------------------------------------------------------------------
class DarwinBootMilestoneTracker {
public:
    DarwinBootMilestoneTracker();
    ~DarwinBootMilestoneTracker() = default;

    // Reset all milestone statuses
    void reset();

    // Explicitly record a milestone
    void record_milestone(DarwinBootMilestone milestone,
                          std::uint64_t instruction_count,
                          std::uint64_t pc,
                          std::uint8_t el,
                          const std::string& evidence);

    // Query status
    [[nodiscard]] bool is_achieved(DarwinBootMilestone milestone) const noexcept;
    [[nodiscard]] const DarwinMilestoneRecord* get_record(DarwinBootMilestone milestone) const noexcept;
    [[nodiscard]] std::vector<DarwinMilestoneRecord> get_achieved_milestones() const;
    [[nodiscard]] std::size_t count_achieved() const noexcept;
    [[nodiscard]] static const char* milestone_name(DarwinBootMilestone m) noexcept;

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
    std::map<DarwinBootMilestone, DarwinMilestoneRecord> milestones_;
};

} // namespace winios::runtime
