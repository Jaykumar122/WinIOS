#include "device/kernel_boot_devicetree.h"

#include "device/apple_devicetree.h"
#include "device/fdt_builder.h"

namespace winios::device {

std::vector<std::uint8_t> KernelBootDeviceTreeBuilder::build_apple_devicetree(
    const KernelBootDeviceTreeConfig& config) {

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPhone15,4");
    root.add_property_string("target-type", "D37AP");
    root.add_property_u32("board-id", 0x18u);
    root.add_property_u32("chip-id", 0x8120u);
    root.add_property_u32("timebase-frequency", static_cast<std::uint32_t>(config.timer_frequency));
    root.add_property_u32("#address-cells", 2u);
    root.add_property_u32("#size-cells", 2u);

    // 1. /chosen
    auto* chosen = root.add_child_node("chosen");
    chosen->add_property_string("name", "chosen");
    chosen->add_property_string("boot-args", config.boot_args);
    chosen->add_property_string("firmware-version", "WinIOS-KernelBoot-205");
    chosen->add_property_u64("dram-base", config.ram_base);
    chosen->add_property_u64("dram-size", config.ram_size);

    // 2. /memory
    auto* mem = root.add_child_node("memory");
    mem->add_property_string("name", "memory");
    mem->add_property_string("device_type", "memory");
    std::uint64_t mem_reg[2] = { config.ram_base, config.ram_size };
    mem->add_property_bytes("reg", mem_reg, sizeof(mem_reg));

    // 3. /cpus
    auto* cpus = root.add_child_node("cpus");
    cpus->add_property_string("name", "cpus");
    cpus->add_property_u32("#address-cells", 1u);
    cpus->add_property_u32("#size-cells", 0u);

    for (std::uint32_t i = 0; i < config.num_cores; ++i) {
        std::string cpu_name = "cpu" + std::to_string(i);
        auto* c = cpus->add_child_node(cpu_name);
        c->add_property_string("name", cpu_name);
        c->add_property_string("device_type", "cpu");
        c->add_property_u32("reg", i);
        c->add_property_string("state", "running");
        c->add_property_u32("timebase-frequency", static_cast<std::uint32_t>(config.timer_frequency));

        bool is_perf = (i < config.p_cores);
        c->add_property_string("cluster-type", is_perf ? "performance" : "efficiency");
        c->add_property_string("compatible", is_perf ? "apple,everest" : "apple,sawtooth");
    }

    // 4. /arm-io
    auto* arm_io = root.add_child_node("arm-io");
    arm_io->add_property_string("name", "arm-io");
    arm_io->add_property_u32("#address-cells", 2u);
    arm_io->add_property_u32("#size-cells", 2u);

    // Interrupt controller (GICv3 / AIC)
    auto* aic = arm_io->add_child_node("aic");
    aic->add_property_string("name", "aic");
    aic->add_property_string("compatible", "apple,aic2");
    aic->add_property_u32("interrupt-controller", 1u);

    // UART Console
    auto* uart = arm_io->add_child_node("uart0");
    uart->add_property_string("name", "uart0");
    uart->add_property_string("compatible", "arm,pl011");
    std::uint64_t uart_reg[2] = { config.uart_base, 0x1000ULL };
    uart->add_property_bytes("reg", uart_reg, sizeof(uart_reg));

    return AppleDeviceTreeBuilder::serialize(root);
}

std::vector<std::uint8_t> KernelBootDeviceTreeBuilder::build_fdt(
    const KernelBootDeviceTreeConfig& config) {

    FdtBuilder fdt;
    fdt.begin_node("");
    fdt.add_property_string("compatible", config.compatible);
    fdt.add_property_string("model", "Apple iPhone 15 (WinIOS Target)");
    fdt.add_property_u32("#address-cells", 2);
    fdt.add_property_u32("#size-cells", 2);

    // /chosen
    fdt.begin_node("chosen");
    fdt.add_property_string("bootargs", config.boot_args);
    fdt.add_property_string("stdout-path", "/arm-io/uart0");
    fdt.end_node();

    // /memory
    fdt.begin_node("memory@0");
    fdt.add_property_string("device_type", "memory");
    fdt.add_property_u64_array("reg", {config.ram_base, config.ram_size});
    fdt.end_node();

    // /cpus
    fdt.begin_node("cpus");
    fdt.add_property_u32("#address-cells", 1);
    fdt.add_property_u32("#size-cells", 0);

    for (std::uint32_t i = 0; i < config.num_cores; ++i) {
        std::string cpu_name = "cpu@" + std::to_string(i);
        fdt.begin_node(cpu_name);
        fdt.add_property_string("device_type", "cpu");
        fdt.add_property_string("compatible", "arm,armv8");
        fdt.add_property_u32("reg", i);
        fdt.add_property_string("enable-method", "psci");
        fdt.end_node();
    }
    fdt.end_node(); // /cpus

    fdt.end_node(); // root
    return fdt.build();
}

bool KernelBootDeviceTreeBuilder::validate_apple_devicetree(
    const std::uint8_t* data,
    std::size_t size,
    std::string* out_error) {

    if (!data || size < 8) {
        if (out_error) *out_error = "Device tree buffer too small";
        return false;
    }

    std::string dt_err;
    auto root = AppleDeviceTreeBuilder::deserialize(data, size, &dt_err);
    if (!root) {
        if (out_error) *out_error = "Failed to deserialize Apple Device Tree: " + dt_err;
        return false;
    }

    // Check mandatory root nodes
    if (!root->find_child("chosen")) {
        if (out_error) *out_error = "Missing mandatory '/chosen' node";
        return false;
    }
    if (!root->find_child("memory")) {
        if (out_error) *out_error = "Missing mandatory '/memory' node";
        return false;
    }
    if (!root->find_child("cpus")) {
        if (out_error) *out_error = "Missing mandatory '/cpus' node";
        return false;
    }

    return true;
}

} // namespace winios::device
