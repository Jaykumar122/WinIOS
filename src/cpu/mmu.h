#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cpu/synchronous_exception.h"

class Memory;

namespace mmu {

// Translation regimes modeled in Phase 37.
enum class TranslationRegime : std::uint8_t {
    EL1_0 = 1, // Stage-1 translation for EL1 and EL0
};

// Access type for translation requests.
enum class AccessKind : std::uint8_t {
    Read    = 0,
    Write   = 1,
    Execute = 2,
};

// Descriptor types at page table levels.
enum class DescriptorKind : std::uint8_t {
    Invalid = 0,
    Block   = 1,
    Table   = 2,
    Page    = 3,
};

// Fault stage/type mapped to AArch64 DFSC/IFSC codes.
enum class MmuFaultType : std::uint8_t {
    AddressSize,
    Translation,
    AccessFlag,
    Permission,
    ExternalWalk,
};

// Strongly-typed translation fault structure.
struct TranslationFault {
    MmuFaultType type{MmuFaultType::Translation};
    unsigned level{0}; // 0, 1, 2, or 3
    std::uint64_t virtual_address{0};
    AccessKind access{AccessKind::Read};
    bool is_write{false};
    bool is_instruction{false};
    bool is_stage2{false};
    std::uint64_t fault_ipa{0};

    // Computes the architectural 6-bit FSC (Fault Status Code) for ESR_EL1 / ESR_EL2.
    std::uint32_t fsc() const noexcept {
        switch (type) {
        case MmuFaultType::AddressSize:
            return 0x00u + (level & 0x3u);
        case MmuFaultType::Translation:
            return 0x04u + (level & 0x3u);
        case MmuFaultType::AccessFlag:
            return 0x08u + (level & 0x3u);
        case MmuFaultType::Permission:
            return 0x0Cu + (level & 0x3u);
        case MmuFaultType::ExternalWalk:
            return 0x14u + (level & 0x3u);
        }
        return 0x04u;
    }
};

// Translation result containing physical address and attributes.
struct TranslationResult {
    std::uint64_t physical_address{0};
    std::uint64_t page_size{4096};
    bool readable{false};
    bool writable{false};
    bool executable{false};
    bool el0_accessible{false};
    std::uint8_t mair_attr{0};
};

// Software TLB entry.
struct TlbEntry {
    bool valid{false};
    std::uint64_t virtual_page{0};   // VA >> 12
    std::uint64_t physical_page{0};  // PA >> 12
    std::uint64_t page_mask{0xFFFFFFFFFFFFF000ULL}; // Mask for page/block size
    std::uint16_t asid{0};
    std::uint16_t vmid{0};
    bool is_stage2{false};
    bool el0_accessible{false};
    bool writable{false};
    bool executable_el0{false};
    bool executable_el1{false};
    bool non_global{false};          // nG bit (0 = global, 1 = non-global)
    std::uint8_t attr_indx{0};       // Memory attribute index from MAIR_EL1
    std::uint8_t shareability{0};    // Shareability field (SH)
    std::uint8_t regime{0};
};

// The MMU class manages system control registers, page-table walks, permissions,
// and the software TLB.
class MMU {
public:
    static constexpr std::uint64_t kPageSize4K = 4096ULL;
    static constexpr std::uint64_t kPageMask4K = ~4095ULL;

    MMU();

    // Reset all MMU control registers and clear TLB.
    void reset() noexcept;

    // System Register accessors
    std::uint64_t get_sctlr_el1() const noexcept { return sctlr_el1_; }
    void set_sctlr_el1(std::uint64_t val) noexcept;

    std::uint64_t get_tcr_el1() const noexcept { return tcr_el1_; }
    void set_tcr_el1(std::uint64_t val) noexcept;

    std::uint64_t get_ttbr0_el1() const noexcept { return ttbr0_el1_; }
    void set_ttbr0_el1(std::uint64_t val) noexcept;

