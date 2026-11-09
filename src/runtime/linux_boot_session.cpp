#include "runtime/linux_boot_session.h"

#include <fstream>
#include <sstream>

namespace winios::runtime {

namespace {

inline std::uint64_t align_up(std::uint64_t val, std::uint64_t align) noexcept {
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

inline std::uint64_t align_down(std::uint64_t val, std::uint64_t align) noexcept {
    if (align == 0) return val;
    return val & ~(align - 1);
}

} // anonymous namespace

LinuxBootSession::LinuxBootSession(const LinuxBootConfig& config)
    : config_(config), memory_map_(config.ram_base, config.ram_size) {}

bool LinuxBootSession::initialize(std::string& out_error) {
    status_ = LinuxSessionStatus::Uninitialized;
    error_.clear();
    total_instructions_ = 0;

    // 1. Setup Virtual Platform
    device::VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = config_.core_count;
    plat_cfg.ram_base   = config_.ram_base;
    plat_cfg.ram_size   = (config_.ram_base > 0) ? (config_.ram_base + config_.ram_size) : config_.ram_size;
    plat_cfg.boot_entry = config_.ram_base + 0x80000ULL;

    platform_ = std::make_unique<device::VirtualPlatform>(plat_cfg);

    firmware::FirmwareConfig fw_cfg;
    fw_cfg.ram_base = config_.ram_base;
    fw_cfg.ram_size = config_.ram_size;
    fw_cfg.core_count = config_.core_count;
    firmware_ = std::make_unique<firmware::VirtualFirmware>(fw_cfg);

    if (!firmware_->initialize_platform(*platform_)) {
        out_error = "Failed to initialize platform: " + firmware_->last_error();
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    // 2. Initialize firmware state machine
    if (!firmware_->initialize_firmware()) {
        out_error = "Failed to initialize virtual firmware: " + firmware_->last_error();
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    // 3. Load Kernel Image
    if (!config_.kernel_data.empty()) {
        kernel_result_ = loader::LinuxImageLoader::load_from_buffer(
            config_.kernel_data.data(), config_.kernel_data.size(),
            platform_->memory(), config_.ram_base, config_.ram_size);
    } else if (!config_.kernel_path.empty()) {
        kernel_result_ = loader::LinuxImageLoader::load_from_file(
            config_.kernel_path, platform_->memory(),
            config_.ram_base, config_.ram_size);
    } else {
        out_error = "No Linux kernel image provided (neither path nor buffer)";
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    if (!kernel_result_.is_success()) {
        out_error = "Kernel load failed: " + kernel_result_.error_message;
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    memory_map_.register_kernel_image(kernel_result_.load_address, kernel_result_.effective_size);

    // 4. Load Initramfs (Optional)
    std::uint64_t initrd_start = 0;
    std::uint64_t initrd_end = 0;
    std::size_t initrd_size = 0;

    const bool has_initrd_buffer = !config_.initramfs_data.empty();
    const bool has_initrd_file   = !config_.initramfs_path.empty();

    if (has_initrd_buffer || has_initrd_file) {
        // Place initramfs 2MB after kernel
        std::uint64_t initrd_target = align_up(kernel_result_.load_address + kernel_result_.effective_size, 0x200000ULL);

        if (has_initrd_buffer) {
            initramfs_result_ = loader::InitramfsLoader::load_from_buffer(
                config_.initramfs_data.data(), config_.initramfs_data.size(),
                platform_->memory(), initrd_target, config_.ram_base, config_.ram_size, &memory_map_);
        } else {
            initramfs_result_ = loader::InitramfsLoader::load_from_file(
                config_.initramfs_path, platform_->memory(),
                initrd_target, config_.ram_base, config_.ram_size, &memory_map_);
        }

        if (!initramfs_result_.is_success()) {
            out_error = "Initramfs load failed: " + initramfs_result_.error_message;
            error_ = out_error;
            status_ = LinuxSessionStatus::Error;
            return false;
        }

        initrd_start = initramfs_result_.load_address;
        initrd_end   = initramfs_result_.end_address;
        initrd_size  = initramfs_result_.size;
    }

    // 5. Build and Load Device Tree Blob (DTB)
    std::vector<std::uint8_t> dtb_blob;
    if (!config_.dtb_data.empty()) {
        dtb_blob = config_.dtb_data;
    } else if (!config_.dtb_path.empty()) {
        std::ifstream dtb_file(config_.dtb_path, std::ios::binary | std::ios::ate);
        if (!dtb_file.is_open()) {
            out_error = "Cannot open DTB file: " + config_.dtb_path;
            error_ = out_error;
            status_ = LinuxSessionStatus::Error;
            return false;
        }
        std::streamsize sz = dtb_file.tellg();
        dtb_blob.resize(static_cast<std::size_t>(sz));
        dtb_file.seekg(0, std::ios::beg);
        dtb_file.read(reinterpret_cast<char*>(dtb_blob.data()), sz);
    } else {
        // Synthesize platform device tree automatically
        dtb_blob = device::FdtBuilder::build_platform_dtb(
            platform_->config(), config_.boot_args, initrd_start, initrd_end);
    }

    if (dtb_blob.empty()) {
        out_error = "Failed to generate or load DTB blob";
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    // Place DTB 2MB below top of RAM
    dtb_size_ = dtb_blob.size();
    dtb_address_ = align_down(config_.ram_base + config_.ram_size - dtb_size_ - 0x1000ULL, 0x1000ULL);

    if (!memory_map_.register_dtb(dtb_address_, dtb_size_)) {
        out_error = "Failed to register DTB in memory map";
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    std::string map_err;
    if (memory_map_.validate(&map_err) != loader::MemoryMapValidationResult::Valid) {
        out_error = "Physical memory layout validation failed: " + map_err;
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    // Write DTB bytes into RAM (flat or base-relative)
    std::uint64_t dtb_mem_offset = dtb_address_;
    if (platform_->memory().size() < dtb_address_ + dtb_size_) {
        dtb_mem_offset = dtb_address_ - config_.ram_base;
    }
    for (std::size_t i = 0; i < dtb_blob.size(); ++i) {
        platform_->memory().write_u8(dtb_mem_offset + i, dtb_blob[i]);
    }

    // 6. Firmware Prepare Guest
    if (!firmware_->prepare_guest(kernel_result_.entry_point, dtb_address_, initrd_start, initrd_size)) {
        out_error = "Firmware guest preparation failed: " + firmware_->last_error();
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    status_ = LinuxSessionStatus::Prepared;
    return true;
}

bool LinuxBootSession::boot(std::string& out_error) {
    if (status_ != LinuxSessionStatus::Prepared) {
        out_error = "Session not in Prepared state";
        error_ = out_error;
        return false;
    }

    if (!firmware_->handoff_to_guest()) {
        out_error = "Firmware handoff to guest failed: " + firmware_->last_error();
        error_ = out_error;
        status_ = LinuxSessionStatus::Error;
        return false;
    }

    status_ = LinuxSessionStatus::Running;
    milestone_tracker_.record_milestone(
        LinuxBootMilestone::FirmwareHandoff,
        total_instructions_,
        platform_->get_cpu(0).get_program_counter(),
        1,
        "Firmware handoff to Linux ARM64 kernel entry");
    return true;
}

std::size_t LinuxBootSession::step(std::size_t max_instructions) {
    if (status_ != LinuxSessionStatus::Running) {
        return 0;
    }

    std::size_t executed = 0;
    while (executed < max_instructions && status_ == LinuxSessionStatus::Running) {
        std::size_t step_count = platform_->step_round_robin();
        if (step_count == 0) {
            // All cores inactive or halted
            status_ = LinuxSessionStatus::Halted;
            break;
        }

        executed += step_count;
        total_instructions_ += step_count;

        // Service platform timers
        platform_->virtual_time().advance_physical_counter(1);

        if (total_instructions_ >= config_.max_instructions) {
            status_ = LinuxSessionStatus::MaxInstructionsReached;
            break;
        }
    }

    if (platform_ && platform_->uart()) {
        const std::string text = platform_->uart()->get_output_text();
        if (!text.empty()) {
            milestone_tracker_.scan_console_text(
                text,
                total_instructions_,
                platform_->get_cpu(0).get_program_counter(),
                static_cast<std::uint8_t>(platform_->get_cpu(0).get_current_el()));
        }
    }

    return executed;
}

LinuxSessionStatus LinuxBootSession::run() {
    if (status_ == LinuxSessionStatus::Uninitialized) {
        std::string err;
        if (!initialize(err)) {
            return status_;
        }
    }
    if (status_ == LinuxSessionStatus::Prepared) {
        std::string err;
        if (!boot(err)) {
            return status_;
        }
    }

    while (status_ == LinuxSessionStatus::Running) {
        step(10000);
    }

    // Export console log if requested
    if (!config_.console_log_path.empty()) {
        std::ofstream log_file(config_.console_log_path);
        if (log_file.is_open()) {
            log_file << get_console_output();
        }
    }

    return status_;
}

std::string LinuxBootSession::get_console_output() const {
    if (!platform_) return "";
    return platform_->uart()->get_output_text();
}

} // namespace winios::runtime
