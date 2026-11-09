#include "device/apple_devicetree.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleDeviceTreeNode Implementation
// -----------------------------------------------------------------------------
AppleDeviceTreeNode::AppleDeviceTreeNode(std::string name)
    : name_(std::move(name)) {}

void AppleDeviceTreeNode::add_property_empty(const std::string& prop_name) {
    AppleDeviceTreeProperty prop;
    prop.name = prop_name;
    properties_.push_back(std::move(prop));
}

void AppleDeviceTreeNode::add_property_u32(const std::string& prop_name, std::uint32_t val) {
    AppleDeviceTreeProperty prop;
    prop.name = prop_name;
    prop.data.resize(4);
    std::memcpy(prop.data.data(), &val, 4);
    properties_.push_back(std::move(prop));
}

void AppleDeviceTreeNode::add_property_u64(const std::string& prop_name, std::uint64_t val) {
    AppleDeviceTreeProperty prop;
    prop.name = prop_name;
    prop.data.resize(8);
    std::memcpy(prop.data.data(), &val, 8);
    properties_.push_back(std::move(prop));
}

void AppleDeviceTreeNode::add_property_string(const std::string& prop_name, const std::string& str) {
    AppleDeviceTreeProperty prop;
    prop.name = prop_name;
    prop.data.assign(str.begin(), str.end());
    prop.data.push_back('\0'); // Null terminator required by Darwin
    properties_.push_back(std::move(prop));
}

void AppleDeviceTreeNode::add_property_bytes(const std::string& prop_name, const void* data, std::size_t size) {
    AppleDeviceTreeProperty prop;
    prop.name = prop_name;
    if (data && size > 0) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        prop.data.assign(p, p + size);
    }
    properties_.push_back(std::move(prop));
}

const AppleDeviceTreeProperty* AppleDeviceTreeNode::find_property(std::string_view prop_name) const noexcept {
    for (const auto& prop : properties_) {
        if (prop.name == prop_name) {
            return &prop;
        }
    }
    return nullptr;
}

AppleDeviceTreeNode* AppleDeviceTreeNode::add_child(std::unique_ptr<AppleDeviceTreeNode> child) {
    if (!child) return nullptr;
    auto* raw = child.get();
    children_.push_back(std::move(child));
    return raw;
}

AppleDeviceTreeNode* AppleDeviceTreeNode::add_child_node(std::string child_name) {
    auto node = std::make_unique<AppleDeviceTreeNode>(std::move(child_name));
    auto* raw = node.get();
    children_.push_back(std::move(node));
    return raw;
}

AppleDeviceTreeNode* AppleDeviceTreeNode::find_child(std::string_view child_name) const noexcept {
    for (const auto& child : children_) {
        if (child->name() == child_name) {
            return child.get();
        }
    }
    return nullptr;
}

const AppleDeviceTreeNode* AppleDeviceTreeNode::find_path(std::string_view path) const noexcept {
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    if (path.empty()) {
        return this;
    }

    std::size_t slash = path.find('/');
    std::string_view head = (slash == std::string_view::npos) ? path : path.substr(0, slash);
    std::string_view tail = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);

    const auto* child = find_child(head);
    if (!child) {
        return nullptr;
    }
    if (tail.empty()) {
        return child;
    }
    return child->find_path(tail);
}

