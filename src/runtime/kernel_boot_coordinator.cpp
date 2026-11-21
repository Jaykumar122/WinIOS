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
    // Populate and validate machine-readable physical and virtual interval maps
    loader::KernelPhysicalIntervalMap phys_map(config.dram_base, config.dram_size);
    loader::KernelVirtualIntervalMap virt_map(0x0ULL, 0xFFFFFFFFFFFFFFFFULL);

    for (const auto& seg : parsed_image.segments) {
        if (seg.vmsize == 0) continue;
        const auto itype = (seg.name == "__TEXT") ? loader::IntervalType::KernelText : loader::IntervalType::KernelData;
        
        loader::MemoryInterval p_iv{};
        p_iv.name = seg.name;
        p_iv.type = itype;
        p_iv.base = config.dram_base + (seg.vmaddr - lowest_va);
        p_iv.size = seg.vmsize;
        p_iv.is_reserved = true;
        p_iv.source_tag = "Mach-O Segment";
        std::string p_err;
        if (!phys_map.add_interval(p_iv, &p_err)) {
            result.error_message = "Physical interval conflict: " + p_err;
            return result;
        }

        loader::MemoryInterval v_iv{};
        v_iv.name = seg.name;
        v_iv.type = itype;
        v_iv.base = seg.vmaddr;
        v_iv.size = seg.vmsize;
        v_iv.prot = seg.initprot;
        // Enforce W^X: if segment is writable, maxprot cannot be executable
        v_iv.max_prot = (seg.name == "__DATA" && (seg.maxprot & 0x4u)) ? (seg.maxprot & ~0x4u) : seg.maxprot;
        v_iv.source_tag = "Mach-O Segment";
        std::string v_err;
        if (!virt_map.add_interval(v_iv, &v_err)) {
            result.error_message = "Virtual interval conflict: " + v_err;
            return result;
        }
    }

    // Add DeviceTree interval
    const std::uint64_t dt_aligned_size = align_up(static_cast<std::uint64_t>(dt_blob.size()), kGranule16K);
    loader::MemoryInterval dt_piv{"DeviceTree", loader::IntervalType::DeviceTree, dt_phys, dt_aligned_size, 0x3, 0x3, true, "Boot Metadata"};
    phys_map.add_interval(dt_piv);
    loader::MemoryInterval dt_viv{"DeviceTree", loader::IntervalType::DeviceTree, lowest_va + (dt_phys - config.dram_base), dt_aligned_size, 0x3, 0x3, true, "Boot Metadata"};
    virt_map.add_interval(dt_viv);

    // Add BootArgs interval
    loader::MemoryInterval ba_piv{"BootArgs", loader::IntervalType::BootArgs, ba_phys, kGranule16K, 0x3, 0x3, true, "Boot Metadata"};
    phys_map.add_interval(ba_piv);
    loader::MemoryInterval ba_viv{"BootArgs", loader::IntervalType::BootArgs, lowest_va + (ba_phys - config.dram_base), kGranule16K, 0x3, 0x3, true, "Boot Metadata"};
    virt_map.add_interval(ba_viv);

    // Add BootstrapStack interval
    loader::MemoryInterval stack_piv{"BootstrapStack", loader::IntervalType::BootstrapStack, stack_base, kStackSize, 0x3, 0x3, true, "Stack"};
    phys_map.add_interval(stack_piv);
    loader::MemoryInterval stack_viv{"BootstrapStack", loader::IntervalType::BootstrapStack, lowest_va + (stack_base - config.dram_base), kStackSize, 0x3, 0x3, true, "Stack"};
    virt_map.add_interval(stack_viv);

    // Add PageTables interval
    const std::uint64_t pt_base = stack_top;
    const std::uint64_t pt_size = 0x8000ULL; // 32 KB for L1+L2 bootstrap tables
    loader::MemoryInterval pt_piv{"PageTables", loader::IntervalType::PageTables, pt_base, pt_size, 0x3, 0x3, true, "MMU"};
    phys_map.add_interval(pt_piv);

    // Add DynamicPool interval (aligned at Frame 16: 0x40000 to reconcile with 496 managed pages)
    std::uint64_t pool_start = pt_base + pt_size;
    if (pool_start < 0x40000ULL) {
        pool_start = 0x40000ULL;
    }
    if (pool_start < config.dram_base + config.dram_size) {
        const std::uint64_t pool_size = (config.dram_base + config.dram_size) - pool_start;
        loader::MemoryInterval pool_piv{"DynamicPool", loader::IntervalType::DynamicPool, pool_start, pool_size, 0x3, 0x3, false, "Free Pool"};
        phys_map.add_interval(pool_piv);
    }

    result.physical_intervals = std::move(phys_map);
    result.virtual_intervals = std::move(virt_map);

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
