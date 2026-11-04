#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

namespace winios {

// -----------------------------------------------------------------------------
// InterruptType: GICv3 Interrupt Classification
// -----------------------------------------------------------------------------
enum class InterruptType : std::uint32_t {
    SGI = 0, // Software Generated Interrupt (INTID 0–15)
    PPI = 1, // Private Peripheral Interrupt (INTID 16–31)
    SPI = 2, // Shared Peripheral Interrupt (INTID 32–1019)
    Extended = 3
};

[[nodiscard]] constexpr InterruptType classify_interrupt(std::uint32_t intid) noexcept {
    if (intid < 16) return InterruptType::SGI;
    if (intid < 32) return InterruptType::PPI;
    if (intid < 1020) return InterruptType::SPI;
    return InterruptType::Extended;
}

[[nodiscard]] constexpr std::string_view interrupt_type_name(InterruptType type) noexcept {
    switch (type) {
    case InterruptType::SGI:      return "SGI";
    case InterruptType::PPI:      return "PPI";
    case InterruptType::SPI:      return "SPI";
    case InterruptType::Extended: return "Extended";
    default:                      return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// InterruptRequest: Scheduled interrupt signal targeting a specific core
// -----------------------------------------------------------------------------
struct InterruptRequest {
    std::size_t target_core{0};
    std::size_t source_core{0};
    std::uint32_t intid{0};
    std::uint8_t priority{0x80u};
    InterruptType type{InterruptType::SPI};
    bool is_fiq{false};
    std::uint64_t request_sequence{0};
};

// -----------------------------------------------------------------------------
// SmpInterruptRouterTelemetry: Telemetry for routed cross-core interrupts
// -----------------------------------------------------------------------------
struct SmpInterruptRouterTelemetry {
    std::uint64_t sgi_count{0};
    std::uint64_t ppi_count{0};
    std::uint64_t spi_count{0};
    std::uint64_t total_requested{0};
    std::uint64_t total_delivered{0};
};

// -----------------------------------------------------------------------------
// SmpInterruptRouter: Cross-Core Interrupt Router with Strict Request/Delivery Separation
//
// Key Design Principles:
// 1. Strict Request-Delivery Separation: Interrupts are enqueued deterministically during
//    execution, but delivered strictly at defined instruction/quantum boundaries to eliminate
//    cross-thread scheduling jitter.
// 2. Per-core Redistributor Isolation: SGIs (0-15) and PPIs (16-31) are routed to per-core
//    queues without cross-core interference.
// 3. Shared Distributor Routing: SPIs (32-1019) are routed to specified target cores.
// 4. Deterministic FIFO Delivery: Delivers interrupts in monotonic sequence order.
// -----------------------------------------------------------------------------
class SmpInterruptRouter {
public:
    explicit SmpInterruptRouter(std::size_t max_cores = 16);

    // Enqueue an interrupt request targeting target_core
    void request_interrupt(std::size_t target_core, std::uint32_t intid,
                           std::uint8_t priority = 0x80u, std::size_t source_core = 0,
                           bool is_fiq = false);

    // Enqueue an SGI targeting multiple cores (core bitmask)
    void request_sgi(std::size_t source_core, std::uint32_t target_core_mask,
                     std::uint32_t sgi_intid, std::uint8_t priority = 0x80u);

    // Query pending interrupt state
    [[nodiscard]] bool has_pending(std::size_t target_core) const noexcept;
    [[nodiscard]] std::size_t pending_count(std::size_t target_core) const noexcept;
    [[nodiscard]] std::optional<InterruptRequest> peek_pending(std::size_t target_core) const noexcept;

    // Delivery phase: pops next pending request and raises asynchronous exception on CPU
    bool deliver_next(std::size_t target_core, CPU& cpu, GICv3* gic = nullptr);

    // Deliver all pending requests for target_core
    std::size_t deliver_all_pending(std::size_t target_core, CPU& cpu, GICv3* gic = nullptr);

    // Clear pending interrupts
    void clear_pending(std::size_t target_core) noexcept;
    void clear_all() noexcept;

    // Snapshot serialization & restoration inspection
    [[nodiscard]] std::size_t max_cores() const noexcept { return queues_.size(); }
    [[nodiscard]] const std::vector<std::deque<InterruptRequest>>& all_queues() const noexcept { return queues_; }
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    void set_next_sequence(std::uint64_t seq) noexcept { next_sequence_ = seq; }
    void restore_queue(std::size_t target_core, const std::vector<InterruptRequest>& requests);

    // Telemetry access
    [[nodiscard]] SmpInterruptRouterTelemetry get_telemetry() const noexcept;
    void reset_telemetry() noexcept;

private:
    void ensure_core(std::size_t core_id);

    std::vector<std::deque<InterruptRequest>> queues_;
    std::uint64_t next_sequence_{1};
    mutable SmpInterruptRouterTelemetry telemetry_{};
};

} // namespace winios
