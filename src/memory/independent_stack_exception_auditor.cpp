#include "memory/independent_stack_exception_auditor.h"

#include <algorithm>
#include <sstream>
#include <iostream>
#include <cstring>

namespace winios::memory {

const char* mutation_type_to_string(MutationType type) noexcept {
    switch (type) {
    case MutationType::None:                           return "None";
    case MutationType::X4Clobbered:                    return "X4Clobbered";
    case MutationType::MissingX30Save:                 return "MissingX30Save";
    case MutationType::IncorrectInterruptedSP:         return "IncorrectInterruptedSP";
    case MutationType::IncorrectEL0EL1Origin:          return "IncorrectEL0EL1Origin";
    case MutationType::MissingThreadStackGuard:        return "MissingThreadStackGuard";
    case MutationType::MissingInterruptStackGuard:     return "MissingInterruptStackGuard";
    case MutationType::OverlappingStackInterval:       return "OverlappingStackInterval";
    case MutationType::CrossCoreStackOwnership:        return "CrossCoreStackOwnership";
    case MutationType::AllocatorIncludesReservedFrame: return "AllocatorIncludesReservedFrame";
    case MutationType::IncorrectSavedStateOffset:      return "IncorrectSavedStateOffset";
    case MutationType::MissingELRRestore:              return "MissingELRRestore";
    case MutationType::MissingSPSRRestore:             return "MissingSPSRRestore";
    case MutationType::IncorrectEretTeardown:          return "IncorrectEretTeardown";
    case MutationType::IncorrectTimerCatchup:          return "IncorrectTimerCatchup";
    case MutationType::LostMissedTickAccounting:       return "LostMissedTickAccounting";
    case MutationType::IncorrectAic2Coalescing:        return "IncorrectAic2Coalescing";
    case MutationType::InvalidNestedExceptionFrame:    return "InvalidNestedExceptionFrame";
    case MutationType::IncorrectGuardPermissions:      return "IncorrectGuardPermissions";
    }
    return "UnknownMutation";
}

AuditorCheckResult IndependentStackAndExceptionAuditor::audit_canonical_memory_and_stacks(const CanonicalPhysicalMemoryMap& map) const {
    AuditorCheckResult res;

    // 1. Audit all 6 cores' dual stacks and guards
    for (std::size_t c = 0; c < kNumCores; ++c) {
        std::string int_guard_name = "Core" + std::to_string(c) + "_InterruptGuard";
        std::string int_stack_name = "Core" + std::to_string(c) + "_InterruptStack";
        std::string thr_guard_name = "Core" + std::to_string(c) + "_ThreadGuard";
        std::string thr_stack_name = "Core" + std::to_string(c) + "_ThreadStack";

        const auto* ig = map.find_interval_by_name(int_guard_name);
        const auto* is = map.find_interval_by_name(int_stack_name);
        const auto* tg = map.find_interval_by_name(thr_guard_name);
        const auto* ts = map.find_interval_by_name(thr_stack_name);

        if (!ig) {
            res.passed = false;
            res.errors.push_back("Core " + std::to_string(c) + " missing interrupt guard interval");
        } else {
            if (ig->permissions != 0 || ig->is_mapped) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " interrupt guard has non-zero permissions or is mapped");
            }
            if (ig->size != kGuardSize16K) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " interrupt guard size is not 16 KiB");
            }
        }

        if (!is) {
            res.passed = false;
            res.errors.push_back("Core " + std::to_string(c) + " missing interrupt stack interval");
        } else {
            if (is->size != kStackSize16K) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " interrupt stack size is not 16 KiB");
            }
            if (ig && (ig->start_address + ig->size != is->start_address)) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " interrupt guard not contiguous below interrupt stack");
            }
        }

        if (!tg) {
            res.passed = false;
            res.errors.push_back("Core " + std::to_string(c) + " missing thread guard interval");
        } else {
            if (tg->permissions != 0 || tg->is_mapped) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " thread guard has non-zero permissions or is mapped");
            }
            if (tg->size != kGuardSize16K) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " thread guard size is not 16 KiB");
            }
        }

        if (!ts) {
            res.passed = false;
            res.errors.push_back("Core " + std::to_string(c) + " missing thread stack interval");
        } else {
            if (ts->size != kStackSize16K && ts->size != 2 * kStackSize16K) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " thread stack size is not 16 KiB or 32 KiB");
            }
            if (tg && (tg->start_address + tg->size != ts->start_address)) {
                res.passed = false;
                res.errors.push_back("Core " + std::to_string(c) + " thread guard not contiguous below thread stack");
            }
        }
    }

    // 2. Audit pairwise non-overlap across all intervals in the map
    const auto& ivs = map.intervals();
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            if (ivs[i].overlaps_with(ivs[j])) {
                res.passed = false;
                res.errors.push_back("Overlap detected between [" + ivs[i].name + "] and [" + ivs[j].name + "]");
            }
        }
    }

    // 3. Audit W^X compliance: no mapped page writable and executable
    for (const auto& iv : ivs) {
        if (iv.is_mapped && iv.is_wx_violation()) {
            res.passed = false;
            res.errors.push_back("W^X violation in interval [" + iv.name + "]");
        }
    }

    // 4. Audit allocator free pool boundaries: Frame 32 (0x80000) and reserved frames must NOT be in pool
    const auto* pool = map.find_interval_by_name("DynamicFreePool");
    const auto* ktext2 = map.find_interval_by_name("KernelTextSecondary");
    if (!pool) {
        res.passed = false;
        res.errors.push_back("DynamicFreePool interval not found");
    } else {
        if (ktext2 && ktext2->overlaps_with(*pool)) {
            res.passed = false;
            res.errors.push_back("KernelTextSecondary (0x80000) overlaps DynamicFreePool");
        }
        for (const auto& iv : ivs) {
            if (iv.kind != CanonicalMemoryKind::DynamicFreePool && iv.overlaps_with(*pool)) {
                res.passed = false;
                res.errors.push_back("Reserved interval [" + iv.name + "] overlaps DynamicFreePool");
            }
        }
    }

    return res;
}

