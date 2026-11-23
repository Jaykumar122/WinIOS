#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "memory/canonical_physical_memory_map.h"
#include "cpu/deterministic_virtual_timer.h"

namespace winios::memory {

enum class MutationType {
    None = 0,
    X4Clobbered = 1,
    MissingX30Save = 2,
    IncorrectInterruptedSP = 3,
    IncorrectEL0EL1Origin = 4,
    MissingThreadStackGuard = 5,
    MissingInterruptStackGuard = 6,
    OverlappingStackInterval = 7,
    CrossCoreStackOwnership = 8,
    AllocatorIncludesReservedFrame = 9,
    IncorrectSavedStateOffset = 10,
    MissingELRRestore = 11,
    MissingSPSRRestore = 12,
    IncorrectEretTeardown = 13,
    IncorrectTimerCatchup = 14,
    LostMissedTickAccounting = 15,
    IncorrectAic2Coalescing = 16,
    InvalidNestedExceptionFrame = 17,
    IncorrectGuardPermissions = 18
};

const char* mutation_type_to_string(MutationType type) noexcept;

struct AuditorCheckResult {
    bool passed{true};
    std::vector<std::string> errors;
    std::vector<std::string> detected_mutations;
};

class IndependentStackAndExceptionAuditor {
public:
    static constexpr std::size_t kNumCores = 6;
    static constexpr std::uint64_t kStackSize16K = 16384ULL;
    static constexpr std::uint64_t kGuardSize16K = 16384ULL;
    static constexpr std::size_t kSavedState64Size = 288;

    IndependentStackAndExceptionAuditor() = default;

    // Stage 262.19: Complete architectural audit of memory, stacks, guards, and allocator
    [[nodiscard]] AuditorCheckResult audit_canonical_memory_and_stacks(const CanonicalPhysicalMemoryMap& map) const;

    // Stage 262.19: Audit exception entry and restore trampoline binaries
    [[nodiscard]] AuditorCheckResult audit_exception_trampolines(const std::uint32_t* save_code,
                                                                 std::size_t save_count,
                                                                 const std::uint32_t* restore_code,
                                                                 std::size_t restore_count,
                                                                 bool is_el0_origin) const;

    // Stage 262.19: Audit timer periodic catchup and coalescing invariants
    [[nodiscard]] AuditorCheckResult audit_timer_catchup(const cpu::DeterministicVirtualTimer& timer,
                                                         std::size_t core_id,
                                                         std::uint64_t previous_deadline,
                                                         std::uint64_t period_ticks,
                                                         std::uint64_t current_counter,
                                                         std::uint64_t next_deadline,
                                                         std::uint64_t reported_missed_ticks) const;

    // Stage 262.20: Verify detection of deliberate architectural mutations (1..18)
    [[nodiscard]] bool test_mutation_detection(MutationType mutation, std::string* out_detection_msg) const;

    // Stage 263.14: Audit independent removal and diagnostic for any of the 12 guards
    [[nodiscard]] bool audit_single_guard_removal(std::size_t core_id,
                                                  bool is_thread_guard,
                                                  std::string* out_error) const;
};

} // namespace winios::memory
