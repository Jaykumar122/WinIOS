#pragma once

// =============================================================================
// WinIOS Phase 52 Stage 34: Independent Reference Execution Model
// =============================================================================
// This reference model independently calculates expected architectural states
// for a bounded subset of AArch64 operations, SMP exclusive monitors, virtual
// interrupts, timers, and virtual DMA descriptor chains.
//
// ISOLATION GUARANTEE: This implementation DOES NOT call or link with CPU::step(),
// SmpScheduler, or VirtualDmaEngine execution paths. All semantics, flag calculations,
// and memory mutations are computed independently from first principles.
// =============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace winios::reference {

// -----------------------------------------------------------------------------
// Reference CPU State
// -----------------------------------------------------------------------------
struct ReferenceCpuState {
    std::array<std::uint64_t, 31> x{};
    std::uint64_t sp{0};
    std::uint64_t pc{0};
    std::uint32_t nzcv{0}; // N: bit 31, Z: bit 30, C: bit 29, V: bit 28
    std::uint64_t retirement_count{0};

    [[nodiscard]] bool get_n() const noexcept { return (nzcv & (1u << 31)) != 0; }
    [[nodiscard]] bool get_z() const noexcept { return (nzcv & (1u << 30)) != 0; }
    [[nodiscard]] bool get_c() const noexcept { return (nzcv & (1u << 29)) != 0; }
    [[nodiscard]] bool get_v() const noexcept { return (nzcv & (1u << 28)) != 0; }

    void set_nzcv(bool n, bool z, bool c, bool v) noexcept {
        nzcv = (n ? (1u << 31) : 0) |
               (z ? (1u << 30) : 0) |
               (c ? (1u << 29) : 0) |
               (v ? (1u << 28) : 0);
    }
};

// -----------------------------------------------------------------------------
// Reference Guest Memory
// -----------------------------------------------------------------------------
class ReferenceMemory {
public:
    explicit ReferenceMemory(std::size_t size_bytes);

    [[nodiscard]] std::size_t size() const noexcept { return ram_.size(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return ram_.data(); }

    [[nodiscard]] std::uint8_t  read_u8(std::uint64_t paddr) const;
    [[nodiscard]] std::uint16_t read_u16(std::uint64_t paddr) const;
    [[nodiscard]] std::uint32_t read_u32(std::uint64_t paddr) const;
    [[nodiscard]] std::uint64_t read_u64(std::uint64_t paddr) const;

    void write_u8(std::uint64_t paddr, std::uint8_t val);
    void write_u16(std::uint64_t paddr, std::uint16_t val);
    void write_u32(std::uint64_t paddr, std::uint32_t val);
    void write_u64(std::uint64_t paddr, std::uint64_t val);

    void write_bytes(std::uint64_t paddr, const std::uint8_t* src, std::size_t len);
    void read_bytes(std::uint64_t paddr, std::uint8_t* dst, std::size_t len) const;

private:
    std::vector<std::uint8_t> ram_;
};

// -----------------------------------------------------------------------------
// Reference Exclusive Monitor
// -----------------------------------------------------------------------------
class ReferenceExclusiveMonitor {
public:
    static constexpr std::size_t kMaxCores = 8;

    void load_exclusive(std::size_t core_id, std::uint64_t paddr, std::uint64_t size);
    [[nodiscard]] bool store_exclusive(std::size_t core_id, std::uint64_t paddr, std::uint64_t size);
    void invalidate_reservations(std::uint64_t paddr, std::uint64_t size);
    [[nodiscard]] bool is_reservation_valid(std::size_t core_id, std::uint64_t paddr, std::uint64_t size) const noexcept;
    void clear() noexcept;

private:
    struct Reservation {
        bool valid{false};
        std::uint64_t paddr{0};
        std::uint64_t size{0};
    };
    std::array<Reservation, kMaxCores> reservations_{};
};

// -----------------------------------------------------------------------------
// Reference Virtual Timer
// -----------------------------------------------------------------------------
class ReferenceVirtualTimer {
public:
    void step_cycles(std::uint64_t cycles) noexcept;
    void set_cval(std::uint64_t cval) noexcept { cval_ = cval; }
    void set_ctl(std::uint32_t ctl) noexcept { ctl_ = ctl; }

    [[nodiscard]] std::uint64_t get_cntvct() const noexcept { return cntvct_; }
    [[nodiscard]] bool is_interrupt_asserted() const noexcept;

private:
    std::uint64_t cntvct_{0};
    std::uint64_t cval_{0};
    std::uint32_t ctl_{0}; // bit 0: enable, bit 1: imask, bit 2: istatus
};

// -----------------------------------------------------------------------------
// Reference Virtual DMA Engine
// -----------------------------------------------------------------------------
struct ReferenceDmaResult {
    bool success{false};
    std::size_t descriptors_processed{0};
    std::size_t bytes_transferred{0};
    std::string error_message;
};

class ReferenceDmaEngine {
public:
    static ReferenceDmaResult execute_descriptor_chain(
        ReferenceMemory& mem,
        ReferenceExclusiveMonitor& monitor,
        std::uint64_t first_desc_paddr,
        std::size_t core_id);
};

// -----------------------------------------------------------------------------
// Reference AArch64 Instruction Executor
// -----------------------------------------------------------------------------
struct StepResult {
    bool success{false};
    bool branch_taken{false};
    std::uint64_t target_pc{0};
    std::string disassembly;
    std::string error_message;
};

class ReferenceExecutor {
public:
    static StepResult step_instruction(
        ReferenceCpuState& cpu,
        ReferenceMemory& mem,
        ReferenceExclusiveMonitor& monitor,
        std::size_t core_id);

