#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_hardware_profile.h"
#include "firmware/apple_boot_args.h"
#include "firmware/apple_boot_config_manager.h"
#include "loader/apple_artifact_admission.h"
#include "loader/apple_kernel_loader.h"
#include "loader/apple_kernel_relocator.h"
#include "memory/apple_physical_memory_map.h"
#include "memory/memory.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// KernelClassification: Distinguishes genuine vs synthetic vs blocked kernels
// -----------------------------------------------------------------------------
enum class KernelClassification {
    GenuineDecrypted,               // Authentic decrypted Apple Mach-O kernelcache
    GenuineEncrypted_Blocked,       // Authentic Apple IMG4/IM4P encrypted with SEP/GID key (blocked)
    SyntheticAppleBootContract,     // Deterministic ARM64 kernel validating Apple boot contract
    Unidentified
};

// -----------------------------------------------------------------------------
// TerminationReason: Reason why kernel entry execution concluded
// -----------------------------------------------------------------------------
enum class TerminationReason {
    BudgetReached,          // Reached configured instruction retirement budget
    Halted_WFI,             // CPU executed WFI (Wait For Interrupt) low-power wait
    Halted_WFE,             // CPU executed WFE (Wait For Event) wait
    SynchronousException,   // CPU took a synchronous exception (MMU, undefined, abort)
    InfiniteLoop,           // Detected execution cycle or branch-to-self (B .)
    AddressOutOfRange,      // Memory access outside backed physical RAM
    SetupError,             // Ingestion, admission, or configuration error
    UnsupportedInstruction, // Encountered unimplemented AArch64 opcode
    TranslationFault,       // MMU translation failure
    PermissionFault,        // MMU page permission failure
    AlignmentFault,         // Alignment violation (PC or memory)
    WatchdogTimeout,        // Watchdog instruction threshold exceeded
    Unknown
};

// -----------------------------------------------------------------------------
// AppleKernelRegisterSnapshot: Full architectural state snapshot
// -----------------------------------------------------------------------------
struct AppleKernelRegisterSnapshot {
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint64_t sp_el1{0};
    std::uint64_t sp_el0{0};
    std::uint64_t x[CPU::kRegisterCount]{0};
    std::uint8_t nzcv{0};
    std::uint8_t daif{0};
    std::uint64_t current_el{1};
    bool sp_sel{true};
    std::uint64_t sctlr_el1{0};
    std::uint64_t tcr_el1{0};
    std::uint64_t ttbr0_el1{0};
    std::uint64_t ttbr1_el1{0};
    std::uint64_t cpacr_el1{0};
    std::uint64_t vbar_el1{0};
    std::uint32_t esr_el1{0};
    std::uint64_t far_el1{0};
    std::uint64_t elr_el1{0};
    std::uint32_t spsr_el1{0};
};

// -----------------------------------------------------------------------------
// AppleInstructionStepRecord: Diagnostic record for a single retired instruction
// -----------------------------------------------------------------------------
struct AppleInstructionStepRecord {
    std::uint64_t step_index{0};
    std::uint64_t pc{0};
    std::uint32_t instruction_word{0};
    std::string disassembly;
    std::uint64_t next_pc{0};
    std::vector<std::string> changed_registers;
    bool exception_taken{false};
};

// -----------------------------------------------------------------------------
// AppleMemoryAccessRecord: Diagnostic record for memory writes during execution
// -----------------------------------------------------------------------------
struct AppleMemoryAccessRecord {
    std::uint64_t instruction_pc{0};
    std::uint64_t memory_offset{0};
    std::uint64_t physical_address{0};
    std::size_t size{0};
    std::uint64_t value{0};
    bool is_write{true};
};

// -----------------------------------------------------------------------------
// AppleKernelExecutionConfig: Configuration for guest kernel execution
// -----------------------------------------------------------------------------
struct AppleKernelExecutionConfig {
    std::uint64_t instruction_budget{1000};
    std::uint64_t kaslr_slide{0};
    std::uint64_t ram_base{0x40000000ULL};                     // 1 GB standard physical DRAM base
    std::uint64_t ram_size{64ULL * 1024 * 1024};               // 64 MB staging RAM
    std::uint64_t boot_stack_phys{0x43F00000ULL};              // Top of early boot stack
    std::uint64_t boot_args_phys{0x43F80000ULL};               // Physical address of struct boot_args
    std::uint64_t devicetree_phys{0x43E00000ULL};              // Physical address of ADTS DeviceTree
    bool force_synthetic{false};
    std::vector<std::uint8_t> kernel_data;
    std::string kernel_path;
    std::vector<std::uint8_t> devicetree_blob;
    firmware::AppleBootConfig boot_config;
    bool trace_instructions{true};
    bool record_memory_loads{false};
    std::size_t max_trace_entries{256};
    bool detect_loops{true};
    std::size_t loop_threshold{16};
    std::string session_id{"win_ios_session_default"};
    std::size_t history_capacity{256};
    std::uint64_t watchdog_instruction_limit{1000000};
};

