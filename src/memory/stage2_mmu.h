#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/mmu.h"
#include "memory/memory.h"

namespace winios::mmu {

// -----------------------------------------------------------------------------
// Stage-2 Translation Granules & Types
// -----------------------------------------------------------------------------
enum class Stage2Granule : std::uint8_t {
    Granule4KB  = 0,
    Granule16KB = 1,
    Granule64KB = 2,
};

enum class Stage2AccessType : std::uint8_t {
    Read    = 0,
    Write   = 1,
    Execute = 2,
};

// -----------------------------------------------------------------------------
// Stage2Fault: Structured Stage-2 translation fault report
// -----------------------------------------------------------------------------
struct Stage2Fault {
    ::mmu::MmuFaultType type{::mmu::MmuFaultType::Translation};
    unsigned level{0}; // Level 0, 1, 2, 3
    std::uint64_t ipa{0};
    Stage2AccessType access{Stage2AccessType::Read};
    bool is_write{false};
    bool is_execute{false};

    [[nodiscard]] std::uint32_t fsc() const noexcept {
        switch (type) {
        case ::mmu::MmuFaultType::AddressSize:
            return 0x00u + (level & 0x3u);
        case ::mmu::MmuFaultType::Translation:
            return 0x04u + (level & 0x3u);
        case ::mmu::MmuFaultType::AccessFlag:
            return 0x08u + (level & 0x3u);
        case ::mmu::MmuFaultType::Permission:
            return 0x0Cu + (level & 0x3u);
        case ::mmu::MmuFaultType::ExternalWalk:
            return 0x14u + (level & 0x3u);
        case ::mmu::MmuFaultType::Alignment:
            return 0x21u;
        }
        return 0x04u;
    }
};

// -----------------------------------------------------------------------------
// Stage2TranslationResult: Successful Stage-2 IPA -> PA translation outcome
// -----------------------------------------------------------------------------
struct Stage2TranslationResult {
    std::uint64_t physical_address{0};
    std::uint64_t page_size{16384}; // Defaults to 16KB granule
    bool readable{false};
    bool writable{false};
    bool executable{false};
    std::uint8_t mem_attr{0};       // Descriptor bits [5:2] MemAttr
    bool is_device{false};
};

// -----------------------------------------------------------------------------
// Stage-2 Fault Injection & EL2 Syndrome Reporting
// -----------------------------------------------------------------------------
struct Stage2InjectedFault {
    std::uint64_t ipa{0};
    std::uint64_t ipa_mask{0xFFFFFFFFFFFFF000ULL};
    ::mmu::MmuFaultType fault_type{::mmu::MmuFaultType::Translation};
    unsigned level{3};
    bool active{true};
    std::size_t trigger_count{0};
    std::size_t max_triggers{1};
};

class Stage2FaultInjector {
public:
    Stage2FaultInjector() = default;
    ~Stage2FaultInjector() = default;

    void add_rule(Stage2InjectedFault rule) {
        rules_.push_back(std::move(rule));
    }

    void clear_rules() noexcept {
        rules_.clear();
    }

    [[nodiscard]] std::size_t rule_count() const noexcept {
        return rules_.size();
    }

    [[nodiscard]] const std::vector<Stage2InjectedFault>& rules() const noexcept {
        return rules_;
    }

    bool check_and_inject(
        std::uint64_t ipa,
        Stage2AccessType access,
        Stage2Fault& out_fault) {
        for (auto& r : rules_) {
            if (!r.active) continue;
            if ((ipa & r.ipa_mask) == (r.ipa & r.ipa_mask)) {
                out_fault.type = r.fault_type;
                out_fault.level = r.level;
                out_fault.ipa = ipa;
                out_fault.access = access;
                out_fault.is_write = (access == Stage2AccessType::Write);
                out_fault.is_execute = (access == Stage2AccessType::Execute);
                r.trigger_count++;
                if (r.trigger_count >= r.max_triggers) {
                    r.active = false;
                }
                return true;
            }
        }
        return false;
    }

private:
    std::vector<Stage2InjectedFault> rules_;
};

struct Stage2SyndromeReport {
    std::uint64_t esr_el2{0};
    std::uint64_t far_el2{0};
    std::uint64_t hpfar_el2{0};
};

inline Stage2SyndromeReport make_stage2_syndrome_report(
    const Stage2Fault& fault,
    std::uint64_t faulting_va = 0) noexcept
{
    Stage2SyndromeReport report{};
    report.far_el2 = (faulting_va != 0) ? faulting_va : fault.ipa;
    // HPFAR_EL2[39:4] holds IPA[47:12] -> (ipa >> 8) & 0x00000FFFFFFFF000ULL
    report.hpfar_el2 = (fault.ipa >> 8u) & 0x00000FFFFFFFF000ULL;

    const std::uint32_t ec = fault.is_execute ? 0x20u : 0x24u; // 0x20 = Instruction Abort, 0x24 = Data Abort
    const std::uint32_t il = 1u; // 32-bit instruction
    std::uint32_t iss = fault.fsc() & 0x3Fu;
    if (!fault.is_execute) {
        if (fault.is_write) {
            iss |= (1u << 6u); // WnR = 1 (write)
        }
    }
    report.esr_el2 = (static_cast<std::uint64_t>(ec) << 26u) |
                     (static_cast<std::uint64_t>(il) << 25u) |
                     iss;
    return report;
}

// -----------------------------------------------------------------------------
// Stage2TranslationEngine
//
// Standalone ARMv8-A / ARMv8.1-A Stage-2 Translation Engine.
// Models EL2-controlled hypervisor translation mapping Intermediate Physical
// Addresses (IPA) to Physical Addresses (PA) with VMID awareness, multi-granule
// page-table walks (16KB primary, 4KB fallback), permission validation,
// and overflow-safe arithmetic.
// -----------------------------------------------------------------------------
class Stage2TranslationEngine {
public:
    Stage2TranslationEngine() = default;

