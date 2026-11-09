#include "device/fdt_builder.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace winios::device {

namespace {

inline std::uint32_t to_be32(std::uint32_t val) noexcept {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x000000FFu) << 24);
}

inline std::uint64_t to_be64(std::uint64_t val) noexcept {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

inline std::uint32_t from_be32(std::uint32_t val) noexcept {
    return to_be32(val);
}

inline std::uint64_t from_be64(std::uint64_t val) noexcept {
    return to_be64(val);
}

inline std::size_t align_up4(std::size_t val) noexcept {
    return (val + 3) & ~std::size_t(3);
}

void append_be32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
    std::uint32_t be = to_be32(val);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&be);
    buf.insert(buf.end(), p, p + sizeof(be));
}

void append_be64(std::vector<std::uint8_t>& buf, std::uint64_t val) {
    std::uint64_t be = to_be64(val);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&be);
    buf.insert(buf.end(), p, p + sizeof(be));
}

void append_bytes(std::vector<std::uint8_t>& buf, const void* data, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    buf.insert(buf.end(), p, p + len);
}

void append_padding4(std::vector<std::uint8_t>& buf) {
    std::size_t rem = buf.size() % 4;
    if (rem != 0) {
        buf.insert(buf.end(), 4 - rem, 0);
    }
}

