#include "runtime/darwin_boot_session.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace winios::runtime {

DarwinBootSession::DarwinBootSession(const DarwinBootConfig& config)
    : config_(config),
      memory_map_(config.ram_base, config.ram_size) {
}

bool DarwinBootSession::initialize(std::string& out_error) {
    if (config_.ram_size < 0x02000000) { // 32 MB minimum
        out_error = "RAM size must be at least 32 MB for Darwin boot VM";
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    // 1. Initialize RAM
    memory_ = std::make_unique<Memory>(config_.ram_size);
    memory_map_.clear_regions();
    milestone_tracker_.reset();

    // 2. Instantiate Peripherals and Virtual Device Bus
    device_bus_ = std::make_shared<device::VirtualDeviceBus>();

    aic_ = std::make_shared<device::AppleAIC>(
        0x41494332u, config_.aic_base, device::AppleAIC::kDefaultAicSize, config_.core_count);

    pmgr_ = std::make_shared<device::ApplePMGR>(
        0x504D4752u, config_.pmgr_base, device::ApplePMGR::kDefaultPmgrSize, 0x8120u);

    display_ = std::make_shared<device::AppleDisplay>(
        0x44495350u, config_.display_base, device::AppleDisplay::kDefaultDisplaySize, config_.framebuffer_base);

    uart_ = std::make_shared<device::VirtualUartDevice>(
        0x55415254u, config_.uart_base, 0x1000u, 33u);

    // Wire UART interrupt to AIC line 33
    uart_->set_interrupt_callback([this](std::uint32_t line, bool asserted) {
        if (aic_) {
            aic_->set_hw_irq(line, asserted);
        }
    });

    device_bus_->register_device(aic_);
    device_bus_->register_device(pmgr_);
    device_bus_->register_device(display_);
    device_bus_->register_device(uart_);

    // 3. Optional Ramdisk Root Filesystem Ingestion
    std::uint64_t ramdisk_base = 0;
    std::uint64_t ramdisk_size = 0;
    if (!config_.ramdisk_data.empty()) {
        const std::uint64_t target_rd_addr = config_.ram_base + 0x04000000ULL; // 64 MB from RAM base
        ramdisk_result_ = loader::RamdiskLoader::load_from_buffer(
            config_.ramdisk_data.data(), config_.ramdisk_data.size(),
            *memory_, target_rd_addr, config_.ram_base, config_.ram_size, &memory_map_);
        if (!ramdisk_result_.is_success()) {
            out_error = "Failed to load in-memory ramdisk: " + ramdisk_result_.error_message;
            status_ = DarwinSessionStatus::Error;
            return false;
        }
        ramdisk_base = ramdisk_result_.physical_address;
        ramdisk_size = ramdisk_result_.size;
    } else if (!config_.ramdisk_path.empty()) {
        const std::uint64_t target_rd_addr = config_.ram_base + 0x04000000ULL;
        ramdisk_result_ = loader::RamdiskLoader::load_from_file(
            config_.ramdisk_path,
            *memory_, target_rd_addr, config_.ram_base, config_.ram_size, &memory_map_);
        if (!ramdisk_result_.is_success()) {
            out_error = "Failed to load ramdisk from file: " + ramdisk_result_.error_message;
            status_ = DarwinSessionStatus::Error;
            return false;
        }
        ramdisk_base = ramdisk_result_.physical_address;
        ramdisk_size = ramdisk_result_.size;
    }

    // 4. Generate Apple DeviceTree (ADTS format) for iPhone 15
    const auto dt_bytes = device::AppleDeviceTreeBuilder::create_iphone15_devicetree(
        config_.ram_base, config_.ram_size, config_.boot_args, config_.framebuffer_base,
        ramdisk_base, ramdisk_size);

    if (dt_bytes.empty()) {
        out_error = "Failed to generate Apple DeviceTree for iPhone 15";
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    // Place DeviceTree in high RAM
    const std::uint64_t dt_offset = config_.ram_size - 0x100000ULL; // 1 MB before top of RAM
    devicetree_phys_addr_ = config_.ram_base + dt_offset;
    devicetree_length_ = dt_bytes.size();

    if (dt_offset + dt_bytes.size() > memory_->size()) {
        out_error = "DeviceTree exceeds guest RAM capacity";
        status_ = DarwinSessionStatus::Error;
        return false;
    }
    std::memcpy(memory_->raw_data() + dt_offset, dt_bytes.data(), dt_bytes.size());
    memory_map_.register_dtb(devicetree_phys_addr_, devicetree_length_);
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::DeviceTreeParsed, 0, 0, 1, "Apple DeviceTree (ADTS) mapped in high RAM");

    // 5. Construct struct boot_args
    firmware::AppleBootVideo video;
    video.v_baseAddr = config_.framebuffer_base;
    video.v_display  = 1;
    video.v_rowBytes = display_->row_bytes();
    video.v_width    = display_->width();
    video.v_height   = display_->height();
    video.v_depth    = display_->depth();

    const std::uint64_t args_offset = dt_offset - sizeof(firmware::AppleBootArgs);
    boot_args_phys_addr_ = config_.ram_base + args_offset;

    boot_args_ = firmware::AppleBootArgsBuilder::create(
        config_.ram_base,
        0xFFFFFFF007004000ULL, // Standard 64-bit Darwin kernel virtual base
        config_.ram_size,
        config_.ram_base + 0x02000000ULL, // Top of early kernel data
        devicetree_phys_addr_,
        static_cast<std::uint32_t>(devicetree_length_),
        config_.boot_args,
        video);

    if (args_offset + sizeof(boot_args_) > memory_->size()) {
        out_error = "struct boot_args exceeds guest RAM capacity";
        status_ = DarwinSessionStatus::Error;
        return false;
    }
    std::memcpy(memory_->raw_data() + args_offset, &boot_args_, sizeof(boot_args_));
    memory_map_.register_reserved("AppleBootArgs", boot_args_phys_addr_, sizeof(boot_args_));
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::BootArgsParsed, 0, 0, 1, "struct boot_args Revision 2 Version 2 mapped");

    // 6. Platform Probing & IOKit Driver Matching
    platform_expert_.load_default_iphone15_personalities();
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::IOKitCatalogPopulated, 0, 0, 1,
        "IOKit driver catalog populated with iPhone 15 personalities");

    auto dt_root = device::AppleDeviceTreeBuilder::deserialize(
        dt_bytes.data(), dt_bytes.size(), &out_error);
    if (!dt_root) {
        out_error = "Failed to deserialize Apple DeviceTree: " + out_error;
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    if (!platform_expert_.probe_and_match(*dt_root, out_error)) {
        out_error = "Platform probing and IOKit driver matching failed: " + out_error;
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    std::string diag_report;
    if (!platform_expert_.verify_platform_readiness(diag_report)) {
        out_error = "Platform readiness verification failed:\n" + diag_report;
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    milestone_tracker_.record_milestone(
        DarwinBootMilestone::PlatformExpertInit, 0, 0, 1, "AppleARMPE verified active for iPhone 15");
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::InterruptControllerMatched, 0, 0, 1, "AppleA16InterruptController active at 0x08000000");
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::PowerManagerMatched, 0, 0, 1, "AppleT8120PMGR active at 0x10000000");
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::DisplayFramebufferMatched, 0, 0, 1, "AppleDCP Super Retina XDR active");
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::ConsoleUartMatched, 0, 0, 1, "AppleSerialUART active at 0x09000000 (IRQ 33)");
    milestone_tracker_.record_milestone(
        DarwinBootMilestone::DriverMatchingComplete, 0, 0, 1, "All essential hardware drivers matched and started");

    if (ramdisk_size > 0) {
        milestone_tracker_.record_milestone(
            DarwinBootMilestone::RootFilesystemLocated, 0, 0, 1,
            "Root filesystem ramdisk active in /chosen/memory-map");
    }

    // 7. Parse and Load Mach-O kernelcache
    if (!config_.kernelcache_data.empty()) {
        kernel_result_ = loader::MachOLoader::load_from_buffer(
            config_.kernelcache_data.data(), config_.kernelcache_data.size(),
            *memory_, config_.ram_base, config_.ram_size, 0, &memory_map_);
        if (!kernel_result_.is_success()) {
            out_error = "Failed to parse in-memory Mach-O kernelcache: " + kernel_result_.error_message;
            status_ = DarwinSessionStatus::Error;
            return false;
        }
    } else if (!config_.kernelcache_path.empty()) {
        kernel_result_ = loader::MachOLoader::load_from_file(
            config_.kernelcache_path,
            *memory_, config_.ram_base, config_.ram_size, 0, &memory_map_);
        if (!kernel_result_.is_success()) {
            out_error = "Failed to load Mach-O kernelcache from file: " + kernel_result_.error_message;
            status_ = DarwinSessionStatus::Error;
            return false;
        }
    } else {
        // Fallback for hardware / unit tests: synthetic entry point at ram_base + 0x10000
        kernel_result_.status = loader::MachOLoadStatus::Success;
        kernel_result_.entry_point = config_.ram_base + 0x10000ULL;
        kernel_result_.initial_sp = config_.ram_base + args_offset - 0x1000ULL;
    }

    // 6. Initialize CPU Cores
    cpus_.clear();
    for (std::size_t i = 0; i < config_.core_count; ++i) {
        cpus_.push_back(std::make_unique<CPU>());
    }

    // Configure boot core (CPU 0) handoff state:
    // Apple ARM64 kernel entry contract: X0 = physical address of struct boot_args
    CPU& boot_cpu = *cpus_[0];
    boot_cpu.reset();
    boot_cpu.set_current_el(1);
    boot_cpu.set_sp_sel(true); // EL1h
    boot_cpu.set_cpacr_el1(0x00300000); // Full FP access
    boot_cpu.write_register(0, boot_args_phys_addr_);
    boot_cpu.set_program_counter(kernel_result_.entry_point);
    if (kernel_result_.initial_sp != 0) {
        boot_cpu.set_stack_pointer(kernel_result_.initial_sp);
    } else {
        boot_cpu.set_stack_pointer(config_.ram_base + args_offset - 0x1000ULL);
    }

    status_ = DarwinSessionStatus::Prepared;
    return true;
}

