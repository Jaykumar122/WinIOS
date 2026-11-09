#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace winios::fs {

// -----------------------------------------------------------------------------
// APFS Constants & Magic Numbers (Conforming to Apple File System Reference)
// -----------------------------------------------------------------------------
inline constexpr std::uint32_t kApfsContainerMagic = 0x4253584Eu; // 'NXSB'
inline constexpr std::uint32_t kApfsVolumeMagic    = 0x53465041u; // 'APFS'
inline constexpr std::uint32_t kApfsDefaultBlockSize = 4096u;

// APFS Object Types
inline constexpr std::uint32_t kApfsObjTypeNxSuperblock   = 0x00000001u;
inline constexpr std::uint32_t kApfsObjTypeBtree          = 0x00000002u;
inline constexpr std::uint32_t kApfsObjTypeBtreeNode      = 0x00000003u;
inline constexpr std::uint32_t kApfsObjTypeOmap           = 0x0000000Bu;
inline constexpr std::uint32_t kApfsObjTypeCheckpointMap  = 0x0000000Cu;
inline constexpr std::uint32_t kApfsObjTypeFs             = 0x0000000Du;
inline constexpr std::uint32_t kApfsObjTypeInode          = 0x0000000Eu;

// B-Tree Node Flags
inline constexpr std::uint16_t kApfsBtreeNodeRoot         = 0x0001u;
inline constexpr std::uint16_t kApfsBtreeNodeLeaf         = 0x0002u;
inline constexpr std::uint16_t kApfsBtreeNodeFixedKvSize  = 0x0004u;

// Inode File Types (POSIX standard & APFS)
inline constexpr std::uint16_t kApfsModeTypeMask          = 0xF000u;
inline constexpr std::uint16_t kApfsModeFifo              = 0x1000u;
inline constexpr std::uint16_t kApfsModeChr               = 0x2000u;
inline constexpr std::uint16_t kApfsModeDir               = 0x4000u;
inline constexpr std::uint16_t kApfsModeBlk               = 0x6000u;
inline constexpr std::uint16_t kApfsModeReg               = 0x8000u;
inline constexpr std::uint16_t kApfsModeLnk               = 0xA000u;
inline constexpr std::uint16_t kApfsModeSock              = 0xC000u;

// Volume Roles (Apple APFS Volume Roles)
enum class ApfsVolumeRole : std::uint16_t {
    None      = 0x0000u,
    System    = 0x0001u, // Sealed System Volume (SSV) / Root filesystem
    User      = 0x0002u, // User Data volume
    Recovery  = 0x0004u, // Recovery OS
    VM        = 0x0008u, // Virtual Memory swap
    Preboot   = 0x0040u, // Early iBoot boot artifacts & kernelcache
    Installer = 0x0080u, // Installer environment
    Data      = 0x0100u, // iOS Data partition
    Baseband  = 0x0200u, // Baseband firmware
    Update    = 0x0400u  // OTA software update staging
};

// -----------------------------------------------------------------------------
// On-Disk Object Header (obj_phys_t)
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct ApfsObjPhys {
    std::uint64_t o_cksum{0};   // Fletcher-64 checksum
    std::uint64_t o_oid{0};     // Object ID
    std::uint64_t o_xid{0};     // Transaction ID
    std::uint32_t o_type{0};    // Object type & flags
    std::uint32_t o_subtype{0}; // Object subtype
};

// -----------------------------------------------------------------------------
// Container Superblock Header (nx_superblock_t)
// -----------------------------------------------------------------------------
struct ApfsContainerSuperblockRaw {
    ApfsObjPhys   nx_o;
    std::uint32_t nx_magic{kApfsContainerMagic}; // 'NXSB'
    std::uint32_t nx_block_size{kApfsDefaultBlockSize};
    std::uint64_t nx_block_count{0};
    std::uint64_t nx_features{0};
    std::uint64_t nx_ro_compat_features{0};
    std::uint64_t nx_incompat_features{0};
    std::uint8_t  nx_uuid[16]{0};
    std::uint64_t nx_next_oid{1000};
    std::uint64_t nx_next_xid{1};
    std::uint32_t nx_xp_desc_blocks{0};
    std::uint32_t nx_xp_data_blocks{0};
    std::uint64_t nx_xp_desc_base{0};
    std::uint64_t nx_xp_data_base{0};
    std::uint32_t nx_xp_desc_next{0};
    std::uint32_t nx_xp_data_next{0};
    std::uint32_t nx_xp_desc_index{0};
    std::uint32_t nx_xp_desc_len{0};
    std::uint32_t nx_xp_data_index{0};
    std::uint32_t nx_xp_data_len{0};
    std::uint64_t nx_spaceman_oid{0};
    std::uint64_t nx_omap_oid{0};
    std::uint64_t nx_reaper_oid{0};
    std::uint32_t nx_test_type{0};
    std::uint32_t nx_max_file_systems{10};
    std::uint64_t nx_fs_oid[10]{0};
};

