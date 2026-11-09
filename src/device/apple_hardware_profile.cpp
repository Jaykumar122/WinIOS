#include "device/apple_hardware_profile.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::device {

AppleHardwareProfile AppleHardwareProfile::get_iphone15_profile() {
    AppleHardwareProfile p;
    p.model = "iPhone15,4";
    p.target_type = "D37AP";
    p.board_id = 0x18u;
    p.chip_id = 0x8120u; // A16 Bionic (T8120)
    p.timebase_frequency_hz = 24000000u; // 24 MHz
    p.page_size_bytes = 16384u;          // 16 KB
    p.page_shift = 14u;
    p.virtual_address_bits = 36u;
    p.standard_ram_size = 6ULL * 1024 * 1024 * 1024; // 6 GB
    p.minimum_ram_size = 256ULL * 1024 * 1024;       // 256 MB VM baseline

    // 6 CPU Cores: 2 Performance 'Everest' + 4 Efficiency 'Sawtooth'
    p.cores = {
        {"cpu0", "Everest",  true,  3460000000ULL, 0},
        {"cpu1", "Everest",  true,  3460000000ULL, 1},
        {"cpu2", "Sawtooth", false, 2020000000ULL, 2},
        {"cpu3", "Sawtooth", false, 2020000000ULL, 3},
        {"cpu4", "Sawtooth", false, 2020000000ULL, 4},
        {"cpu5", "Sawtooth", false, 2020000000ULL, 5}
    };

    // Verified hardware MMIO apertures
    p.peripherals = {
        {"aic",   "/arm-io/aic",   0x08000000ULL, 0x100000ULL, 0,  false},
        {"uart0", "/arm-io/uart0", 0x09000000ULL, 0x1000ULL,   33, true},
        {"pmgr",  "/arm-io/pmgr",  0x10000000ULL, 0x100000ULL, 0,  false},
        {"disp0", "/arm-io/disp0", 0x4F000000ULL, 1179ULL * 2556ULL * 4ULL, 0, false}
    };

    p.display.width = 1179u;
    p.display.height = 2556u;
    p.display.depth = 32u;
    p.display.row_bytes = 1179u * 4u;
    p.display.default_framebuffer_base = 0x4F000000ULL;

    return p;
}

AppleHardwareProfile AppleHardwareProfile::get_iphone14_pro_profile() {
    AppleHardwareProfile p = get_iphone15_profile();
    p.model = "iPhone15,2";
    p.target_type = "D73AP";
    p.board_id = 0x18u;
    p.chip_id = 0x8120u; // A16 Bionic (T8120)
    return p;
}

