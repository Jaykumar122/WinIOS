#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace winios {

class GICv3 {
public:
    static constexpr std::uint64_t kGicdBase = 0x08000000ULL;
    static constexpr std::uint64_t kGicdSize = 0x00010000ULL; // 64 KB
    static constexpr std::uint64_t kGicrBase = 0x080A0000ULL;
    static constexpr std::uint64_t kGicrCoreStride = 0x00020000ULL; // 128 KB per core (RD + SGI)

    static constexpr std::size_t kMaxCores = 8;
    static constexpr std::uint32_t kNumSgiPpi = 32u;
    static constexpr std::uint32_t kNumSpis   = 96u; // INTID 32..127
    static constexpr std::uint32_t kTotalIrqs = kNumSgiPpi + kNumSpis; // 128
    static constexpr std::uint32_t kNumListRegs = 4u;
    static constexpr std::uint32_t kSpuriousIntId = 1023u;

    GICv3() noexcept;

    void reset() noexcept;

    std::size_t get_core_count() const noexcept { return num_cores_; }
    void set_core_count(std::size_t count) noexcept;

    // Address range queries
    bool is_gicd_address(std::uint64_t pa) const noexcept {
        return pa >= kGicdBase && pa < kGicdBase + kGicdSize;
    }
    bool is_gicr_address(std::uint64_t pa) const noexcept {
        return pa >= kGicrBase && pa < kGicrBase + num_cores_ * kGicrCoreStride;
    }
    bool is_mmio_address(std::uint64_t pa) const noexcept {
        return is_gicd_address(pa) || is_gicr_address(pa);
    }

    // MMIO read / write (8, 16, 32, 64-bit)
    std::uint64_t read_mmio(std::uint64_t pa, unsigned width) const noexcept;
    void write_mmio(std::uint64_t pa, std::uint64_t value, unsigned width) noexcept;

    // Timer PPI level lines (INTID 26 = CNTHP, 27 = CNTV, 30 = CNTP)
    void set_ppi_level(std::uint32_t intid, bool level, std::size_t core_id = 0) noexcept;

    // SPI level lines
    void set_spi_level(std::uint32_t intid, bool level) noexcept;

    // Returns true if GIC has been explicitly configured/enabled by software on core_id
    bool is_enabled(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        // A distributor write on another core may set this flag for every
        // core, so the read must not race with it. The remaining ICC/ICH
        // fields stay owner-confined (see the mutex_ comment below).
        return icc_[c].gic_enabled.load(std::memory_order_acquire);
    }

    // Interrupt signaling structure
    struct PendingSignal {
        bool has_interrupt{false};
        bool is_fiq{false};         // true = FIQ / Group 0, false = IRQ / Group 1
        std::uint32_t intid{kSpuriousIntId};
        std::uint8_t priority{0xFFu};
        bool is_virtual{false};
        int lr_index{-1};
    };

    // Evaluates the current interrupt signals for CPU execution on core_id
    PendingSignal evaluate(std::uint64_t current_el, std::uint64_t hcr_el2, std::size_t core_id = 0) const noexcept;

    // --- CPU Interface System Registers (ICC) for core_id ---
    std::uint8_t read_icc_pmr(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_pmr_el1;
    }
    void write_icc_pmr(std::uint8_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_pmr_el1 = val;
        icc_[c].gic_enabled = true;
    }

    std::uint32_t read_icc_bpr0(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_bpr0_el1;
    }
    void write_icc_bpr0(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_bpr0_el1 = static_cast<std::uint8_t>(val & 0x7u);
    }

    std::uint32_t read_icc_bpr1(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_bpr1_el1;
    }
    void write_icc_bpr1(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_bpr1_el1 = static_cast<std::uint8_t>(val & 0x7u);
    }

    std::uint32_t read_icc_ctlr(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_ctlr_el1;
    }
    void write_icc_ctlr(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_ctlr_el1 = val;
    }

    // Running Priority Register (ICC_RPR_EL1)
    std::uint32_t read_icc_rpr(std::size_t core_id = 0) const noexcept;

    // Deactivate Interrupt Register (ICC_DIR_EL1)
    void write_icc_dir(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id = 0) noexcept;

    std::uint32_t read_icc_igrpen0(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_igrpen0_el1;
    }
    void write_icc_igrpen0(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_igrpen0_el1 = val & 0x1u;
        icc_[c].gic_enabled = true;
    }