    // Reset control registers and state
    void reset() noexcept;

    // Fault injection
    void set_fault_injector(Stage2FaultInjector* injector) noexcept {
        fault_injector_ = injector;
    }
    [[nodiscard]] Stage2FaultInjector* fault_injector() const noexcept {
        return fault_injector_;
    }

    // Register accessors
    [[nodiscard]] std::uint64_t vttbr() const noexcept { return vttbr_el2_; }
    void set_vttbr(std::uint64_t val) noexcept { vttbr_el2_ = val; }

    [[nodiscard]] std::uint64_t vtcr() const noexcept { return vtcr_el2_; }
    void set_vtcr(std::uint64_t val) noexcept { vtcr_el2_ = val; }

    [[nodiscard]] std::uint64_t hcr() const noexcept { return hcr_el2_; }
    void set_hcr(std::uint64_t val) noexcept { hcr_el2_ = val; }

    // Control status
    [[nodiscard]] bool is_enabled() const noexcept {
        return (hcr_el2_ & 0x1ULL) != 0; // HCR_EL2.VM bit enables Stage 2
    }

    [[nodiscard]] std::uint16_t current_vmid() const noexcept {
        return static_cast<std::uint16_t>((vttbr_el2_ >> 48u) & 0xFFFFu);
    }

    [[nodiscard]] std::uint64_t translation_table_base() const noexcept {
        return vttbr_el2_ & 0x0000FFFFFFFFF000ULL;
    }

    [[nodiscard]] Stage2Granule granule() const noexcept;
    [[nodiscard]] unsigned ipa_size_bits() const noexcept;
    [[nodiscard]] unsigned start_level() const noexcept;

    // Core translation routine: IPA -> PA
    [[nodiscard]] std::optional<Stage2TranslationResult> translate(
        const Memory& memory,
        std::uint64_t ipa,
        Stage2AccessType access,
        Stage2Fault* out_fault = nullptr) const;

    // Helper to map a page in memory (constructing valid descriptors)
    static bool map_page_16k(
        Memory& memory,
        std::uint64_t table_base_pa,
        std::uint64_t ipa,
        std::uint64_t pa,
        bool readable,
        bool writable,
        bool executable,
        std::uint8_t mem_attr = 0xF);

    static bool map_page_4k(
        Memory& memory,
        std::uint64_t table_base_pa,
        std::uint64_t ipa,
        std::uint64_t pa,
        bool readable,
        bool writable,
        bool executable,
        std::uint8_t mem_attr = 0xF);

    // Snapshot serialization & restoration
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const void* data, std::size_t size);

private:
    Stage2FaultInjector* fault_injector_{nullptr};
    std::uint64_t hcr_el2_{0};
    std::uint64_t vttbr_el2_{0};
    std::uint64_t vtcr_el2_{0x80003550ULL}; // 48-bit IPA, 4KB default

    // Walking implementations
    std::optional<Stage2TranslationResult> walk_16k(
        const Memory& memory,
        std::uint64_t ipa,
        Stage2AccessType access,
        Stage2Fault* out_fault) const;

    std::optional<Stage2TranslationResult> walk_4k(
        const Memory& memory,
        std::uint64_t ipa,
        Stage2AccessType access,
        Stage2Fault* out_fault) const;
};

} // namespace winios::mmu