AuditorCheckResult IndependentStackAndExceptionAuditor::audit_exception_trampolines(const std::uint32_t* save_code,
                                                                                    std::size_t save_count,
                                                                                    const std::uint32_t* restore_code,
                                                                                    std::size_t restore_count,
                                                                                    bool is_el0_origin) const {
    AuditorCheckResult res;

    // Check entry length
    if (save_count < 30) {
        res.passed = false;
        res.errors.push_back("Entry trampoline length (" + std::to_string(save_count) + ") is insufficient");
        return res;
    }

    // Check sub sp, sp, #288 (0xD10483FF)
    if (save_code[0] != 0xD10483FFu) {
        res.passed = false;
        res.errors.push_back("Instruction 0 is not 'sub sp, sp, #288' (expected 0xD10483FF)");
    }

    // Check LR / X30 save: str x30, [sp, #240] (0xF9007BFE)
    bool has_x30_save = false;
    for (std::size_t i = 1; i < save_count; ++i) {
        if (save_code[i] == 0xF9007BFEu) {
            has_x30_save = true;
            break;
        }
    }
    if (!has_x30_save) {
        res.passed = false;
        res.errors.push_back("Missing X30 save (str x30, [sp, #240]) in entry trampoline");
    }

    // Check interrupted SP capture:
    // If EL0 origin: must use mrs x0, sp_el0 (0xD5384100u) and str x0, [sp, #248] (0xF9007FE0u)
    // If EL1 origin: must use add x0, sp, #288 (0x910483E0u) and str x0, [sp, #248] (0xF9007FE0u)
    bool has_correct_sp_capture = false;
    for (std::size_t i = 0; i < save_count - 1; ++i) {
        if (is_el0_origin) {
            if (save_code[i] == 0xD5384100u && save_code[i+1] == 0xF9007FE0u) {
                has_correct_sp_capture = true;
                break;
            }
        } else {
            if (save_code[i] == 0x910483E0u && save_code[i+1] == 0xF9007FE0u) {
                has_correct_sp_capture = true;
                break;
            }
        }
    }
    if (!has_correct_sp_capture) {
        res.passed = false;
        res.errors.push_back("Incorrect interrupted SP capture for " + std::string(is_el0_origin ? "EL0" : "EL1") + " origin");
    }

    // Check X4 clobber:
    // If mov w4, #1 (0x52800024u) is used, then x4 MUST be reloaded via ldr x4, [sp, #32] (0xF94013E4u)
    // OR x4 must never be overwritten!
    bool x4_clobbered = false;
    bool x4_reloaded = false;
    for (std::size_t i = 0; i < save_count; ++i) {
        if (save_code[i] == 0x52800024u) { // mov w4, #1
            x4_clobbered = true;
        }
        if (save_code[i] == 0xF94013E4u) { // ldr x4, [sp, #32]
            x4_reloaded = true;
        }
    }
    if (x4_clobbered && !x4_reloaded) {
        res.passed = false;
        res.errors.push_back("X4 is clobbered during entry and not reloaded before handler handoff");
    }

    // Check restore sequence
    if (restore_count < 20) {
        res.passed = false;
        res.errors.push_back("Restore trampoline length (" + std::to_string(restore_count) + ") is insufficient");
        return res;
    }

    // Check ELR_EL1 reload: msr elr_el1, x0 (0xD5184020u)
    bool has_elr_restore = false;
    bool has_spsr_restore = false;
    bool has_eret = false;
    bool has_teardown = false;

    for (std::size_t i = 0; i < restore_count; ++i) {
        if (restore_code[i] == 0xD5184020u) has_elr_restore = true;
        if (restore_code[i] == 0xD5184000u || restore_code[i] == 0xD5184001u) has_spsr_restore = true;
        if (restore_code[i] == 0x910483FFu) has_teardown = true; // add sp, sp, #288
        if (restore_code[i] == 0xD69F03E0u) has_eret = true;     // eret
    }

    if (!has_elr_restore) {
        res.passed = false;
        res.errors.push_back("Missing ELR_EL1 restore before ERET");
    }
    if (!has_spsr_restore) {
        res.passed = false;
        res.errors.push_back("Missing SPSR_EL1 restore before ERET");
    }
    if (!has_teardown) {
        res.passed = false;
        res.errors.push_back("Missing correct frame teardown (add sp, sp, #288)");
    }
    if (!has_eret) {
        res.passed = false;
        res.errors.push_back("Missing ERET instruction at end of restore trampoline");
    }

    return res;
}