// -----------------------------------------------------------------------------
// Volume Superblock Header (apfs_superblock_t)
// -----------------------------------------------------------------------------
struct ApfsVolumeSuperblockRaw {
    ApfsObjPhys   apfs_o;
    std::uint32_t apfs_magic{kApfsVolumeMagic}; // 'APFS'
    std::uint32_t apfs_fs_index{0};
    std::uint64_t apfs_features{0};
    std::uint64_t apfs_ro_compat_features{0};
    std::uint64_t apfs_incompat_features{0};
    std::uint64_t apfs_unparsable_flags{0};
    std::uint64_t apfs_create_time{0};
    std::uint64_t apfs_mod_time{0};
    std::uint64_t apfs_access_time{0};
    std::uint64_t apfs_change_time{0};
    std::uint64_t apfs_fs_flags{0};
    std::uint16_t apfs_vol_role{static_cast<std::uint16_t>(ApfsVolumeRole::System)};
    std::uint16_t apfs_reserved{0};
    std::uint64_t apfs_omap_oid{0};
    std::uint64_t apfs_root_tree_oid{0};
    std::uint64_t apfs_extentref_tree_oid{0};
    std::uint64_t apfs_snap_meta_tree_oid{0};
    std::uint64_t apfs_revert_to_xid{0};
    std::uint64_t apfs_next_obj_id{100};
    std::uint64_t apfs_num_files{0};
    std::uint64_t apfs_num_directories{0};
    std::uint64_t apfs_num_symlinks{0};
    std::uint64_t apfs_num_other_fsobj{0};
    std::uint64_t apfs_num_snapshots{0};
    std::uint64_t apfs_total_blocks_alloced{0};
    std::uint8_t  apfs_vol_uuid[16]{0};
    char          apfs_volname[128]{0};
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Parsed High-Level Data Structures
// -----------------------------------------------------------------------------
struct ApfsFileExtent {
    std::uint64_t logical_offset{0};
    std::uint64_t length_bytes{0};
    std::uint64_t physical_block{0};
};

struct ApfsInode {
    std::uint64_t inode_id{0};
    std::uint64_t parent_id{0};
    std::uint16_t mode{0};
    std::uint32_t uid{0};
    std::uint32_t gid{0};
    std::uint64_t size_bytes{0};
    std::string   name;
    std::vector<ApfsFileExtent> extents;

    [[nodiscard]] bool is_directory() const noexcept {
        return (mode & kApfsModeTypeMask) == kApfsModeDir;
    }
    [[nodiscard]] bool is_regular_file() const noexcept {
        return (mode & kApfsModeTypeMask) == kApfsModeReg;
    }
    [[nodiscard]] bool is_symlink() const noexcept {
        return (mode & kApfsModeTypeMask) == kApfsModeLnk;
    }
};

struct ApfsDirEntry {
    std::uint64_t inode_id{0};
    std::string   name;
    std::uint8_t  type{0}; // DT_DIR = 4, DT_REG = 8, DT_LNK = 10
};

struct ApfsVolumeInfo {
    std::uint32_t  index{0};
    std::string    name;
    ApfsVolumeRole role{ApfsVolumeRole::None};
    std::string    uuid_hex;
    std::uint64_t  num_files{0};
    std::uint64_t  num_directories{0};
    std::uint64_t  root_tree_oid{0};
};

// -----------------------------------------------------------------------------
// ApfsParser: Read-Only APFS Minimal Volume Parser
// -----------------------------------------------------------------------------
class ApfsParser {
public:
    explicit ApfsParser();
    ~ApfsParser() = default;