bool DarwinBootSession::prepare_handoff(std::string& out_error) {
    if (status_ != DarwinSessionStatus::Prepared) {
        out_error = "Cannot prepare handoff: session is not initialized/prepared";
        return false;
    }

    // Re-verify X0 points to valid boot_args
    if (cpus_.empty() || cpus_[0]->read_register(0) != boot_args_phys_addr_) {
        out_error = "CPU register X0 corrupted before handoff";
        status_ = DarwinSessionStatus::Error;
        return false;
    }

    status_ = DarwinSessionStatus::BootHandoffReady;
    return true;
}

std::size_t DarwinBootSession::step(std::size_t max_instructions) {
    if (status_ != DarwinSessionStatus::Prepared &&
        status_ != DarwinSessionStatus::BootHandoffReady &&
        status_ != DarwinSessionStatus::Running) {
        return 0;
    }

    status_ = DarwinSessionStatus::Running;
    std::size_t executed = 0;
    CPU& cpu = *cpus_[0];

    while (executed < max_instructions) {
        if (total_instructions_ >= config_.max_instructions) {
            status_ = DarwinSessionStatus::MaxInstructionsReached;
            break;
        }

        // Check AIC interrupt assertion
        if (aic_->has_pending_interrupt(0)) {
            // Evaluated interrupt line can be sampled by CPU
        }

        cpu.step(*memory_);
        total_instructions_++;
        executed++;

        if (uart_) {
            milestone_tracker_.scan_console_text(
                uart_->get_output_text(), total_instructions_,
                cpu.get_program_counter(), static_cast<std::uint8_t>(cpu.get_current_el()));
        }

        if (cpu.get_current_el() == 0) {
            milestone_tracker_.record_milestone(
                DarwinBootMilestone::LaunchdSpawnPrepared,
                total_instructions_, cpu.get_program_counter(), 0, "Userspace execution active in EL0");
        }
    }

    return executed;
}

DarwinSessionStatus DarwinBootSession::run() {
    step(config_.max_instructions);
    return status_;
}

std::string DarwinBootSession::get_console_output() const {
    if (uart_) {
        return uart_->get_output_text();
    }
    return {};
}

} // namespace winios::runtime
