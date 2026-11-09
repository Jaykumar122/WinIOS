#include "fs/apfs_parser.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::fs {

namespace {

std::string format_uuid(const std::uint8_t uuid[16]) {
    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(uuid[i]);
    }
    return oss.str();
}

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> components;
    std::string token;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!token.empty()) {
                components.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        components.push_back(token);
    }
    return components;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// ApfsParser Implementation
// -----------------------------------------------------------------------------
ApfsParser::ApfsParser() = default;

std::uint64_t ApfsParser::compute_fletcher64(const void* data, std::size_t size) {
    if (!data || size < 8) return 0;
    const auto* words = reinterpret_cast<const std::uint32_t*>(data);
    std::size_t count = size / 4;

    // Fletcher-64 skips the 8-byte o_cksum field at offset 0
    words += 2;
    count -= 2;

    std::uint64_t sum1 = 0;
    std::uint64_t sum2 = 0;
    constexpr std::uint64_t kMod = 0xFFFFFFFFULL;

    for (std::size_t i = 0; i < count; ++i) {
        sum1 = (sum1 + words[i]) % kMod;
        sum2 = (sum2 + sum1) % kMod;
    }

    return (sum2 << 32) | sum1;
}

bool ApfsParser::load_container(const std::uint8_t* data, std::size_t size, std::string& out_error) {
    raw_data_ = data;
    raw_size_ = size;
    container_valid_ = false;
    volumes_.clear();
    volume_infos_.clear();
    inodes_.clear();
    directory_tables_.clear();
    volume_selected_ = false;

    if (!data || size < kApfsDefaultBlockSize) {
        out_error = "Container image buffer is too small (below minimum 4096-byte block)";
        return false;
    }

    // Inspect container superblock ('NXSB' magic at byte 0 or byte 32)
    std::size_t sb_offset = 0;
    if (size >= sizeof(ApfsContainerSuperblockRaw)) {
        const auto* raw_sb = reinterpret_cast<const ApfsContainerSuperblockRaw*>(data);
        if (raw_sb->nx_magic == kApfsContainerMagic) {
            sb_offset = 0;
        } else if (size >= 0x20 + sizeof(ApfsContainerSuperblockRaw)) {
            const auto* alt_sb = reinterpret_cast<const ApfsContainerSuperblockRaw*>(data + 0x20);
            if (alt_sb->nx_magic == kApfsContainerMagic) {
                sb_offset = 0x20;
            }
        }
    }

    const auto* sb_ptr = reinterpret_cast<const ApfsContainerSuperblockRaw*>(data + sb_offset);
    if (sb_ptr->nx_magic != kApfsContainerMagic) {
        out_error = "Missing valid APFS Container Superblock signature ('NXSB')";
        return false;
    }

    std::memcpy(&container_sb_, sb_ptr, sizeof(ApfsContainerSuperblockRaw));
    if (container_sb_.nx_block_size == 0 || container_sb_.nx_block_size > 65536) {
        out_error = "Invalid APFS block size";
        return false;
    }

    // Scan blocks for volume superblocks ('APFS')
    const std::size_t block_sz = container_sb_.nx_block_size;
    const std::size_t num_blocks = size / block_sz;

    for (std::size_t b = 1; b < num_blocks; ++b) {
        const std::size_t b_off = b * block_sz;
        if (b_off + sizeof(ApfsVolumeSuperblockRaw) <= size) {
            const auto* v_sb = reinterpret_cast<const ApfsVolumeSuperblockRaw*>(data + b_off);
            if (v_sb->apfs_magic == kApfsVolumeMagic) {
                volumes_.push_back(*v_sb);

                ApfsVolumeInfo info;
                info.index = static_cast<std::uint32_t>(volumes_.size() - 1);
                info.name = std::string(v_sb->apfs_volname);
                info.role = static_cast<ApfsVolumeRole>(v_sb->apfs_vol_role);
                info.uuid_hex = format_uuid(v_sb->apfs_vol_uuid);
                info.num_files = v_sb->apfs_num_files;
                info.num_directories = v_sb->apfs_num_directories;
                info.root_tree_oid = v_sb->apfs_root_tree_oid;
                volume_infos_.push_back(info);
            }
        }
    }

    if (volumes_.empty()) {
        out_error = "No APFS volumes discovered inside container";
        return false;
    }

    container_valid_ = true;

    // Automatically select System volume if present, otherwise volume 0
    std::uint32_t default_vol = 0;
    for (std::size_t i = 0; i < volume_infos_.size(); ++i) {
        if (volume_infos_[i].role == ApfsVolumeRole::System) {
            default_vol = static_cast<std::uint32_t>(i);
            break;
        }
    }

    std::string sel_err;
    return select_volume(default_vol, sel_err);
}