    static bool evaluate_condition(std::uint32_t cond, std::uint32_t nzcv) noexcept;
};

// -----------------------------------------------------------------------------
// Differential Divergence Diagnostics
// -----------------------------------------------------------------------------
struct DivergenceReport {
    bool has_divergence{false};
    std::size_t core_id{0};
    std::uint64_t pc{0};
    std::uint64_t retirement_count{0};
    std::string field_name;
    std::uint64_t expected_value{0};
    std::uint64_t actual_value{0};
    std::string message;
};

class DifferentialValidator {
public:
    static DivergenceReport compare_cpu_state(
        std::size_t core_id,
        const ReferenceCpuState& expected,
        std::uint64_t actual_pc,
        std::uint32_t actual_nzcv,
        std::uint64_t actual_retirements,
        const std::uint64_t* actual_x31);

    static DivergenceReport compare_memory(
        const ReferenceMemory& expected,
        const std::uint8_t* actual_ram,
        std::size_t actual_size);
};

// -----------------------------------------------------------------------------
// Reference MMU & Page Table Walker
// -----------------------------------------------------------------------------
enum class ReferenceMmuFaultType : std::uint8_t {
    None         = 0,
    AddressSize  = 1,
    Translation  = 2,
    AccessFlag   = 3,
    Permission   = 4
};

struct ReferenceTranslationResult {
    bool success{false};
    std::uint64_t physical_address{0};
    std::uint64_t page_size{4096};
    bool readable{false};
    bool writable{false};
    bool executable{false};
    bool el0_accessible{false};
    ReferenceMmuFaultType fault_type{ReferenceMmuFaultType::None};
    unsigned fault_level{0};
    std::uint32_t fsc{0};
};

class ReferencePageTableWalker {
public:
    static ReferenceTranslationResult walk_stage1(
        const ReferenceMemory& mem,
        std::uint64_t ttbr0,
        std::uint64_t tcr,
        std::uint64_t sctlr,
        std::uint64_t va,
        int access_kind, // 0 = Read, 1 = Write, 2 = Execute
        unsigned current_el);
};

// -----------------------------------------------------------------------------
// Reference Exception Model
// -----------------------------------------------------------------------------
struct ReferenceExceptionEntryResult {
    std::uint64_t target_pc{0};
    unsigned target_el{1};
    std::uint32_t spsr{0};
    std::uint64_t elr{0};
    std::uint32_t esr{0};
    std::uint64_t far{0};
    std::uint8_t daif{0xF};
    bool sp_sel{true};
    std::uint32_t vector_offset{0};
};

class ReferenceExceptionModel {
public:
    static std::uint32_t calculate_vector_offset(unsigned origin_el, unsigned target_el, bool sp_sel, int exception_type) noexcept;
    static std::uint32_t calculate_vector_offset(unsigned origin_el, bool sp_sel, int exception_type) noexcept {
        return calculate_vector_offset(origin_el, 1u, sp_sel, exception_type);
    }

    static ReferenceExceptionEntryResult take_synchronous_exception(
        std::uint64_t current_pc,
        unsigned current_el,
        bool sp_sel,
        std::uint8_t daif,
        std::uint8_t nzcv,
        std::uint64_t vbar_el1,
        std::uint32_t ec,
        std::uint32_t iss,
        std::uint64_t fault_address,
        bool has_fault_address,
        std::uint64_t preferred_return,
        unsigned target_el = 1);

    static ReferenceExceptionEntryResult take_asynchronous_exception(
        std::uint64_t current_pc,
        unsigned current_el,
        bool sp_sel,
        std::uint8_t daif,
        std::uint8_t nzcv,
        std::uint64_t vbar_el1,
        bool is_fiq,
        std::uint32_t intid,
        std::uint8_t priority,
        unsigned target_el = 1);

    static bool execute_eret(
        ReferenceCpuState& cpu,
        std::uint32_t spsr,
        std::uint64_t elr,
        std::string& error_msg);
};

// -----------------------------------------------------------------------------
// Reference Syndrome Model
// -----------------------------------------------------------------------------
class ReferenceSyndromeModel {
public:
    static std::uint32_t compute_data_abort_esr(
        bool is_same_el,
        std::uint32_t fsc,
        bool is_write,
        unsigned access_size);

