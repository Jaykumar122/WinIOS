#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "cpu/sysreg.h"
#include "runtime/apple_kernel_execution_engine.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// AppleGuestExceptionClass: Distinguishable categories of guest execution faults
// -----------------------------------------------------------------------------
enum class AppleGuestExceptionClass : std::uint32_t {
    UndefinedInstruction        = 0,
    IllegalInstructionEncoding  = 1,
    PrivilegedInstruction       = 2,
    SystemRegisterAccess        = 3,
    InstructionAbort            = 4,
    DataAbort                   = 5,
    TranslationFault            = 6,
    PermissionFault             = 7,
    AddressSizeFault            = 8,
    AlignmentFault              = 9,
    ExternalAbort               = 10,
    StackAccessFault            = 11,
    MmioAccessFault             = 12,
    SynchronousException        = 13,
    InterruptInterruption       = 14,
    BudgetExhaustion            = 15,
    WatchdogTimeout             = 16,
    Unknown                     = 99
};

// -----------------------------------------------------------------------------
// MemoryAccessKind: Memory transaction type during fault
// -----------------------------------------------------------------------------
enum class MemoryAccessKind : std::uint8_t {
    Read    = 0,
    Write   = 1,
    Execute = 2
};

// -----------------------------------------------------------------------------
// AppleGuestExceptionSyndrome: Decoded architectural ESR_EL1 bitfields
// -----------------------------------------------------------------------------
struct AppleGuestExceptionSyndrome {
    std::uint32_t raw_esr{0};
    std::uint32_t ec{0};         // ESR[31:26] Exception Class
    bool il{false};              // ESR[25] Instruction Length (1 = 32-bit)
    std::uint32_t iss{0};        // ESR[23:0] Instruction Specific Syndrome
    std::uint32_t fsc{0};        // ISS[5:0] Fault Status Code (for aborts)
    bool wnr{false};             // ISS[6] Write-not-Read (for data aborts)
    std::string ec_name;
    std::string fsc_name;

    static AppleGuestExceptionSyndrome decode(std::uint32_t esr) noexcept;
};

// -----------------------------------------------------------------------------
// AppleGuestSysRegFaultInfo: System register fault diagnostics
// -----------------------------------------------------------------------------
struct AppleGuestSysRegFaultInfo {
    std::uint16_t key{0};
    std::uint8_t op0{0};
    std::uint8_t op1{0};
    std::uint8_t crn{0};
    std::uint8_t crm{0};
    std::uint8_t op2{0};
    std::string reg_name;
    bool is_read{true};
    unsigned current_el{1};
    unsigned required_el{1};
    std::string fault_subtype; // "PrivilegeViolation", "UnknownRegister", "UnimplementedRegister", "ReadOnlyWrite"
};

// -----------------------------------------------------------------------------
// AppleGuestFaultContext: Comprehensive architectural fault context
// -----------------------------------------------------------------------------
struct AppleGuestFaultContext {
    AppleGuestExceptionClass category{AppleGuestExceptionClass::Unknown};
    std::uint64_t guest_pc{0};
    std::uint32_t instruction_word{0};
    unsigned instruction_length{4};
    std::string disassembly;
    bool has_fault_va{false};
    std::uint64_t fault_va{0};
    bool has_fault_pa{false};
    std::uint64_t fault_pa{0};
    MemoryAccessKind access_kind{MemoryAccessKind::Read};
    std::size_t access_width{0};
    unsigned exception_level{1};
    AppleGuestExceptionSyndrome syndrome{};
    AppleGuestSysRegFaultInfo sysreg_info{};
    std::uint64_t retired_instructions_at_fault{0};
    std::string session_id;
    KernelClassification artifact_classification{KernelClassification::Unidentified};
    std::string description;
};

// -----------------------------------------------------------------------------
// AppleBoundedHistoryBuffer: Deterministic bounded ring buffer for diagnostics
// -----------------------------------------------------------------------------
template <typename T>
class AppleBoundedHistoryBuffer {
public:
    explicit AppleBoundedHistoryBuffer(std::size_t max_capacity = 256)
        : max_capacity_(max_capacity == 0 ? 1 : max_capacity) {
        ring_.resize(max_capacity_);
    }

    void push_back(const T& item) {
        if (count_ < max_capacity_) {
            ring_[(head_ + count_) % max_capacity_] = item;
            ++count_;
        } else {
            ring_[head_] = item;
            head_ = (head_ + 1) % max_capacity_;
            ++total_overflows_;
        }
        ++total_pushed_;
    }

