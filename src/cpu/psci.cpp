// Phase 48: PSCI virtual service implementation.
// ARM DEN0022 PSCI v1.1 subset.
#include "cpu/cpu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include <stdexcept>

namespace winios {

PSCIService::PSCIService(SMPCluster& cluster)
    : cluster_(cluster)
{
    const std::size_t n = cluster_.core_count();
    core_states_.reserve(n);
    suspend_entry_.resize(n, 0);
    suspend_context_.resize(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        // Core 0 starts running (On); all secondary cores start Off.
        core_states_.push_back(std::make_unique<std::atomic<CorePowerState>>(
            i == 0 ? CorePowerState::On : CorePowerState::Off));
    }
}

bool PSCIService::handle_smc(CPU& cpu, std::size_t core_id) {
    const auto fn_id = static_cast<std::uint32_t>(cpu.read_register(0));
    std::int64_t result = kSuccess;
    bool handled = true;

    switch (fn_id) {
    case kVersion:
        result = do_version();
        break;

    case kCpuOn32: {
        const std::uint64_t target = cpu.read_register(1) & 0xFFFF'FFFFull;
        const std::uint64_t entry  = cpu.read_register(2) & 0xFFFF'FFFFull;
        const std::uint64_t ctx_id = cpu.read_register(3) & 0xFFFF'FFFFull;
        result = do_cpu_on(target, entry, ctx_id, core_id);
        break;
    }
    case kCpuOn64: {
        const std::uint64_t target = cpu.read_register(1);
        const std::uint64_t entry  = cpu.read_register(2);
        const std::uint64_t ctx_id = cpu.read_register(3);
        result = do_cpu_on(target, entry, ctx_id, core_id);
        break;
    }

    case kCpuOff:
        result = do_cpu_off(core_id);
        // CPU_OFF does not return on success; the caller core is now halted.
        // If it failed (e.g. core 0 returned DENIED), it returns to caller.
        cpu.set_program_counter(cpu.get_program_counter() + 4);
        cpu.write_register(0, static_cast<std::uint64_t>(result));
        return true; // handled; caller must not advance PC again

    case kCpuSuspend32: {
        const auto power_state = static_cast<std::uint32_t>(cpu.read_register(1) & 0xFFFF'FFFFull);
        const auto entry       = cpu.read_register(2) & 0xFFFF'FFFFull;
        const auto ctx_id      = cpu.read_register(3) & 0xFFFF'FFFFull;
        result = do_cpu_suspend(power_state, entry, ctx_id, cpu, core_id);
        break;
    }
    case kCpuSuspend64: {
        const auto power_state = static_cast<std::uint32_t>(cpu.read_register(1));
        const auto entry       = cpu.read_register(2);
        const auto ctx_id      = cpu.read_register(3);
        result = do_cpu_suspend(power_state, entry, ctx_id, cpu, core_id);
        break;
    }

    case kAffinityInfo32: {
        const std::uint64_t target = cpu.read_register(1) & 0xFFFF'FFFFull;
        const auto level = static_cast<std::uint32_t>(cpu.read_register(2) & 0xFFFF'FFFFull);
        result = do_affinity_info(target, level);
        break;
    }
    case kAffinityInfo64: {
        const std::uint64_t target = cpu.read_register(1);
        const auto level = static_cast<std::uint32_t>(cpu.read_register(2));
        result = do_affinity_info(target, level);
        break;
    }

    case kMigrateInfoType:
        result = do_migrate_info_type();
        break;

    case kPsciFeatures: {
        const auto query_id = static_cast<std::uint32_t>(cpu.read_register(1));
        result = do_features(query_id);
        break;
    }

    case kSystemOff:
        do_system_off();
        // SYSTEM_OFF halts the virtual system.
        cpu.write_register(0, static_cast<std::uint64_t>(result));
        cpu.set_program_counter(cpu.get_program_counter() + 4);
        return true;

    case kSystemReset:
        do_system_reset();
        // SYSTEM_RESET resets/halts the virtual system.
        cpu.write_register(0, static_cast<std::uint64_t>(result));
        cpu.set_program_counter(cpu.get_program_counter() + 4);
        return true;

    default:
        // Any unrecognized call in the standard PSCI range (0x8400xxxx or 0xC400xxxx)
        // returns NOT_SUPPORTED per PSCI specification. Non-PSCI calls pass through to EL3.
        if ((fn_id >= 0x84000000u && fn_id <= 0x8400FFFFu) ||
            (fn_id >= 0xC4000000u && fn_id <= 0xC400FFFFu)) {
            handled = true;
            result = kNotSupported;
        } else {
            handled = false;
        }
        break;
    }

    if (handled) {
        cpu.write_register(0, static_cast<std::uint64_t>(result));
        cpu.set_program_counter(cpu.get_program_counter() + 4);
    }
    return handled;
}

CorePowerState PSCIService::get_core_state(std::size_t core_id) const noexcept {
    if (core_id >= core_states_.size()) {
        return CorePowerState::Off;
    }
    const CorePowerState state = core_states_[core_id]->load(std::memory_order_acquire);
    // If the core was marked as suspended, check if it has woken up.
    if ((state == CorePowerState::SuspendedRetention || state == CorePowerState::SuspendedPowerdown) &&
        !cluster_.get_core(core_id).is_waiting()) {
        return CorePowerState::On;
    }
    return state;
}

void PSCIService::set_core_state(std::size_t core_id, CorePowerState state) noexcept {
    if (core_id < core_states_.size()) {
        core_states_[core_id]->store(state, std::memory_order_release);
    }
}

void PSCIService::notify_core_wakeup(std::size_t core_id) {
    if (core_id >= core_states_.size()) return;
    const CorePowerState state = core_states_[core_id]->load(std::memory_order_acquire);
    if (state == CorePowerState::SuspendedPowerdown) {
        CPU& core = cluster_.get_core(core_id);
        if (suspend_entry_[core_id] != 0) {
            core.set_program_counter(suspend_entry_[core_id]);
            core.write_register(0, suspend_context_[core_id]);
        }
    }
    if (state == CorePowerState::SuspendedRetention || state == CorePowerState::SuspendedPowerdown) {
        set_core_state(core_id, CorePowerState::On);
    }
}

void PSCIService::resume_core_from_suspend(std::size_t core_id) {
    if (core_id >= core_states_.size()) return;
    notify_core_wakeup(core_id);
    cluster_.get_core(core_id).wake_from_wfi();
}

std::size_t PSCIService::affinity_to_core_id(std::uint64_t affinity) const noexcept {
    // In our cluster, MPIDR_EL1 is (0x80000000ULL | core_id).
    // Aff0 is bits [7:0], Aff1 is bits [15:8], Aff2 is bits [23:16], Aff3 is bits [39:32].
    const std::uint64_t aff = affinity & 0x000000FF00FFFFFFULL;
    // For single-cluster flat core indexing, reject affinities with non-zero Aff1/Aff2/Aff3.
    if ((aff & ~0xFFULL) != 0ULL) {
        return cluster_.core_count(); // out of range
    }
    return static_cast<std::size_t>(aff & 0xFFu);
}

std::int64_t PSCIService::do_version() const noexcept {
    return static_cast<std::int64_t>(kPsciVersion);
}

std::int64_t PSCIService::do_cpu_on(std::uint64_t target_affinity,
                                    std::uint64_t entry_pc,
                                    std::uint64_t context_id,
                                    std::size_t /*caller_core*/) {
    const std::size_t target_id = affinity_to_core_id(target_affinity);
    if (target_id >= cluster_.core_count()) {
        return kInvalidParams;
    }

    // AArch64 entry address must be 4-byte aligned.
    if ((entry_pc & 0x3u) != 0u) {
        return kInvalidParams;
    }

    // Check current power state.
    const CorePowerState cur = get_core_state(target_id);
    if (cur == CorePowerState::On) {
        return kAlreadyOn;
    }
    if (cur == CorePowerState::OnPending) {
        return kOnPending;
    }

    // Transition: Off -> OnPending -> On.
    core_states_[target_id]->store(CorePowerState::OnPending,
                                   std::memory_order_release);

    // Start the secondary core: reset registers, set PC, set X0 = context_id.
    cluster_.start_secondary_core(target_id, entry_pc);
    CPU& target_cpu = cluster_.get_core(target_id);
    target_cpu.set_psci_service(this);
    target_cpu.write_register(0, context_id);

    core_states_[target_id]->store(CorePowerState::On, std::memory_order_release);
    return kSuccess;
}

std::int64_t PSCIService::do_cpu_off(std::size_t caller_core) {
    if (caller_core == 0) {
        // Cannot power off the primary boot core while other operations are running.
        // Return DENIED per PSCI specification §5.5.
        return kDenied;
    }
    set_core_state(caller_core, CorePowerState::Off);
    cluster_.get_core(caller_core).stop(1);
    cluster_.halt_core(caller_core);
    return kSuccess;
}

std::int64_t PSCIService::do_cpu_suspend(std::uint32_t power_state,
                                         std::uint64_t entry_pc,
                                         std::uint64_t context_id,
                                         CPU& cpu,
                                         std::size_t core_id) {
    // power_state bit 16: State Type (0 = retention, 1 = powerdown) per PSCI 1.0+ / bit 24 in PSCI 0.2.
    // We check both bit 16 and bit 24 to accept both PSCI conventions.
    const bool is_powerdown = ((power_state & (1u << 16u)) != 0u) || ((power_state & (1u << 24u)) != 0u);
    const CorePowerState suspended_state =
        is_powerdown ? CorePowerState::SuspendedPowerdown
                     : CorePowerState::SuspendedRetention;

    set_core_state(core_id, suspended_state);

    if (is_powerdown) {
        if ((entry_pc & 0x3u) != 0u) {
            return kInvalidParams;
        }
        suspend_entry_[core_id] = entry_pc;
        suspend_context_[core_id] = context_id;
    }

    // Set core in waiting state (WFI equivalent)
    cpu.set_waiting_for_interrupt(true);
    return kSuccess;
}

std::int64_t PSCIService::do_affinity_info(std::uint64_t target_affinity,
                                           std::uint32_t affinity_level) const noexcept {
    // Only affinity level 0 (individual CPU) is supported.
    if (affinity_level != 0) {
        return kInvalidParams;
    }
    const std::size_t target_id = affinity_to_core_id(target_affinity);
    if (target_id >= cluster_.core_count()) {
        return kInvalidParams;
    }
    const CorePowerState state = get_core_state(target_id);
    // PSCI AFFINITY_INFO return: 0=ON, 1=OFF, 2=ON_PENDING
    switch (state) {
    case CorePowerState::On:
    case CorePowerState::SuspendedRetention:
    case CorePowerState::SuspendedPowerdown:
        return 0; // ON
    case CorePowerState::Off:
        return 1; // OFF
    case CorePowerState::OnPending:
        return 2; // ON_PENDING
    }
    return 1; // default: OFF
}

std::int64_t PSCIService::do_migrate_info_type() const noexcept {
    // 2 = Trusted OS is either not present or does not require migration.
    return 2;
}

std::int64_t PSCIService::do_features(std::uint32_t feature_id) const noexcept {
    switch (feature_id) {
    case kVersion:
    case kCpuSuspend32:
    case kCpuSuspend64:
    case kCpuOff:
    case kCpuOn32:
    case kCpuOn64:
    case kAffinityInfo32:
    case kAffinityInfo64:
    case kMigrateInfoType:
    case kSystemOff:
    case kSystemReset:
    case kPsciFeatures:
        return 0; // Supported, no special capability flags
    default:
        return kNotSupported;
    }
}

void PSCIService::do_system_off() {
    system_off_.store(true, std::memory_order_release);
    const std::size_t n = cluster_.core_count();
    for (std::size_t i = 0; i < n; ++i) {
        set_core_state(i, CorePowerState::Off);
        cluster_.get_core(i).stop(2);
        cluster_.halt_core(i);
    }
}

void PSCIService::do_system_reset() {
    system_reset_.store(true, std::memory_order_release);
    cluster_.get_virtual_time().reset_all();
    const std::size_t n = cluster_.core_count();
    for (std::size_t i = 0; i < n; ++i) {
        set_core_state(i, CorePowerState::Off);
        cluster_.get_core(i).stop(3);
        cluster_.halt_core(i);
    }
}

} // namespace winios