    std::uint32_t read_icc_igrpen1(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_igrpen1_el1;
    }
    void write_icc_igrpen1(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].icc_igrpen1_el1 = val & 0x1u;
        icc_[c].gic_enabled = true;
    }

    std::uint32_t read_icc_sre_el1(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_sre_el1;
    }
    void write_icc_sre_el1(std::uint32_t val, std::size_t core_id = 0) noexcept {
        (void)val; (void)core_id;
    }

    std::uint32_t read_icc_sre_el2(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].icc_sre_el2;
    }
    void write_icc_sre_el2(std::uint32_t val, std::size_t core_id = 0) noexcept {
        (void)val; (void)core_id;
    }

    // Acknowledge (IAR)
    std::uint32_t acknowledge_group0(std::uint64_t current_el, std::size_t core_id = 0) noexcept;
    std::uint32_t acknowledge_group1(std::uint64_t current_el, std::size_t core_id = 0) noexcept;

    // End of Interrupt (EOIR)
    void end_of_interrupt_group0(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id = 0) noexcept;
    void end_of_interrupt_group1(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id = 0) noexcept;

    // Highest Priority Pending (HPPIR)
    std::uint32_t read_hppir0(std::uint64_t current_el, std::size_t core_id = 0) const noexcept;
    std::uint32_t read_hppir1(std::uint64_t current_el, std::size_t core_id = 0) const noexcept;

    // Software Generated Interrupt dispatch (Phase 42)
    void send_sgi(std::size_t sender_core, std::uint64_t sgi1r) noexcept;

    // --- Virtual Interface System Registers (ICH, EL2) for core_id ---
    std::uint32_t read_ich_hcr(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].ich_hcr_el2;
    }
    void write_ich_hcr(std::uint32_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].ich_hcr_el2 = val;
        icc_[c].gic_enabled = true;
    }

    std::uint64_t read_ich_vtr(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].ich_vtr_el2;
    }

    std::uint32_t read_ich_misr(std::size_t core_id = 0) const noexcept;
    std::uint32_t read_ich_eisr(std::size_t core_id = 0) const noexcept;
    std::uint32_t read_ich_elrsr(std::size_t core_id = 0) const noexcept;

    std::uint64_t read_ich_vmcr(std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return icc_[c].ich_vmcr_el2;
    }
    void write_ich_vmcr(std::uint64_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        icc_[c].ich_vmcr_el2 = val;
        icc_[c].gic_enabled = true;
    }

    std::uint64_t read_ich_lr(unsigned index, std::size_t core_id = 0) const noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        return (index < kNumListRegs) ? icc_[c].ich_lr_el2[index] : 0ULL;
    }
    void write_ich_lr(unsigned index, std::uint64_t val, std::size_t core_id = 0) noexcept {
        const std::size_t c = (core_id < num_cores_) ? core_id : 0;
        if (index < kNumListRegs) {
            icc_[c].ich_lr_el2[index] = val;
            icc_[c].gic_enabled = true;
        }
    }

    struct DistributorState {
        std::uint32_t gicd_ctlr{0x50u};
        std::uint32_t gicd_typer{0x03780003u};
        std::uint32_t gicd_igroupr[4]{};
        std::uint32_t gicd_igrpmodr[4]{};
        std::uint32_t gicd_isenabler[4]{};
        std::uint32_t gicd_ispendr[4]{};
        std::uint32_t gicd_isactiver[4]{};
        std::uint8_t  gicd_ipriorityr[128]{};
        std::uint32_t gicd_icfgr[8]{};
        std::uint32_t gicd_level[4]{};
        std::uint64_t gicd_irouter[96]{};
    };

    struct RedistributorState {
        std::uint32_t ctlr{0};
        std::uint32_t waker{0};
        std::uint32_t igroupr0{0};
        std::uint32_t igrpmodr0{0};
        std::uint32_t isenabler0{0};
        std::uint32_t ispendr0{0};
        std::uint32_t isactiver0{0};
        std::uint8_t  ipriorityr[32]{};
        std::uint32_t icfgr0{0xAAAAAAAAu}; // SGIs edge-triggered
        std::uint32_t icfgr1{0};           // PPIs
        std::uint32_t level0{0};           // PPI level inputs
    };

    struct CpuInterfaceState {
        std::uint8_t  icc_pmr_el1{0};
        std::uint8_t  icc_bpr0_el1{2};
        std::uint8_t  icc_bpr1_el1{3};
        std::uint32_t icc_ctlr_el1{0};
        std::uint32_t icc_igrpen0_el1{0};
        std::uint32_t icc_igrpen1_el1{0};
        std::uint32_t icc_sre_el1{0x7u};
        std::uint32_t icc_sre_el2{0xFu};
        std::vector<std::uint8_t> active_priorities{};

        std::uint32_t ich_hcr_el2{0};
        std::uint64_t ich_vtr_el2{0x60000000ULL | (3ULL << 29) | (1ULL << 26) | (1ULL << 23) | (6ULL << 0) | (5ULL << 19)};
        std::uint64_t ich_vmcr_el2{0x0000000000480000ULL | (3ULL << 21) | (4ULL << 18)};
        std::uint64_t ich_lr_el2[kNumListRegs]{};
        std::vector<std::uint8_t> active_virt_priorities{};

        // Written for all cores by GICD_CTLR/GICD_* MMIO on any core, read by
        // each core's WFI/WFE path, so it is atomic rather than mutex-guarded.
        std::atomic<bool> gic_enabled{false};
    };

    // Public state accessors for probes / tests
    RedistributorState& get_redistributor(std::size_t core_id) noexcept {
        return redistributors_[(core_id < num_cores_) ? core_id : 0];
    }
    const RedistributorState& get_redistributor(std::size_t core_id) const noexcept {
        return redistributors_[(core_id < num_cores_) ? core_id : 0];
    }
    CpuInterfaceState& get_cpu_interface(std::size_t core_id) noexcept {
        return icc_[(core_id < num_cores_) ? core_id : 0];
    }
    const CpuInterfaceState& get_cpu_interface(std::size_t core_id) const noexcept {
        return icc_[(core_id < num_cores_) ? core_id : 0];
    }

    DistributorState get_distributor_state() const noexcept;
    void set_distributor_state(const DistributorState& s) noexcept;

