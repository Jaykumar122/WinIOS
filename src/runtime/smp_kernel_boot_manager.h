#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CPU;
class Memory;

namespace winios::runtime {

enum class SmpCoreBootPhase {
    Offline,
    HandoffReady,
    Bootstrapping,
    RendezvousOnline,
    IdleReady
};

struct SmpCoreBootConfig {
    std::size_t      core_id{0};
    bool             is_primary{false};
    bool             is_performance_core{false};
    std::uint64_t    mpidr_el1{0};
    std::uint64_t    initial_sp{0};
    std::uint64_t    tpidr_el1{0};
    std::uint64_t    entry_pc{0};
    std::uint64_t    vbar_el1{0};
    SmpCoreBootPhase phase{SmpCoreBootPhase::Offline};
    std::uint64_t    instructions_retired{0};
};

class SmpKernelBootManager {
public:
    static constexpr std::size_t   kMaxCores = 8;
    static constexpr std::uint64_t kStackStride16K = 0x4000ULL; // 16 KB per core
    static constexpr std::uint64_t kDefaultRendezvousBasePA = 0x20000ULL;

    SmpKernelBootManager() noexcept = default;

    bool initialize(
        std::size_t num_cores,
        std::uint64_t kernel_entry_pc,
        std::uint64_t boot_args_phys,
        std::uint64_t stack_pool_base,
        std::uint64_t vbar_base,
        std::uint64_t rendezvous_base_pa = kDefaultRendezvousBasePA);

    bool boot_primary_core(CPU& primary_cpu, ::Memory& memory);

    bool bring_up_secondary_core(
        std::size_t core_id,
        CPU& secondary_cpu,
        ::Memory& memory);

    bool step_core(std::size_t core_id, CPU& cpu, ::Memory& memory);

    bool check_rendezvous(::Memory& memory) const noexcept;

    std::size_t num_cores() const noexcept { return cores_.size(); }
    std::size_t online_cores() const noexcept;
    std::uint64_t rendezvous_base_pa() const noexcept { return rendezvous_base_pa_; }

    const SmpCoreBootConfig* get_core_config(std::size_t core_id) const noexcept;

private:
    std::vector<SmpCoreBootConfig> cores_;
    std::uint64_t kernel_entry_pc_{0};
    std::uint64_t boot_args_phys_{0};
    std::uint64_t stack_pool_base_{0};
    std::uint64_t vbar_base_{0};
    std::uint64_t rendezvous_base_pa_{kDefaultRendezvousBasePA};
};

} // namespace winios::runtime