// -----------------------------------------------------------------------------
// AppleKernelExecutionEvidence: Full deterministic evidence record of execution
// -----------------------------------------------------------------------------
struct AppleKernelExecutionEvidence {
    bool setup_success{false};
    bool execution_completed{false};
    KernelClassification kernel_classification{KernelClassification::Unidentified};
    std::string blocker_reason;
    std::string kernel_identifier;
    std::string sha256_hex;
    std::uint64_t applied_kaslr_slide{0};
    std::uint64_t phys_entry_point{0};
    std::uint64_t virt_entry_point{0};
    std::uint64_t boot_args_phys_addr{0};
    firmware::AppleBootArgs staged_boot_args{};
    AppleKernelRegisterSnapshot initial_state{};
    AppleKernelRegisterSnapshot final_state{};
    std::uint64_t instructions_retired{0};
    TerminationReason termination_reason{TerminationReason::BudgetReached};
    std::string termination_detail;
    std::vector<AppleInstructionStepRecord> instruction_trace;
    std::vector<AppleMemoryAccessRecord> memory_access_trace;
    std::vector<std::string> exceptions_encountered;
    loader::AppleKernelLoadResult load_result{};
    loader::AppleKernelRelocationResult relocation_result{};
    std::string session_id;
    std::vector<std::uint8_t> serialized_fault_record;
    std::string fault_analysis_markdown;
};

// -----------------------------------------------------------------------------
// AppleKernelExecutionEngine: Manages guest CPU execution of Apple kernels
// -----------------------------------------------------------------------------
class AppleKernelExecutionEngine {
public:
    explicit AppleKernelExecutionEngine(const AppleKernelExecutionConfig& config = AppleKernelExecutionConfig{});
    ~AppleKernelExecutionEngine() = default;

    // Sets up guest memory, loads/relocates kernel, configures boot_args & CPU
    bool initialize(std::string* out_error = nullptr);

    // Runs kernel execution up to configured instruction budget
    AppleKernelExecutionEvidence run();

    // Runs kernel execution for up to max_steps instructions
    AppleKernelExecutionEvidence step_n(std::size_t max_steps);

    // Low-level accessors for inspection and debugging
    [[nodiscard]] const CPU& cpu() const noexcept { return cpu_; }
    [[nodiscard]] CPU& cpu() noexcept { return cpu_; }
    [[nodiscard]] const Memory& memory() const noexcept { return *memory_; }
    [[nodiscard]] Memory& memory() noexcept { return *memory_; }
    [[nodiscard]] const memory::ApplePhysicalMemoryMap& memory_map() const noexcept { return memory_map_; }
    [[nodiscard]] const AppleKernelExecutionConfig& config() const noexcept { return config_; }

    // Helpers
    static AppleKernelRegisterSnapshot capture_snapshot(const CPU& cpu);
    static std::vector<std::uint8_t> create_synthetic_apple_boot_kernel(
        std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL);
    static std::string generate_evidence_markdown(const AppleKernelExecutionEvidence& ev);

    // Diagnostic string conversions
    static const char* classification_to_string(KernelClassification c) noexcept;
    static const char* termination_reason_to_string(TerminationReason r) noexcept;

private:
    AppleKernelExecutionConfig config_;
    std::unique_ptr<Memory> memory_;
    memory::ApplePhysicalMemoryMap memory_map_;
    CPU cpu_;
    bool initialized_{false};
    KernelClassification classification_{KernelClassification::Unidentified};
    std::string blocker_description_;
    loader::AppleKernelLoadResult load_result_;
    loader::AppleKernelRelocationResult relocation_result_;
    firmware::AppleBootArgs staged_boot_args_;
    AppleKernelRegisterSnapshot initial_snapshot_;
    std::uint64_t instructions_retired_{0};
    std::vector<AppleInstructionStepRecord> instruction_trace_;
    std::vector<AppleMemoryAccessRecord> memory_access_trace_;
    std::vector<std::string> exceptions_encountered_;

    void write_phys_bytes(std::uint64_t phys_addr, const void* data, std::size_t len);
    void read_phys_bytes(std::uint64_t phys_addr, void* dest, std::size_t len) const;
};

} // namespace winios::runtime
