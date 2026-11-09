#include "runtime/darwin_platform_expert.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::runtime {

namespace {

std::vector<std::string> extract_compatible_tokens(const std::vector<std::uint8_t>& data) {
    std::vector<std::string> tokens;
    if (data.empty()) return tokens;

    const auto* p = reinterpret_cast<const char*>(data.data());
    std::size_t offset = 0;

    // Check if null-delimited
    while (offset < data.size()) {
        std::size_t len = 0;
        while (offset + len < data.size() && p[offset + len] != '\0') {
            len++;
        }
        if (len > 0) {
            tokens.emplace_back(p + offset, len);
        }
        offset += len + 1;
    }

    return tokens;
}

} // anonymous namespace

DarwinPlatformExpert::DarwinPlatformExpert() {
    load_default_iphone15_personalities();
}

void DarwinPlatformExpert::register_personality(const IOKitPersonality& personality) {
    catalog_.push_back(personality);
}

void DarwinPlatformExpert::clear_catalog() noexcept {
    catalog_.clear();
}

void DarwinPlatformExpert::load_default_iphone15_personalities() {
    clear_catalog();

    // 1. Apple ARM Platform Expert (Root System Platform Driver)
    IOKitPersonality pe;
    pe.io_class = "AppleARMPE";
    pe.provider_class = "IOPlatformExpertDevice";
    pe.compatible_matches = {"Apple,iPhone15,4", "Apple,t8120", "AppleARM"};
    pe.probe_score = 100000;
    register_personality(pe);

    // 2. Apple AIC 2 (Apple Interrupt Controller)
    IOKitPersonality aic;
    aic.io_class = "AppleA16InterruptController";
    aic.provider_class = "IOPlatformDevice";
    aic.name_matches = {"aic"};
    aic.compatible_matches = {"apple,aic-2", "apple,aic"};
    aic.probe_score = 50000;
    register_personality(aic);

    // 3. Apple PMGR (Power Management Controller)
    IOKitPersonality pmgr;
    pmgr.io_class = "AppleT8120PMGR";
    pmgr.provider_class = "IOPlatformDevice";
    pmgr.name_matches = {"pmgr"};
    pmgr.compatible_matches = {"apple,t8120-pmgr", "apple,pmgr"};
    pmgr.probe_score = 50000;
    register_personality(pmgr);

    // 4. Apple Display / Framebuffer (Super Retina XDR Panel)
    IOKitPersonality disp;
    disp.io_class = "AppleDCP";
    disp.provider_class = "IOPlatformDevice";
    disp.name_matches = {"disp0", "display"};
    disp.compatible_matches = {"apple,display", "apple,framebuffer"};
    disp.probe_score = 50000;
    register_personality(disp);

    // 5. Apple Serial UART (Debug Console)
    IOKitPersonality uart;
    uart.io_class = "AppleSerialUART";
    uart.provider_class = "IOPlatformDevice";
    uart.name_matches = {"uart0", "uart"};
    uart.compatible_matches = {"apple,uart-1", "arm,pl011"};
    uart.probe_score = 40000;
    register_personality(uart);

    // 6. Everest High-Performance CPU Driver
    IOKitPersonality everest;
    everest.io_class = "AppleEverestCPU";
    everest.provider_class = "IOPlatformDevice";
    everest.compatible_matches = {"apple,everest"};
    everest.property_matches = {{"device_type", "cpu"}};
    everest.probe_score = 30000;
    register_personality(everest);

    // 7. Sawtooth High-Efficiency CPU Driver
    IOKitPersonality sawtooth;
    sawtooth.io_class = "AppleSawtoothCPU";
    sawtooth.provider_class = "IOPlatformDevice";
    sawtooth.compatible_matches = {"apple,sawtooth"};
    sawtooth.property_matches = {{"device_type", "cpu"}};
    sawtooth.probe_score = 30000;
    register_personality(sawtooth);

    // 8. Ramdisk / Root Filesystem Disk Driver
    IOKitPersonality ramdisk;
    ramdisk.io_class = "AppleDiskImageRootFS";
    ramdisk.provider_class = "IOPlatformDevice";
    ramdisk.name_matches = {"RAMDisk", "memory-map"};
    ramdisk.probe_score = 60000;
    register_personality(ramdisk);

    // 9. Apple ANS2 NVMe Storage Controller
    IOKitPersonality ans2;
    ans2.io_class = "AppleANS2NVMeController";
    ans2.provider_class = "IOPlatformDevice";
    ans2.name_matches = {"ans2", "nvme"};
    ans2.compatible_matches = {"apple,ans2", "apple,nvme"};
    ans2.probe_score = 50000;
    register_personality(ans2);
}

