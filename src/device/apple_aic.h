#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "device/virtual_device.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleAIC: Apple Interrupt Controller v2 (AIC 2) Emulation
// Models the AIC 2 interrupt controller used in Apple Silicon (T8120 / A16 Bionic /
// iPhone 15,4). Supports per-core IPIs, timer interrupt routing, hardware IRQ
// masking/routing across up to 8 CPU cores, and event popping / EOI lifecycle.
// -----------------------------------------------------------------------------
class AppleAIC : public VirtualDevice {
public:
    static constexpr std::uint64_t kDefaultAicBase = 0x08000000ULL;
    static constexpr std::uint64_t kDefaultAicSize = 0x00100000ULL; // 1 MB
    static constexpr std::size_t   kMaxCores        = 8;
    static constexpr std::uint32_t kMaxIrqs         = 512;
    static constexpr std::size_t   kNumIrqWords     = kMaxIrqs / 32; // 16 words

    // Event Types encoded in AIC_EVENT bits [31:16]
    static constexpr std::uint32_t kEventTypeHw    = 0x0000u;
    static constexpr std::uint32_t kEventTypeIpi   = 0x0001u;
    static constexpr std::uint32_t kEventTypeTimer = 0x0002u;
    static constexpr std::uint32_t kEventNone      = 0xFFFFFFFFu;

    // Register Offsets
    static constexpr std::uint64_t kRegInfo           = 0x0004u; // AIC Hardware Information
    static constexpr std::uint64_t kRegConfig         = 0x0010u; // Global Config / Enable
    static constexpr std::uint64_t kRegIpiSend        = 0x0040u; // Global IPI Send (target core mask)

    // Per-Core Registers (Banked at 0x2000 or stride 0x80 per core)
    static constexpr std::uint64_t kRegCoreBase       = 0x2000u;
    static constexpr std::uint64_t kCoreStride        = 0x0080u;
    static constexpr std::uint64_t kCoreOffWhoAmI     = 0x0000u; // Current core index
    static constexpr std::uint64_t kCoreOffEvent      = 0x0004u; // Pop active event / write EOI
    static constexpr std::uint64_t kCoreOffIpiAck     = 0x0008u; // Read/Clear pending IPIs
    static constexpr std::uint64_t kCoreOffIpiMask    = 0x000Cu; // Per-core IPI mask

    // Target Routing Table (0x4000 .. 0x47FF: 4 bytes per IRQ)
    static constexpr std::uint64_t kRegTargetBase     = 0x4000u;

    // Hardware IRQ Masking & Status Tables (16 words each)
    static constexpr std::uint64_t kRegMaskSetBase    = 0x8000u;
    static constexpr std::uint64_t kRegMaskClrBase    = 0x8080u;
    static constexpr std::uint64_t kRegHwPendingBase  = 0x8100u;
    static constexpr std::uint64_t kRegHwStateBase    = 0x8180u;
    static constexpr std::uint64_t kRegSwSetBase      = 0x8200u;
    static constexpr std::uint64_t kRegSwClrBase      = 0x8280u;

    explicit AppleAIC(std::uint32_t device_id = 0x41494332u, // 'AIC2'
                      std::uint64_t mmio_base = kDefaultAicBase,
                      std::uint64_t mmio_size = kDefaultAicSize,
                      std::size_t num_cores = 6); // 6 cores for iPhone 15 A16 Bionic

    ~AppleAIC() override = default;

    // VirtualDevice Interface
    std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    std::vector<std::uint8_t> serialize_state() const override;
    bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Core Topology Configuration
    [[nodiscard]] std::size_t core_count() const noexcept { return num_cores_; }
    void set_core_count(std::size_t cores) noexcept;

    // Active Core Context (for unindexed access at 0x2000)
    void set_current_core(std::size_t core_id) noexcept {
        current_core_ = (core_id < num_cores_) ? core_id : 0;
    }
    [[nodiscard]] std::size_t current_core() const noexcept { return current_core_; }

    // Hardware IRQ Line Control
    void set_hw_irq(std::uint32_t irq, bool asserted) noexcept;
    [[nodiscard]] bool is_hw_irq_asserted(std::uint32_t irq) const noexcept;
    [[nodiscard]] bool is_hw_irq_masked(std::uint32_t irq) const noexcept;
    void mask_hw_irq(std::uint32_t irq) noexcept;
    void unmask_hw_irq(std::uint32_t irq) noexcept;

    // IRQ Target Core Affinity (bitmask of cores, e.g. 0x01 = Core 0)
    void set_irq_target(std::uint32_t irq, std::uint32_t core_mask) noexcept;
    [[nodiscard]] std::uint32_t get_irq_target(std::uint32_t irq) const noexcept;

    // Inter-Processor Interrupts (IPI)
    void send_ipi(std::uint32_t target_core_mask) noexcept;
    [[nodiscard]] std::uint32_t get_pending_ipi(std::size_t core_id) const noexcept;
    void clear_ipi(std::size_t core_id, std::uint32_t ipi_mask) noexcept;

    // Architectural SoC Timer Interrupts (routed from CPU timers)
    void set_timer_irq(std::size_t core_id, bool asserted) noexcept;
    [[nodiscard]] bool is_timer_irq_asserted(std::size_t core_id) const noexcept;

    // Software Interrupts
    void set_sw_irq(std::uint32_t irq, bool asserted) noexcept;

    // Core Interrupt Status & Evaluation
    [[nodiscard]] bool has_pending_interrupt(std::size_t core_id) const noexcept;
    [[nodiscard]] std::uint32_t pop_event(std::size_t core_id) noexcept;
    void acknowledge_eoi(std::size_t core_id, std::uint32_t event_code) noexcept;

private:
    void update_core_irq_lines();
    [[nodiscard]] std::uint32_t evaluate_highest_event(std::size_t core_id) const noexcept;

    mutable std::mutex lock_;

    std::size_t num_cores_{6};
    std::size_t current_core_{0};
    bool aic_enabled_{true};

    // Hardware IRQ State Tables (16 x 32 = 512 lines)
    std::array<std::uint32_t, kNumIrqWords> mask_bits_{};       // 1 = Masked (disabled)
    std::array<std::uint32_t, kNumIrqWords> hw_pending_bits_{};  // 1 = Asserted
    std::array<std::uint32_t, kNumIrqWords> sw_pending_bits_{};  // 1 = SW triggered
    std::array<std::uint32_t, kMaxIrqs>     target_cores_{};     // Target core bitmask

    // Per-Core State
    struct CoreState {
        std::uint32_t ipi_pending{0};
        std::uint32_t ipi_mask{0};
        bool timer_pending{false};
        std::uint32_t active_event{kEventNone};
        bool irq_line_asserted{false};
    };
    std::array<CoreState, kMaxCores> cores_{};
};

} // namespace winios::device
