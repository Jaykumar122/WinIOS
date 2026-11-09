#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/gicv3.h"
#include "runtime/linux_boot_session.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// LinuxBootCpuSnapshot: Architectural CPU register state
// -----------------------------------------------------------------------------
struct LinuxBootCpuSnapshot {
    std::uint16_t core_id{0};
    bool active{false};
    bool halted{false};
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint8_t nzcv{0};
    std::uint8_t current_el{1};
    std::uint8_t daif{0xF};

    std::uint64_t sctlr_el1{0};
    std::uint64_t cpacr_el1{0};
    std::uint64_t vbar_el1{0};
    std::uint64_t elr_el1{0};
    std::uint32_t spsr_el1{0};
    std::uint32_t esr_el1{0};
    std::uint64_t tpidr_el0{0};

    std::array<std::uint64_t, 31> gpr{};
};

// -----------------------------------------------------------------------------
// LinuxBootPlatformSnapshot: Full virtual platform state checkpoint
// -----------------------------------------------------------------------------
struct LinuxBootPlatformSnapshot {
    static constexpr std::uint32_t kMagic = 0x4C425353u; // "LBSS"
    static constexpr std::uint32_t kVersion = 1;

    std::uint32_t magic{kMagic};
    std::uint32_t version{kVersion};
    std::uint64_t total_instructions{0};
    std::uint64_t physical_counter{0};
    std::uint64_t platform_hash{0};

    LinuxSessionStatus status{LinuxSessionStatus::Uninitialized};

    std::vector<LinuxBootCpuSnapshot> cpus;
    std::vector<std::uint8_t> ram_bytes;

    std::vector<std::uint8_t> uart_state;
    std::vector<std::uint8_t> dma_state;

    GICv3::DistributorState gic_distributor;
    std::vector<GICv3::RedistributorState> gic_redistributors;

    std::string milestone_report;
};

// -----------------------------------------------------------------------------
// LinuxBootSnapshotManager: Capture, restoration, and replay verification
// -----------------------------------------------------------------------------
class LinuxBootSnapshotManager {
public:
    static LinuxBootPlatformSnapshot capture(const LinuxBootSession& session);
    static bool restore(LinuxBootSession& target_session,
                        const LinuxBootPlatformSnapshot& snapshot,
                        std::string* out_error = nullptr);

    static std::vector<std::uint8_t> serialize(const LinuxBootPlatformSnapshot& snapshot);
    static LinuxBootPlatformSnapshot deserialize(const void* data,
                                                std::size_t size,
                                                std::string* out_error = nullptr);

    static bool verify_deterministic_replay(LinuxBootSession& reference_session,
                                            LinuxBootSession& restored_session,
                                            std::size_t steps,
                                            std::string* out_divergence = nullptr);
};

} // namespace winios::runtime
