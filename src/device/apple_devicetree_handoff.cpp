#include "device/apple_devicetree_handoff.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "loader/macho_loader.h"

namespace winios::device {

namespace {

constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) noexcept {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_b(std::int32_t word_offset) noexcept {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_b_cond(unsigned cond, std::int32_t word_offset) noexcept {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (cond & 0xFu);
}

constexpr std::uint32_t enc_cbz_x(unsigned rt, std::int32_t word_offset) noexcept {
    return 0xB4000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_w_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xB9400000u | (((byte_offset >> 2) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_x_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xF9400000u | (((byte_offset >> 3) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_cmp_imm_w(unsigned rn, std::uint16_t imm12) noexcept {
    return 0x7100001Fu | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_add_imm_x(unsigned rd, unsigned rn, std::uint16_t imm12) noexcept {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_str_w_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xB9000000u | (((byte_offset >> 2) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str_x_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xF9000000u | (((byte_offset >> 3) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_cbz_w(unsigned rt, std::int32_t word_offset) noexcept {
    return 0x34000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm_w(unsigned rd, unsigned rn, std::uint16_t imm12) noexcept {
    return 0x11000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_sub_imm_w(unsigned rd, unsigned rn, std::uint16_t imm12) noexcept {
    return 0x51000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_sub_imm_x(unsigned rd, unsigned rn, std::uint16_t imm12) noexcept {
    return 0xD1000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_cmp_reg_x(unsigned rn, unsigned rm) noexcept {
    return 0xEB00001Fu | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_cmp_reg_w(unsigned rn, unsigned rm) noexcept {
    return 0x6B00001Fu | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u);
}

constexpr std::uint32_t enc_add_reg_x(unsigned rd, unsigned rn, unsigned rm) noexcept {
    return 0x8B000000u | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_ubfm_w(unsigned rd, unsigned rn, unsigned immr, unsigned imms) noexcept {
    return 0x53000000u | ((immr & 0x1Fu) << 16u) | ((imms & 0x1Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_bl(std::int32_t word_offset) noexcept {
    return 0x94000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_ret() noexcept {
    return 0xD65F03C0u;
}

static bool validate_staged_node(
    const Memory& mem,
    std::uint64_t dtb_base,
    std::size_t& curr_offset,
    std::size_t dtb_len,
    std::string* out_error) {

    if (curr_offset + sizeof(AppleDtEntryHeader) > dtb_len) {
        if (out_error) *out_error = "DeviceTree entry header truncated at offset " + std::to_string(curr_offset);
        return false;
    }

    const std::uint32_t nprops = mem.read_u32(static_cast<std::size_t>(dtb_base + curr_offset));
    const std::uint32_t nchildren = mem.read_u32(static_cast<std::size_t>(dtb_base + curr_offset + 4));
    curr_offset += sizeof(AppleDtEntryHeader);

    if (nprops > 1024) {
        if (out_error) *out_error = "Excessive property count in node: " + std::to_string(nprops);
        return false;
    }

    for (std::uint32_t p = 0; p < nprops; ++p) {
        if (curr_offset + sizeof(AppleDtPropertyHeader) > dtb_len) {
            if (out_error) *out_error = "DeviceTree property header truncated at offset " + std::to_string(curr_offset);
            return false;
        }

        char name[kAppleDtPropertyMaxName + 1] = {0};
        for (std::size_t i = 0; i < kAppleDtPropertyMaxName; ++i) {
            name[i] = static_cast<char>(mem.read_u8(static_cast<std::size_t>(dtb_base + curr_offset + i)));
        }
        const std::uint32_t prop_len = mem.read_u32(static_cast<std::size_t>(dtb_base + curr_offset + kAppleDtPropertyMaxName));
        curr_offset += sizeof(AppleDtPropertyHeader);

        const std::size_t pad = (4 - (prop_len % 4)) % 4;
        const std::size_t total_payload = prop_len + pad;

        if (curr_offset + total_payload > dtb_len) {
            if (out_error) *out_error = "Property '" + std::string(name) + "' payload exceeds DTB size";
            return false;
        }
        curr_offset += total_payload;
    }

    if (nchildren > 1024) {
        if (out_error) *out_error = "Excessive child count in node: " + std::to_string(nchildren);
        return false;
    }

    for (std::uint32_t c = 0; c < nchildren; ++c) {
        if (!validate_staged_node(mem, dtb_base, curr_offset, dtb_len, out_error)) {
            return false;
        }
    }

    return true;
}

} // anonymous namespace

bool AppleDeviceTreeHandoffManager::validate_staging_contract(
    const AppleDeviceTreeStagingContract& contract,
    std::string* out_error) {

    if (contract.dtb_phys == 0 || contract.dtb_len == 0) {
        if (out_error) *out_error = "DeviceTree physical address or length is zero";
        return false;
    }
    if (contract.dtb_len < sizeof(AppleDtEntryHeader)) {
        if (out_error) *out_error = "DeviceTree length smaller than minimum header (8 bytes)";
        return false;
    }
    // Apple Silicon natural alignment: 8-byte boundary
    if ((contract.dtb_phys & 0x7ULL) != 0) {
        if (out_error) *out_error = "DeviceTree physical address is not 8-byte aligned";
        return false;
    }
    // Integer overflow protection
    if (contract.dtb_phys + contract.dtb_len < contract.dtb_phys) {
        if (out_error) *out_error = "Integer overflow in DeviceTree physical memory range";
        return false;
    }
    // RAM boundary check
    if (contract.dtb_phys < contract.ram_base ||
        (contract.dtb_phys + contract.dtb_len) > (contract.ram_base + contract.ram_size)) {
        if (out_error) *out_error = "DeviceTree physical memory range lies outside backed RAM boundary";
        return false;
    }
    // Overlap checks
    if (contract.kernel_phys_top > contract.kernel_phys_start) {
        if (!(contract.dtb_phys + contract.dtb_len <= contract.kernel_phys_start ||
              contract.dtb_phys >= contract.kernel_phys_top)) {
            if (out_error) *out_error = "DeviceTree memory region overlaps with kernel load area";
            return false;
        }
    }
    if (contract.boot_args_phys != 0 && contract.boot_args_size > 0) {
        if (!(contract.dtb_phys + contract.dtb_len <= contract.boot_args_phys ||
              contract.dtb_phys >= contract.boot_args_phys + contract.boot_args_size)) {
            if (out_error) *out_error = "DeviceTree memory region overlaps with struct boot_args";
            return false;
        }
    }
    if (contract.framebuffer_phys != 0 && contract.framebuffer_size > 0) {
        if (!(contract.dtb_phys + contract.dtb_len <= contract.framebuffer_phys ||
              contract.dtb_phys >= contract.framebuffer_phys + contract.framebuffer_size)) {
            if (out_error) *out_error = "DeviceTree memory region overlaps with display framebuffer";
            return false;
        }
    }

    return true;
}

bool AppleDeviceTreeHandoffManager::validate_staged_memory(
    const Memory& mem,
    std::uint64_t dtb_phys,
    std::uint32_t dtb_len,
    std::string* out_error) {

    if (dtb_phys + dtb_len > mem.size()) {
        if (out_error) *out_error = "DeviceTree extent exceeds guest physical Memory buffer size";
        return false;
    }
    if (dtb_len < sizeof(AppleDtEntryHeader)) {
        if (out_error) *out_error = "DeviceTree length smaller than minimum header size";
        return false;
    }

    std::size_t offset = 0;
    return validate_staged_node(mem, dtb_phys, offset, dtb_len, out_error);
}

bool AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(
    const AppleDeviceTreeNode& root,
    std::string* out_error) {

    // 1. Root properties
    const auto* prop_model = root.find_property("model");
    if (!prop_model || prop_model->data.empty()) {
        if (out_error) *out_error = "Missing required root property 'model'";
        return false;
    }
    const std::string model_str(reinterpret_cast<const char*>(prop_model->data.data()));
    if (model_str != "iPhone15,4") {
        if (out_error) *out_error = "Root property 'model' does not match iPhone 15 (iPhone15,4): " + model_str;
        return false;
    }

    const auto* prop_target = root.find_property("target-type");
    if (!prop_target || prop_target->data.empty()) {
        if (out_error) *out_error = "Missing required root property 'target-type'";
        return false;
    }
    const std::string target_str(reinterpret_cast<const char*>(prop_target->data.data()));
    if (target_str != "D37AP") {
        if (out_error) *out_error = "Root property 'target-type' does not match D37AP: " + target_str;
        return false;
    }

    const auto* prop_chip = root.find_property("chip-id");
    if (!prop_chip || prop_chip->data.size() < 4) {
        if (out_error) *out_error = "Missing or invalid root property 'chip-id'";
        return false;
    }
    const std::uint32_t chip_id = *reinterpret_cast<const std::uint32_t*>(prop_chip->data.data());
    if (chip_id != 0x8120u) {
        if (out_error) *out_error = "Root property 'chip-id' does not match A16 Bionic (0x8120)";
        return false;
    }

    // 2. /chosen node
    const auto* chosen = root.find_path("/chosen");
    if (!chosen) {
        if (out_error) *out_error = "Missing required node '/chosen'";
        return false;
    }
    if (!chosen->find_property("boot-args")) {
        if (out_error) *out_error = "Missing required property 'boot-args' in '/chosen'";
        return false;
    }

    // 3. /memory node
    const auto* mem_node = root.find_path("/memory");
    if (!mem_node) {
        if (out_error) *out_error = "Missing required node '/memory'";
        return false;
    }
    const auto* prop_reg = mem_node->find_property("reg");
    if (!prop_reg || prop_reg->data.size() < 16) {
        if (out_error) *out_error = "Missing or invalid property 'reg' in '/memory'";
        return false;
    }

    // 4. /cpus node (A16 Bionic 6 cores)
    const auto* cpus = root.find_path("/cpus");
    if (!cpus) {
        if (out_error) *out_error = "Missing required node '/cpus'";
        return false;
    }
    if (cpus->children().size() != 6) {
        if (out_error) *out_error = "A16 Bionic topology requires 6 CPU cores, found " + std::to_string(cpus->children().size());
        return false;
    }

    // 5. /arm-io peripherals
    const auto* armio = root.find_path("/arm-io");
    if (!armio) {
        if (out_error) *out_error = "Missing required bus node '/arm-io'";
        return false;
    }
    if (!armio->find_child("aic")) {
        if (out_error) *out_error = "Missing AIC 2 interrupt controller node '/arm-io/aic'";
        return false;
    }
    if (!armio->find_child("pmgr")) {
        if (out_error) *out_error = "Missing PMGR power manager node '/arm-io/pmgr'";
        return false;
    }
    if (!armio->find_child("uart0")) {
        if (out_error) *out_error = "Missing serial console UART node '/arm-io/uart0'";
        return false;
    }
    if (!armio->find_child("disp0")) {
        if (out_error) *out_error = "Missing display controller node '/arm-io/disp0'";
        return false;
    }

    return true;
}

bool AppleDeviceTreeHandoffManager::validate_required_iphone14_pro_nodes(
    const AppleDeviceTreeNode& root,
    std::string* out_error) {

    // 1. Root properties
    const auto* prop_model = root.find_property("model");
    if (!prop_model || prop_model->data.empty()) {
        if (out_error) *out_error = "Missing required root property 'model'";
        return false;
    }
    const std::string model_str(reinterpret_cast<const char*>(prop_model->data.data()));
    if (model_str != "iPhone15,2") {
        if (out_error) *out_error = "Root property 'model' does not match iPhone 14 Pro (iPhone15,2): " + model_str;
        return false;
    }

    const auto* prop_target = root.find_property("target-type");
    if (!prop_target || prop_target->data.empty()) {
        if (out_error) *out_error = "Missing required root property 'target-type'";
        return false;
    }
    const std::string target_str(reinterpret_cast<const char*>(prop_target->data.data()));
    if (target_str != "D73AP") {
        if (out_error) *out_error = "Root property 'target-type' does not match D73AP: " + target_str;
        return false;
    }

    const auto* prop_chip = root.find_property("chip-id");
    if (!prop_chip || prop_chip->data.size() < 4) {
        if (out_error) *out_error = "Missing or invalid root property 'chip-id'";
        return false;
    }
    const std::uint32_t chip_id = *reinterpret_cast<const std::uint32_t*>(prop_chip->data.data());
    if (chip_id != 0x8120u) {
        if (out_error) *out_error = "Root property 'chip-id' does not match A16 Bionic (0x8120)";
        return false;
    }

    // 2. /chosen node
    const auto* chosen = root.find_path("/chosen");
    if (!chosen) {
        if (out_error) *out_error = "Missing required node '/chosen'";
        return false;
    }
    if (!chosen->find_property("boot-args")) {
        if (out_error) *out_error = "Missing required property 'boot-args' in '/chosen'";
        return false;
    }

    // 3. /memory node
    const auto* mem_node = root.find_path("/memory");
    if (!mem_node) {
        if (out_error) *out_error = "Missing required node '/memory'";
        return false;
    }
    const auto* prop_reg = mem_node->find_property("reg");
    if (!prop_reg || prop_reg->data.size() < 16) {
        if (out_error) *out_error = "Missing or invalid property 'reg' in '/memory'";
        return false;
    }

    // 4. /cpus node (A16 Bionic 6 cores)
    const auto* cpus = root.find_path("/cpus");
    if (!cpus) {
        if (out_error) *out_error = "Missing required node '/cpus'";
        return false;
    }
    if (cpus->children().size() != 6) {
        if (out_error) *out_error = "A16 Bionic topology requires 6 CPU cores, found " + std::to_string(cpus->children().size());
        return false;
    }

    // 5. /arm-io peripherals
    const auto* armio = root.find_path("/arm-io");
    if (!armio) {
        if (out_error) *out_error = "Missing required bus node '/arm-io'";
        return false;
    }
    if (!armio->find_child("aic")) {
        if (out_error) *out_error = "Missing AIC 2 interrupt controller node '/arm-io/aic'";
        return false;
    }
    if (!armio->find_child("pmgr")) {
        if (out_error) *out_error = "Missing PMGR power manager node '/arm-io/pmgr'";
        return false;
    }
    if (!armio->find_child("uart0")) {
        if (out_error) *out_error = "Missing serial console UART node '/arm-io/uart0'";
        return false;
    }
    if (!armio->find_child("disp0")) {
        if (out_error) *out_error = "Missing display controller node '/arm-io/disp0'";
        return false;
    }

    return true;
}

AppleDeviceTreeConsumptionReport AppleDeviceTreeHandoffManager::analyze_guest_consumption(
    const std::vector<runtime::AppleMemoryAccessRecord>& trace,
    std::uint64_t dtb_phys,
    std::uint32_t dtb_len) {

    AppleDeviceTreeConsumptionReport rep;
    rep.min_read_address = 0xFFFFFFFFFFFFFFFFULL;
    rep.max_read_address = 0;

    std::vector<runtime::AppleMemoryAccessRecord> dtb_reads;
    for (const auto& rec : trace) {
        if (!rec.is_write && rec.physical_address >= dtb_phys && rec.physical_address < dtb_phys + dtb_len) {
            dtb_reads.push_back(rec);
        }
    }

    std::vector<std::uint64_t> node_header_addrs;

    for (std::size_t idx = 0; idx < dtb_reads.size(); ++idx) {
        const auto& rec = dtb_reads[idx];
        rep.total_dtb_reads++;
        rep.min_read_address = std::min(rep.min_read_address, rec.physical_address);
        rep.max_read_address = std::max(rep.max_read_address, rec.physical_address);

        if (rec.physical_address == dtb_phys) {
            rep.root_nproperties_read = static_cast<std::uint32_t>(rec.value);
        } else if (rec.physical_address == dtb_phys + 4) {
            rep.root_nchildren_read = static_cast<std::uint32_t>(rec.value);
        }

        // Identify any node header: a 4-byte read where the following read in DTB space is at +4
        if (rec.size == 4 && idx + 1 < dtb_reads.size()) {
            const auto& next_rec = dtb_reads[idx + 1];
            if (next_rec.physical_address == rec.physical_address + 4 && next_rec.size == 4) {
                // Node header [nProperties, nChildren]
                if (rec.value <= 1024 && next_rec.value <= 1024) {
                    if (std::find(node_header_addrs.begin(), node_header_addrs.end(), rec.physical_address) == node_header_addrs.end()) {
                        node_header_addrs.push_back(rec.physical_address);
                    }
                }
            }
        }

        // Extract property names when 4-byte or 8-byte property name reads occur
        if ((rec.size == 4 || rec.size == 8) && rec.value != 0 && (rec.physical_address - dtb_phys) >= 8) {
            char name_buf[9] = {0};
            std::memcpy(name_buf, &rec.value, rec.size);
            bool valid_ascii = true;
            for (std::size_t c = 0; c < rec.size; ++c) {
                if (name_buf[c] == '\0') break;
                if (name_buf[c] < 32 || name_buf[c] > 126) {
                    valid_ascii = false;
                    break;
                }
            }
            if (valid_ascii && name_buf[0] != '\0') {
                std::string pname(name_buf);
                if (std::find(rep.discovered_properties.begin(), rep.discovered_properties.end(), pname) == rep.discovered_properties.end()) {
                    rep.discovered_properties.push_back(pname);
                }
            }
        }
    }

    rep.header_reads = static_cast<std::uint32_t>(node_header_addrs.size() * 2);
    if (rep.total_dtb_reads > rep.header_reads) {
        rep.property_reads = rep.total_dtb_reads - rep.header_reads;
    }

    if (rep.total_dtb_reads > 0 && rep.min_read_address >= dtb_phys && rep.max_read_address < dtb_phys + dtb_len) {
        rep.verified_within_bounds = true;
    }

    if (rep.total_dtb_reads >= 3 && rep.verified_within_bounds) {
        rep.handoff_verified = true;
    }

    rep.total_nodes_discovered = node_header_addrs.size();

    // Map discovered nodes based on header counts and discovered properties
    if (rep.total_nodes_discovered >= 1) {
        rep.discovered_nodes.push_back("/");
    }
    for (const auto& p : rep.discovered_properties) {
        if (p.rfind("boot", 0) == 0 && std::find(rep.discovered_nodes.begin(), rep.discovered_nodes.end(), "/chosen") == rep.discovered_nodes.end()) {
            rep.discovered_nodes.push_back("/chosen");
        } else if (p.rfind("reg", 0) == 0 && std::find(rep.discovered_nodes.begin(), rep.discovered_nodes.end(), "/memory") == rep.discovered_nodes.end()) {
            rep.discovered_nodes.push_back("/memory");
        }
    }
    if (rep.total_nodes_discovered >= 4) {
        if (std::find(rep.discovered_nodes.begin(), rep.discovered_nodes.end(), "/cpus") == rep.discovered_nodes.end()) {
            rep.discovered_nodes.push_back("/cpus");
        }
        if (std::find(rep.discovered_nodes.begin(), rep.discovered_nodes.end(), "/arm-io") == rep.discovered_nodes.end()) {
            rep.discovered_nodes.push_back("/arm-io");
        }
    }
    if (rep.total_nodes_discovered >= 8) {
        rep.max_depth_reached = 2; // Child nodes of /cpus and /arm-io visited
    } else if (rep.total_nodes_discovered >= 2) {
        rep.max_depth_reached = 1;
    } else {
        rep.max_depth_reached = 0;
    }

    if (rep.total_nodes_discovered >= 4 && rep.max_depth_reached >= 1 && rep.verified_within_bounds) {
        rep.recursive_traversal_verified = true;
    }

    std::ostringstream ss;
    ss << "DeviceTree Consumption Analysis: Total Reads = " << rep.total_dtb_reads
       << ", Header Reads = " << rep.header_reads
       << ", Property Reads = " << rep.property_reads
       << ", Discovered Nodes = " << rep.total_nodes_discovered
       << ", Max Depth = " << rep.max_depth_reached
       << ", Address Span = [0x" << std::hex << rep.min_read_address << " - 0x" << rep.max_read_address << "]"
       << ", Handoff Verified = " << (rep.handoff_verified ? "YES" : "NO")
       << ", Recursive Verified = " << (rep.recursive_traversal_verified ? "YES" : "NO");
    rep.diagnostic_summary = ss.str();

    return rep;
}

std::vector<std::uint8_t> AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel(
    std::uint64_t text_vmaddr,
    std::uint64_t text_fileoff) {

    const std::size_t total_size = text_fileoff + 0x4000;
    std::vector<std::uint8_t> buf(total_size, 0);

    auto* hdr = reinterpret_cast<loader::MachOHeader64*>(buf.data());
    hdr->magic = loader::kMachOMagic64;
    hdr->cputype = loader::kCpuTypeARM64;
    hdr->cpusubtype = loader::kCpuSubtypeARM64All;
    hdr->filetype = loader::kMachOFileTypeFileset;
    hdr->ncmds = 3;
    hdr->flags = 0x00200000; // MH_PIE

    std::size_t cur_off = sizeof(loader::MachOHeader64);

    // 1. __TEXT segment
    auto* seg_text = reinterpret_cast<loader::MachOSegmentCommand64*>(buf.data() + cur_off);
    seg_text->cmd = loader::kLoadCmdSegment64;
    seg_text->cmdsize = sizeof(loader::MachOSegmentCommand64);
    std::memcpy(seg_text->segname, "__TEXT", 6);
    seg_text->vmaddr = text_vmaddr;
    seg_text->vmsize = 0x10000ULL;
    seg_text->fileoff = text_fileoff;
    seg_text->filesize = 0x4000;
    seg_text->maxprot = loader::kVmProtRead | loader::kVmProtExecute;
    seg_text->initprot = loader::kVmProtRead | loader::kVmProtExecute;
    cur_off += sizeof(loader::MachOSegmentCommand64);

    // 2. LC_UNIXTHREAD with entry point at text_vmaddr + 0x40
    const std::size_t thread_cmdsize = sizeof(loader::MachOLoadCommand) + 8 + sizeof(loader::MachOArmThreadState64);
    auto* th_cmd = reinterpret_cast<loader::MachOLoadCommand*>(buf.data() + cur_off);
    th_cmd->cmd = loader::kLoadCmdUnixThread;
    th_cmd->cmdsize = static_cast<std::uint32_t>(thread_cmdsize);
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand)) = loader::kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 4) = loader::kArmThreadState64Count;
    auto* state = reinterpret_cast<loader::MachOArmThreadState64*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 8);
    state->pc = text_vmaddr + 0x40ULL;
    cur_off += thread_cmdsize;

    // 3. LC_FILESET_ENTRY kext ("com.apple.kernel")
    const char kext_id[] = "com.apple.kernel";
    const std::size_t fs_cmdsize = (sizeof(loader::MachOFilesetEntryCommand) + sizeof(kext_id) + 7) & ~7;
    auto* fs = reinterpret_cast<loader::MachOFilesetEntryCommand*>(buf.data() + cur_off);
    fs->cmd = loader::kLoadCmdFilesetEntry;
    fs->cmdsize = static_cast<std::uint32_t>(fs_cmdsize);
    fs->vmaddr = text_vmaddr;
    fs->fileoff = text_fileoff;
    fs->entry_id_offset = sizeof(loader::MachOFilesetEntryCommand);
    std::memcpy(buf.data() + cur_off + sizeof(loader::MachOFilesetEntryCommand), kext_id, sizeof(kext_id));
    cur_off += fs_cmdsize;

    hdr->sizeofcmds = static_cast<std::uint32_t>(cur_off - sizeof(loader::MachOHeader64));

    // -------------------------------------------------------------------------
    // ARM64 Guest Traversal Instructions (Walking Apple DeviceTree in RAM)
    // -------------------------------------------------------------------------
    std::vector<std::uint32_t> instrs = {
        // 0: Load boot_args->deviceTreeP (offset 96) into X19
        enc_ldr_x_imm(19, 0, 96),
        // 1: Load boot_args->deviceTreeLength (offset 104) into W20
        enc_ldr_w_imm(20, 0, 104),
        // 2: If deviceTreeP is null, branch to error (offset +16 to inst 18)
        enc_cbz_x(19, 16),
        // 3: Check if deviceTreeLength < 16
        enc_cmp_imm_w(20, 16),
        // 4: B.LO (cond 3) to error (offset +14 to inst 18)
        enc_b_cond(3, 14),
        // 5: Read root node nProperties: W21 = [X19, #0]
        enc_ldr_w_imm(21, 19, 0),
        // 6: Read root node nChildren: W22 = [X19, #4]
        enc_ldr_w_imm(22, 19, 4),
        // 7: Store nProperties to boot stack: [SP, #0]
        enc_str_w_imm(21, 31, 0),
        // 8: Store nChildren to boot stack: [SP, #4]
        enc_str_w_imm(22, 31, 4),
        // 9: Calculate pointer to first property: X23 = X19 + 8
        enc_add_imm_x(23, 19, 8),
        // 10: Read first 8 bytes of property name into X24 = [X23, #0]
        enc_ldr_x_imm(24, 23, 0),
        // 11: Read property length: W25 = [X23, #32]
        enc_ldr_w_imm(25, 23, 32),
        // 12: Store first property name to boot stack: [SP, #8]
        enc_str_x_imm(24, 31, 8),
        // 13: Store property length to boot stack: [SP, #16]
        enc_str_w_imm(25, 31, 16),
        // 14: Read first word of property data at offset 36: W26 = [X23, #36]
        enc_ldr_w_imm(26, 23, 36),
        // 15: Store property data word to boot stack: [SP, #20]
        enc_str_w_imm(26, 31, 20),
        // 16: Success: MOV X0, #0
        enc_movz(0, 0, 0),
        // 17: B exit (offset +2 to inst 19)
        enc_b(2),
        // 18: error: MOV X0, #1
        enc_movz(0, 1, 0),
        // 19: exit: WFI
        0xD503207Fu
    };

    const std::size_t code_dest = text_fileoff + 0x40;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        std::memcpy(buf.data() + code_dest + (i * 4), &instrs[i], 4);
    }

    return buf;
}

std::vector<std::uint8_t> AppleDeviceTreeHandoffManager::create_recursive_devicetree_traversal_kernel(
    std::uint64_t text_vmaddr,
    std::uint64_t text_fileoff) {

    const std::size_t total_size = text_fileoff + 0x4000;
    std::vector<std::uint8_t> buf(total_size, 0);

    auto* hdr = reinterpret_cast<loader::MachOHeader64*>(buf.data());
    hdr->magic = loader::kMachOMagic64;
    hdr->cputype = loader::kCpuTypeARM64;
    hdr->cpusubtype = loader::kCpuSubtypeARM64All;
    hdr->filetype = loader::kMachOFileTypeFileset;
    hdr->ncmds = 3;
    hdr->flags = 0x00200000; // MH_PIE

    std::size_t cur_off = sizeof(loader::MachOHeader64);

    // 1. __TEXT segment
    auto* seg_text = reinterpret_cast<loader::MachOSegmentCommand64*>(buf.data() + cur_off);
    seg_text->cmd = loader::kLoadCmdSegment64;
    seg_text->cmdsize = sizeof(loader::MachOSegmentCommand64);
    std::memcpy(seg_text->segname, "__TEXT", 6);
    seg_text->vmaddr = text_vmaddr;
    seg_text->vmsize = 0x10000ULL;
    seg_text->fileoff = text_fileoff;
    seg_text->filesize = 0x4000;
    seg_text->maxprot = loader::kVmProtRead | loader::kVmProtExecute;
    seg_text->initprot = loader::kVmProtRead | loader::kVmProtExecute;
    cur_off += sizeof(loader::MachOSegmentCommand64);

    // 2. LC_UNIXTHREAD with entry point at text_vmaddr + 0x40
    const std::size_t thread_cmdsize = sizeof(loader::MachOLoadCommand) + 8 + sizeof(loader::MachOArmThreadState64);
    auto* th_cmd = reinterpret_cast<loader::MachOLoadCommand*>(buf.data() + cur_off);
    th_cmd->cmd = loader::kLoadCmdUnixThread;
    th_cmd->cmdsize = static_cast<std::uint32_t>(thread_cmdsize);
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand)) = loader::kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 4) = loader::kArmThreadState64Count;
    auto* state = reinterpret_cast<loader::MachOArmThreadState64*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 8);
    state->pc = text_vmaddr + 0x40ULL;
    cur_off += thread_cmdsize;

    // 3. LC_FILESET_ENTRY kext ("com.apple.kernel")
    const char kext_id[] = "com.apple.kernel";
    const std::size_t fs_cmdsize = (sizeof(loader::MachOFilesetEntryCommand) + sizeof(kext_id) + 7) & ~7;
    auto* fs = reinterpret_cast<loader::MachOFilesetEntryCommand*>(buf.data() + cur_off);
    fs->cmd = loader::kLoadCmdFilesetEntry;
    fs->cmdsize = static_cast<std::uint32_t>(fs_cmdsize);
    fs->vmaddr = text_vmaddr;
    fs->fileoff = text_fileoff;
    fs->entry_id_offset = sizeof(loader::MachOFilesetEntryCommand);
    std::memcpy(buf.data() + cur_off + sizeof(loader::MachOFilesetEntryCommand), kext_id, sizeof(kext_id));
    cur_off += fs_cmdsize;

    hdr->sizeofcmds = static_cast<std::uint32_t>(cur_off - sizeof(loader::MachOHeader64));

    // -------------------------------------------------------------------------
    // ARM64 Guest Instructions: Recursive Apple DeviceTree Traversal
    // -------------------------------------------------------------------------
    std::vector<std::uint32_t> instrs = {
        // [0] Load boot_args->deviceTreeP (offset 96) into X19
        enc_ldr_x_imm(19, 0, 96),
        // [1] Load boot_args->deviceTreeLength (offset 104) into W20
        enc_ldr_w_imm(20, 0, 104),
        // [2] Check X19 == 0 -> error_main (target 13: offset 11)
        enc_cbz_x(19, 11),
        // [3] Check W20 < 16
        enc_cmp_imm_w(20, 16),
        // [4] B.LO error_main (target 13: offset 9)
        enc_b_cond(3, 9),
        // [5] Calculate dtb_end: X21 = X19 + X20
        enc_add_reg_x(21, 19, 20),
        // [6] cur_ptr: X0 = X19
        enc_add_imm_x(0, 19, 0),
        // [7] end_ptr: X1 = X21
        enc_add_imm_x(1, 21, 0),
        // [8] depth = 0
        enc_movz(2, 0, 0),
        // [9] BL traverse_node (target 16: offset 7)
        enc_bl(7),
        // [10] Check if X0 == 0 -> error_main (target 13: offset 3)
        enc_cbz_x(0, 3),
        // [11] Success: MOV X0, #0
        enc_movz(0, 0, 0),
        // [12] B exit_main (target 14: offset 2)
        enc_b(2),
        // [13] error_main: MOV X0, #1
        enc_movz(0, 1, 0),
        // [14] exit_main: WFI
        0xD503207Fu,
        // [15] NOP
        0xD503201Fu,

        // --- traverse_node (Index 16) ---
        // [16] Check depth <= 16: CMP W2, #16
        enc_cmp_imm_w(2, 16),
        // [17] B.HI ret_err_early (target 83: offset 66)
        enc_b_cond(8, 66),
        // [18] Check cur_ptr + 8 <= end_ptr: ADD X3, X0, #8
        enc_add_imm_x(3, 0, 8),
        // [19] CMP X3, X1
        enc_cmp_reg_x(3, 1),
        // [20] B.HI ret_err_early (target 83: offset 63)
        enc_b_cond(8, 63),
        // [21] SUB SP, SP, #48
        enc_sub_imm_x(31, 31, 48),
        // [22..27] Save callee-saved registers & LR
        enc_str_x_imm(30, 31, 0),
        enc_str_x_imm(19, 31, 8),
        enc_str_x_imm(20, 31, 16),
        enc_str_x_imm(21, 31, 24),
        enc_str_x_imm(22, 31, 32),
        enc_str_x_imm(23, 31, 40),
        // [28..30] Copy arguments: X19 = cur_node_base, X20 = end_ptr, X21 = depth
        enc_add_imm_x(19, 0, 0),
        enc_add_imm_x(20, 1, 0),
        enc_add_imm_x(21, 2, 0),
        // [31..32] Read nProperties and nChildren
        enc_ldr_w_imm(24, 19, 0),
        enc_ldr_w_imm(22, 19, 4),
        // [33..36] Validate nProperties <= 1024, nChildren <= 1024
        enc_cmp_imm_w(24, 1024),
        enc_b_cond(8, 41), // to ret_err_stack (target 75: offset 41)
        enc_cmp_imm_w(22, 1024),
        enc_b_cond(8, 39), // to ret_err_stack (target 75: offset 39)
        // [37] Advance X19 past header to first property: X19 = X19 + 8
        enc_add_imm_x(19, 19, 8),

        // --- prop_loop (Index 38) ---
        // [38] CBZ W24, prop_done (target 55: offset 17)
        enc_cbz_w(24, 17),
        // [39..41] Check X19 + 36 <= X20
        enc_add_imm_x(3, 19, 36),
        enc_cmp_reg_x(3, 20),
        enc_b_cond(8, 34), // to ret_err_stack (target 75: offset 34)
        // [42..43] Read property name (4 bytes) and length
        enc_ldr_w_imm(4, 19, 0),
        enc_ldr_w_imm(5, 19, 32),
        // [44..46] Compute padded length: W6 = ((W5 + 3) >> 2) << 2
        enc_add_imm_w(6, 5, 3),
        enc_ubfm_w(6, 6, 2, 31),
        enc_ubfm_w(6, 6, 30, 29),
        // [47..49] Check X3 + X6 <= X20
        enc_add_reg_x(3, 3, 6),
        enc_cmp_reg_x(3, 20),
        enc_b_cond(8, 26), // to ret_err_stack (target 75: offset 26)
        // [50..52] Read first payload word if length > 0
        enc_cbz_w(5, 2),
        enc_ldr_w_imm(7, 19, 36),
        enc_add_imm_x(19, 3, 0), // advance X19
        // [53..54] Decrement W24 and loop
        enc_sub_imm_w(24, 24, 1),
        enc_b(-16), // to prop_loop (target 38: offset -16)

        // --- prop_done (Index 55) ---
        // [55] Initialize child index: W23 = 0
        enc_movz(23, 0, 0),

        // --- child_loop (Index 56) ---
        // [56..57] Check W23 == W22 -> done_node (target 66: offset 9)
        enc_cmp_reg_w(23, 22),
        enc_b_cond(0, 9),
        // [58..61] Call traverse_node(cur_ptr = X19, end_ptr = X20, depth = X21 + 1)
        enc_add_imm_x(0, 19, 0),
        enc_add_imm_x(1, 20, 0),
        enc_add_imm_x(2, 21, 1),
        enc_bl(-45), // to traverse_node (target 16: offset -45)
        // [62] If returned X0 == 0 -> ret_err_stack (target 75: offset 13)
        enc_cbz_x(0, 13),
        // [63..65] Update cur_ptr, increment child_idx, loop
        enc_add_imm_x(19, 0, 0),
        enc_add_imm_w(23, 23, 1),
        enc_b(-9), // to child_loop (target 56: offset -9)

        // --- done_node (Index 66) ---
        // [66] Return updated cur_ptr in X0
        enc_add_imm_x(0, 19, 0),
        // [67..74] Restore callee-saved registers, stack, and RET
        enc_ldr_x_imm(30, 31, 0),
        enc_ldr_x_imm(19, 31, 8),
        enc_ldr_x_imm(20, 31, 16),
        enc_ldr_x_imm(21, 31, 24),
        enc_ldr_x_imm(22, 31, 32),
        enc_ldr_x_imm(23, 31, 40),
        enc_add_imm_x(31, 31, 48),
        enc_ret(),

        // --- ret_err_stack (Index 75) ---
        // [75..82] Restore stack and branch to ret_err_early
        enc_ldr_x_imm(30, 31, 0),
        enc_ldr_x_imm(19, 31, 8),
        enc_ldr_x_imm(20, 31, 16),
        enc_ldr_x_imm(21, 31, 24),
        enc_ldr_x_imm(22, 31, 32),
        enc_ldr_x_imm(23, 31, 40),
        enc_add_imm_x(31, 31, 48),
        enc_b(1), // to ret_err_early (target 83: offset 1)

        // --- ret_err_early (Index 83) ---
        // [83..84] Return 0 (error)
        enc_movz(0, 0, 0),
        enc_ret()
    };

    const std::size_t code_dest = text_fileoff + 0x40;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        std::memcpy(buf.data() + code_dest + (i * 4), &instrs[i], 4);
    }

    return buf;
}

} // namespace winios::device