void serialize_node(const FdtNode& node,
                    std::vector<std::uint8_t>& struct_block,
                    std::vector<std::uint8_t>& strings_block,
                    std::map<std::string, std::uint32_t>& string_offsets) {
    // 1. FDT_BEGIN_NODE token
    append_be32(struct_block, kFdtBeginNode);

    // 2. Node name (null-terminated), padded to 4 bytes
    append_bytes(struct_block, node.name.c_str(), node.name.size() + 1);
    append_padding4(struct_block);

    // 3. Properties
    for (const auto& prop : node.properties) {
        // String offset for property name
        std::uint32_t name_offset = 0;
        auto it = string_offsets.find(prop.name);
        if (it != string_offsets.end()) {
            name_offset = it->second;
        } else {
            name_offset = static_cast<std::uint32_t>(strings_block.size());
            string_offsets[prop.name] = name_offset;
            append_bytes(strings_block, prop.name.c_str(), prop.name.size() + 1);
        }

        // FDT_PROP token
        append_be32(struct_block, kFdtProp);
        append_be32(struct_block, static_cast<std::uint32_t>(prop.data.size()));
        append_be32(struct_block, name_offset);

        // Property data, padded to 4 bytes
        if (!prop.data.empty()) {
            append_bytes(struct_block, prop.data.data(), prop.data.size());
            append_padding4(struct_block);
        }
    }

    // 4. Children
    for (const auto& child : node.children) {
        serialize_node(*child, struct_block, strings_block, string_offsets);
    }

    // 5. FDT_END_NODE token
    append_be32(struct_block, kFdtEndNode);
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// FdtNode Implementation
// -----------------------------------------------------------------------------
FdtNode* FdtNode::find_child(std::string_view child_name) const {
    for (const auto& child : children) {
        if (child->name == child_name) {
            return child.get();
        }
    }
    return nullptr;
}

const FdtProperty* FdtNode::find_property(std::string_view prop_name) const {
    for (const auto& prop : properties) {
        if (prop.name == prop_name) {
            return &prop;
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// FdtBuilder Implementation
// -----------------------------------------------------------------------------
FdtBuilder::FdtBuilder() {
    root_ = std::make_shared<FdtNode>();
    root_->name = "";
    node_stack_.push_back(root_.get());
}

void FdtBuilder::begin_node(const std::string& name) {
    if (node_stack_.empty()) {
        throw std::runtime_error("FdtBuilder: node stack underflow");
    }

    auto child = std::make_shared<FdtNode>();
    child->name = name;
    node_stack_.back()->children.push_back(child);
    node_stack_.push_back(child.get());
}

void FdtBuilder::end_node() {
    if (node_stack_.size() <= 1) {
        throw std::runtime_error("FdtBuilder: cannot pop root node");
    }
    node_stack_.pop_back();
}

void FdtBuilder::add_property_empty(const std::string& name) {
    if (node_stack_.empty()) return;
    node_stack_.back()->properties.push_back({name, {}});
}

void FdtBuilder::add_property_u32(const std::string& name, std::uint32_t value) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data;
    append_be32(data, value);
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_u64(const std::string& name, std::uint64_t value) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data;
    append_be64(data, value);
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_u32_array(const std::string& name, const std::vector<std::uint32_t>& values) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data;
    data.reserve(values.size() * sizeof(std::uint32_t));
    for (std::uint32_t v : values) {
        append_be32(data, v);
    }
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_u64_array(const std::string& name, const std::vector<std::uint64_t>& values) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data;
    data.reserve(values.size() * sizeof(std::uint64_t));
    for (std::uint64_t v : values) {
        append_be64(data, v);
    }
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_string(const std::string& name, const std::string& value) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data(value.begin(), value.end());
    data.push_back(0); // null terminator
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_string_list(const std::string& name, const std::vector<std::string>& values) {
    if (node_stack_.empty()) return;
    std::vector<std::uint8_t> data;
    for (const auto& v : values) {
        data.insert(data.end(), v.begin(), v.end());
        data.push_back(0);
    }
    node_stack_.back()->properties.push_back({name, std::move(data)});
}

void FdtBuilder::add_property_bytes(const std::string& name, const void* data, std::size_t size) {
    if (node_stack_.empty()) return;
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> buf(p, p + size);
    node_stack_.back()->properties.push_back({name, std::move(buf)});
}

void FdtBuilder::add_mem_reserve(std::uint64_t address, std::uint64_t size) {
    mem_reserves_.push_back({address, size});
}

std::vector<std::uint8_t> FdtBuilder::build() {
    std::vector<std::uint8_t> struct_block;
    std::vector<std::uint8_t> strings_block;
    std::map<std::string, std::uint32_t> string_offsets;

    // Serialize node hierarchy starting at root
    serialize_node(*root_, struct_block, strings_block, string_offsets);
    // Terminate structure block with FDT_END
    append_be32(struct_block, kFdtEnd);

    // Compute layout offsets
    std::uint32_t header_size = static_cast<std::uint32_t>(sizeof(FdtRawHeader));
    // Memory reservation block follows header, 8-byte aligned (header is 40 bytes = 8*5)
    std::uint32_t off_mem_rsvmap = header_size;
    std::uint32_t rsv_entries = static_cast<std::uint32_t>(mem_reserves_.size() + 1); // +1 for terminator
    std::uint32_t size_mem_rsvmap = rsv_entries * static_cast<std::uint32_t>(sizeof(FdtMemReserveEntry));

    std::uint32_t off_dt_struct = off_mem_rsvmap + size_mem_rsvmap;
    std::uint32_t size_dt_struct = static_cast<std::uint32_t>(struct_block.size());

    std::uint32_t off_dt_strings = off_dt_struct + size_dt_struct;
    std::uint32_t size_dt_strings = static_cast<std::uint32_t>(strings_block.size());

    std::uint32_t total_size = static_cast<std::uint32_t>((off_dt_strings + size_dt_strings + 7) & ~std::size_t(7));

    std::vector<std::uint8_t> dtb;
    dtb.resize(total_size, 0);

    // Populate header
    auto* hdr = reinterpret_cast<FdtRawHeader*>(dtb.data());
    hdr->magic = to_be32(kFdtMagic);
    hdr->totalsize = to_be32(total_size);
    hdr->off_dt_struct = to_be32(off_dt_struct);
    hdr->off_dt_strings = to_be32(off_dt_strings);
    hdr->off_mem_rsvmap = to_be32(off_mem_rsvmap);
    hdr->version = to_be32(kFdtVersion);
    hdr->last_comp_version = to_be32(kFdtLastCompVersion);
    hdr->boot_cpuid_phys = to_be32(0);
    hdr->size_dt_strings = to_be32(size_dt_strings);
    hdr->size_dt_struct = to_be32(size_dt_struct);

    // Populate memory reservation block
    auto* rsv = reinterpret_cast<FdtMemReserveEntry*>(dtb.data() + off_mem_rsvmap);
    for (std::size_t i = 0; i < mem_reserves_.size(); ++i) {
        rsv[i].address = to_be64(mem_reserves_[i].address);
        rsv[i].size = to_be64(mem_reserves_[i].size);
    }
    // Null terminator entry
    rsv[mem_reserves_.size()].address = 0;
    rsv[mem_reserves_.size()].size = 0;

    // Copy structure block
    std::memcpy(dtb.data() + off_dt_struct, struct_block.data(), struct_block.size());

    // Copy strings block
    if (!strings_block.empty()) {
        std::memcpy(dtb.data() + off_dt_strings, strings_block.data(), strings_block.size());
    }

    return dtb;
}

std::vector<std::uint8_t> FdtBuilder::build_platform_dtb(
    const VirtualPlatformConfig& config,
    const std::string& bootargs,
    std::uint64_t initrd_start,
    std::uint64_t initrd_end) {

    FdtBuilder builder;

    // Root properties
    builder.add_property_u32("#address-cells", 2);
    builder.add_property_u32("#size-cells", 2);
    builder.add_property_string_list("compatible", {"linux,dummy-virt", "riscv-virtio"});
    builder.add_property_string("model", "WinIOS Virtual AArch64 Machine");

    // chosen node
    builder.begin_node("chosen");
    builder.add_property_string("bootargs", bootargs);
    builder.add_property_string("stdout-path", "/pl011@9000000");
    if (initrd_start > 0 && initrd_end > initrd_start) {
        builder.add_property_u64("linux,initrd-start", initrd_start);
        builder.add_property_u64("linux,initrd-end", initrd_end);
    }
    builder.end_node();

    // cpus node
    builder.begin_node("cpus");
    builder.add_property_u32("#address-cells", 1);
    builder.add_property_u32("#size-cells", 0);

    for (std::size_t c = 0; c < config.core_count; ++c) {
        std::ostringstream cpu_name;
        cpu_name << "cpu@" << c;
        builder.begin_node(cpu_name.str());
        builder.add_property_string("device_type", "cpu");
        builder.add_property_string("compatible", "arm,arm-v8");
        builder.add_property_u32("reg", static_cast<std::uint32_t>(c));
        builder.add_property_string("enable-method", "psci");
        builder.end_node();
    }
    builder.end_node();

    // psci node
    builder.begin_node("psci");
    builder.add_property_string_list("compatible", {"arm,psci-1.0", "arm,psci-0.2", "arm,psci"});
    builder.add_property_string("method", "hvc");
    builder.end_node();

    // memory node
    std::ostringstream mem_name;
    mem_name << "memory@" << std::hex << config.ram_base;
    builder.begin_node(mem_name.str());
    builder.add_property_string("device_type", "memory");
    builder.add_property_u64_array("reg", {config.ram_base, static_cast<std::uint64_t>(config.ram_size)});
    builder.end_node();

    // gic node (GICv3)
    std::ostringstream gic_name;
    gic_name << "intc@" << std::hex << config.gicd_base;
    builder.begin_node(gic_name.str());
    builder.add_property_string("compatible", "arm,gic-v3");
    builder.add_property_u32("#interrupt-cells", 3);
    builder.add_property_empty("interrupt-controller");
    // Reg: GICD (0x10000 bytes) and GICR (0x20000 bytes per core)
    std::uint64_t gicr_size = config.core_count * 0x20000ULL;
    builder.add_property_u64_array("reg", {
        config.gicd_base, 0x10000ULL,
        config.gicr_base, gicr_size
    });
    builder.end_node();

    // timer node (ARM Generic Timer)
    builder.begin_node("timer");
    builder.add_property_string("compatible", "arm,armv8-timer");
    // Interrupts: Sec Phys (PPI 13), Non-sec Phys (PPI 14), Virt (PPI 11), Hyp (PPI 10)
    // Format: <type number flags>: 1 = PPI, flags 0xf08 = level active-low for all CPUs
    builder.add_property_u32_array("interrupts", {
        1, 13, 0xf08,
        1, 14, 0xf08,
        1, 11, 0xf08,
        1, 10, 0xf08
    });
    builder.add_property_empty("always-on");
    builder.end_node();

    // pl011 UART node
    std::ostringstream uart_name;
    uart_name << "pl011@" << std::hex << config.uart_base;
    builder.begin_node(uart_name.str());
    builder.add_property_string_list("compatible", {"arm,pl011", "arm,primecell"});
    builder.add_property_u64_array("reg", {config.uart_base, 0x1000ULL});
    // Interrupt: SPI uart_irq - 32 (type 0 = SPI, num, flags 4 = level high)
    std::uint32_t spi_num = (config.uart_irq >= 32) ? (config.uart_irq - 32) : 1;
    builder.add_property_u32_array("interrupts", {0, spi_num, 4});
    builder.end_node();

    return builder.build();
}

// -----------------------------------------------------------------------------
// FdtReader Implementation
// -----------------------------------------------------------------------------
FdtReader::FdtReader(const std::vector<std::uint8_t>& dtb) {
    if (!dtb.empty()) {
        parse(dtb.data(), dtb.size());
    } else {
        error_ = "Empty DTB buffer";
    }
}

FdtReader::FdtReader(const void* data, std::size_t size) {
    if (data && size > 0) {
        parse(static_cast<const std::uint8_t*>(data), size);
    } else {
        error_ = "Null data or zero size";
    }
}

bool FdtReader::parse(const std::uint8_t* data, std::size_t size) {
    is_valid_ = false;
    error_.clear();
    root_ = nullptr;
    mem_reserves_.clear();

    if (size < sizeof(FdtRawHeader)) {
        error_ = "DTB buffer too small for header";
        return false;
    }

    const auto* hdr = reinterpret_cast<const FdtRawHeader*>(data);
    std::uint32_t magic = from_be32(hdr->magic);
    if (magic != kFdtMagic) {
        error_ = "Invalid FDT magic";
        return false;
    }

    total_size_ = from_be32(hdr->totalsize);
    if (total_size_ > size) {
        error_ = "Truncated FDT buffer";
        return false;
    }

    version_ = from_be32(hdr->version);
    boot_cpuid_ = from_be32(hdr->boot_cpuid_phys);

    std::uint32_t off_mem_rsvmap = from_be32(hdr->off_mem_rsvmap);
    std::uint32_t off_dt_struct = from_be32(hdr->off_dt_struct);
    std::uint32_t size_dt_struct = from_be32(hdr->size_dt_struct);
    std::uint32_t off_dt_strings = from_be32(hdr->off_dt_strings);
    std::uint32_t size_dt_strings = from_be32(hdr->size_dt_strings);

    if (off_mem_rsvmap + sizeof(FdtMemReserveEntry) > total_size_ ||
        off_dt_struct + size_dt_struct > total_size_ ||
        off_dt_strings + size_dt_strings > total_size_) {
        error_ = "FDT offsets extend beyond total size";
        return false;
    }

    // 1. Parse Memory Reservation Block
    std::size_t cur_rsv = off_mem_rsvmap;
    while (cur_rsv + sizeof(FdtMemReserveEntry) <= total_size_) {
        const auto* rsv = reinterpret_cast<const FdtMemReserveEntry*>(data + cur_rsv);
        std::uint64_t addr = from_be64(rsv->address);
        std::uint64_t sz = from_be64(rsv->size);
        if (addr == 0 && sz == 0) {
            break; // terminator
        }
        mem_reserves_.push_back({addr, sz});
        cur_rsv += sizeof(FdtMemReserveEntry);
    }

    // 2. Parse Structure Block
    std::size_t struct_pos = off_dt_struct;
    std::size_t struct_end = off_dt_struct + size_dt_struct;
    std::vector<std::shared_ptr<FdtNode>> stack;

    while (struct_pos + 4 <= struct_end) {
        std::uint32_t token = from_be32(*reinterpret_cast<const std::uint32_t*>(data + struct_pos));
        struct_pos += 4;

        if (token == kFdtBeginNode) {
            std::size_t str_start = struct_pos;
            while (struct_pos < struct_end && data[struct_pos] != '\0') {
                struct_pos++;
            }
            if (struct_pos >= struct_end) {
                error_ = "Unterminated node name string";
                return false;
            }
            std::string name(reinterpret_cast<const char*>(data + str_start));
            struct_pos++; // skip null terminator
            struct_pos = align_up4(struct_pos);

            auto new_node = std::make_shared<FdtNode>();
            new_node->name = name;

            if (stack.empty()) {
                root_ = new_node;
            } else {
                stack.back()->children.push_back(new_node);
            }
            stack.push_back(new_node);

        } else if (token == kFdtEndNode) {
            if (stack.empty()) {
                error_ = "FDT stack underflow on FDT_END_NODE";
                return false;
            }
            stack.pop_back();

        } else if (token == kFdtProp) {
            if (struct_pos + 8 > struct_end) {
                error_ = "Truncated FDT_PROP token";
                return false;
            }
            if (stack.empty()) {
                error_ = "FDT_PROP outside of any node";
                return false;
            }

            std::uint32_t len = from_be32(*reinterpret_cast<const std::uint32_t*>(data + struct_pos));
            std::uint32_t nameoff = from_be32(*reinterpret_cast<const std::uint32_t*>(data + struct_pos + 4));
            struct_pos += 8;

            if (nameoff >= size_dt_strings) {
                error_ = "Property name offset out of strings block";
                return false;
            }

            const char* prop_name_ptr = reinterpret_cast<const char*>(data + off_dt_strings + nameoff);
            std::string prop_name(prop_name_ptr);

            if (struct_pos + len > struct_end) {
                error_ = "Truncated property data";
                return false;
            }

            std::vector<std::uint8_t> prop_data;
            if (len > 0) {
                prop_data.assign(data + struct_pos, data + struct_pos + len);
                struct_pos += align_up4(len);
            }

            stack.back()->properties.push_back({prop_name, std::move(prop_data)});

        } else if (token == kFdtNop) {
            // No-op
            continue;
        } else if (token == kFdtEnd) {
            // Successfully reached end of structure block
            break;
        } else {
            error_ = "Unrecognized FDT token: " + std::to_string(token);
            return false;
        }
    }

    if (!root_) {
        error_ = "FDT has no root node";
        return false;
    }

    is_valid_ = true;
    return true;
}

const FdtNode* FdtReader::find_node_by_path(std::string_view path) const {
    if (!root_ || path.empty() || path[0] != '/') {
        return nullptr;
    }

    if (path == "/") {
        return root_.get();
    }

    // Split path by '/'
    const FdtNode* current = root_.get();
    std::size_t start = 1;
    while (start < path.size()) {
        std::size_t slash = path.find('/', start);
        std::string_view segment = (slash == std::string_view::npos)
                                       ? path.substr(start)
                                       : path.substr(start, slash - start);
        if (!segment.empty()) {
            current = current->find_child(segment);
            if (!current) {
                return nullptr;
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    return current;
}

const FdtProperty* FdtReader::find_property_by_path(std::string_view path, std::string_view prop_name) const {
    const FdtNode* node = find_node_by_path(path);
    if (!node) return nullptr;
    return node->find_property(prop_name);
}

std::string FdtReader::get_string_property(std::string_view path, std::string_view prop_name, const std::string& default_val) const {
    const auto* prop = find_property_by_path(path, prop_name);
    if (!prop || prop->data.empty()) return default_val;
    return std::string(reinterpret_cast<const char*>(prop->data.data()));
}

std::uint32_t FdtReader::get_u32_property(std::string_view path, std::string_view prop_name, std::uint32_t default_val) const {
    const auto* prop = find_property_by_path(path, prop_name);
    if (!prop || prop->data.size() < sizeof(std::uint32_t)) return default_val;
    return from_be32(*reinterpret_cast<const std::uint32_t*>(prop->data.data()));
}

std::uint64_t FdtReader::get_u64_property(std::string_view path, std::string_view prop_name, std::uint64_t default_val) const {
    const auto* prop = find_property_by_path(path, prop_name);
    if (!prop || prop->data.size() < sizeof(std::uint64_t)) return default_val;
    return from_be64(*reinterpret_cast<const std::uint64_t*>(prop->data.data()));
}

} // namespace winios::device