    static std::uint32_t compute_instruction_abort_esr(
        bool is_same_el,
        std::uint32_t fsc);

    static std::uint32_t compute_svc_esr(std::uint16_t imm16);
    static std::uint32_t compute_pc_alignment_esr(bool is_same_el);
    static std::uint32_t compute_undefined_instruction_esr();
};

// -----------------------------------------------------------------------------
// Reference GIC Model
// -----------------------------------------------------------------------------
struct ReferenceGicSignal {
    bool has_interrupt{false};
    bool is_fiq{false};
    std::uint32_t intid{1023};
    std::uint8_t priority{0xFF};
};

class ReferenceGicModel {
public:
    static constexpr std::size_t kMaxCores = 8;
    static constexpr std::uint32_t kTotalIrqs = 128; // 16 SGI + 16 PPI + 96 SPI

    ReferenceGicModel() noexcept;
    void reset() noexcept;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    void set_pmr(std::size_t core_id, std::uint8_t pmr) noexcept;
    void set_irq_priority(std::uint32_t intid, std::uint8_t priority) noexcept;
    void set_irq_enabled(std::uint32_t intid, bool enabled, std::size_t core_id = 0) noexcept;
    void set_irq_pending(std::uint32_t intid, bool pending, std::size_t core_id = 0) noexcept;

    [[nodiscard]] ReferenceGicSignal evaluate(std::size_t core_id, unsigned current_el) const noexcept;
    std::uint32_t acknowledge_irq(std::size_t core_id) noexcept;
    void end_of_interrupt(std::size_t core_id, std::uint32_t intid) noexcept;

private:
    bool enabled_{true};
    std::array<std::uint8_t, kMaxCores> pmr_{};
    std::array<std::uint8_t, kTotalIrqs> priority_{};
    std::array<std::array<bool, 32>, kMaxCores> ppi_enabled_{};
    std::array<std::array<bool, 32>, kMaxCores> ppi_pending_{};
    std::array<std::array<bool, 32>, kMaxCores> ppi_active_{};
    std::array<bool, 96> spi_enabled_{};
    std::array<bool, 96> spi_pending_{};
    std::array<bool, 96> spi_active_{};
    std::array<std::size_t, 96> spi_target_core_{};
};

// -----------------------------------------------------------------------------
// Reference Litmus Verifier
// -----------------------------------------------------------------------------
struct LitmusOutcome {
    std::uint64_t r0{0};
    std::uint64_t r1{0};
    bool valid{false};
    bool is_forbidden{false};
};

class ReferenceLitmusVerifier {
public:
    static LitmusOutcome verify_message_passing(std::uint64_t read_flag, std::uint64_t read_data);
    static LitmusOutcome verify_store_buffering(std::uint64_t read_y, std::uint64_t read_x);
};

// -----------------------------------------------------------------------------
// Reference System Register Model (Stage 36)
// -----------------------------------------------------------------------------
struct ReferenceSysRegState {
    unsigned current_el{1};
    std::uint8_t daif{0xF};
    bool sp_sel{true};
    bool il{false};
    bool pstate_ss{false};

    std::uint32_t spsr_el1{0};
    std::uint64_t elr_el1{0};
    std::uint32_t esr_el1{0};
    std::uint64_t far_el1{0};
    std::uint64_t vbar_el1{0};

    std::uint64_t sctlr_el1{0x00C50838ULL};
    std::uint64_t tcr_el1{0};
    std::uint64_t ttbr0_el1{0};
    std::uint64_t ttbr1_el1{0};
    std::uint64_t mair_el1{0};
    std::uint64_t cpacr_el1{0};

    std::uint64_t tpidr_el0{0};
    std::uint64_t tpidr_el1{0};
    std::uint64_t tpidrro_el0{0};
};

enum class ReferenceSysRegStatus : std::uint8_t {
    Success,
    UndefinedException,
    FpSimdTrap,
    TrapToEl2,
    TrapToEl3,
    UnallocatedEncoding
};

struct ReferenceSysRegResult {
    ReferenceSysRegStatus status{ReferenceSysRegStatus::Success};
    bool is_mrs{false};
    std::uint16_t key{0};
    std::uint32_t rt{0};
    std::uint64_t value{0};
    std::string reg_name;
};

class ReferenceSysRegModel {
public:
    static ReferenceSysRegResult step_sysreg_instruction(
        ReferenceCpuState& cpu,
        ReferenceSysRegState& sysregs,
        std::uint32_t instruction);

    static bool check_privilege(
        unsigned current_el,
        std::uint16_t key,
        bool is_read) noexcept;

    static std::uint64_t write_wn_zero_extends_xn(std::uint64_t original_xn, std::uint32_t wn_val) noexcept;
    static std::uint32_t read_wn_lower32(std::uint64_t xn_val) noexcept;
    static bool is_fp_simd_enabled(unsigned current_el, std::uint64_t cpacr_el1) noexcept;
};

} // namespace winios::reference
