#include "firmware/virtual_firmware.h"

#include <iostream>
#include <sstream>

namespace winios::firmware {

const char* boot_lifecycle_state_to_string(BootLifecycleState state) noexcept {
    switch (state) {
    case BootLifecycleState::Uninitialized:       return "Uninitialized";
    case BootLifecycleState::PlatformInitialized: return "PlatformInitialized";
    case BootLifecycleState::FirmwareInitialized: return "FirmwareInitialized";
    case BootLifecycleState::GuestPrepared:       return "GuestPrepared";
    case BootLifecycleState::GuestHandoff:        return "GuestHandoff";
    case BootLifecycleState::Running:             return "Running";
    case BootLifecycleState::Halted:              return "Halted";
    case BootLifecycleState::Shutdown:            return "Shutdown";
    case BootLifecycleState::Error:               return "Error";
    }
    return "Unknown";
}

VirtualFirmware::VirtualFirmware(FirmwareConfig config)
    : config_(std::move(config)) {}

void VirtualFirmware::set_config(const FirmwareConfig& config) {
    if (state_ != BootLifecycleState::Uninitialized && state_ != BootLifecycleState::PlatformInitialized) {
        transition_to_error("Cannot reconfigure firmware after guest preparation or handoff");
        return;
    }
    config_ = config;
}

void VirtualFirmware::transition_to_error(std::string message) {
    state_ = BootLifecycleState::Error;
    last_error_ = std::move(message);
}

bool VirtualFirmware::is_valid_ram_address(std::uint64_t address, std::size_t size) const noexcept {
    if (size == 0) return true;
    if (address < config_.ram_base) return false;
    const std::uint64_t offset = address - config_.ram_base;
    if (offset > config_.ram_size) return false;
    if (size > config_.ram_size - offset) return false;
    return true;
}

bool VirtualFirmware::initialize_platform(device::VirtualPlatform& platform) {
    if (state_ != BootLifecycleState::Uninitialized) {
        transition_to_error("Invalid lifecycle transition: initialize_platform requires Uninitialized state");
        return false;
    }

    if (platform.core_count() == 0) {
        transition_to_error("VirtualPlatform must have at least one CPU core");
        return false;
    }

    if (platform.memory().size() == 0) {
        transition_to_error("VirtualPlatform must have non-zero RAM size");
        return false;
    }

    bound_platform_ = &platform;
    config_.core_count = platform.core_count();
    config_.ram_size = platform.memory().size();
    state_ = BootLifecycleState::PlatformInitialized;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::initialize_firmware() {
    if (state_ != BootLifecycleState::PlatformInitialized) {
        transition_to_error("Invalid lifecycle transition: initialize_firmware requires PlatformInitialized state");
        return false;
    }

    if (!bound_platform_) {
        transition_to_error("No VirtualPlatform bound to firmware coordinator");
        return false;
    }

    if (config_.ram_size == 0) {
        transition_to_error("Firmware RAM size cannot be zero");
        return false;
    }

    // Verify configured RAM size is within bound platform RAM capacity
    if (config_.ram_size > bound_platform_->memory().size()) {
        transition_to_error("Configured firmware RAM size exceeds platform memory capacity");
        return false;
    }

    state_ = BootLifecycleState::FirmwareInitialized;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::prepare_guest(std::uint64_t kernel_entry, std::uint64_t dtb_address,
                                    std::uint64_t initrd_address, std::uint64_t initrd_size) {
    if (state_ != BootLifecycleState::FirmwareInitialized) {
        transition_to_error("Invalid lifecycle transition: prepare_guest requires FirmwareInitialized state");
        return false;
    }

    // Alignment checks: Linux ARM64 requires 4-byte aligned entry and 8-byte aligned DTB pointer
    if ((kernel_entry & 0x3u) != 0) {
        transition_to_error("Kernel entry point must be 4-byte aligned");
        return false;
    }

    if ((dtb_address & 0x7u) != 0) {
        transition_to_error("Device tree pointer (DTB) must be 8-byte aligned");
        return false;
    }

    // Range checks: must fall within RAM
    if (!is_valid_ram_address(kernel_entry, 4)) {
        transition_to_error("Kernel entry point is outside valid guest RAM");
        return false;
    }

    if (!is_valid_ram_address(dtb_address, 8)) {
        transition_to_error("Device tree address is outside valid guest RAM");
        return false;
    }

    // Collision check: DTB and kernel entry must not collide
    if (kernel_entry == dtb_address) {
        transition_to_error("Kernel entry and DTB address collide");
        return false;
    }

    if (initrd_address != 0 && initrd_size > 0) {
        if (!is_valid_ram_address(initrd_address, initrd_size)) {
            transition_to_error("Initramfs region extends outside valid guest RAM");
            return false;
        }

        // Check overlap with DTB (assuming minimum 64-byte DTB header)
        if (std::max(initrd_address, dtb_address) < std::min(initrd_address + initrd_size, dtb_address + 64)) {
            transition_to_error("Initramfs region overlaps with Device Tree Blob");
            return false;
        }
    }

    config_.kernel_entry = kernel_entry;
    config_.dtb_address = dtb_address;
    config_.initrd_address = initrd_address;
    config_.initrd_size = initrd_size;

    state_ = BootLifecycleState::GuestPrepared;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::handoff_to_guest() {
    if (state_ != BootLifecycleState::GuestPrepared) {
        transition_to_error("Invalid lifecycle transition: handoff_to_guest requires GuestPrepared state");
        return false;
    }

    if (!bound_platform_) {
        transition_to_error("No VirtualPlatform bound during handoff");
        return false;
    }

    // 1. Primary Core (Core 0) Configuration per Linux ARM64 Boot Protocol:
    // - Primary CPU boots at kernel_entry
    // - X0 = physical address of device tree blob (dtb_address)
    // - X1 = 0, X2 = 0, X3 = 0 (reserved)
    // - X4..X30 = 0
    // - CurrentEL = EL1 (or EL2 if configured)
    // - All interrupts masked (DAIF = 0b1111)
    // - MMU disabled (SCTLR_EL1.M = 0)
    // - FP/SIMD enabled (CPACR_EL1.FPEN = 0b11)
    auto& primary_cpu = bound_platform_->get_cpu(0);
    primary_cpu.reset();
    primary_cpu.set_program_counter(config_.kernel_entry);

    // Initial register conventions
    primary_cpu.write_register(0, config_.dtb_address);
    primary_cpu.write_register(1, 0);
    primary_cpu.write_register(2, 0);
    primary_cpu.write_register(3, 0);
    for (std::uint32_t r = 4; r < 31; ++r) {
        primary_cpu.write_register(r, 0);
    }

    primary_cpu.set_current_el(config_.target_el);
    primary_cpu.set_sp_sel(true);
    primary_cpu.set_stack_pointer(0); // Kernel establishes its own init stack
    primary_cpu.set_daif(0xFu);        // Mask D, A, I, F
    primary_cpu.set_cpacr_el1(3ULL << 20u); // Enable FP/SIMD (FPEN=11)
    primary_cpu.clear_exclusive_monitor();

    // 2. Secondary Cores (Core 1..N-1): Held quiescent
    const std::size_t total_cores = bound_platform_->core_count();
    for (std::size_t c = 1; c < total_cores; ++c) {
        auto& sec_cpu = bound_platform_->get_cpu(c);
        sec_cpu.reset();
        sec_cpu.set_program_counter(0);
        for (std::uint32_t r = 0; r < 31; ++r) {
            sec_cpu.write_register(r, 0);
        }
        bound_platform_->set_core_active(c, false); // Quiescent until CPU_ON
    }

    bound_platform_->set_core_active(0, true);

    state_ = BootLifecycleState::GuestHandoff;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::start_execution() {
    if (state_ != BootLifecycleState::GuestHandoff) {
        transition_to_error("Invalid lifecycle transition: start_execution requires GuestHandoff state");
        return false;
    }

    state_ = BootLifecycleState::Running;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::halt_guest() {
    if (state_ != BootLifecycleState::Running) {
        transition_to_error("Invalid lifecycle transition: halt_guest requires Running state");
        return false;
    }

    state_ = BootLifecycleState::Halted;
    last_error_.clear();
    return true;
}

bool VirtualFirmware::shutdown() {
    state_ = BootLifecycleState::Shutdown;
    last_error_.clear();
    return true;
}

void VirtualFirmware::reset() {
    if (bound_platform_) {
        bound_platform_->reset(0);
    }
    state_ = BootLifecycleState::Uninitialized;
    last_error_.clear();
    bound_platform_ = nullptr;
}

bool VirtualFirmware::verify_initial_cpu_state(std::size_t core_id) const {
    if (state_ != BootLifecycleState::GuestHandoff && state_ != BootLifecycleState::Running) {
        return false;
    }

    if (!bound_platform_ || core_id >= bound_platform_->core_count()) {
        return false;
    }

    const auto& cpu = bound_platform_->get_cpu(core_id);
    if (core_id == 0) {
        if (cpu.get_program_counter() != config_.kernel_entry) return false;
        if (cpu.read_register(0) != config_.dtb_address) return false;
        if (cpu.read_register(1) != 0) return false;
        if (cpu.read_register(2) != 0) return false;
        if (cpu.read_register(3) != 0) return false;
        if (cpu.get_current_el() != config_.target_el) return false;
        if ((cpu.get_daif() & 0xFu) != 0xFu) return false;
        if (!cpu.is_fp_simd_enabled(static_cast<unsigned>(config_.target_el))) return false;
        return true;
    } else {
        // Secondary cores must be quiescent
        if (bound_platform_->is_core_active(core_id)) return false;
        return true;
    }
}

} // namespace winios::firmware