void DarwinPlatformExpert::traverse_devicetree_node(
    const device::AppleDeviceTreeNode& node,
    const std::string& parent_path) {

    std::string current_path;
    if (parent_path.empty() || parent_path == "/") {
        current_path = node.name().empty() ? "/" : "/" + node.name();
    } else {
        current_path = parent_path + "/" + node.name();
    }

    IORegistryDevice dev;
    dev.path = current_path;
    dev.name = node.name();

    // Copy properties into registry device
    for (const auto& prop : node.properties()) {
        dev.raw_properties[prop.name] = prop.data;

        if (prop.name == "device_type" && !prop.data.empty()) {
            dev.device_type = reinterpret_cast<const char*>(prop.data.data());
        } else if (prop.name == "compatible") {
            dev.compatible_tokens = extract_compatible_tokens(prop.data);
        } else if (prop.name == "reg" && prop.data.size() >= 16) {
            std::uint64_t reg_vals[2] = {0, 0};
            std::memcpy(reg_vals, prop.data.data(), 16);
            dev.mmio_base = reg_vals[0];
            dev.mmio_size = reg_vals[1];
        } else if (prop.name == "interrupts" && prop.data.size() >= 4) {
            std::memcpy(&dev.irq_line, prop.data.data(), 4);
        } else if (prop.name == "model" && current_path == "/") {
            platform_model_ = reinterpret_cast<const char*>(prop.data.data());
        } else if (prop.name == "target-type" && current_path == "/") {
            target_type_ = reinterpret_cast<const char*>(prop.data.data());
        } else if (prop.name == "board-id" && prop.data.size() >= 4 && current_path == "/") {
            std::memcpy(&board_id_, prop.data.data(), 4);
        } else if (prop.name == "chip-id" && prop.data.size() >= 4 && current_path == "/") {
            std::memcpy(&chip_id_, prop.data.data(), 4);
        } else if (prop.name == "RAMDisk" && prop.data.size() >= 16) {
            // Also treat RAMDisk property as a virtual device in registry
            std::uint64_t rd_vals[2] = {0, 0};
            std::memcpy(rd_vals, prop.data.data(), 16);
            dev.mmio_base = rd_vals[0];
            dev.mmio_size = rd_vals[1];
        }
    }

    registry_.push_back(std::move(dev));

    // Recursively process children
    for (const auto& child : node.children()) {
        if (child) {
            traverse_devicetree_node(*child, current_path);
        }
    }
}

