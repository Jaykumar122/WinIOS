#include "cpu/arm64_cpu_boot_state.h"

#include <iomanip>
#include <sstream>

#include "cpu/cpu.h"

namespace winios::cpu {

const char* kernel_boot_exception_level_string(KernelBootExceptionLevel el) noexcept {
    switch (el) {
        case KernelBootExceptionLevel::EL1: return "EL1";
        case KernelBootExceptionLevel::EL2: return "EL2";
    }
    return "Unknown";
}

bool Arm64CpuBootInitializer::validate_config(const CpuBootConfig& config, std::string* out_error) {
    if (config.initial_pc == 0) {
        if (out_error) *out_error = "Initial PC cannot be 0";
        return false;
    }

    if ((config.initial_pc % 4) != 0) {
        if (out_error) *out_error = "Initial PC 0x" + std::to_string(config.initial_pc) + " must be 4-byte aligned";
        return false;
    }

    if (config.initial_sp == 0) {
        if (out_error) *out_error = "Initial SP cannot be 0";
        return false;
    }

    if ((config.initial_sp % 16) != 0) {
        if (out_error) *out_error = "Initial SP 0x" + std::to_string(config.initial_sp) + " must be 16-byte aligned";
        return false;
    }

    if (config.vbar_el1 != 0 && (config.vbar_el1 % 2048) != 0) {
        if (out_error) *out_error = "VBAR_EL1 0x" + std::to_string(config.vbar_el1) + " must be 2048-byte aligned";
        return false;
    }

    return true;
}

bool Arm64CpuBootInitializer::initialize_cpu(CPU& cpu, const CpuBootConfig& config, std::string* out_error) {
    if (!validate_config(config, out_error)) {
        return false;
    }

    cpu.reset();

    // 1. Exception Level and Stack Configuration
    if (config.exception_level == KernelBootExceptionLevel::EL1) {
        cpu.set_current_el(1);
        cpu.set_sp_sel(true); // EL1h (SP_EL1 selected)
        cpu.set_sp_el1(config.initial_sp);
        cpu.set_spsr_el1(config.spsr_el1);
    } else {
        cpu.set_current_el(2);
        cpu.set_sp_sel(true); // EL2h
        cpu.set_sp_el2(config.initial_sp);
    }

    // 2. Program Counter
    cpu.set_program_counter(config.initial_pc);

    // 3. Register State Handoff
    cpu.write_register(0, config.boot_args_phys);
    if (config.set_x19_boot_args) {
        cpu.write_register(19, config.boot_args_phys);
    }

    // 4. System Registers
    if (config.vbar_el1 != 0) {
        cpu.set_vbar_el1(config.vbar_el1);
    }
    if (config.sctlr_el1 != 0) {
        cpu.set_sctlr_el1(config.sctlr_el1);
    }
    if (config.tcr_el1 != 0) {
        cpu.set_tcr_el1(config.tcr_el1);
    }
    if (config.ttbr0_el1 != 0) {
        cpu.set_ttbr0_el1(config.ttbr0_el1);
    }
    if (config.ttbr1_el1 != 0) {
        cpu.set_ttbr1_el1(config.ttbr1_el1);
    }
    if (config.mair_el1 != 0) {
        cpu.set_mair_el1(config.mair_el1);
    }

    return true;
}

std::string Arm64CpuBootInitializer::export_boot_state_json(const CpuBootConfig& config) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"initial_pc\": \"0x" << std::hex << config.initial_pc << "\",\n";
    oss << "  \"initial_sp\": \"0x" << config.initial_sp << "\",\n";
    oss << "  \"boot_args_phys\": \"0x" << config.boot_args_phys << "\",\n";
    oss << "  \"exception_level\": \"" << kernel_boot_exception_level_string(config.exception_level) << "\",\n";
    oss << "  \"vbar_el1\": \"0x" << config.vbar_el1 << "\",\n";
    oss << "  \"sctlr_el1\": \"0x" << config.sctlr_el1 << "\",\n";
    oss << "  \"tcr_el1\": \"0x" << config.tcr_el1 << "\",\n";
    oss << "  \"ttbr0_el1\": \"0x" << config.ttbr0_el1 << "\",\n";
    oss << "  \"ttbr1_el1\": \"0x" << config.ttbr1_el1 << "\",\n";
    oss << "  \"mair_el1\": \"0x" << config.mair_el1 << "\",\n";
    oss << "  \"spsr_el1\": \"0x" << config.spsr_el1 << "\",\n";
    oss << "  \"set_x19_boot_args\": " << (config.set_x19_boot_args ? "true" : "false") << "\n";
    oss << "}\n";
    return oss.str();
}

} // namespace winios::cpu
