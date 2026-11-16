#include "smp_kernel_boot_manager.h"

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

bool SmpKernelBootManager::initialize(
    std::size_t num_cores,
    std::uint64_t kernel_entry_pc,
    std::uint64_t boot_args_phys,
    std::uint64_t stack_pool_base,
    std::uint64_t vbar_base,
    std::uint64_t rendezvous_base_pa) {

    if (num_cores == 0) return false;
    if (num_cores > kMaxCores) num_cores = kMaxCores;

    kernel_entry_pc_ = kernel_entry_pc;
    boot_args_phys_ = boot_args_phys;
    stack_pool_base_ = stack_pool_base;
    vbar_base_ = vbar_base;
    rendezvous_base_pa_ = rendezvous_base_pa;

    cores_.clear();
    cores_.reserve(num_cores);

    for (std::size_t i = 0; i < num_cores; ++i) {
        SmpCoreBootConfig cfg;
        cfg.core_id = i;
        cfg.is_primary = (i == 0);
        // iPhone 15 A16 Bionic topology: first 2 cores are Everest P-cores, remainder are Sawtooth E-cores
        cfg.is_performance_core = (i < 2);

        const std::uint64_t cluster_aff = cfg.is_performance_core ? 0x0000ULL : 0x0100ULL;
        cfg.mpidr_el1 = 0x80000000ULL | cluster_aff | static_cast<std::uint64_t>(i);

        // Dedicated 16KB stack per core
        cfg.initial_sp = stack_pool_base_ + ((i + 1) * kStackStride16K);
        cfg.tpidr_el1 = stack_pool_base_ + (i * 0x100ULL);
        cfg.entry_pc = kernel_entry_pc_;
        cfg.vbar_el1 = vbar_base_;
        cfg.phase = SmpCoreBootPhase::HandoffReady;
        cfg.instructions_retired = 0;

        cores_.push_back(cfg);
    }

    return true;
}

bool SmpKernelBootManager::boot_primary_core(CPU& primary_cpu, ::Memory& memory) {
    if (cores_.empty()) return false;

    auto& core = cores_[0];
    primary_cpu.reset();
    primary_cpu.set_current_el(1);
    primary_cpu.set_sp_sel(true);
    primary_cpu.set_stack_pointer(core.initial_sp);
    primary_cpu.set_program_counter(core.entry_pc);
    primary_cpu.write_register(0, boot_args_phys_);
    primary_cpu.write_register(19, boot_args_phys_);
    primary_cpu.set_vbar_el1(core.vbar_el1);

    core.phase = SmpCoreBootPhase::Bootstrapping;

    // Check-in to shared rendezvous array
    if (rendezvous_base_pa_ < memory.size()) {
        memory.write_u32(static_cast<std::size_t>(rendezvous_base_pa_), 1u);
    }
    core.phase = SmpCoreBootPhase::RendezvousOnline;

    return true;
}

bool SmpKernelBootManager::bring_up_secondary_core(
    std::size_t core_id,
    CPU& secondary_cpu,
    ::Memory& memory) {

    if (core_id == 0 || core_id >= cores_.size()) return false;

    auto& core = cores_[core_id];
    secondary_cpu.reset();
    secondary_cpu.set_current_el(1);
    secondary_cpu.set_sp_sel(true);
    secondary_cpu.set_stack_pointer(core.initial_sp);
    secondary_cpu.set_program_counter(core.entry_pc);
    secondary_cpu.write_register(0, static_cast<std::uint64_t>(core_id));
    secondary_cpu.write_register(1, boot_args_phys_);
    secondary_cpu.set_vbar_el1(core.vbar_el1);

    core.phase = SmpCoreBootPhase::Bootstrapping;

    // Signal online in shared memory rendezvous slot
    const std::uint64_t slot_pa = rendezvous_base_pa_ + (core_id * 8ULL);
    if (slot_pa < memory.size()) {
        memory.write_u32(static_cast<std::size_t>(slot_pa), 1u);
    }
    core.phase = SmpCoreBootPhase::RendezvousOnline;

    return true;
}

bool SmpKernelBootManager::step_core(std::size_t core_id, CPU& cpu, ::Memory& memory) {
    if (core_id >= cores_.size()) return false;

    cpu.step(memory);
    cores_[core_id].instructions_retired++;
    return true;
}

bool SmpKernelBootManager::check_rendezvous(::Memory& memory) const noexcept {
    if (cores_.empty()) return false;

    const std::size_t mem_size = memory.size();
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        const std::uint64_t slot_pa = rendezvous_base_pa_ + (i * 8ULL);
        if (slot_pa >= mem_size) return false;
        if (memory.read_u32(static_cast<std::size_t>(slot_pa)) != 1u) {
            return false;
        }
    }
    return true;
}

std::size_t SmpKernelBootManager::online_cores() const noexcept {
    std::size_t count = 0;
    for (const auto& core : cores_) {
        if (core.phase == SmpCoreBootPhase::RendezvousOnline ||
            core.phase == SmpCoreBootPhase::IdleReady) {
            ++count;
        }
    }
    return count;
}

const SmpCoreBootConfig* SmpKernelBootManager::get_core_config(std::size_t core_id) const noexcept {
    if (core_id >= cores_.size()) return nullptr;
    return &cores_[core_id];
}

} // namespace winios::runtime