bool AppleHardwareProfile::validate_devicetree(
    const AppleDeviceTreeNode& root,
    std::string& out_error) const {

    // 1. Root properties
    const auto* model_prop = root.find_property("model");
    if (!model_prop) {
        out_error = "DeviceTree root missing 'model' property";
        return false;
    }
    const std::string model_str(reinterpret_cast<const char*>(model_prop->data.data()));
    if (model_str != model) {
        out_error = "DeviceTree model mismatch: expected '" + model + "', got '" + model_str + "'";
        return false;
    }

    const auto* target_prop = root.find_property("target-type");
    if (!target_prop) {
        out_error = "DeviceTree root missing 'target-type' property";
        return false;
    }
    const std::string target_str(reinterpret_cast<const char*>(target_prop->data.data()));
    if (target_str != target_type) {
        out_error = "DeviceTree target-type mismatch: expected '" + target_type + "', got '" + target_str + "'";
        return false;
    }

    const auto* board_prop = root.find_property("board-id");
    if (!board_prop || board_prop->data.size() < 4) {
        out_error = "DeviceTree missing or invalid 'board-id'";
        return false;
    }
    std::uint32_t bid = 0;
    std::memcpy(&bid, board_prop->data.data(), 4);
    if (bid != board_id) {
        out_error = "DeviceTree board-id mismatch";
        return false;
    }

    const auto* chip_prop = root.find_property("chip-id");
    if (!chip_prop || chip_prop->data.size() < 4) {
        out_error = "DeviceTree missing or invalid 'chip-id'";
        return false;
    }
    std::uint32_t cid = 0;
    std::memcpy(&cid, chip_prop->data.data(), 4);
    if (cid != chip_id) {
        out_error = "DeviceTree chip-id mismatch";
        return false;
    }

    const auto* tb_prop = root.find_property("timebase-frequency");
    if (!tb_prop || tb_prop->data.size() < 4) {
        out_error = "DeviceTree missing or invalid 'timebase-frequency'";
        return false;
    }
    std::uint32_t tbf = 0;
    std::memcpy(&tbf, tb_prop->data.data(), 4);
    if (tbf != timebase_frequency_hz) {
        out_error = "DeviceTree timebase-frequency mismatch";
        return false;
    }

    // 2. CPU Nodes
    const auto* cpus_node = root.find_child("cpus");
    if (!cpus_node) {
        out_error = "DeviceTree missing '/cpus' node";
        return false;
    }

    for (const auto& c : cores) {
        const auto* cnode = cpus_node->find_child(c.name);
        if (!cnode) {
            out_error = "DeviceTree /cpus missing core node: " + c.name;
            return false;
        }
        const auto* comp_prop = cnode->find_property("compatible");
        if (!comp_prop) {
            out_error = "Core " + c.name + " missing 'compatible' property";
            return false;
        }
        const std::string comp_str(reinterpret_cast<const char*>(comp_prop->data.data()));
        const std::string expected_comp = c.is_performance ? "apple,everest" : "apple,sawtooth";
        if (comp_str != expected_comp) {
            out_error = "Core " + c.name + " compatible mismatch: expected '" + expected_comp + "'";
            return false;
        }
    }

    // 3. Peripheral Nodes
    for (const auto& p : peripherals) {
        const auto* pnode = root.find_path(p.device_tree_path);
        if (!pnode) {
            out_error = "DeviceTree missing peripheral node: " + p.device_tree_path;
            return false;
        }
        const auto* reg_prop = pnode->find_property("reg");
        if (!reg_prop || reg_prop->data.size() < 16) {
            out_error = "Peripheral " + p.name + " missing or invalid 'reg' property";
            return false;
        }
        std::uint64_t reg_vals[2] = {0, 0};
        std::memcpy(reg_vals, reg_prop->data.data(), 16);
        if (reg_vals[0] != p.base_address) {
            out_error = "Peripheral " + p.name + " base address mismatch";
            return false;
        }
        if (p.has_irq) {
            const auto* irq_prop = pnode->find_property("interrupts");
            if (!irq_prop || irq_prop->data.size() < 4) {
                out_error = "Peripheral " + p.name + " missing 'interrupts' property";
                return false;
            }
            std::uint32_t irq = 0;
            std::memcpy(&irq, irq_prop->data.data(), 4);
            if (irq != p.irq_line) {
                out_error = "Peripheral " + p.name + " IRQ line mismatch";
                return false;
            }
        }
    }

    return true;
}

bool AppleHardwareProfile::validate_memory_layout(
    std::uint64_t ram_base,
    std::size_t ram_size,
    std::string& out_error) const {

    if (ram_size < minimum_ram_size) {
        out_error = "RAM size (" + std::to_string(ram_size / (1024 * 1024)) +
                    " MB) is below minimum requirement (" +
                    std::to_string(minimum_ram_size / (1024 * 1024)) + " MB)";
        return false;
    }

    if ((ram_base % page_size_bytes) != 0) {
        out_error = "RAM base address is not aligned to 16 KB page granule (0x4000)";
        return false;
    }

    if ((ram_size % page_size_bytes) != 0) {
        out_error = "RAM size is not a multiple of 16 KB page granule";
        return false;
    }

    return true;
}

std::string AppleHardwareProfile::dump_profile_summary() const {
    std::ostringstream ss;
    ss << "# Apple Silicon Hardware Profile: " << model << " (" << target_type << ")\n";
    ss << "- Chip ID: 0x" << std::hex << chip_id << " (A16 Bionic / T8120)\n";
    ss << "- Board ID: 0x" << board_id << std::dec << "\n";
    ss << "- Page Granule: " << page_size_bytes / 1024 << " KB (shift: " << page_shift << ")\n";
    ss << "- Virtual Address Space: " << virtual_address_bits << "-bit (64 GB)\n";
    ss << "- Timebase Frequency: " << timebase_frequency_hz << " Hz (24 MHz)\n";
    ss << "- CPU Topology: " << cores.size() << " Cores (2 Everest @ 3.46 GHz + 4 Sawtooth @ 2.02 GHz)\n";
    ss << "- Display Resolution: " << display.width << "x" << display.height << " @ 32 bpp\n";
    return ss.str();
}

} // namespace winios::device
