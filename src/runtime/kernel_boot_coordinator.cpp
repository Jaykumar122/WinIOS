#include "kernel_boot_coordinator.h"

#include <algorithm>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

namespace winios::runtime {

namespace {

constexpr std::uint64_t kGranule16K = 0x4000ULL; // 16 KB Darwin page granule
constexpr std::uint64_t kStackSize  = 0x10000ULL; // 64 KB bootstrap kernel stack

inline std::uint64_t align_up(std::uint64_t val, std::uint64_t alignment) noexcept {
    return (val + alignment - 1ULL) & ~(alignment - 1ULL);
}

} // namespace

KernelBootResult KernelBootCoordinator::prepare_and_boot(
    CPU& cpu,
    ::Memory& memory,
    const KernelBootConfig& config) {

    KernelBootResult result;

    // 1. Validate inputs
    if (config.raw_kernel_image.empty()) {
        result.error_message = "Raw kernel image is empty";
        return result;
    }

    // 2. Parse Kernel Image
    loader::KernelParsedImage parsed_image;
    const auto parse_status = loader::KernelImageParser::parse(
        config.raw_kernel_image.data(),
        config.raw_kernel_image.size(),
        parsed_image);

    if (parse_status != loader::KernelImageParseStatus::Success) {
        result.error_message = "Kernel image parsing failed: " +
                               std::string(loader::kernel_image_parse_status_string(parse_status)) +
                               (parsed_image.error_message.empty() ? "" : " - " + parsed_image.error_message);
        return result;
    }

    // 3. Map Kernel Image into Guest RAM
    const auto map_res = loader::KernelImageMapper::map_image(
        memory,
        config.raw_kernel_image.data(),
        config.raw_kernel_image.size(),
        parsed_image,
        config.dram_base,
        config.dram_size);

    if (!map_res.success) {
        result.error_message = "Kernel image mapping failed: " + map_res.error_message;
        return result;
    }

    result.segments_mapped = map_res.segments_mapped;
    result.bytes_mapped = map_res.bytes_mapped;

    // 4. Compute Dynamic Footprint & Placement
    std::uint64_t lowest_va = parsed_image.segments.front().vmaddr;
    std::uint64_t highest_va = parsed_image.segments.front().vmaddr + parsed_image.segments.front().vmsize;
    for (const auto& seg : parsed_image.segments) {
        if (seg.vmaddr < lowest_va) lowest_va = seg.vmaddr;
        if (seg.vmaddr + seg.vmsize > highest_va) highest_va = seg.vmaddr + seg.vmsize;
    }
    const std::uint64_t kernel_footprint = highest_va - lowest_va;

    // Physical placement addresses
    const std::uint64_t dt_phys = align_up(config.dram_base + kernel_footprint, kGranule16K);

    // 5. Build and Map DeviceTree Blob
    device::KernelBootDeviceTreeConfig dt_cfg;
    dt_cfg.boot_args = config.command_line;
    dt_cfg.ram_base = config.dram_base;
    dt_cfg.ram_size = config.dram_size;
    dt_cfg.num_cores = config.num_cores;

    std::vector<std::uint8_t> dt_blob;
    if (config.enable_apple_devicetree) {
        dt_blob = device::KernelBootDeviceTreeBuilder::build_apple_devicetree(dt_cfg);
    } else {
        dt_blob = device::KernelBootDeviceTreeBuilder::build_fdt(dt_cfg);
    }

    const std::uint64_t dt_mem_off = (dt_phys >= config.dram_base && dt_phys < config.dram_base + memory.size())
                                         ? (dt_phys - config.dram_base)
                                         : dt_phys;

    if (dt_mem_off + dt_blob.size() > memory.size()) {
        result.error_message = "DeviceTree exceeds guest memory capacity";
        return result;
    }

    for (std::size_t i = 0; i < dt_blob.size(); ++i) {
        memory.write_u8(static_cast<std::size_t>(dt_mem_off + i), dt_blob[i]);
    }

    // 6. Build and Map AppleBootArgsRev2
    const std::uint64_t ba_phys = align_up(dt_phys + dt_blob.size(), kGranule16K);
    const std::uint64_t ba_mem_off = (ba_phys >= config.dram_base && ba_phys < config.dram_base + memory.size())
                                         ? (ba_phys - config.dram_base)
                                         : ba_phys;

    const std::uint64_t stack_base = align_up(ba_phys + 736ULL, kGranule16K);
    const std::uint64_t stack_top = stack_base + kStackSize;
    const std::uint64_t top_of_kernel_data = stack_top;

    firmware::KernelBootArgsBuilder ba_builder;
    ba_builder.set_virt_base(lowest_va)
              .set_phys_base(config.dram_base)
              .set_mem_size(config.dram_size)
              .set_top_of_kernel_data(top_of_kernel_data)
              .set_device_tree(dt_phys, static_cast<std::uint32_t>(dt_blob.size()))
              .set_command_line(config.command_line)
              .set_boot_flags(config.boot_flags);

    std::string ba_err;
    if (!ba_builder.write_to_memory(memory, ba_mem_off, &ba_err)) {
        result.error_message = "Failed to write BootArgs: " + ba_err;
        return result;
    }

    // 7. Configure and Apply CPU Boot State
    cpu::CpuBootConfig cpu_cfg;
    cpu_cfg.initial_pc = parsed_image.entry_point_vaddr;
    cpu_cfg.initial_sp = (config.custom_initial_sp != 0) ? config.custom_initial_sp : stack_top;
    cpu_cfg.boot_args_phys = ba_phys;
    cpu_cfg.exception_level = cpu::KernelBootExceptionLevel::EL1;
    cpu_cfg.vbar_el1 = config.vbar_el1;
    cpu_cfg.sctlr_el1 = config.sctlr_el1;
    cpu_cfg.tcr_el1 = config.tcr_el1;
    cpu_cfg.ttbr0_el1 = config.ttbr0_el1;
    cpu_cfg.ttbr1_el1 = config.ttbr1_el1;
    cpu_cfg.mair_el1 = config.mair_el1;
    cpu_cfg.spsr_el1 = 0x3C5; // EL1h, DAIF all masked
    cpu_cfg.set_x19_boot_args = true;

    std::string cpu_err;
    if (!cpu::Arm64CpuBootInitializer::initialize_cpu(cpu, cpu_cfg, &cpu_err)) {
        result.error_message = "CPU boot state initialization failed: " + cpu_err;
        return result;
    }

    // 8. Attach Apple AIC2 Interrupt Controller if requested
    if (config.enable_apple_aic) {
        auto aic = std::make_shared<device::AppleAIC>(
            0x41494332u, config.aic_base, config.aic_size, config.num_cores);
        cpu.attach_aic(aic);
        result.aic = aic;
    }

    // 9. Populate success outcome
    result.success = true;
    result.entry_pc = parsed_image.entry_point_vaddr;
    result.boot_args_phys = ba_phys;
    result.device_tree_phys = dt_phys;
    result.device_tree_size = static_cast<std::uint32_t>(dt_blob.size());
    result.initial_sp = cpu_cfg.initial_sp;
    result.top_of_kernel_data = top_of_kernel_data;

    return result;
}

} // namespace winios::runtime