std::shared_ptr<IOKitMatchedDriver> DarwinPlatformExpert::match_device(
    const IORegistryDevice& dev) {

    const bool is_root = (dev.path == "/");
    const std::string expected_provider = is_root ? "IOPlatformExpertDevice" : "IOPlatformDevice";

    const IOKitPersonality* best_personality = nullptr;
    std::int32_t highest_score = -1;

    for (const auto& p : catalog_) {
        if (p.provider_class != expected_provider) {
            continue;
        }

        bool matched = false;
        std::int32_t current_score = p.probe_score;

        // 1. Name match
        if (!p.name_matches.empty()) {
            for (const auto& nm : p.name_matches) {
                if (dev.name == nm || dev.path.ends_with("/" + nm) || dev.raw_properties.count(nm) > 0) {
                    matched = true;
                    current_score += 2000;
                    break;
                }
            }
        }

        // 2. Compatible tokens match
        if (!p.compatible_matches.empty() && !dev.compatible_tokens.empty()) {
            for (const auto& cm : p.compatible_matches) {
                for (const auto& ct : dev.compatible_tokens) {
                    if (ct == cm) {
                        matched = true;
                        current_score += 5000;
                        break;
                    }
                }
            }
        }

        // 3. Property constraints match
        if (!p.property_matches.empty()) {
            bool all_props_match = true;
            for (const auto& [req_prop, req_val] : p.property_matches) {
                auto it = dev.raw_properties.find(req_prop);
                if (it == dev.raw_properties.end()) {
                    all_props_match = false;
                    break;
                }
                const std::string actual_str(reinterpret_cast<const char*>(it->second.data()));
                if (actual_str != req_val) {
                    all_props_match = false;
                    break;
                }
            }
            if (all_props_match) {
                matched = true;
                current_score += 1000;
            } else {
                matched = false; // Disqualified if explicit property constraint failed
            }
        }

        if (matched && current_score > highest_score) {
            highest_score = current_score;
            best_personality = &p;
        }
    }

    if (!best_personality) {
        return nullptr;
    }

    auto driver = std::make_shared<IOKitMatchedDriver>();
    driver->personality = *best_personality;
    driver->device_path = dev.path;
    driver->device_name = dev.name;
    driver->mmio_base = dev.mmio_base;
    driver->mmio_size = dev.mmio_size;
    driver->irq_line = dev.irq_line;
    driver->final_score = highest_score;
    driver->state = IOKitDriverState::Active; // Probe & start transition
    driver->status_info = "Driver " + best_personality->io_class + " successfully matched on " + dev.path;

    return driver;
}

bool DarwinPlatformExpert::probe_and_match(
    const device::AppleDeviceTreeNode& root_node,
    std::string& out_error) {

    registry_.clear();
    matched_drivers_.clear();
    platform_model_.clear();
    target_type_.clear();
    board_id_ = 0;
    chip_id_ = 0;

    // 1. Traverse DeviceTree and construct IORegistry tree
    traverse_devicetree_node(root_node, "");

    if (registry_.empty()) {
        out_error = "DeviceTree traversal resulted in empty IORegistry";
        return false;
    }

    // 2. Perform driver matching on all registry devices
    for (auto& dev : registry_) {
        auto driver = match_device(dev);
        if (driver) {
            dev.matched_driver = driver;
            matched_drivers_.push_back(*driver);
        }
    }

    // 3. Verify that platform root driver matched
    if (!find_driver_by_class("AppleARMPE")) {
        out_error = "Failed to match AppleARMPE platform expert to root DeviceTree node";
        return false;
    }

    return true;
}

bool DarwinPlatformExpert::probe_and_match_adts(
    const void* adts_data,
    std::size_t size,
    std::string& out_error) {

    auto root = device::AppleDeviceTreeBuilder::deserialize(adts_data, size, &out_error);
    if (!root) {
        return false;
    }

    return probe_and_match(*root, out_error);
}

const IOKitMatchedDriver* DarwinPlatformExpert::find_driver_by_class(std::string_view io_class) const noexcept {
    for (const auto& d : matched_drivers_) {
        if (d.personality.io_class == io_class) {
            return &d;
        }
    }
    return nullptr;
}

const IOKitMatchedDriver* DarwinPlatformExpert::find_driver_by_path(std::string_view device_path) const noexcept {
    for (const auto& d : matched_drivers_) {
        if (d.device_path == device_path) {
            return &d;
        }
    }
    return nullptr;
}

const IORegistryDevice* DarwinPlatformExpert::find_device_by_path(std::string_view path) const noexcept {
    for (const auto& dev : registry_) {
        if (dev.path == path) {
            return &dev;
        }
    }
    return nullptr;
}