    std::uint64_t get_ttbr1_el1() const noexcept { return ttbr1_el1_; }
    void set_ttbr1_el1(std::uint64_t val) noexcept;

    std::uint64_t get_mair_el1() const noexcept { return mair_el1_; }
    void set_mair_el1(std::uint64_t val) noexcept;

    std::uint64_t get_par_el1() const noexcept { return par_el1_; }
    void set_par_el1(std::uint64_t val) noexcept;

    // Phase 40: EL2 Hypervisor & Virtualization MMU Registers
    std::uint64_t get_hcr_el2() const noexcept { return hcr_el2_; }
    void set_hcr_el2(std::uint64_t val) noexcept;

    std::uint64_t get_vttbr_el2() const noexcept { return vttbr_el2_; }
    void set_vttbr_el2(std::uint64_t val) noexcept;

    std::uint64_t get_vtcr_el2() const noexcept { return vtcr_el2_; }
    void set_vtcr_el2(std::uint64_t val) noexcept;

    static constexpr std::uint64_t kIdAa64Mmfr0Val = 0x2100032310201126ULL;
    std::uint64_t get_id_aa64mmfr0_el1() const noexcept { return kIdAa64Mmfr0Val; }

    // Control flags
    bool is_mmu_enabled() const noexcept { return is_stage1_enabled(); }
    bool is_stage1_enabled() const noexcept { return (sctlr_el1_ & 0x1ULL) != 0; }
    bool is_stage2_enabled(unsigned current_el) const noexcept {
        return ((hcr_el2_ & 0x1ULL) != 0) && (current_el < 2);
    }
    bool is_alignment_check_enabled() const noexcept { return (sctlr_el1_ & 0x2ULL) != 0; }
    bool is_wxn_enabled() const noexcept { return (sctlr_el1_ & (1ULL << 19u)) != 0; }

    // TLB operations
    void invalidate_tlb_all() noexcept;
    void invalidate_tlb_va(std::uint64_t va, std::uint16_t asid, bool last_level_only = false) noexcept;
    void invalidate_tlb_asid(std::uint16_t asid) noexcept;
    void invalidate_tlb_vaa(std::uint64_t va, bool last_level_only = false) noexcept;
    void invalidate_tlb_vmid(std::uint16_t vmid) noexcept;

    bool is_tlb_enabled() const noexcept { return tlb_enabled_; }
    void set_tlb_enabled(bool enabled) noexcept { tlb_enabled_ = enabled; }

    // Address translation interface.
    // Handles Stage-1, Stage-2, and composite two-stage translation (VA -> IPA -> PA).
    // On translation fault, returns std::nullopt and populates fault out-parameter.
    std::optional<std::uint64_t> translate(
        const Memory& memory,
        std::uint64_t virtual_address,
        AccessKind access,
        unsigned current_el,
        TranslationFault* out_fault);

private:
    std::optional<std::uint64_t> walk_stage1(
        const Memory& memory,
        std::uint64_t virtual_address,
        AccessKind access,
        unsigned current_el,
        TranslationFault* out_fault);

    std::optional<std::uint64_t> walk_stage2(
        const Memory& memory,
        std::uint64_t ipa,
        AccessKind access,
        TranslationFault* out_fault);

    // MMU Registers
    std::uint64_t sctlr_el1_{0x00C50838ULL};
    std::uint64_t tcr_el1_{0};
    std::uint64_t ttbr0_el1_{0};
    std::uint64_t ttbr1_el1_{0};
    std::uint64_t mair_el1_{0};
    std::uint64_t par_el1_{0};

    // Phase 40: EL2 MMU Registers
    std::uint64_t hcr_el2_{0};
    std::uint64_t vttbr_el2_{0};
    std::uint64_t vtcr_el2_{0x80003550ULL}; // 48-bit IPA default

    // TLB State
    static constexpr std::size_t kTlbSize = 64;
    std::vector<TlbEntry> tlb_;
    bool tlb_enabled_{true};
};

} // namespace mmu