std::optional<ApfsVolumeInfo> ApfsParser::find_volume_by_role(ApfsVolumeRole role) const {
    for (const auto& vi : volume_infos_) {
        if (vi.role == role) {
            return vi;
        }
    }
    return std::nullopt;
}

std::optional<ApfsVolumeInfo> ApfsParser::find_volume_by_name(const std::string& name) const {
    for (const auto& vi : volume_infos_) {
        if (vi.name == name) {
            return vi;
        }
    }
    return std::nullopt;
}

bool ApfsParser::select_volume(std::uint32_t volume_index, std::string& out_error) {
    if (volume_index >= volumes_.size()) {
        out_error = "Volume index out of range";
        return false;
    }

    active_volume_index_ = volume_index;
    volume_selected_ = true;
    inodes_.clear();
    directory_tables_.clear();

    const auto& v = volumes_[volume_index];
    const std::size_t block_sz = container_sb_.nx_block_size;

    // Read root B-Tree if within buffer bounds
    if (v.apfs_root_tree_oid != 0 && v.apfs_root_tree_oid * block_sz < raw_size_) {
        const std::size_t tree_off = static_cast<std::size_t>(v.apfs_root_tree_oid) * block_sz;
        const auto* tree_ptr = raw_data_ + tree_off;

        // Parse filesystem objects table: header + inodes
        std::uint32_t num_inodes = 0;
        std::memcpy(&num_inodes, tree_ptr, 4);

        if (num_inodes > 0 && num_inodes < 1000) {
            std::size_t cur = 4;
            for (std::uint32_t i = 0; i < num_inodes && tree_off + cur + 36 <= raw_size_; ++i) {
                ApfsInode ino;
                std::memcpy(&ino.inode_id, tree_ptr + cur, 8); cur += 8;
                std::memcpy(&ino.parent_id, tree_ptr + cur, 8); cur += 8;
                std::memcpy(&ino.mode, tree_ptr + cur, 2); cur += 2;
                std::memcpy(&ino.uid, tree_ptr + cur, 4); cur += 4;
                std::memcpy(&ino.gid, tree_ptr + cur, 4); cur += 4;
                std::memcpy(&ino.size_bytes, tree_ptr + cur, 8); cur += 8;

                std::uint16_t name_len = 0;
                std::memcpy(&name_len, tree_ptr + cur, 2); cur += 2;
                if (cur + name_len <= block_sz) {
                    ino.name.assign(reinterpret_cast<const char*>(tree_ptr + cur), name_len);
                    cur += name_len;
                }

                std::uint16_t num_extents = 0;
                std::memcpy(&num_extents, tree_ptr + cur, 2); cur += 2;
                for (std::uint16_t e = 0; e < num_extents && cur + 24 <= block_sz; ++e) {
                    ApfsFileExtent ext;
                    std::memcpy(&ext.logical_offset, tree_ptr + cur, 8); cur += 8;
                    std::memcpy(&ext.length_bytes, tree_ptr + cur, 8); cur += 8;
                    std::memcpy(&ext.physical_block, tree_ptr + cur, 8); cur += 8;
                    ino.extents.push_back(ext);
                }

                inodes_.push_back(ino);
            }

            // Build directory hierarchy from inodes
            for (const auto& parent : inodes_) {
                if (parent.is_directory()) {
                    std::vector<ApfsDirEntry> entries;
                    for (const auto& child : inodes_) {
                        if (child.parent_id == parent.inode_id && child.inode_id != parent.inode_id) {
                            ApfsDirEntry entry;
                            entry.inode_id = child.inode_id;
                            entry.name = child.name;
                            entry.type = child.is_directory() ? 4 : (child.is_regular_file() ? 8 : 10);
                            entries.push_back(entry);
                        }
                    }
                    directory_tables_.emplace_back(parent.inode_id, entries);
                }
            }
        }
    }

    return true;
}

std::optional<ApfsInode> ApfsParser::lookup_inode(std::uint64_t inode_id) const {
    for (const auto& ino : inodes_) {
        if (ino.inode_id == inode_id) {
            return ino;
        }
    }
    return std::nullopt;
}

