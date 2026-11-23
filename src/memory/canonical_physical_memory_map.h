#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace winios::memory {

// -----------------------------------------------------------------------------
// CanonicalMemoryKind: Granular classification of physical intervals
// -----------------------------------------------------------------------------
enum class CanonicalMemoryKind {
    KernelTextPrimary,      // Primary Mach-O __TEXT segment (Frame 0: 0x00000000..0x00004000)
    KernelTextSecondary,    // Secondary / Historical kernel text (Frame 32: 0x00080000..0x00084000)
    KernelData,             // Mach-O __DATA segment (Frames 4..11: 0x00010000..0x00030000)
    LowPaddingScratch,      // Low bootstrap scratch/padding (Frames 1..3: 0x00004000..0x00010000)
    DeviceTree,             // Apple DeviceTree Blob (Frame 12: 0x00030000..0x00034000)
    BootArgs,               // BootArgs Structure (Frame 13: 0x00034000..0x00038000)
    BootstrapStack,         // Cold-boot early stack (Frame 14: 0x00038000..0x0003C000)
    PageTables,             // Stage 1 Translation Tables (Frames 18..19: 0x00048000..0x00050000)
    CoreGuard,              // Per-core unmapped fault-trapping stack guard
    CoreInterruptGuard,     // Independent unmapped guard below interrupt stack (Campaign 262)
    CoreThreadGuard,        // Independent unmapped guard below thread stack (Campaign 262)
    SmpSharedMemory,        // SMP shared memory / mailboxes (Campaign 262)
    DiagnosticGuard,        // Guard protecting staged diagnostic code (Campaign 262)
    CoreInterruptStack,     // Per-core exception / interrupt stack (SP_EL1)
    CoreThreadStack,        // Per-core thread stack / CPU data area (SP_EL0)
    AllocatorMetadata,      // Allocator watermark and management tables (Frame 36: 0x00090000..0x00094000)
    DynamicFreePool,        // Available allocatable frames (Frames 37..511: 0x00094000..0x00800000)
    MmioDevice              // Sub-1GB MMIO device aperture
};

const char* canonical_memory_kind_to_string(CanonicalMemoryKind kind) noexcept;

// -----------------------------------------------------------------------------
// CanonicalMemoryInterval: Self-describing physical address interval
// -----------------------------------------------------------------------------
struct CanonicalMemoryInterval {
    std::string         name;
    CanonicalMemoryKind kind{CanonicalMemoryKind::DynamicFreePool};
    std::uint64_t       start_address{0};
    std::uint64_t       end_address{0}; // Exclusive end
    std::uint64_t       size{0};
    std::string         owner;
    std::string         purpose;
    std::string         memory_type;    // "RAM", "MMIO", "Guard"
    std::uint32_t       permissions{0}; // 1=R, 2=W, 4=X
    bool                is_mapped{true};
    std::uint64_t       page_granule{16384ULL};
    std::string         reservation_reason;
    std::int32_t        core_ownership{-1}; // -1 = Global/All, 0..5 = Core index
    std::string         provenance;     // "Category A", "Category B", "Category C", "Category D"
    std::string         address_source;

    [[nodiscard]] bool overlaps_with(const CanonicalMemoryInterval& other) const noexcept {
        if (size == 0 || other.size == 0) return false;
        // Checked addition to prevent 64-bit overflow
        if (start_address + size < start_address || other.start_address + other.size < other.start_address) {
            return true;
        }
        return start_address < other.end_address && other.start_address < end_address;
    }

    [[nodiscard]] bool is_wx_violation() const noexcept {
        return (permissions & 0x6u) == 0x6u; // Both W (2) and X (4)
    }

    [[nodiscard]] bool contains(std::uint64_t addr, std::uint64_t len = 1) const noexcept {
        if (size == 0 || len == 0) return false;
        if (addr + len < addr) return false;
        return addr >= start_address && (addr + len) <= end_address;
    }
};

// -----------------------------------------------------------------------------
// ConflictRecord: Structured collision diagnostic
// -----------------------------------------------------------------------------
struct ConflictRecord {
    std::string   owner_a;
    std::string   owner_b;
    std::uint64_t overlap_start{0};
    std::uint64_t overlap_end{0};
    std::uint64_t overlap_size{0};
    std::string   description;
};