bool DarwinPlatformExpert::verify_platform_readiness(std::string& out_diagnostic) const {
    std::ostringstream ss;
    bool all_passed = true;

    ss << "=== Apple Silicon Platform Probing Diagnostic Report ===\n";
    ss << "Model: " << (platform_model_.empty() ? "Unknown" : platform_model_) << "\n";
    ss << "Target Type: " << (target_type_.empty() ? "Unknown" : target_type_) << "\n";
    ss << "Board ID: 0x" << std::hex << board_id_ << ", Chip ID: 0x" << chip_id_ << std::dec << "\n\n";

    // 1. AppleARMPE
    const auto* pe = find_driver_by_class("AppleARMPE");
    if (pe && pe->state == IOKitDriverState::Active) {
        ss << "[PASS] Platform Expert: AppleARMPE active on root node (score: " << pe->final_score << ")\n";
    } else {
        ss << "[FAIL] Platform Expert: AppleARMPE not active\n";
        all_passed = false;
    }

    // 2. AIC 2
    const auto* aic = find_driver_by_class("AppleA16InterruptController");
    if (aic && aic->state == IOKitDriverState::Active && aic->mmio_base == 0x08000000ULL) {
        ss << "[PASS] Interrupt Controller: AppleA16InterruptController active at 0x"
           << std::hex << aic->mmio_base << " (size: 0x" << aic->mmio_size << std::dec << ")\n";
    } else {
        ss << "[FAIL] Interrupt Controller: AppleA16InterruptController missing or misconfigured\n";
        all_passed = false;
    }

    // 3. PMGR
    const auto* pmgr = find_driver_by_class("AppleT8120PMGR");
    if (pmgr && pmgr->state == IOKitDriverState::Active && pmgr->mmio_base == 0x10000000ULL) {
        ss << "[PASS] Power Manager: AppleT8120PMGR active at 0x"
           << std::hex << pmgr->mmio_base << " (size: 0x" << pmgr->mmio_size << std::dec << ")\n";
    } else {
        ss << "[FAIL] Power Manager: AppleT8120PMGR missing or misconfigured\n";
        all_passed = false;
    }

    // 4. Display / DCP
    const auto* dcp = find_driver_by_class("AppleDCP");
    if (dcp && dcp->state == IOKitDriverState::Active) {
        ss << "[PASS] Display Controller: AppleDCP active at 0x"
           << std::hex << dcp->mmio_base << " (framebuffer size: 0x" << dcp->mmio_size << std::dec << ")\n";
    } else {
        ss << "[FAIL] Display Controller: AppleDCP missing or misconfigured\n";
        all_passed = false;
    }

    // 5. Serial UART
    const auto* uart = find_driver_by_class("AppleSerialUART");
    if (uart && uart->state == IOKitDriverState::Active && uart->mmio_base == 0x09000000ULL) {
        ss << "[PASS] Console UART: AppleSerialUART active at 0x"
           << std::hex << uart->mmio_base << " (IRQ: " << uart->irq_line << ")\n";
    } else {
        ss << "[FAIL] Console UART: AppleSerialUART missing or misconfigured\n";
        all_passed = false;
    }

    // 6. CPU Cores
    std::size_t everest_count = 0;
    std::size_t sawtooth_count = 0;
    for (const auto& d : matched_drivers_) {
        if (d.personality.io_class == "AppleEverestCPU") everest_count++;
        else if (d.personality.io_class == "AppleSawtoothCPU") sawtooth_count++;
    }

    if (everest_count == 2 && sawtooth_count == 4) {
        ss << "[PASS] CPU Topology: 6 cores verified (2 Everest Performance + 4 Sawtooth Efficiency)\n";
    } else {
        ss << "[FAIL] CPU Topology mismatch: " << everest_count << " Everest, " << sawtooth_count << " Sawtooth\n";
        all_passed = false;
    }

    // 7. Optional RootFS
    const auto* rootfs = find_driver_by_class("AppleDiskImageRootFS");
    if (rootfs) {
        ss << "[INFO] Root Filesystem: AppleDiskImageRootFS attached at 0x"
           << std::hex << rootfs->mmio_base << " (size: 0x" << rootfs->mmio_size << std::dec << " bytes)\n";
    }

    out_diagnostic = ss.str();
    return all_passed;
}

} // namespace winios::runtime
