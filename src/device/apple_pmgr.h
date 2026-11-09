#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "device/virtual_device.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// ApplePMGR: Apple Power Management (PMGR) Controller
// Emulates the Apple Silicon PMGR for the T8120 (Apple A16 Bionic / iPhone 15,4).
// Manages SoC power domains, clock gating acknowledgments, chip identification,
// and system reset handshakes expected by ApplePMGR.kext in Darwin/iOS.
// -----------------------------------------------------------------------------
class ApplePMGR : public VirtualDevice {
public:
    static constexpr std::uint64_t kDefaultPmgrBase = 0x10000000ULL;
    static constexpr std::uint64_t kDefaultPmgrSize = 0x00100000ULL; // 1 MB
    static constexpr std::uint32_t kChipIdT8120     = 0x8120u;       // A16 Bionic
    static constexpr std::uint32_t kPmgrRevision    = 0x0001u;       // Rev A0

    // Global Management Registers
    static constexpr std::uint64_t kRegChipId       = 0x0000u; // Read Chip ID (0x8120)
    static constexpr std::uint64_t kRegRevision     = 0x0004u; // Chip Silicon Revision
    static constexpr std::uint64_t kRegSysStatus    = 0x0008u; // Global System Power Status
    static constexpr std::uint64_t kRegResetTrigger = 0x0010u; // Software Reset Trigger

    // Standard Power Domain Offsets (T8120 SoC Topology)
    static constexpr std::uint64_t kDomainCpuCluster0 = 0x0100u; // Everest Performance Cluster
    static constexpr std::uint64_t kDomainCpuCluster1 = 0x0108u; // Sawtooth Efficiency Cluster
    static constexpr std::uint64_t kDomainAic         = 0x0180u; // Apple Interrupt Controller
    static constexpr std::uint64_t kDomainUart0       = 0x0200u; // Serial Debug Console
    static constexpr std::uint64_t kDomainDisplay     = 0x0280u; // Display Framebuffer Controller
    static constexpr std::uint64_t kDomainGpu         = 0x0300u; // Apple 5-Core GPU
    static constexpr std::uint64_t kDomainAne         = 0x0380u; // Apple Neural Engine
    static constexpr std::uint64_t kDomainMedia       = 0x0400u; // Media Codec Engine

    // Power State Values
    static constexpr std::uint32_t kPowerStateOff     = 0x0u;
    static constexpr std::uint32_t kPowerStateLow     = 0x1u;
    static constexpr std::uint32_t kPowerStateActive  = 0x4u;
    static constexpr std::uint32_t kPowerStateFull    = 0xFu;

    explicit ApplePMGR(std::uint32_t device_id = 0x504D4752u, // 'PMGR'
                       std::uint64_t mmio_base = kDefaultPmgrBase,
                       std::uint64_t mmio_size = kDefaultPmgrSize,
                       std::uint32_t chip_id = kChipIdT8120);

    ~ApplePMGR() override = default;

    // VirtualDevice Interface
    std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    std::vector<std::uint8_t> serialize_state() const override;
    bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Power Domain Inspection & Management
    [[nodiscard]] std::uint32_t get_chip_id() const noexcept { return chip_id_; }
    [[nodiscard]] bool is_domain_powered(std::uint64_t offset) const noexcept;
    [[nodiscard]] std::uint32_t get_domain_power_state(std::uint64_t offset) const noexcept;
    void set_domain_power_state(std::uint64_t offset, std::uint32_t state) noexcept;

    // Reset Handshake Inspection
    [[nodiscard]] bool was_reset_requested() const noexcept { return reset_requested_; }
    void clear_reset_request() noexcept { reset_requested_ = false; }

private:
    void initialize_default_domains();

    mutable std::mutex lock_;
    std::uint32_t chip_id_{kChipIdT8120};
    std::uint32_t sys_status_{1u}; // 1 = Running
    bool reset_requested_{false};

    // Power gate registers: offset -> power state (bits [3:0])
    std::unordered_map<std::uint64_t, std::uint32_t> domain_states_;
};

} // namespace winios::device