    void push_back(T&& item) {
        if (count_ < max_capacity_) {
            ring_[(head_ + count_) % max_capacity_] = std::move(item);
            ++count_;
        } else {
            ring_[head_] = std::move(item);
            head_ = (head_ + 1) % max_capacity_;
            ++total_overflows_;
        }
        ++total_pushed_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return max_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::uint64_t total_pushed() const noexcept { return total_pushed_; }
    [[nodiscard]] std::uint64_t total_overflows() const noexcept { return total_overflows_; }

    [[nodiscard]] const T& operator[](std::size_t index) const {
        if (index >= count_) {
            throw std::out_of_range("History buffer index out of range");
        }
        return ring_[(head_ + index) % max_capacity_];
    }

    [[nodiscard]] std::vector<T> to_vector() const {
        std::vector<T> result;
        result.reserve(count_);
        for (std::size_t i = 0; i < count_; ++i) {
            result.push_back(ring_[(head_ + i) % max_capacity_]);
        }
        return result;
    }

    void clear() noexcept {
        head_ = 0;
        count_ = 0;
        total_pushed_ = 0;
        total_overflows_ = 0;
    }

    void set_capacity(std::size_t new_cap) {
        if (new_cap == 0) new_cap = 1;
        auto current_items = to_vector();
        max_capacity_ = new_cap;
        ring_.resize(max_capacity_);
        head_ = 0;
        count_ = 0;
        const std::size_t start = (current_items.size() > new_cap) ? (current_items.size() - new_cap) : 0;
        for (std::size_t i = start; i < current_items.size(); ++i) {
            push_back(std::move(current_items[i]));
        }
    }

private:
    std::size_t max_capacity_{256};
    std::vector<T> ring_;
    std::size_t head_{0};
    std::size_t count_{0};
    std::uint64_t total_pushed_{0};
    std::uint64_t total_overflows_{0};
};

// -----------------------------------------------------------------------------
// AppleGuestFaultRecord: Deterministic reproduction record for kernel faults
// -----------------------------------------------------------------------------
struct AppleGuestFaultRecord {
    std::string session_id;
    KernelClassification artifact_classification{KernelClassification::Unidentified};
    std::string artifact_id;
    std::string artifact_sha256;
    std::uint64_t initial_pc{0};
    std::uint64_t faulting_pc{0};
    AppleKernelRegisterSnapshot initial_state{};
    AppleKernelRegisterSnapshot fault_state{};
    std::uint64_t instructions_retired{0};
    TerminationReason termination_reason{TerminationReason::Unknown};
    std::string termination_detail;
    AppleGuestFaultContext fault_context{};
    std::vector<AppleInstructionStepRecord> instruction_history;
    std::vector<AppleMemoryAccessRecord> memory_history;
    std::uint64_t sctlr_el1{0};
    std::uint64_t tcr_el1{0};
    std::uint64_t ttbr0_el1{0};
    std::uint64_t ttbr1_el1{0};

    // Stable serialization and deserialization
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    static bool deserialize(const std::uint8_t* data, std::size_t len,
                            AppleGuestFaultRecord* out_record,
                            std::string* out_error = nullptr);
};

// -----------------------------------------------------------------------------
// AppleKernelFaultAnalyzer: Centralized analyzer for guest kernel execution faults
// -----------------------------------------------------------------------------
class AppleKernelFaultAnalyzer {
public:
    AppleKernelFaultAnalyzer() = default;
    ~AppleKernelFaultAnalyzer() = default;

    // Analyzes CPU architectural state and memory when execution terminates or faults
    [[nodiscard]] AppleGuestFaultContext analyze_cpu_fault(
        const CPU& cpu,
        const Memory& memory,
        std::uint64_t pc_at_fault,
        TerminationReason reason,
        const std::string& error_message = "",
        KernelClassification classification = KernelClassification::Unidentified,
        const std::string& session_id = "") const;

    // Checks system-register access and generates diagnostic fault info
    [[nodiscard]] AppleGuestSysRegFaultInfo check_sysreg_access(
        std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2,
        unsigned current_el, bool is_read, const CPU& cpu) const;

    // Decodes ESR_EL1 syndrome
    [[nodiscard]] static AppleGuestExceptionSyndrome decode_esr(std::uint32_t esr) noexcept {
        return AppleGuestExceptionSyndrome::decode(esr);
    }

    // Diagnostic string conversions
    [[nodiscard]] static const char* exception_class_to_string(AppleGuestExceptionClass c) noexcept;
    [[nodiscard]] static const char* memory_access_kind_to_string(MemoryAccessKind k) noexcept;

    // Generates human-readable Markdown diagnostic report
    [[nodiscard]] static std::string generate_diagnostic_markdown(const AppleGuestFaultRecord& record);
};

// Helper: Adler-32 integrity checksum calculation (non-cryptographic data checksum)
std::uint32_t compute_adler32(const std::uint8_t* data, std::size_t len) noexcept;

} // namespace winios::runtime
