#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class CPU;

namespace winios::cpu {

// -----------------------------------------------------------------------------
// Exception Level for Kernel Boot Entry
// -----------------------------------------------------------------------------
enum class KernelBootExceptionLevel {
    EL1,
    EL2
};

const char* kernel_boot_exception_level_string(KernelBootExceptionLevel el) noexcept;

// -----------------------------------------------------------------------------
// CPU Boot Configuration
// -----------------------------------------------------------------------------
struct CpuBootConfig {
    std::uint64_t initial_pc{0};
    std::uint64_t initial_sp{0};
    std::uint64_t boot_args_phys{0};
    KernelBootExceptionLevel exception_level{KernelBootExceptionLevel::EL1};
    std::uint64_t vbar_el1{0};
    std::uint64_t sctlr_el1{0};
    std::uint64_t tcr_el1{0};
    std::uint64_t ttbr0_el1{0};
    std::uint64_t ttbr1_el1{0};
    std::uint64_t mair_el1{0};
    std::uint32_t spsr_el1{0x3C5}; // PSTATE: EL1h, DAIF masked (D=1, A=1, I=1, F=1, M[3:0]=0b0101)
    bool          set_x19_boot_args{true}; // Darwin convention: X19 holds boot_args pointer
};

// -----------------------------------------------------------------------------
// ARM64 CPU Boot State Initializer & State Exporter
// -----------------------------------------------------------------------------
class Arm64CpuBootInitializer {
public:
    static bool validate_config(const CpuBootConfig& config, std::string* out_error = nullptr);

    static bool initialize_cpu(CPU& cpu, const CpuBootConfig& config, std::string* out_error = nullptr);

    static std::string export_boot_state_json(const CpuBootConfig& config);
};

} // namespace winios::cpu