std::vector<ApfsDirEntry> ApfsParser::read_directory(std::uint64_t dir_inode_id) const {
    for (const auto& dt : directory_tables_) {
        if (dt.first == dir_inode_id) {
            return dt.second;
        }
    }
    return {};
}

std::optional<ApfsInode> ApfsParser::lookup_path(const std::string& path) const {
    if (inodes_.empty()) return std::nullopt;

    auto components = split_path(path);
    if (components.empty()) {
        // Root directory
        return lookup_inode(1);
    }

    std::uint64_t current_id = 1; // Start at root
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& seg = components[i];
        auto entries = read_directory(current_id);
        bool found = false;
        for (const auto& e : entries) {
            if (e.name == seg) {
                current_id = e.inode_id;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }

    return lookup_inode(current_id);
}

bool ApfsParser::read_file_content(const ApfsInode& inode, std::vector<std::uint8_t>& out_data, std::string& out_error) const {
    if (inode.is_directory()) {
        out_error = "Target is a directory, not a regular file";
        return false;
    }

    out_data.clear();
    out_data.reserve(static_cast<std::size_t>(inode.size_bytes));

    const std::size_t block_sz = container_sb_.nx_block_size;

    for (const auto& ext : inode.extents) {
        const std::size_t phys_off = static_cast<std::size_t>(ext.physical_block) * block_sz;
        if (phys_off + ext.length_bytes > raw_size_) {
            out_error = "Extent block exceeds raw container boundaries";
            return false;
        }
        out_data.insert(out_data.end(), raw_data_ + phys_off, raw_data_ + phys_off + ext.length_bytes);
    }

    if (out_data.size() > inode.size_bytes) {
        out_data.resize(static_cast<std::size_t>(inode.size_bytes));
    }

    return true;
}

bool ApfsParser::read_file_by_path(const std::string& path, std::vector<std::uint8_t>& out_data, std::string& out_error) const {
    auto ino = lookup_path(path);
    if (!ino) {
        out_error = "File not found: " + path;
        return false;
    }
    return read_file_content(*ino, out_data, out_error);
}

// -----------------------------------------------------------------------------
// ApfsVolumeDiscovery Implementation
// -----------------------------------------------------------------------------
ApfsVolumeDiscovery::ApfsVolumeDiscovery(ApfsParser& parser) : parser_(parser) {}

std::optional<ApfsVolumeInfo> ApfsVolumeDiscovery::discover_root_volume() const {
    // 1. Prioritize volume with APFS_VOL_ROLE_SYSTEM (0x0001)
    auto sys = parser_.find_volume_by_role(ApfsVolumeRole::System);
    if (sys) return sys;

    // 2. Fallback to volume names
    auto by_name = parser_.find_volume_by_name("iPhone OS");
    if (by_name) return by_name;
    return parser_.find_volume_by_name("System");
}

std::optional<ApfsVolumeInfo> ApfsVolumeDiscovery::discover_data_volume() const {
    auto user_role = parser_.find_volume_by_role(ApfsVolumeRole::User);
    if (user_role) return user_role;
    auto data_role = parser_.find_volume_by_role(ApfsVolumeRole::Data);
    if (data_role) return data_role;
    return parser_.find_volume_by_name("Data");
}

std::optional<ApfsVolumeInfo> ApfsVolumeDiscovery::discover_preboot_volume() const {
    auto preboot_role = parser_.find_volume_by_role(ApfsVolumeRole::Preboot);
    if (preboot_role) return preboot_role;
    return parser_.find_volume_by_name("Preboot");
}

ApfsRootFsValidationResult ApfsVolumeDiscovery::validate_root_filesystem(std::uint32_t volume_index) {
    ApfsRootFsValidationResult res;
    std::string err;

    if (!parser_.select_volume(volume_index, err)) {
        res.validation_errors.push_back("Failed to select volume index " + std::to_string(volume_index) + ": " + err);
        return res;
    }

    if (volume_index < parser_.volumes().size()) {
        const auto& vi = parser_.volumes()[volume_index];
        res.volume_name = vi.name;
        res.is_ssv = (vi.role == ApfsVolumeRole::System);
    }

    // 1. Verify Root directory (Inode 1)
    auto root_ino = parser_.lookup_inode(1);
    if (!root_ino || !root_ino->is_directory()) {
        res.validation_errors.push_back("Missing or invalid root directory (Inode 1)");
        return res;
    }

    // 2. Check and record directories
    const std::vector<std::string> standard_dirs = {
        "/sbin", "/System", "/System/Library", "/usr", "/private", "/Library", "/Applications"
    };
    for (const auto& d : standard_dirs) {
        auto ino = parser_.lookup_path(d);
        if (ino && ino->is_directory()) {
            res.discovered_directories.push_back(d);
        }
    }

    // 3. Validate /sbin/launchd
    auto launchd_ino = parser_.lookup_path("/sbin/launchd");
    if (launchd_ino && launchd_ino->is_regular_file()) {
        std::vector<std::uint8_t> launchd_hdr;
        if (parser_.read_file_content(*launchd_ino, launchd_hdr, err) && launchd_hdr.size() >= 16) {
            const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(launchd_hdr.data());
            const std::uint32_t cputype = *reinterpret_cast<const std::uint32_t*>(launchd_hdr.data() + 4);
            // Mach-O 64-bit ARM64 / ARM64e executable validation
            if (magic == 0xFEEDFACFu && cputype == 0x0100000Cu) {
                res.launchd_present = true;
                res.launchd_path = "/sbin/launchd";
                res.launchd_size = launchd_ino->size_bytes;
            } else {
                res.validation_errors.push_back("/sbin/launchd header is not a valid ARM64 Mach-O executable");
            }
        }
    } else {
        res.validation_errors.push_back("Missing critical system executable /sbin/launchd");
    }

    // 4. Validate /System/Library/CoreServices/SystemVersion.plist
    auto plist_ino = parser_.lookup_path("/System/Library/CoreServices/SystemVersion.plist");
    if (plist_ino && plist_ino->is_regular_file()) {
        std::vector<std::uint8_t> plist_data;
        if (parser_.read_file_content(*plist_ino, plist_data, err)) {
            std::string xml(plist_data.begin(), plist_data.end());
            res.system_version_present = true;

            // Extract ProductVersion
            const std::string pv_tag = "<key>ProductVersion</key>";
            auto pv_pos = xml.find(pv_tag);
            if (pv_pos != std::string::npos) {
                auto str_start = xml.find("<string>", pv_pos);
                auto str_end = xml.find("</string>", str_start);
                if (str_start != std::string::npos && str_end != std::string::npos) {
                    res.product_version = xml.substr(str_start + 8, str_end - (str_start + 8));
                }
            }

            // Extract ProductBuildVersion
            const std::string pb_tag = "<key>ProductBuildVersion</key>";
            auto pb_pos = xml.find(pb_tag);
            if (pb_pos != std::string::npos) {
                auto str_start = xml.find("<string>", pb_pos);
                auto str_end = xml.find("</string>", str_start);
                if (str_start != std::string::npos && str_end != std::string::npos) {
                    res.build_version = xml.substr(str_start + 8, str_end - (str_start + 8));
                }
            }
        }
    } else {
        res.validation_errors.push_back("Missing /System/Library/CoreServices/SystemVersion.plist");
    }

    res.valid = (res.launchd_present && res.system_version_present && res.validation_errors.empty());
    return res;
}

std::string ApfsVolumeDiscovery::generate_discovery_report(const ApfsRootFsValidationResult& res) const {
    std::ostringstream oss;
    oss << "=================================================================\n";
    oss << " iOS APFS Root Filesystem Discovery & Validation Audit           \n";
    oss << "=================================================================\n";
    oss << " Volume Name:       " << res.volume_name << "\n";
    oss << " Sealed Volume:     " << (res.is_ssv ? "YES (SSV APFS_VOL_ROLE_SYSTEM)" : "NO") << "\n";
    oss << " Root Status:       " << (res.valid ? "VALIDATED FOR BOOT HANDOFF" : "REJECTED") << "\n";
    oss << " iOS Version:       " << (res.product_version.empty() ? "UNKNOWN" : res.product_version) << "\n";
    oss << " Build Version:     " << (res.build_version.empty() ? "UNKNOWN" : res.build_version) << "\n";
    oss << " Launchd Path:      " << (res.launchd_present ? res.launchd_path : "MISSING") << " (" << res.launchd_size << " bytes)\n";
    oss << " Core Directories:  " << res.discovered_directories.size() << " verified\n";
    for (const auto& d : res.discovered_directories) {
        oss << "   - " << d << "\n";
    }
    if (!res.validation_errors.empty()) {
        oss << " Validation Errors: " << res.validation_errors.size() << "\n";
        for (const auto& e : res.validation_errors) {
            oss << "   [ERROR] " << e << "\n";
        }
    }
    oss << "=================================================================\n";
    return oss.str();
}

// -----------------------------------------------------------------------------
// ApfsSyntheticBuilder Implementation
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> ApfsSyntheticBuilder::create_iphone15_root_container(
    const std::string& os_version,
    const std::string& build_train) {

    constexpr std::size_t kBlockSize = 4096;
    constexpr std::size_t kTotalBlocks = 16;
    std::vector<std::uint8_t> img(kTotalBlocks * kBlockSize, 0);

    // 1. Block 0: Container Superblock (NXSB)
    auto* nx = reinterpret_cast<ApfsContainerSuperblockRaw*>(img.data());
    nx->nx_o.o_oid = 1;
    nx->nx_o.o_xid = 100;
    nx->nx_o.o_type = kApfsObjTypeNxSuperblock;
    nx->nx_magic = kApfsContainerMagic;
    nx->nx_block_size = static_cast<std::uint32_t>(kBlockSize);
    nx->nx_block_count = kTotalBlocks;
    nx->nx_fs_oid[0] = 1; // System volume at Block 1
    nx->nx_fs_oid[1] = 2; // Data volume at Block 2
    nx->nx_fs_oid[2] = 3; // Preboot volume at Block 3

    // UUID
    const std::uint8_t container_uuid[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    std::memcpy(nx->nx_uuid, container_uuid, 16);
    nx->nx_o.o_cksum = ApfsParser::compute_fletcher64(nx, sizeof(ApfsContainerSuperblockRaw));

    // 2. Block 1: System Volume Superblock (APFS)
    auto* vol0 = reinterpret_cast<ApfsVolumeSuperblockRaw*>(img.data() + 1 * kBlockSize);
    vol0->apfs_o.o_oid = 10;
    vol0->apfs_o.o_xid = 100;
    vol0->apfs_o.o_type = kApfsObjTypeFs;
    vol0->apfs_magic = kApfsVolumeMagic;
    vol0->apfs_fs_index = 0;
    vol0->apfs_vol_role = static_cast<std::uint16_t>(ApfsVolumeRole::System);
    vol0->apfs_root_tree_oid = 4; // B-tree root at Block 4
    vol0->apfs_num_files = 2;
    vol0->apfs_num_directories = 5;
    std::snprintf(vol0->apfs_volname, sizeof(vol0->apfs_volname), "iPhone OS");
    const std::uint8_t sys_uuid[16] = {
        0xD3, 0x7A, 0x01, 0x55, 0x15, 0x40, 0x4B, 0x1A,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11
    };
    std::memcpy(vol0->apfs_vol_uuid, sys_uuid, 16);
    vol0->apfs_o.o_cksum = ApfsParser::compute_fletcher64(vol0, sizeof(ApfsVolumeSuperblockRaw));

    // 3. Block 2: Data Volume Superblock (APFS)
    auto* vol1 = reinterpret_cast<ApfsVolumeSuperblockRaw*>(img.data() + 2 * kBlockSize);
    vol1->apfs_o.o_oid = 11;
    vol1->apfs_o.o_xid = 100;
    vol1->apfs_o.o_type = kApfsObjTypeFs;
    vol1->apfs_magic = kApfsVolumeMagic;
    vol1->apfs_fs_index = 1;
    vol1->apfs_vol_role = static_cast<std::uint16_t>(ApfsVolumeRole::User);
    std::snprintf(vol1->apfs_volname, sizeof(vol1->apfs_volname), "Data");
    vol1->apfs_o.o_cksum = ApfsParser::compute_fletcher64(vol1, sizeof(ApfsVolumeSuperblockRaw));

    // 4. Block 3: Preboot Volume Superblock (APFS)
    auto* vol2 = reinterpret_cast<ApfsVolumeSuperblockRaw*>(img.data() + 3 * kBlockSize);
    vol2->apfs_o.o_oid = 12;
    vol2->apfs_o.o_xid = 100;
    vol2->apfs_o.o_type = kApfsObjTypeFs;
    vol2->apfs_magic = kApfsVolumeMagic;
    vol2->apfs_fs_index = 2;
    vol2->apfs_vol_role = static_cast<std::uint16_t>(ApfsVolumeRole::Preboot);
    std::snprintf(vol2->apfs_volname, sizeof(vol2->apfs_volname), "Preboot");
    vol2->apfs_o.o_cksum = ApfsParser::compute_fletcher64(vol2, sizeof(ApfsVolumeSuperblockRaw));

    // 5. Payload Blocks:
    // Block 5: Mach-O 64-bit binary for /sbin/launchd
    std::uint8_t* launchd_data = img.data() + 5 * kBlockSize;
    const std::uint32_t macho_magic = 0xFEEDFACFu; // MH_MAGIC_64
    const std::uint32_t cputype     = 0x0100000Cu; // CPU_TYPE_ARM64
    const std::uint32_t cpusubtype  = 0x80000002u; // CPU_SUBTYPE_ARM64E
    const std::uint32_t filetype    = 0x00000002u; // MH_EXECUTE
    std::memcpy(launchd_data + 0, &macho_magic, 4);
    std::memcpy(launchd_data + 4, &cputype, 4);
    std::memcpy(launchd_data + 8, &cpusubtype, 4);
    std::memcpy(launchd_data + 12, &filetype, 4);
    const std::size_t launchd_size = 64;

    // Block 6: XML Plist for /System/Library/CoreServices/SystemVersion.plist
    std::uint8_t* plist_data = img.data() + 6 * kBlockSize;
    std::string plist_content =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>ProductBuildVersion</key>\n"
        "\t<string>" + build_train + "</string>\n"
        "\t<key>ProductName</key>\n"
        "\t<string>iPhone OS</string>\n"
        "\t<key>ProductVersion</key>\n"
        "\t<string>" + os_version + "</string>\n"
        "</dict>\n"
        "</plist>\n";
    std::memcpy(plist_data, plist_content.data(), plist_content.size());
    const std::size_t plist_size = plist_content.size();

    // 6. Block 4: Root Inode & Directory Table (B-Tree Serialized representation)
    std::uint8_t* tree_buf = img.data() + 4 * kBlockSize;
    std::size_t cur = 0;

    struct InodeDef {
        std::uint64_t id;
        std::uint64_t parent;
        std::uint16_t mode;
        std::uint64_t size;
        std::string name;
        std::uint64_t phys_blk;
    };

    std::vector<InodeDef> defs = {
        {1, 1, kApfsModeDir | 0755, 0, "", 0},
        {2, 1, kApfsModeDir | 0755, 0, "sbin", 0},
        {3, 2, kApfsModeReg | 0755, launchd_size, "launchd", 5},
        {4, 1, kApfsModeDir | 0755, 0, "System", 0},
        {5, 4, kApfsModeDir | 0755, 0, "Library", 0},
        {6, 5, kApfsModeDir | 0755, 0, "CoreServices", 0},
        {7, 6, kApfsModeReg | 0644, plist_size, "SystemVersion.plist", 6},
        {8, 1, kApfsModeDir | 0755, 0, "usr", 0},
        {9, 1, kApfsModeDir | 0755, 0, "private", 0},
        {10, 1, kApfsModeDir | 0755, 0, "Library", 0},
        {11, 1, kApfsModeDir | 0755, 0, "Applications", 0}
    };

    const std::uint32_t num_inodes = static_cast<std::uint32_t>(defs.size());
    std::memcpy(tree_buf + cur, &num_inodes, 4); cur += 4;

    for (const auto& d : defs) {
        std::memcpy(tree_buf + cur, &d.id, 8); cur += 8;
        std::memcpy(tree_buf + cur, &d.parent, 8); cur += 8;
        std::memcpy(tree_buf + cur, &d.mode, 2); cur += 2;
        std::uint32_t uid = 0, gid = 0;
        std::memcpy(tree_buf + cur, &uid, 4); cur += 4;
        std::memcpy(tree_buf + cur, &gid, 4); cur += 4;
        std::memcpy(tree_buf + cur, &d.size, 8); cur += 8;

        const std::uint16_t name_len = static_cast<std::uint16_t>(d.name.size());
        std::memcpy(tree_buf + cur, &name_len, 2); cur += 2;
        if (name_len > 0) {
            std::memcpy(tree_buf + cur, d.name.data(), name_len);
            cur += name_len;
        }

        const std::uint16_t num_extents = (d.phys_blk > 0) ? 1 : 0;
        std::memcpy(tree_buf + cur, &num_extents, 2); cur += 2;
        if (num_extents > 0) {
            std::uint64_t zero_off = 0;
            std::memcpy(tree_buf + cur, &zero_off, 8); cur += 8;
            std::memcpy(tree_buf + cur, &d.size, 8); cur += 8;
            std::memcpy(tree_buf + cur, &d.phys_blk, 8); cur += 8;
        }
    }

    return img;
}

} // namespace winios::fs