// -----------------------------------------------------------------------------
// Binary Serialization & Deserialization Helpers
// -----------------------------------------------------------------------------
namespace {

void serialize_node(const AppleDeviceTreeNode& node, std::vector<std::uint8_t>& buf) {
    // 1. Entry Header
    AppleDtEntryHeader hdr;
    hdr.nProperties = static_cast<std::uint32_t>(node.properties().size());
    hdr.nChildren = static_cast<std::uint32_t>(node.children().size());

    const auto* hdr_bytes = reinterpret_cast<const std::uint8_t*>(&hdr);
    buf.insert(buf.end(), hdr_bytes, hdr_bytes + sizeof(hdr));

    // 2. Properties
    for (const auto& prop : node.properties()) {
        AppleDtPropertyHeader prop_hdr{};
        const std::size_t copy_name = std::min(prop.name.size(), kAppleDtPropertyMaxName - 1);
        std::memcpy(prop_hdr.name, prop.name.data(), copy_name);
        prop_hdr.name[copy_name] = '\0';
        prop_hdr.length = static_cast<std::uint32_t>(prop.data.size());

        const auto* p_hdr = reinterpret_cast<const std::uint8_t*>(&prop_hdr);
        buf.insert(buf.end(), p_hdr, p_hdr + sizeof(prop_hdr));

        // Data payload
        if (!prop.data.empty()) {
            buf.insert(buf.end(), prop.data.begin(), prop.data.end());
        }

        // Align to 4 bytes
        const std::size_t pad = (4 - (prop.data.size() % 4)) % 4;
        for (std::size_t i = 0; i < pad; ++i) {
            buf.push_back(0);
        }
    }

    // 3. Children (Recursive)
    for (const auto& child : node.children()) {
        serialize_node(*child, buf);
    }
}

std::unique_ptr<AppleDeviceTreeNode> deserialize_node(
    const std::uint8_t*& p,
    const std::uint8_t* end,
    std::string* out_error) {

    if (p + sizeof(AppleDtEntryHeader) > end) {
        if (out_error) *out_error = "Truncated Apple DeviceTree entry header";
        return nullptr;
    }

    AppleDtEntryHeader hdr;
    std::memcpy(&hdr, p, sizeof(hdr));
    p += sizeof(hdr);

    auto node = std::make_unique<AppleDeviceTreeNode>();

    // 1. Read Properties
    for (std::uint32_t i = 0; i < hdr.nProperties; ++i) {
        if (p + sizeof(AppleDtPropertyHeader) > end) {
            if (out_error) *out_error = "Truncated Apple DeviceTree property header";
            return nullptr;
        }

        AppleDtPropertyHeader prop_hdr;
        std::memcpy(&prop_hdr, p, sizeof(prop_hdr));
        p += sizeof(prop_hdr);

        char name_buf[kAppleDtPropertyMaxName + 1] = {0};
        std::memcpy(name_buf, prop_hdr.name, kAppleDtPropertyMaxName);
        std::string prop_name(name_buf);

        if (p + prop_hdr.length > end) {
            if (out_error) *out_error = "Property '" + prop_name + "' data exceeds stream";
            return nullptr;
        }

        const std::size_t pad = (4 - (prop_hdr.length % 4)) % 4;
        const std::size_t total_prop_bytes = prop_hdr.length + pad;
        if (p + total_prop_bytes > end) {
            if (out_error) *out_error = "Property '" + prop_name + "' padding exceeds stream";
            return nullptr;
        }

        node->add_property_bytes(prop_name, p, prop_hdr.length);
        p += total_prop_bytes;

        // Propagate node name from "name" property
        if (prop_name == "name" && prop_hdr.length > 0) {
            std::string nname(reinterpret_cast<const char*>(node->find_property("name")->data.data()));
            node->set_name(nname);
        }
    }

    // 2. Read Children (Recursive)
    for (std::uint32_t c = 0; c < hdr.nChildren; ++c) {
        auto child = deserialize_node(p, end, out_error);
        if (!child) {
            return nullptr;
        }
        node->add_child(std::move(child));
    }

    return node;
}

} // anonymous namespace

std::vector<std::uint8_t> AppleDeviceTreeBuilder::serialize(const AppleDeviceTreeNode& root) {
    std::vector<std::uint8_t> buf;
    buf.reserve(8192);
    serialize_node(root, buf);
    return buf;
}

std::unique_ptr<AppleDeviceTreeNode> AppleDeviceTreeBuilder::deserialize(
    const void* data,
    std::size_t size,
    std::string* out_error) {

    if (!data || size < sizeof(AppleDtEntryHeader)) {
        if (out_error) *out_error = "DeviceTree payload too small or null";
        return nullptr;
    }

    const auto* p = static_cast<const std::uint8_t*>(data);
    const auto* end = p + size;

    return deserialize_node(p, end, out_error);
}

