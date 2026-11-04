#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"

#include <cstdint>
#include <string>

namespace winios::replay {

struct ReplayProfile;

// -----------------------------------------------------------------------------
// ReplayComponentHashes: Canonical Component-Level Architectural State Hashes
// -----------------------------------------------------------------------------
struct ReplayComponentHashes {
    std::uint64_t cpu_registers_hash{0};
    std::uint64_t simd_registers_hash{0};
    std::uint64_t system_registers_hash{0};
    std::uint64_t memory_hash{0};
    std::uint64_t timer_hash{0};
    std::uint64_t gic_hash{0};
    std::uint64_t combined_hash{0};

    bool operator==(const ReplayComponentHashes& other) const noexcept {
        return cpu_registers_hash == other.cpu_registers_hash &&
               simd_registers_hash == other.simd_registers_hash &&
               system_registers_hash == other.system_registers_hash &&
               memory_hash == other.memory_hash &&
               timer_hash == other.timer_hash &&
               gic_hash == other.gic_hash &&
               combined_hash == other.combined_hash;
    }
    bool operator!=(const ReplayComponentHashes& other) const noexcept {
        return !(*this == other);
    }
};

// -----------------------------------------------------------------------------
// ReplayStateHash: Canonical State Hashing Engine
// -----------------------------------------------------------------------------
class ReplayStateHash {
public:
    // Hashes integer, SIMD, and system registers of a single core
    static void hash_cpu(const CPU& cpu,
                         std::uint64_t& out_regs_hash,
                         std::uint64_t& out_simd_hash,
                         std::uint64_t& out_sysreg_hash) noexcept;

    // Hashes guest RAM
    [[nodiscard]] static std::uint64_t hash_memory(
        const Memory& memory,
        ReplayProfile* profile = nullptr
    ) noexcept;

    // Hashes virtual and physical timers for a core
    [[nodiscard]] static std::uint64_t hash_timers(const VirtualTime& vt, std::uint16_t core_id) noexcept;

    // Hashes GICv3 interrupt controller state for a core
    [[nodiscard]] static std::uint64_t hash_gic(const GICv3& gic, std::uint16_t core_id) noexcept;

    // Computes full component-level and combined state hashes for a machine instance
    [[nodiscard]] static ReplayComponentHashes compute_state_hashes(
        const CPU& cpu,
        const Memory& memory,
        const VirtualTime* vt = nullptr,
        const GICv3* gic = nullptr,
        ReplayProfile* profile = nullptr
    ) noexcept;

    // Compares two component hashes and formats human-readable difference
    [[nodiscard]] static bool compare_and_diff(
        const ReplayComponentHashes& expected,
        const ReplayComponentHashes& actual,
        std::string& out_diff_description
    );
};

} // namespace winios::replay
