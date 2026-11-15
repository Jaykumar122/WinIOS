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