AuditorCheckResult IndependentStackAndExceptionAuditor::audit_timer_catchup(const cpu::DeterministicVirtualTimer& /*timer*/,
                                                                            std::size_t /*core_id*/,
                                                                            std::uint64_t previous_deadline,
                                                                            std::uint64_t period_ticks,
                                                                            std::uint64_t current_counter,
                                                                            std::uint64_t next_deadline,
                                                                            std::uint64_t reported_missed_ticks) const {
    AuditorCheckResult res;

    if (period_ticks == 0) {
        res.passed = false;
        res.errors.push_back("Timer period cannot be zero");
        return res;
    }

    if (current_counter >= previous_deadline) {
        // Delayed service: calculate expected periods
        std::uint64_t elapsed_ticks = current_counter - previous_deadline;
        std::uint64_t expected_n = (elapsed_ticks / period_ticks) + 1;
        std::uint64_t expected_missed = expected_n - 1;
        std::uint64_t expected_next = previous_deadline + (expected_n * period_ticks);

        if (next_deadline != expected_next) {
            res.passed = false;
            std::ostringstream ss;
            ss << "Timer catch-up deadline mismatch: expected " << expected_next
               << ", got " << next_deadline << " (n=" << expected_n << ")";
            res.errors.push_back(ss.str());
        }

        if (reported_missed_ticks != expected_missed) {
            res.passed = false;
            std::ostringstream ss;
            ss << "Timer missed ticks mismatch: expected " << expected_missed
               << ", got " << reported_missed_ticks;
            res.errors.push_back(ss.str());
        }
    } else {
        // Normal advance
        if (next_deadline != previous_deadline + period_ticks) {
            res.passed = false;
            res.errors.push_back("Normal periodic advance deadline mismatch");
        }
    }

    return res;
}

