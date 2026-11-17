#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::vfs {

// -----------------------------------------------------------------------------
// VFS Node & Metadata Definitions
// -----------------------------------------------------------------------------

enum class VfsNodeType : std::uint8_t {
    Unknown = 0,
    RegularFile,
    Directory,
    CharacterDevice,
    BlockDevice,
    Symlink,
    Fifo
};

// POSIX / Darwin directory entry
struct VfsDirEntry {
    std::uint64_t ino{0};
    std::uint8_t  type{0}; // DT_UNKNOWN=0, DT_FIFO=1, DT_CHR=2, DT_DIR=4, DT_BLK=6, DT_REG=8, DT_LNK=10
    std::string   name;
};

// POSIX / Darwin 64-bit stat structure (stat64 compatible layout & metadata)
#pragma pack(push, 4)
struct DarwinStat64 {
    std::int32_t  st_dev{0};
    std::uint16_t st_mode{0};
    std::uint16_t st_nlink{1};
    std::uint64_t st_ino{0};
    std::uint32_t st_uid{0};
    std::uint32_t st_gid{0};
    std::int32_t  st_rdev{0};
    std::int64_t  st_atime{0};
    std::int64_t  st_atimensec{0};
    std::int64_t  st_mtime{0};
    std::int64_t  st_mtimensec{0};
    std::int64_t  st_ctime{0};
    std::int64_t  st_ctimensec{0};
    std::int64_t  st_birthtime{0};
    std::int64_t  st_birthtimensec{0};
    std::int64_t  st_size{0};
    std::int64_t  st_blocks{0};
    std::int32_t  st_blksize{16384};
    std::uint32_t st_flags{0};
    std::uint32_t st_gen{0};
    std::int32_t  st_lspare{0};
    std::int64_t  st_qspare[2]{0, 0};
};
#pragma pack(pop)

struct VfsStat {
    std::uint64_t ino{0};
    std::uint32_t mode{0644};
    std::uint32_t nlink{1};
    std::uint32_t uid{0};
    std::uint32_t gid{0};
    std::uint64_t rdev{0};
    std::uint64_t size{0};
    std::uint64_t blksize{16384};
    std::uint64_t blocks{0};
    VfsNodeType   type{VfsNodeType::RegularFile};

    void to_darwin_stat64(DarwinStat64& out) const noexcept;
};

// -----------------------------------------------------------------------------
// VFS Node Hierarchy
// -----------------------------------------------------------------------------

struct VfsNode;

using DeviceReadHandler = std::function<std::size_t(void* buf, std::size_t count)>;
using DeviceWriteHandler = std::function<std::size_t(const void* buf, std::size_t count)>;

struct VfsNode : public std::enable_shared_from_this<VfsNode> {
    std::string                             name;
    VfsNodeType                             type{VfsNodeType::RegularFile};
    VfsStat                                 stat{};
    std::vector<std::uint8_t>               content;
    std::string                             symlink_target;
    std::weak_ptr<VfsNode>                  parent;
    std::map<std::string, std::shared_ptr<VfsNode>> children;

    DeviceReadHandler                       dev_read{nullptr};
    DeviceWriteHandler                      dev_write{nullptr};

    [[nodiscard]] bool is_directory() const noexcept { return type == VfsNodeType::Directory; }
    [[nodiscard]] bool is_regular_file() const noexcept { return type == VfsNodeType::RegularFile; }
    [[nodiscard]] bool is_symlink() const noexcept { return type == VfsNodeType::Symlink; }
    [[nodiscard]] bool is_device() const noexcept { return type == VfsNodeType::CharacterDevice || type == VfsNodeType::BlockDevice; }
};

// -----------------------------------------------------------------------------
// File Descriptor & Open Flags
// -----------------------------------------------------------------------------

inline constexpr std::uint32_t kO_RDONLY   = 0x0000u;
inline constexpr std::uint32_t kO_WRONLY   = 0x0001u;
inline constexpr std::uint32_t kO_RDWR     = 0x0002u;
inline constexpr std::uint32_t kO_ACCMODE  = 0x0003u;
inline constexpr std::uint32_t kO_NONBLOCK = 0x0004u;
inline constexpr std::uint32_t kO_APPEND   = 0x0008u;
inline constexpr std::uint32_t kO_CREAT    = 0x0200u;
inline constexpr std::uint32_t kO_TRUNC    = 0x0400u;
inline constexpr std::uint32_t kO_EXCL     = 0x0800u;
inline constexpr std::uint32_t kO_CLOEXEC  = 0x01000000u;

inline constexpr int kSeekSet = 0;
inline constexpr int kSeekCur = 1;
inline constexpr int kSeekEnd = 2;

struct VfsDescriptor {
    int                      fd{-1};
    std::shared_ptr<VfsNode> node{nullptr};
    std::uint32_t            flags{0};
    std::uint64_t            offset{0};
    bool                     cloexec{false};

    [[nodiscard]] bool is_valid() const noexcept { return node != nullptr; }
};

// -----------------------------------------------------------------------------
// Virtual File System Manager
// -----------------------------------------------------------------------------

class VirtualFileSystem {
public:
    VirtualFileSystem();
    ~VirtualFileSystem() = default;

    // Initialize standard Darwin rootfs layout (/dev, /tmp, /usr, /bin, character devices)
    void initialize_standard_hierarchy();

    // Canonical path normalization (resolves '.', '..', consecutive slashes, relative paths)
    static std::string normalize_path(const std::string& path, const std::string& cwd = "/");

    // Path lookup & traversal (supports symlink recursion limit)
    std::shared_ptr<VfsNode> lookup(
        const std::string& path,
        bool follow_symlinks = true,
        int max_symlinks = 32) const;

    // Directory operations
    bool mkdir(const std::string& path, std::uint32_t mode = 0755);
    bool rmdir(const std::string& path);
    bool readdir(const std::string& path, std::vector<VfsDirEntry>& out_entries) const;

    // File operations
    bool create_file(const std::string& path, std::uint32_t mode = 0644);
    bool write_file(const std::string& path, const void* data, std::size_t size);
    bool read_file(const std::string& path, std::vector<std::uint8_t>& out_data) const;
    bool unlink(const std::string& path);
    bool exists(const std::string& path) const;

    // Metadata
    bool stat(const std::string& path, VfsStat& out_stat, bool follow_symlinks = true) const;

    // Symlinks
    bool symlink(const std::string& target, const std::string& linkpath);

    // Device registration
    bool register_device(
        const std::string& path,
        DeviceReadHandler read_fn,
        DeviceWriteHandler write_fn,
        bool is_block = false,
        std::uint32_t mode = 0666);

    // Descriptor binding & I/O operations
    std::shared_ptr<VfsDescriptor> open(
        const std::string& path,
        std::uint32_t flags,
        std::uint32_t mode = 0644,
        int* out_errno = nullptr);

    std::int64_t read(VfsDescriptor& desc, void* buf, std::size_t count);
    std::int64_t write(VfsDescriptor& desc, const void* buf, std::size_t count);
    std::int64_t lseek(VfsDescriptor& desc, std::int64_t offset, int whence);
    bool fstat(const VfsDescriptor& desc, VfsStat& out_stat) const;
    void close(VfsDescriptor& desc);

private:
    std::shared_ptr<VfsNode>       root_;
    mutable std::recursive_mutex   mutex_;
    std::uint64_t                  next_inode_{100};

    std::uint64_t allocate_inode() noexcept { return next_inode_++; }
};

} // namespace winios::vfs