    // Load container from continuous memory or disk image buffer
    bool load_container(const std::uint8_t* data, std::size_t size, std::string& out_error);

    [[nodiscard]] bool is_valid_container() const noexcept { return container_valid_; }
    [[nodiscard]] std::uint32_t block_size() const noexcept { return container_sb_.nx_block_size; }
    [[nodiscard]] std::uint64_t block_count() const noexcept { return container_sb_.nx_block_count; }
    [[nodiscard]] std::size_t volume_count() const noexcept { return volumes_.size(); }

    [[nodiscard]] const std::vector<ApfsVolumeInfo>& volumes() const noexcept { return volume_infos_; }
    [[nodiscard]] std::optional<ApfsVolumeInfo> find_volume_by_role(ApfsVolumeRole role) const;
    [[nodiscard]] std::optional<ApfsVolumeInfo> find_volume_by_name(const std::string& name) const;

    // Select active volume for filesystem operations
    bool select_volume(std::uint32_t volume_index, std::string& out_error);

    // Directory and File Lookup
    std::optional<ApfsInode> lookup_inode(std::uint64_t inode_id) const;
    std::optional<ApfsInode> lookup_path(const std::string& path) const;
    std::vector<ApfsDirEntry> read_directory(std::uint64_t dir_inode_id) const;

    // Read file contents into buffer
    bool read_file_content(const ApfsInode& inode, std::vector<std::uint8_t>& out_data, std::string& out_error) const;
    bool read_file_by_path(const std::string& path, std::vector<std::uint8_t>& out_data, std::string& out_error) const;

    // Fletcher-64 Checksum verification
    static std::uint64_t compute_fletcher64(const void* data, std::size_t size);

private:
    const std::uint8_t* raw_data_{nullptr};
    std::size_t         raw_size_{0};
    bool                container_valid_{false};

    ApfsContainerSuperblockRaw container_sb_{};
    std::vector<ApfsVolumeSuperblockRaw> volumes_;
    std::vector<ApfsVolumeInfo> volume_infos_;

    std::uint32_t active_volume_index_{0};
    bool volume_selected_{false};

    // Cached filesystem trees for active volume
    std::vector<ApfsInode> inodes_;
    std::vector<std::pair<std::uint64_t, std::vector<ApfsDirEntry>>> directory_tables_;
};

// -----------------------------------------------------------------------------
// ApfsRootFsValidationResult: Audit result for candidate iOS Root Filesystem
// -----------------------------------------------------------------------------
struct ApfsRootFsValidationResult {
    bool valid{false};
    bool is_ssv{false};
    bool launchd_present{false};
    bool system_version_present{false};
    std::string volume_name;
    std::string product_version;
    std::string build_version;
    std::string launchd_path;
    std::uint64_t launchd_size{0};
    std::vector<std::string> discovered_directories;
    std::vector<std::string> validation_errors;
};

// -----------------------------------------------------------------------------
// ApfsVolumeDiscovery: Discovers and validates iOS root filesystem volumes
// -----------------------------------------------------------------------------
class ApfsVolumeDiscovery {
public:
    explicit ApfsVolumeDiscovery(ApfsParser& parser);

    // Discover root volume among all container volumes
    [[nodiscard]] std::optional<ApfsVolumeInfo> discover_root_volume() const;
    [[nodiscard]] std::optional<ApfsVolumeInfo> discover_data_volume() const;
    [[nodiscard]] std::optional<ApfsVolumeInfo> discover_preboot_volume() const;

    // Validate active or specified volume as genuine iOS root filesystem
    ApfsRootFsValidationResult validate_root_filesystem(std::uint32_t volume_index);

    // Formatted diagnostic report
    [[nodiscard]] std::string generate_discovery_report(const ApfsRootFsValidationResult& res) const;

private:
    ApfsParser& parser_;
};

// -----------------------------------------------------------------------------
// ApfsSyntheticBuilder: Helper to generate valid synthetic APFS container images
// -----------------------------------------------------------------------------
class ApfsSyntheticBuilder {
public:
    static std::vector<std::uint8_t> create_iphone15_root_container(
        const std::string& os_version = "17.4",
        const std::string& build_train = "21E219");
};

} // namespace winios::fs