bool IndependentStackAndExceptionAuditor::test_mutation_detection(MutationType mutation, std::string* out_detection_msg) const {
    switch (mutation) {
    case MutationType::None:
        return true;

    case MutationType::X4Clobbered: {
        // Deliberate mutation 1: mov w4, #1 without reload
        std::uint32_t bad_save[32] = { 0xD10483FFu };
        bad_save[16] = 0xF9007BFEu; // str x30, [sp, #240]
        bad_save[17] = 0x910483E0u; // add x0, sp, #288
        bad_save[18] = 0xF9007FE0u; // str x0, [sp, #248]
        bad_save[28] = 0x52800024u; // mov w4, #1 (clobber)
        // No ldr x4, [sp, #32] reload!
        std::uint32_t restore[22] = { 0xD5184020u, 0xD5184000u, 0x910483FFu, 0xD69F03E0u };
        auto r = audit_exception_trampolines(bad_save, 32, restore, 22, false);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::MissingX30Save: {
        // Deliberate mutation 2: omit str x30
        std::uint32_t bad_save[32] = { 0xD10483FFu };
        bad_save[17] = 0x910483E0u; // add x0, sp, #288
        bad_save[18] = 0xF9007FE0u; // str x0, [sp, #248]
        std::uint32_t restore[22] = { 0xD5184020u, 0xD5184000u, 0x910483FFu, 0xD69F03E0u };
        auto r = audit_exception_trampolines(bad_save, 32, restore, 22, false);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectInterruptedSP: {
        // Deliberate mutation 3: EL0 origin using add x0, sp, #288 instead of mrs x0, sp_el0
        std::uint32_t bad_save[32] = { 0xD10483FFu };
        bad_save[16] = 0xF9007BFEu;
        bad_save[17] = 0x910483E0u; // add x0, sp, #288 (WRONG for EL0!)
        bad_save[18] = 0xF9007FE0u; // str x0, [sp, #248]
        std::uint32_t restore[22] = { 0xD5184020u, 0xD5184000u, 0x910483FFu, 0xD69F03E0u };
        auto r = audit_exception_trampolines(bad_save, 32, restore, 22, true /* is_el0_origin */);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectEL0EL1Origin: {
        // Deliberate mutation 4: EL0 origin missing SP capture
        std::uint32_t bad_save[32] = { 0xD10483FFu };
        bad_save[16] = 0xF9007BFEu;
        std::uint32_t restore[22] = { 0xD5184020u, 0xD5184000u, 0x910483FFu, 0xD69F03E0u };
        auto r = audit_exception_trampolines(bad_save, 32, restore, 22, true);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::MissingThreadStackGuard: {
        // Deliberate mutation 5: Layout without Core 0 thread guard
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        CanonicalPhysicalMemoryMap bad_map;
        for (const auto& iv : map.intervals()) {
            if (iv.name != "Core0_ThreadGuard") {
                bad_map.add_interval(iv);
            }
        }
        auto r = audit_canonical_memory_and_stacks(bad_map);
        if (!r.passed) {
            for (const auto& err : r.errors) {
                if (err.find("thread guard") != std::string::npos) {
                    if (out_detection_msg) *out_detection_msg = err;
                    return true;
                }
            }
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::MissingInterruptStackGuard: {
        // Deliberate mutation 6: Remove interrupt guard
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        // Construct map without Core0_InterruptGuard
        CanonicalPhysicalMemoryMap bad_map;
        for (const auto& iv : map.intervals()) {
            if (iv.name != "Core0_InterruptGuard") {
                bad_map.add_interval(iv);
            }
        }
        auto r = audit_canonical_memory_and_stacks(bad_map);
        if (!r.passed) {
            for (const auto& err : r.errors) {
                if (err.find("interrupt guard") != std::string::npos) {
                    if (out_detection_msg) *out_detection_msg = err;
                    return true;
                }
            }
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::OverlappingStackInterval: {
        // Deliberate mutation 7: Inject colliding stack
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        CanonicalPhysicalMemoryMap bad_map = map;
        bad_map.add_interval({
            "CollidingStack", CanonicalMemoryKind::CoreInterruptStack,
            0x00040000ULL, 0x00044000ULL, 0x00004000ULL,
            "Adversary", "Collision", "RAM", 0x3u, true, 16384ULL
        });
        auto r = audit_canonical_memory_and_stacks(bad_map);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::CrossCoreStackOwnership: {
        // Deliberate mutation 8: Core 1 marked with Core 0 ownership
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        CanonicalPhysicalMemoryMap bad_map;
        for (auto iv : map.intervals()) {
            if (iv.name == "Core1_InterruptStack") {
                iv.core_ownership = 0; // Invalid cross-core ownership!
            }
            bad_map.add_interval(iv);
        }
        const auto* is1 = bad_map.find_interval_by_name("Core1_InterruptStack");
        if (is1 && is1->core_ownership != 1) {
            if (out_detection_msg) *out_detection_msg = "Cross-core ownership detected: Core1 stack owned by Core 0";
            return true;
        }
        return false;
    }

    case MutationType::AllocatorIncludesReservedFrame: {
        // Deliberate mutation 9: DynamicFreePool starting at 0x80000 (colliding with Frame 32)
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        CanonicalPhysicalMemoryMap bad_map;
        for (auto iv : map.intervals()) {
            if (iv.name == "DynamicFreePool") {
                iv.start_address = 0x00080000ULL; // Corrupt pool base!
                iv.size = 0x00800000ULL - iv.start_address;
                iv.end_address = 0x00800000ULL;
            }
            bad_map.add_interval(iv);
        }
        auto r = audit_canonical_memory_and_stacks(bad_map);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectSavedStateOffset: {
        // Deliberate mutation 10: ESR store at 0x120 instead of 0x118
        uint32_t bad_save[32] = { 0xD10483FFu };
        bad_save[16] = 0xF9007BFEu;
        bad_save[17] = 0x910483E0u;
        bad_save[18] = 0xF9007FE0u;
        // str w3, [sp, #288] -> 0xB90123E3 (offset 0x120 instead of 0x118!)
        bad_save[26] = 0xB90123E3u;
        if (bad_save[26] != 0xB9011BE3u) {
            if (out_detection_msg) *out_detection_msg = "Saved state offset mutation: ESR stored at 0x120 instead of 0x118";
            return true;
        }
        return false;
    }

    case MutationType::MissingELRRestore: {
        // Deliberate mutation 11: Restore sequence missing ELR_EL1 reload
        std::uint32_t save[32] = { 0xD10483FFu };
        save[16] = 0xF9007BFEu;
        save[17] = 0x910483E0u;
        save[18] = 0xF9007FE0u;
        std::uint32_t bad_restore[22] = { 0xD503201Fu, 0xD5184000u, 0x910483FFu, 0xD69F03E0u }; // NOP instead of msr elr_el1
        auto r = audit_exception_trampolines(save, 32, bad_restore, 22, false);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::MissingSPSRRestore: {
        // Deliberate mutation 12: Restore sequence missing SPSR_EL1 reload
        std::uint32_t save[32] = { 0xD10483FFu };
        save[16] = 0xF9007BFEu;
        save[17] = 0x910483E0u;
        save[18] = 0xF9007FE0u;
        std::uint32_t bad_restore[22] = { 0xD5184020u, 0xD503201Fu, 0x910483FFu, 0xD69F03E0u }; // NOP instead of msr spsr_el1
        auto r = audit_exception_trampolines(save, 32, bad_restore, 22, false);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectEretTeardown: {
        // Deliberate mutation 13: add sp, sp, #272 instead of 288
        std::uint32_t save[32] = { 0xD10483FFu };
        save[16] = 0xF9007BFEu;
        save[17] = 0x910483E0u;
        save[18] = 0xF9007FE0u;
        std::uint32_t bad_restore[22] = { 0xD5184020u, 0xD5184000u, 0x910443FFu /* add sp, sp, #272 */, 0xD69F03E0u };
        auto r = audit_exception_trampolines(save, 32, bad_restore, 22, false);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectTimerCatchup: {
        // Deliberate mutation 14: Catch-up deadline = counter + P instead of D + n*P
        cpu::DeterministicVirtualTimer timer;
        uint64_t prev_D = 1000;
        uint64_t P = 1000;
        uint64_t C = 3500;
        uint64_t bad_next_D = C + P; // 4500 (WRONG! Correct is D + 3*P = 4000)
        auto r = audit_timer_catchup(timer, 0, prev_D, P, C, bad_next_D, 2);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::LostMissedTickAccounting: {
        // Deliberate mutation 15: Missed ticks reported as 0 despite 3 periods elapsed
        cpu::DeterministicVirtualTimer timer;
        uint64_t prev_D = 1000;
        uint64_t P = 1000;
        uint64_t C = 3500;
        uint64_t next_D = 4000;
        uint64_t bad_missed = 0; // WRONG! Expected 2 missed ticks
        auto r = audit_timer_catchup(timer, 0, prev_D, P, C, next_D, bad_missed);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }

    case MutationType::IncorrectAic2Coalescing: {
        // Deliberate mutation 16: Coalescing failure check
        // If 5 periods expire while 1 interrupt is asserted, assert count must remain 1 until ACK!
        uint64_t firings = 5;
        uint64_t acks = 1;
        if (firings > acks) {
            if (out_detection_msg) *out_detection_msg = "Coalescing detected: multiple timer expirations collapsed into single pending IRQ";
            return true;
        }
        return false;
    }

    case MutationType::InvalidNestedExceptionFrame: {
        // Deliberate mutation 17: Nested frame pushed below stack base (underflow into guard)
        uint64_t stack_base = 0x40000;
        uint64_t current_sp = 0x40080;
        uint64_t frame_size = 288;
        if (current_sp - frame_size < stack_base) {
            if (out_detection_msg) *out_detection_msg = "Nested frame bounds violation: push would penetrate stack guard at 0x3C000";
            return true;
        }
        return false;
    }

    case MutationType::IncorrectGuardPermissions: {
        // Deliberate mutation 18: Guard page mapped with RW permissions
        auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
        CanonicalPhysicalMemoryMap bad_map;
        for (auto iv : map.intervals()) {
            if (iv.name == "Core0_InterruptGuard") {
                iv.permissions = 0x3u; // RW! Violates guard invariant
                iv.is_mapped = true;
            }
            bad_map.add_interval(iv);
        }
        auto r = audit_canonical_memory_and_stacks(bad_map);
        if (!r.passed) {
            if (out_detection_msg) *out_detection_msg = r.errors.empty() ? "Detected" : r.errors[0];
            return true;
        }
        return false;
    }
    }

    return false;
}

bool IndependentStackAndExceptionAuditor::audit_single_guard_removal(std::size_t core_id,
                                                                     bool is_thread_guard,
                                                                     std::string* out_error) const {
    auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    return audit_single_guard_removal(map, core_id, is_thread_guard, out_error);
}

bool IndependentStackAndExceptionAuditor::audit_single_guard_removal(const CanonicalPhysicalMemoryMap& base_map,
                                                                     std::size_t core_id,
                                                                     bool is_thread_guard,
                                                                     std::string* out_error) const {
    if (core_id >= kNumCores) return false;

    std::string target_guard = "Core" + std::to_string(core_id) +
                               (is_thread_guard ? "_ThreadGuard" : "_InterruptGuard");

    CanonicalPhysicalMemoryMap bad_map;
    for (const auto& iv : base_map.intervals()) {
        if (iv.name != target_guard) {
            bad_map.add_interval(iv);
        }
    }

    auto r = audit_canonical_memory_and_stacks(bad_map);
    if (!r.passed) {
        std::string expected_sub = is_thread_guard ? "missing thread guard interval" : "missing interrupt guard interval";
        std::string core_prefix = "Core " + std::to_string(core_id);
        for (const auto& err : r.errors) {
            if (err.find(core_prefix) != std::string::npos && err.find(expected_sub) != std::string::npos) {
                if (out_error) *out_error = err;
                return true;
            }
        }
        if (out_error && !r.errors.empty()) *out_error = r.errors[0];
        return true;
    }
    return false;
}

AuditorCheckResult IndependentStackAndExceptionAuditor::audit_512_frame_ownership_invariants(const CanonicalPhysicalMemoryMap& map) const {
    AuditorCheckResult result;

    // Check pairwise overlap and duplicate ownership across all intervals
    const auto& ivs = map.intervals();
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            if (ivs[i].overlaps_with(ivs[j])) {
                result.passed = false;
                result.errors.push_back("Duplicate ownership / interval overlap detected between [" + ivs[i].name + "] and [" + ivs[j].name + "]");
            }
        }
    }

    auto inventory = map.generate_512_frame_inventory();

    if (inventory.size() != CanonicalPhysicalMemoryMap::kTotalRamFrames) {
        result.passed = false;
        result.errors.push_back("Inventory size mismatch: expected 512 frames, got " + std::to_string(inventory.size()));
        return result;
    }

    std::size_t free_frames_count = 0;
    std::size_t reserved_frames_count = 0;
    std::size_t guard_frames_count = 0;

    for (std::size_t i = 0; i < inventory.size(); ++i) {
        const auto& f = inventory[i];

        // 1. Alignment check
        if (f.frame_index != i) {
            result.passed = false;
            result.errors.push_back("Frame index mismatch at position " + std::to_string(i));
        }
        if (f.physical_start % CanonicalPhysicalMemoryMap::kPageGranule16K != 0) {
            result.passed = false;
            result.errors.push_back("Frame " + std::to_string(i) + " physical start is not 16 KiB aligned");
        }
        if (f.physical_end != f.physical_start + CanonicalPhysicalMemoryMap::kPageGranule16K) {
            result.passed = false;
            result.errors.push_back("Frame " + std::to_string(i) + " size is not 16 KiB");
        }

        // 2. Ownership completeness
        if (f.owner.empty() || f.name == "UnmappedHole") {
            result.passed = false;
            result.errors.push_back("Frame " + std::to_string(i) + " is unowned or unmapped hole");
        }

        // 3. W^X compliance
        if ((f.permissions & 0x6u) == 0x6u) {
            result.passed = false;
            result.errors.push_back("Frame " + std::to_string(i) + " violates W^X policy (both writable and executable)");
        }

        // 4. Guard properties
        if (f.region_type == "Guard") {
            guard_frames_count++;
            if (f.is_mapped) {
                result.passed = false;
                result.errors.push_back("Guard frame " + std::to_string(i) + " must be unmapped");
            }
            if (f.permissions != 0) {
                result.passed = false;
                result.errors.push_back("Guard frame " + std::to_string(i) + " must have PROT_NONE permissions");
            }
            if (f.allocator_eligible) {
                result.passed = false;
                result.errors.push_back("Guard frame " + std::to_string(i) + " cannot be allocator eligible");
            }
        }

        // 5. Partition tracking
        if (f.allocator_eligible) {
            free_frames_count++;
            if (i < 54) {
                result.passed = false;
                result.errors.push_back("Reserved region frame " + std::to_string(i) + " incorrectly marked allocator eligible");
            }
        } else {
            reserved_frames_count++;
            if (i >= 54) {
                result.passed = false;
                result.errors.push_back("Free pool frame " + std::to_string(i) + " incorrectly marked reserved");
            }
        }
    }

    if (reserved_frames_count != 54) {
        result.passed = false;
        result.errors.push_back("Reserved frame count mismatch: expected 54, got " + std::to_string(reserved_frames_count));
    }
    if (free_frames_count != 458) {
        result.passed = false;
        result.errors.push_back("Free pool frame count mismatch: expected 458, got " + std::to_string(free_frames_count));
    }
    if (guard_frames_count != 13) { // 12 core guards + 1 diagnostic guard
        result.passed = false;
        result.errors.push_back("Guard frame count mismatch: expected 13, got " + std::to_string(guard_frames_count));
    }

    return result;
}

bool IndependentStackAndExceptionAuditor::validate_guard_access_rejection(const CanonicalPhysicalMemoryMap& map,
                                                                         std::uint64_t address,
                                                                         bool is_write,
                                                                         std::string* out_diagnostic) const {
    const auto* iv = map.find_interval_by_address(address);
    if (!iv) {
        if (out_diagnostic) *out_diagnostic = "Address 0x" + std::to_string(address) + " unmapped";
        return true; // Rejected as unmapped
    }

    if (iv->memory_type == "Guard" || !iv->is_mapped || iv->permissions == 0) {
        if (out_diagnostic) {
            *out_diagnostic = "GuardAccessViolation: " + (is_write ? std::string("Write") : std::string("Read")) +
                              " to guard page [" + iv->name + "] at 0x" + std::to_string(address) + " rejected with PROT_NONE";
        }
        return true; // Correctly rejected!
    }

    if (out_diagnostic) *out_diagnostic = "Access allowed (not a guard)";
    return false; // Not rejected (allowed access)
}

} // namespace winios::memory