// -----------------------------------------------------------------------------
// CanonicalPhysicalMemoryMap: Authoritative single source of memory geometry
// -----------------------------------------------------------------------------
class CanonicalPhysicalMemoryMap {
public:
    static constexpr std::uint64_t kDefaultRamBase    = 0x00000000ULL;
    static constexpr std::uint64_t kDefaultRamSize    = 0x00800000ULL; // 8 MiB (512 frames)
    static constexpr std::uint64_t kPageGranule16K    = 16384ULL;      // 16 KiB
    static constexpr std::size_t   kTotalRamFrames    = 512;
    static constexpr std::size_t   kNumCores          = 6;

    explicit CanonicalPhysicalMemoryMap(std::uint64_t ram_base = kDefaultRamBase,
                                        std::uint64_t ram_size = kDefaultRamSize);

    // Add an interval with strict validation
    bool add_interval(const CanonicalMemoryInterval& interval, std::string* out_error = nullptr);

    // Accessors
    [[nodiscard]] std::uint64_t ram_base() const noexcept { return ram_base_; }
    [[nodiscard]] std::uint64_t ram_size() const noexcept { return ram_size_; }
    [[nodiscard]] std::uint64_t ram_end() const noexcept { return ram_base_ + ram_size_; }
    [[nodiscard]] const std::vector<CanonicalMemoryInterval>& intervals() const noexcept { return intervals_; }

    [[nodiscard]] const CanonicalMemoryInterval* find_interval_by_name(std::string_view name) const;
    [[nodiscard]] const CanonicalMemoryInterval* find_interval_by_address(std::uint64_t addr) const;

    // Allocator free ranges derivation: subtracts all reserved intervals from RAM envelope
    struct FreeRange {
        std::uint64_t start{0};
        std::uint64_t end{0};
        std::uint64_t size{0};
        std::size_t   frame_count{0};
    };
    [[nodiscard]] std::vector<FreeRange> derive_free_ranges() const;

    // Checks if a given address/range intersects ANY reserved or occupied interval
    [[nodiscard]] bool is_range_reserved(std::uint64_t base, std::uint64_t size) const;

    // Six-core stack geometry query
    struct CoreStackGeometry {
        std::size_t   core_id{0};
        std::uint64_t guard_base{0};
        std::uint64_t guard_size{0};
        std::uint64_t int_stack_base{0};
        std::uint64_t int_stack_size{0};
        std::uint64_t int_stack_initial_sp{0};
        std::uint64_t thread_stack_base{0};
        std::uint64_t thread_stack_size{0};
        std::uint64_t thread_stack_initial_sp{0};

        // Campaign 262 dual-guard extensions:
        std::uint64_t int_guard_base{0};
        std::uint64_t int_guard_size{16384ULL};
        std::uint64_t thread_guard_base{0};
        std::uint64_t thread_guard_size{16384ULL};
    };
    [[nodiscard]] std::optional<CoreStackGeometry> get_core_stack_geometry(std::size_t core_id) const;

    // Build the default authoritative iPhone 15 A16 8 MiB canonical layout
    static CanonicalPhysicalMemoryMap create_canonical_iphone15_layout();
    // Build the Campaign 262 dual-stack, independent-guard canonical layout
    static CanonicalPhysicalMemoryMap create_canonical_dual_stack_layout();

private:
    std::uint64_t ram_base_;
    std::uint64_t ram_size_;
    std::vector<CanonicalMemoryInterval> intervals_;
};

// -----------------------------------------------------------------------------
// PhysicalMemoryOverlapAuditor: Independent auditor for overlap & integrity
// -----------------------------------------------------------------------------
class PhysicalMemoryOverlapAuditor {
public:
    explicit PhysicalMemoryOverlapAuditor(const CanonicalPhysicalMemoryMap& map);

    // Audit for any overlapping intervals
    [[nodiscard]] bool audit_overlaps(std::vector<ConflictRecord>& out_conflicts) const;

    // Audit for W^X compliance across all mapped intervals
    [[nodiscard]] bool audit_wx_compliance(std::vector<std::string>& out_violations) const;

    // Audit for page-granule alignment (16 KiB multiples)
    [[nodiscard]] bool audit_granule_alignment(std::vector<std::string>& out_errors) const;

    // Audit six-core stack isolation: ensure each core has disjoint stacks & guards
    [[nodiscard]] bool audit_six_core_stack_isolation(std::vector<std::string>& out_errors) const;

    // Audit allocator exclusion: ensure free pool does NOT overlap kernel text, stacks, etc.
    [[nodiscard]] bool audit_allocator_exclusion(std::vector<std::string>& out_errors) const;

    // Full comprehensive audit
    [[nodiscard]] bool run_full_audit(std::vector<std::string>& out_report) const;

private:
    const CanonicalPhysicalMemoryMap& map_;
};

} // namespace winios::memory