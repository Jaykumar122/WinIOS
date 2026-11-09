#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/apple_kernel_execution_engine.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// XnuSourceLocation: Maps an XNU startup routine to its canonical Apple source
// -----------------------------------------------------------------------------
struct XnuSourceLocation {
    std::string function_name;
    std::string source_file;
    std::uint32_t line_number{0};
    std::string description;
};

// -----------------------------------------------------------------------------
// XnuEarlyBootMilestone: Canonical progression milestones for early XNU boot
// -----------------------------------------------------------------------------
enum class XnuEarlyBootMilestone {
    ColdBootEntry,                  // Entry at _start with X0 = physical boot_args
    BootArgsConsumed,               // cold-boot args loaded: virtBase, physBase, memSize, bootFlags
    DeviceTreeInspected,            // Staged DeviceTree physical pointer read & verified non-null
    BootstrapStackActive,           // Early bootstrap stack active (SP loaded with early stack)
    EarlySysregConfigured,          // Early CPACR_EL1, TCR_EL1, SCTLR_EL1 system registers setup
    EarlyVmInit,                    // arm_vm_init: early 16 KB translation table bootstrap
    ArmInitTransition,              // Assembly handoff to C entry: arm_init(args)
    PlatformExpertInit,             // PE_init_platform: AppleARMPE Platform Expert initialization
    ConsoleInitialized,             // PE_initialize_console: Framebuffer / UART banner output
    KernelBootstrap,                // kernel_bootstrap: Mach scheduler, pmap, VM, IPC initialization
    MachineStartup,                 // machine_startup: Architecture and platform driver start
    FaultHalted                     // Execution halted prematurely due to architectural fault/trap
};

// -----------------------------------------------------------------------------
// XnuSymbolEntry: Symbol definition for PC resolution
// -----------------------------------------------------------------------------
struct XnuSymbolEntry {
    std::string name;
    std::uint64_t vmaddr{0};
    std::size_t size{0};
    XnuSourceLocation source_loc;
};

// -----------------------------------------------------------------------------
// XnuSymbolMap: Resolves execution PCs to symbols and XNU source lines
// -----------------------------------------------------------------------------
class XnuSymbolMap {
public:
    XnuSymbolMap() = default;

    void add_symbol(const std::string& name,
                    std::uint64_t vmaddr,
                    std::size_t size,
                    const XnuSourceLocation& loc);

    [[nodiscard]] bool lookup_address(
        std::uint64_t addr,
        std::string* out_symbol = nullptr,
        std::uint64_t* out_offset = nullptr,
        XnuSourceLocation* out_loc = nullptr) const;

    [[nodiscard]] static XnuSymbolMap create_standard_xnu_arm64_map(
        std::uint64_t kernel_base = 0xFFFFFE0007004000ULL);

    [[nodiscard]] std::size_t symbol_count() const noexcept { return symbols_.size(); }

private:
    std::vector<XnuSymbolEntry> symbols_;
};

// -----------------------------------------------------------------------------
// XnuMilestoneRecord: Formal evidence record for an early-boot milestone
// -----------------------------------------------------------------------------
struct XnuMilestoneRecord {
    XnuEarlyBootMilestone milestone{XnuEarlyBootMilestone::ColdBootEntry};
    std::string name;
    bool achieved{false};
    bool is_synthetic{false}; // True if produced by synthetic contract test; false if genuine XNU
    std::uint64_t instruction_count{0};
    std::uint64_t pc{0};
    std::uint8_t current_el{1};
    std::string symbol_at_pc;
    XnuSourceLocation source_location;
    AppleKernelRegisterSnapshot registers{};
    std::string diagnostic_evidence;
    std::string fault_detail;
};

// -----------------------------------------------------------------------------
// XnuEarlyBootObservationReport: Comprehensive evaluation of early boot trace
// -----------------------------------------------------------------------------
struct XnuEarlyBootObservationReport {
    bool observation_success{false};
    bool genuine_xnu_execution{false}; // Strictly false for synthetic test kernels
    KernelClassification kernel_classification{KernelClassification::Unidentified};
    std::string blocker_description;
    std::uint64_t total_instructions_retired{0};
    TerminationReason termination_reason{TerminationReason::BudgetReached};
    std::vector<XnuMilestoneRecord> milestones_achieved;
    std::size_t count_achieved{0};
    XnuEarlyBootMilestone furthest_milestone{XnuEarlyBootMilestone::ColdBootEntry};
    bool fault_occurred{false};
    std::string fault_summary;
    std::string formatted_markdown_report;
};

// -----------------------------------------------------------------------------
// AppleXnuEarlyBootObserver: Evaluates guest execution against Apple XNU startup
// -----------------------------------------------------------------------------
class AppleXnuEarlyBootObserver {
public:
    explicit AppleXnuEarlyBootObserver(XnuSymbolMap symbols = XnuSymbolMap::create_standard_xnu_arm64_map());
    ~AppleXnuEarlyBootObserver() = default;

    void reset();
    void set_symbol_map(XnuSymbolMap symbols) noexcept { symbol_map_ = std::move(symbols); }
    [[nodiscard]] const XnuSymbolMap& symbol_map() const noexcept { return symbol_map_; }

    // Evaluates guest kernel execution evidence and produces a rigorous milestone report
    [[nodiscard]] XnuEarlyBootObservationReport analyze_execution(
        const AppleKernelExecutionEvidence& ev,
        std::uint64_t devicetree_phys,
        std::uint32_t devicetree_len) const;

    // Generates a synthetic fileset kernel modeling the genuine start.s cold-boot path
    [[nodiscard]] static std::vector<std::uint8_t> create_synthetic_early_boot_kernel(
        std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL,
        std::uint64_t text_fileoff = 0x4000ULL);

    [[nodiscard]] static const char* milestone_name(XnuEarlyBootMilestone m) noexcept;

private:
    XnuSymbolMap symbol_map_;
};

} // namespace winios::runtime