std::vector<std::uint8_t> AppleDeviceTreeBuilder::create_iphone15_devicetree(
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    const std::string& boot_args,
    std::uint64_t framebuffer_base,
    std::uint64_t ramdisk_base,
    std::uint64_t ramdisk_size) {

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPhone15,4");
    
    // Multi-string compatible: "Apple,iPhone15,4\0Apple,t8120\0AppleARM\0"
    const char comp[] = "Apple,iPhone15,4\0Apple,t8120\0AppleARM";
    root.add_property_bytes("compatible", comp, sizeof(comp));

    root.add_property_string("target-type", "D37AP");
    root.add_property_u32("board-id", 0x18u);
    root.add_property_u32("chip-id", 0x8120u); // A16 Bionic (T8120)
    root.add_property_u32("timebase-frequency", 24000000u); // 24 MHz
    root.add_property_u32("#address-cells", 2u);
    root.add_property_u32("#size-cells", 2u);

    // 1. /chosen
    auto* chosen = root.add_child_node("chosen");
    chosen->add_property_string("name", "chosen");
    chosen->add_property_string("boot-args", boot_args);
    chosen->add_property_string("firmware-version", "WinIOS-1.0-A16");

    if (ramdisk_size > 0) {
        chosen->add_property_u64("ramdisk-base", ramdisk_base);
        chosen->add_property_u64("ramdisk-size", ramdisk_size);

        auto* memory_map = chosen->add_child_node("memory-map");
        memory_map->add_property_string("name", "memory-map");
        std::uint64_t ramdisk_range[2] = { ramdisk_base, ramdisk_size };
        memory_map->add_property_bytes("RAMDisk", ramdisk_range, sizeof(ramdisk_range));
    }

    // 2. /memory
    auto* mem = root.add_child_node("memory");
    mem->add_property_string("name", "memory");
    mem->add_property_string("device_type", "memory");
    std::uint64_t mem_reg[2] = { ram_base, ram_size };
    mem->add_property_bytes("reg", mem_reg, sizeof(mem_reg));

    // 3. /cpus (A16 Bionic 6-core: 2 Performance 'Everest' + 4 Efficiency 'Sawtooth')
    auto* cpus = root.add_child_node("cpus");
    cpus->add_property_string("name", "cpus");
    cpus->add_property_u32("#address-cells", 1u);
    cpus->add_property_u32("#size-cells", 0u);

    const char* cpu_names[6] = {"cpu0", "cpu1", "cpu2", "cpu3", "cpu4", "cpu5"};
    const bool is_perf[6]    = {true,   true,   false,  false,  false,  false};

    for (std::uint32_t i = 0; i < 6; ++i) {
        auto* c = cpus->add_child_node(cpu_names[i]);
        c->add_property_string("name", cpu_names[i]);
        c->add_property_string("device_type", "cpu");
        c->add_property_u32("reg", i);
        c->add_property_string("state", (i == 0) ? "running" : "idle");
        c->add_property_string("compatible", is_perf[i] ? "apple,everest" : "apple,sawtooth");
        c->add_property_u64("clock-frequency", is_perf[i] ? 3460000000ULL : 2020000000ULL);
    }

    // 4. /arm-io (Apple Silicon Peripherals)
    auto* armio = root.add_child_node("arm-io");
    armio->add_property_string("name", "arm-io");
    armio->add_property_string("compatible", "apple,arm-io");
    armio->add_property_u32("#address-cells", 2u);
    armio->add_property_u32("#size-cells", 2u);

    // 4.1 AIC (Apple Interrupt Controller)
    auto* aic = armio->add_child_node("aic");
    aic->add_property_string("name", "aic");
    const char aic_comp[] = "apple,aic-2\0apple,aic";
    aic->add_property_bytes("compatible", aic_comp, sizeof(aic_comp));
    std::uint64_t aic_reg[2] = { 0x08000000ULL, 0x100000ULL };
    aic->add_property_bytes("reg", aic_reg, sizeof(aic_reg));
    aic->add_property_empty("interrupt-controller");
    aic->add_property_u32("#interrupt-cells", 3u);

    // 4.2 UART0 (Serial Console)
    auto* uart = armio->add_child_node("uart0");
    uart->add_property_string("name", "uart0");
    const char uart_comp[] = "apple,uart-1\0arm,pl011";
    uart->add_property_bytes("compatible", uart_comp, sizeof(uart_comp));
    std::uint64_t uart_reg[2] = { 0x09000000ULL, 0x1000ULL };
    uart->add_property_bytes("reg", uart_reg, sizeof(uart_reg));
    uart->add_property_u32("interrupts", 33u);

    // 4.3 PMGR (Power Management)
    auto* pmgr = armio->add_child_node("pmgr");
    pmgr->add_property_string("name", "pmgr");
    const char pmgr_comp[] = "apple,t8120-pmgr\0apple,pmgr";
    pmgr->add_property_bytes("compatible", pmgr_comp, sizeof(pmgr_comp));
    std::uint64_t pmgr_reg[2] = { 0x10000000ULL, 0x100000ULL };
    pmgr->add_property_bytes("reg", pmgr_reg, sizeof(pmgr_reg));

    // 4.4 Display / Framebuffer (iPhone 15: 1179 x 2556, 32-bit depth)
    auto* disp = armio->add_child_node("disp0");
    disp->add_property_string("name", "disp0");
    const char disp_comp[] = "apple,display\0apple,framebuffer";
    disp->add_property_bytes("compatible", disp_comp, sizeof(disp_comp));
    std::uint64_t disp_reg[2] = { framebuffer_base, 1179ULL * 2556ULL * 4ULL };
    disp->add_property_bytes("reg", disp_reg, sizeof(disp_reg));
    disp->add_property_u32("width", 1179u);
    disp->add_property_u32("height", 2556u);
    disp->add_property_u32("depth", 32u);
    disp->add_property_u32("rowbytes", 1179u * 4u);

    // 4.5 ANS2 (Apple NAND Subsystem 2 / NVMe Storage)
    auto* ans2 = armio->add_child_node("ans2");
    ans2->add_property_string("name", "ans2");
    const char ans2_comp[] = "apple,ans2\0apple,nvme";
    ans2->add_property_bytes("compatible", ans2_comp, sizeof(ans2_comp));
    std::uint64_t ans2_reg[2] = { 0x0C000000ULL, 0x10000ULL };
    ans2->add_property_bytes("reg", ans2_reg, sizeof(ans2_reg));
    ans2->add_property_u32("interrupts", 48u);

    return serialize(root);
}

std::vector<std::uint8_t> AppleDeviceTreeBuilder::create_iphone14_pro_devicetree(
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    const std::string& boot_args,
    std::uint64_t framebuffer_base,
    std::uint64_t ramdisk_base,
    std::uint64_t ramdisk_size) {

    // Same base topology as iPhone 15, but identity is iPhone15,2 / D73AP
    auto dtb = create_iphone15_devicetree(ram_base, ram_size, boot_args, framebuffer_base, ramdisk_base, ramdisk_size);
    auto root = deserialize(dtb.data(), dtb.size());
    if (root) {
        // Mutate identity to iPhone 14 Pro
        for (auto& prop : const_cast<std::vector<AppleDeviceTreeProperty>&>(root->properties())) {
            if (prop.name == "model") {
                prop.data = {'i', 'P', 'h', 'o', 'n', 'e', '1', '5', ',', '2', '\0'};
            } else if (prop.name == "target-type") {
                prop.data = {'D', '7', '3', 'A', 'P', '\0'};
            }
        }
        return serialize(*root);
    }
    return dtb;
}

} // namespace winios::device
