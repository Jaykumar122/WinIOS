// Phase 48: PSCI (Power State Coordination Interface) virtual service.
// Implements ARM DEN0022 PSCI v1.1 subset for virtual machine use.
// This is a host-side interceptor: it handles known PSCI SMC calls
// before they reach the guest EL3 exception vectors.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

class CPU;

namespace winios {

class SMPCluster;

// Per-core power state as defined by PSCI specification (ARM DEN0022).
enum class CorePowerState : std::uint8_t {
    Off                 = 0,  // Core is powered off / halted
    OnPending           = 1,  // CPU_ON issued; startup in progress
    On                  = 2,  // Core is running
    SuspendedRetention  = 3,  // CPU_SUSPEND with retention (WFI-like)
    SuspendedPowerdown  = 4,  // CPU_SUSPEND with powerdown
};

class PSCIService {
public:
    // PSCI Function IDs (ARM DEN0022 §5)
    static constexpr std::uint32_t kVersion         = 0x84000000u;
    static constexpr std::uint32_t kCpuSuspend32    = 0x84000001u;
    static constexpr std::uint32_t kCpuOff          = 0x84000002u;
    static constexpr std::uint32_t kCpuOn32         = 0x84000003u;
    static constexpr std::uint32_t kAffinityInfo32  = 0x84000004u;
    static constexpr std::uint32_t kMigrateInfoType = 0x84000006u;
    static constexpr std::uint32_t kSystemOff       = 0x84000008u;
    static constexpr std::uint32_t kSystemReset     = 0x84000009u;
    static constexpr std::uint32_t kPsciFeatures    = 0x8400000Au;
    static constexpr std::uint32_t kCpuSuspend64   = 0xC4000001u;
    static constexpr std::uint32_t kCpuOn64        = 0xC4000003u;
    static constexpr std::uint32_t kAffinityInfo64 = 0xC4000004u;

    // PSCI Return Codes (ARM DEN0022 §5.2.2)
    static constexpr std::int64_t kSuccess         =  0;
    static constexpr std::int64_t kNotSupported    = -1;
    static constexpr std::int64_t kInvalidParams   = -2;
    static constexpr std::int64_t kDenied          = -3;
    static constexpr std::int64_t kAlreadyOn       = -4;
    static constexpr std::int64_t kOnPending       = -5;
    static constexpr std::int64_t kInternalFailure = -6;

    // PSCI version advertised: v1.1 (major=1, minor=1)
    static constexpr std::uint32_t kPsciVersion = 0x00010001u;

    explicit PSCIService(SMPCluster& cluster);
    ~PSCIService() = default;
    PSCIService(const PSCIService&) = delete;
    PSCIService& operator=(const PSCIService&) = delete;

    // Try to handle an SMC as a PSCI call.
    // Returns true if the SMC was handled (X0 updated, PC advanced by 4).
    // Returns false if not a PSCI function ID (caller should proceed to EL3).
    bool handle_smc(CPU& cpu, std::size_t core_id);

    // Query the current power state of a core.
    CorePowerState get_core_state(std::size_t core_id) const noexcept;

    // Set the power state of a core directly (used during startup).
    void set_core_state(std::size_t core_id, CorePowerState state) noexcept;

    // Wake up a suspended core explicitly or via interrupt notification.
    void notify_core_wakeup(std::size_t core_id);
    void resume_core_from_suspend(std::size_t core_id);

    // System power requests (set by SYSTEM_OFF / SYSTEM_RESET).
    bool system_off_requested() const noexcept {
        return system_off_.load(std::memory_order_acquire);
    }
    bool system_reset_requested() const noexcept {
        return system_reset_.load(std::memory_order_acquire);
    }
    void reset_system_flags() noexcept {
        system_off_.store(false, std::memory_order_release);
        system_reset_.store(false, std::memory_order_release);
    }

private:
    SMPCluster& cluster_;

    // Per-core power states, indexed by core_id.
    std::vector<std::unique_ptr<std::atomic<CorePowerState>>> core_states_;
    std::vector<std::uint64_t> suspend_entry_;
    std::vector<std::uint64_t> suspend_context_;

    std::atomic<bool> system_off_{false};
    std::atomic<bool> system_reset_{false};

    // Convert MPIDR-style affinity to a core index.
    // We use the simple linear model: core_id = Aff0 (bits [7:0]).
    // Returns core_count() if the affinity is out of range.
    std::size_t affinity_to_core_id(std::uint64_t affinity) const noexcept;

    // PSCI function implementations; return PSCI return code.
    std::int64_t do_version() const noexcept;
    std::int64_t do_cpu_on(std::uint64_t target_affinity,
                           std::uint64_t entry_pc,
                           std::uint64_t context_id,
                           std::size_t caller_core);
    std::int64_t do_cpu_off(std::size_t caller_core);
    std::int64_t do_cpu_suspend(std::uint32_t power_state,
                                std::uint64_t entry_pc,
                                std::uint64_t context_id,
                                CPU& cpu,
                                std::size_t core_id);
    std::int64_t do_affinity_info(std::uint64_t target_affinity,
                                  std::uint32_t affinity_level) const noexcept;
    std::int64_t do_migrate_info_type() const noexcept;
    std::int64_t do_features(std::uint32_t feature_id) const noexcept;
    void do_system_off();
    void do_system_reset();
};

} // namespace winios