private:
    std::uint64_t read_gicd(std::uint64_t offset, unsigned width) const noexcept;
    void write_gicd(std::uint64_t offset, std::uint64_t val, unsigned width) noexcept;

    std::uint64_t read_gicr(std::uint64_t offset, unsigned width) const noexcept;
    void write_gicr(std::uint64_t offset, std::uint64_t val, unsigned width) noexcept;

    // Helper to evaluate highest priority pending virtual interrupt
    int get_highest_pending_virt_lr(std::uint8_t& out_prio, std::size_t core_id) const noexcept;

    // Running active priority helpers
    std::uint8_t get_running_priority(std::size_t core_id) const noexcept;
    std::uint8_t get_running_virt_priority(std::size_t core_id) const noexcept;

    std::size_t num_cores_{1};

    // --- Distributor (GICD) state (shared) ---
    std::uint32_t gicd_ctlr_{0x50u}; // ARE_NS | DS
    std::uint32_t gicd_typer_{0x03780003u}; // A3V | No1N | LPIS | IDbits=0xF | ITLinesNumber=3
    std::uint32_t gicd_igroupr_[4]{};     // 128 INTIDs (words 1..3 for SPIs)
    std::uint32_t gicd_igrpmodr_[4]{};    // Group modifier
    std::uint32_t gicd_isenabler_[4]{};
    std::uint32_t gicd_ispendr_[4]{};
    std::uint32_t gicd_isactiver_[4]{};
    std::uint8_t  gicd_ipriorityr_[128]{};
    std::uint32_t gicd_icfgr_[8]{};       // 2 bits per INTID (words 2..7 for SPIs)
    std::uint32_t gicd_level_[4]{};
    std::uint64_t gicd_irouter_[96]{};    // SPI affinity routing (INTID 32..127)

    // --- Redistributor (GICR) state (per-core) ---
    RedistributorState redistributors_[kMaxCores]{};

    // --- CPU & Virtual Interface (ICC/ICH) state (per-core) ---
    CpuInterfaceState icc_[kMaxCores]{};

    // Phase 43: serializes distributor/redistributor state and any cross-core
    // access (SGI dispatch, PPI level updates, evaluation, MMIO). Per-core ICC
    // slots are owner-confined and are not touched by other cores. Recursive
    // because public entry points call private accessors that may lock again.
    mutable std::recursive_mutex mutex_;
};

} // namespace winios
