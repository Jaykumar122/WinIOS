#include "vfs/vfs.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::vfs {

// -----------------------------------------------------------------------------
// VfsStat to DarwinStat64 mapping
// -----------------------------------------------------------------------------

void VfsStat::to_darwin_stat64(DarwinStat64& out) const noexcept {
    std::memset(&out, 0, sizeof(out));
    out.st_ino = ino;
    out.st_nlink = static_cast<std::uint16_t>(nlink);
    out.st_uid = uid;
    out.st_gid = gid;
    out.st_rdev = static_cast<std::int32_t>(rdev);
    out.st_size = static_cast<std::int64_t>(size);
    out.st_blksize = static_cast<std::int32_t>(blksize != 0 ? blksize : 16384);
    out.st_blocks = static_cast<std::int64_t>((size + 511) / 512);

    std::uint16_t fmt = 0;
    switch (type) {
        case VfsNodeType::RegularFile:     fmt = 0100000; break; // S_IFREG
        case VfsNodeType::Directory:       fmt = 0040000; break; // S_IFDIR
        case VfsNodeType::CharacterDevice: fmt = 0020000; break; // S_IFCHR
        case VfsNodeType::BlockDevice:     fmt = 0060000; break; // S_IFBLK
        case VfsNodeType::Symlink:         fmt = 0120000; break; // S_IFLNK
        case VfsNodeType::Fifo:            fmt = 0010000; break; // S_IFIFO
        default:                           fmt = 0100000; break;
    }
    out.st_mode = static_cast<std::uint16_t>(fmt | (mode & 07777));
}

// -----------------------------------------------------------------------------
// VirtualFileSystem Implementation
// -----------------------------------------------------------------------------

VirtualFileSystem::VirtualFileSystem() {
    root_ = std::make_shared<VfsNode>();
    root_->name = "/";
    root_->type = VfsNodeType::Directory;
    root_->stat.ino = 2; // Standard Unix root inode
    root_->stat.mode = 0755;
    root_->stat.type = VfsNodeType::Directory;
    root_->stat.nlink = 2;

    initialize_standard_hierarchy();
}

std::string VirtualFileSystem::normalize_path(const std::string& path, const std::string& cwd) {
    if (path.empty()) {
        return cwd.empty() ? "/" : cwd;
    }

    std::string full_path;
    if (path.front() == '/') {
        full_path = path;
    } else {
        full_path = cwd + "/" + path;
    }

    std::vector<std::string> parts;
    std::string token;
    for (char c : full_path) {
        if (c == '/' || c == '\\') {
            if (!token.empty()) {
                if (token == ".") {
                    // Current directory: ignore
                } else if (token == "..") {
                    if (!parts.empty()) parts.pop_back();
                } else {
                    parts.push_back(token);
                }
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }

    if (!token.empty()) {
        if (token == ".") {
            // Ignore
        } else if (token == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(token);
        }
    }

    if (parts.empty()) {
        return "/";
    }

    std::string result;
    for (const auto& part : parts) {
        result += "/" + part;
    }
    return result;
}

std::shared_ptr<VfsNode> VirtualFileSystem::lookup(
    const std::string& path,
    bool follow_symlinks,
    int max_symlinks) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string norm = normalize_path(path);
    if (norm == "/") return root_;

    std::vector<std::string> components;
    std::stringstream ss(norm);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) components.push_back(item);
    }

    std::shared_ptr<VfsNode> current = root_;
    int symlink_hops = 0;

    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& comp = components[i];
        if (!current || !current->is_directory()) {
            return nullptr;
        }

        auto it = current->children.find(comp);
        if (it == current->children.end()) {
            return nullptr;
        }

        current = it->second;

        // If intermediate component is symlink, or final component is symlink and follow_symlinks=true
        if (current->is_symlink() && (follow_symlinks || i + 1 < components.size())) {
            if (++symlink_hops > max_symlinks) {
                return nullptr; // ELOOP
            }

            std::string remaining;
            for (std::size_t j = i + 1; j < components.size(); ++j) {
                remaining += "/" + components[j];
            }

            std::string target_path = current->symlink_target;
            if (target_path.empty() || target_path.front() != '/') {
                // Relative symlink: resolve relative to parent directory
                std::string parent_path = "/";
                auto p = current->parent.lock();
                if (p && p != root_) {
                    // Reconstruct parent path
                    parent_path = norm.substr(0, norm.rfind('/'));
                }
                target_path = parent_path + "/" + target_path;
            }

            std::string resolved_full = normalize_path(target_path + remaining);
            return lookup(resolved_full, follow_symlinks, max_symlinks - symlink_hops);
        }
    }

    return current;
}

bool VirtualFileSystem::mkdir(const std::string& path, std::uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(path);
    if (norm == "/") return false; // Root already exists

    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent || !parent->is_directory()) {
        return false;
    }

    if (parent->children.find(name) != parent->children.end()) {
        return false; // EEXIST
    }

    auto dir_node = std::make_shared<VfsNode>();
    dir_node->name = name;
    dir_node->type = VfsNodeType::Directory;
    dir_node->stat.ino = allocate_inode();
    dir_node->stat.mode = mode;
    dir_node->stat.type = VfsNodeType::Directory;
    dir_node->stat.nlink = 2;
    dir_node->parent = parent;

    parent->children[name] = dir_node;
    parent->stat.nlink++;
    return true;
}

bool VirtualFileSystem::rmdir(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(path);
    if (norm == "/") return false; // Cannot remove root

    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent) return false;

    auto it = parent->children.find(name);
    if (it == parent->children.end()) return false;

    auto dir = it->second;
    if (!dir->is_directory() || !dir->children.empty()) {
        return false; // ENOTEMPTY or ENOTDIR
    }

    parent->children.erase(it);
    if (parent->stat.nlink > 2) parent->stat.nlink--;
    return true;
}

bool VirtualFileSystem::readdir(const std::string& path, std::vector<VfsDirEntry>& out_entries) const {
    out_entries.clear();
    auto dir = lookup(path, true);
    if (!dir || !dir->is_directory()) return false;

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Entry for '.'
    VfsDirEntry dot{};
    dot.ino = dir->stat.ino;
    dot.type = 4; // DT_DIR
    dot.name = ".";
    out_entries.push_back(dot);

    // Entry for '..'
    VfsDirEntry dotdot{};
    auto parent = dir->parent.lock();
    dotdot.ino = parent ? parent->stat.ino : dir->stat.ino;
    dotdot.type = 4; // DT_DIR
    dotdot.name = "..";
    out_entries.push_back(dotdot);

    for (const auto& [child_name, child_node] : dir->children) {
        VfsDirEntry entry{};
        entry.ino = child_node->stat.ino;
        switch (child_node->type) {
            case VfsNodeType::RegularFile:     entry.type = 8;  break; // DT_REG
            case VfsNodeType::Directory:       entry.type = 4;  break; // DT_DIR
            case VfsNodeType::CharacterDevice: entry.type = 2;  break; // DT_CHR
            case VfsNodeType::BlockDevice:     entry.type = 6;  break; // DT_BLK
            case VfsNodeType::Symlink:         entry.type = 10; break; // DT_LNK
            case VfsNodeType::Fifo:            entry.type = 1;  break; // DT_FIFO
            default:                           entry.type = 0;  break;
        }
        entry.name = child_name;
        out_entries.push_back(entry);
    }

    return true;
}

bool VirtualFileSystem::create_file(const std::string& path, std::uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(path);
    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent || !parent->is_directory()) return false;

    if (parent->children.find(name) != parent->children.end()) {
        return false; // EEXIST
    }

    auto file_node = std::make_shared<VfsNode>();
    file_node->name = name;
    file_node->type = VfsNodeType::RegularFile;
    file_node->stat.ino = allocate_inode();
    file_node->stat.mode = mode;
    file_node->stat.type = VfsNodeType::RegularFile;
    file_node->stat.nlink = 1;
    file_node->stat.size = 0;
    file_node->parent = parent;

    parent->children[name] = file_node;
    return true;
}

bool VirtualFileSystem::write_file(const std::string& path, const void* data, std::size_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto node = lookup(path, true);
    if (!node) {
        // Attempt create if parent exists
        const std::string norm = normalize_path(path);
        const auto last_slash = norm.rfind('/');
        const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
        const std::string name = norm.substr(last_slash + 1);

        auto parent = lookup(parent_path, true);
        if (!parent || !parent->is_directory()) return false;

        auto new_node = std::make_shared<VfsNode>();
        new_node->name = name;
        new_node->type = VfsNodeType::RegularFile;
        new_node->stat.ino = allocate_inode();
        new_node->stat.mode = 0644;
        new_node->stat.type = VfsNodeType::RegularFile;
        new_node->stat.nlink = 1;
        new_node->parent = parent;
        parent->children[name] = new_node;
        node = new_node;
    }

    if (!node->is_regular_file()) return false;

    node->content.resize(size);
    if (size > 0 && data != nullptr) {
        std::memcpy(node->content.data(), data, size);
    }
    node->stat.size = size;
    node->stat.blocks = (size + 511) / 512;
    return true;
}

bool VirtualFileSystem::read_file(const std::string& path, std::vector<std::uint8_t>& out_data) const {
    out_data.clear();
    auto node = lookup(path, true);
    if (!node || !node->is_regular_file()) return false;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    out_data = node->content;
    return true;
}

bool VirtualFileSystem::unlink(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(path);
    if (norm == "/") return false;

    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent) return false;

    auto it = parent->children.find(name);
    if (it == parent->children.end()) return false;

    if (it->second->is_directory()) return false; // Must use rmdir

    parent->children.erase(it);
    return true;
}

bool VirtualFileSystem::rename(const std::string& oldpath, const std::string& newpath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm_old = normalize_path(oldpath);
    const std::string norm_new = normalize_path(newpath);
    if (norm_old == "/" || norm_new == "/") return false;
    if (norm_old == norm_new) return true;

    const auto old_last_slash = norm_old.rfind('/');
    const std::string old_parent_path = (old_last_slash == 0) ? "/" : norm_old.substr(0, old_last_slash);
    const std::string old_name = norm_old.substr(old_last_slash + 1);

    const auto new_last_slash = norm_new.rfind('/');
    const std::string new_parent_path = (new_last_slash == 0) ? "/" : norm_new.substr(0, new_last_slash);
    const std::string new_name = norm_new.substr(new_last_slash + 1);

    auto old_parent = lookup(old_parent_path, true);
    if (!old_parent || !old_parent->is_directory()) return false;

    auto old_it = old_parent->children.find(old_name);
    if (old_it == old_parent->children.end()) return false;

    auto new_parent = lookup(new_parent_path, true);
    if (!new_parent || !new_parent->is_directory()) return false;

    auto node_to_move = old_it->second;

    auto new_it = new_parent->children.find(new_name);
    if (new_it != new_parent->children.end()) {
        if (new_it->second->is_directory()) {
            return false;
        }
        new_parent->children.erase(new_it);
    }

    old_parent->children.erase(old_it);
    node_to_move->name = new_name;
    node_to_move->parent = new_parent;
    new_parent->children[new_name] = node_to_move;
    return true;
}

bool VirtualFileSystem::exists(const std::string& path) const {
    return lookup(path, true) != nullptr;
}

bool VirtualFileSystem::stat(const std::string& path, VfsStat& out_stat, bool follow_symlinks) const {
    auto node = lookup(path, follow_symlinks);
    if (!node) return false;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    out_stat = node->stat;
    return true;
}

bool VirtualFileSystem::symlink(const std::string& target, const std::string& linkpath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(linkpath);
    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent || !parent->is_directory()) return false;

    if (parent->children.find(name) != parent->children.end()) {
        return false; // EEXIST
    }

    auto sym_node = std::make_shared<VfsNode>();
    sym_node->name = name;
    sym_node->type = VfsNodeType::Symlink;
    sym_node->symlink_target = target;
    sym_node->stat.ino = allocate_inode();
    sym_node->stat.mode = 0777;
    sym_node->stat.type = VfsNodeType::Symlink;
    sym_node->stat.nlink = 1;
    sym_node->stat.size = target.size();
    sym_node->parent = parent;

    parent->children[name] = sym_node;
    return true;
}

bool VirtualFileSystem::register_device(
    const std::string& path,
    DeviceReadHandler read_fn,
    DeviceWriteHandler write_fn,
    bool is_block,
    std::uint32_t mode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string norm = normalize_path(path);
    const auto last_slash = norm.rfind('/');
    const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
    const std::string name = norm.substr(last_slash + 1);

    auto parent = lookup(parent_path, true);
    if (!parent || !parent->is_directory()) return false;

    auto dev_node = std::make_shared<VfsNode>();
    dev_node->name = name;
    dev_node->type = is_block ? VfsNodeType::BlockDevice : VfsNodeType::CharacterDevice;
    dev_node->stat.ino = allocate_inode();
    dev_node->stat.mode = mode;
    dev_node->stat.type = dev_node->type;
    dev_node->stat.nlink = 1;
    dev_node->dev_read = std::move(read_fn);
    dev_node->dev_write = std::move(write_fn);
    dev_node->parent = parent;

    parent->children[name] = dev_node;
    return true;
}

void VirtualFileSystem::initialize_standard_hierarchy() {
    // 1. Create standard root filesystem directories
    const std::vector<std::string> standard_dirs = {
        "/bin", "/sbin", "/etc", "/tmp", "/var",
        "/usr", "/usr/bin", "/usr/lib", "/usr/sbin",
        "/System", "/System/Library", "/System/Library/Frameworks",
        "/Applications", "/dev",
        "/private", "/private/var", "/private/var/mobile"
    };

    for (const auto& d : standard_dirs) {
        mkdir(d, 0755);
    }

    // Default configuration files
    const char hosts_content[] = "127.0.0.1 localhost\n::1 localhost\n";
    write_file("/etc/hosts", hosts_content, sizeof(hosts_content) - 1);

    // 2. Register character devices in /dev
    // /dev/null
    register_device("/dev/null",
        [](void*, std::size_t) -> std::size_t { return 0; /* EOF */ },
        [](const void*, std::size_t count) -> std::size_t { return count; /* Discard */ });

    // /dev/zero
    register_device("/dev/zero",
        [](void* buf, std::size_t count) -> std::size_t {
            if (buf && count > 0) std::memset(buf, 0, count);
            return count;
        },
        [](const void*, std::size_t count) -> std::size_t { return count; /* Discard */ });

    // /dev/random & /dev/urandom
    auto random_read = [](void* buf, std::size_t count) -> std::size_t {
        if (!buf || count == 0) return 0;
        std::uint8_t* p = reinterpret_cast<std::uint8_t*>(buf);
        static std::uint32_t seed = 0x12345678;
        for (std::size_t i = 0; i < count; ++i) {
            seed = seed * 1664525u + 1013904223u;
            p[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFFu);
        }
        return count;
    };

    register_device("/dev/random", random_read,
        [](const void*, std::size_t count) -> std::size_t { return count; });

    register_device("/dev/urandom", random_read,
        [](const void*, std::size_t count) -> std::size_t { return count; });
}

// -----------------------------------------------------------------------------
// Descriptor Binding & I/O
// -----------------------------------------------------------------------------

std::shared_ptr<VfsDescriptor> VirtualFileSystem::open(
    const std::string& path,
    std::uint32_t flags,
    std::uint32_t mode,
    int* out_errno) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto node = lookup(path, true);

    if (!node) {
        if ((flags & kO_CREAT) != 0) {
            // Create file
            const std::string norm = normalize_path(path);
            const auto last_slash = norm.rfind('/');
            const std::string parent_path = (last_slash == 0) ? "/" : norm.substr(0, last_slash);
            const std::string name = norm.substr(last_slash + 1);

            auto parent = lookup(parent_path, true);
            if (!parent || !parent->is_directory()) {
                if (out_errno) *out_errno = 2; // ENOENT
                return nullptr;
            }

            auto new_node = std::make_shared<VfsNode>();
            new_node->name = name;
            new_node->type = VfsNodeType::RegularFile;
            new_node->stat.ino = allocate_inode();
            new_node->stat.mode = mode;
            new_node->stat.type = VfsNodeType::RegularFile;
            new_node->stat.nlink = 1;
            new_node->parent = parent;
            parent->children[name] = new_node;
            node = new_node;
        } else {
            if (out_errno) *out_errno = 2; // ENOENT
            return nullptr;
        }
    } else {
        if ((flags & kO_CREAT) != 0 && (flags & kO_EXCL) != 0) {
            if (out_errno) *out_errno = 17; // EEXIST
            return nullptr;
        }

        if ((flags & kO_TRUNC) != 0 && node->is_regular_file()) {
            node->content.clear();
            node->stat.size = 0;
            node->stat.blocks = 0;
        }
    }

    auto desc = std::make_shared<VfsDescriptor>();
    desc->node = node;
    desc->flags = flags;
    desc->cloexec = (flags & kO_CLOEXEC) != 0;
    desc->offset = (flags & kO_APPEND) ? node->stat.size : 0;
    return desc;
}

std::int64_t VirtualFileSystem::read(VfsDescriptor& desc, void* buf, std::size_t count) {
    if (!desc.is_valid() || !buf || count == 0) return 0;
    if ((desc.flags & kO_ACCMODE) == kO_WRONLY) return -1; // EBADF

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto node = desc.node;

    if (node->is_device() && node->dev_read) {
        return static_cast<std::int64_t>(node->dev_read(buf, count));
    }

    if (!node->is_regular_file()) {
        return -1; // EISDIR or invalid
    }

    if (desc.offset >= node->content.size()) {
        return 0; // EOF
    }

    const std::size_t bytes_available = node->content.size() - static_cast<std::size_t>(desc.offset);
    const std::size_t bytes_to_read = std::min(count, bytes_available);

    std::memcpy(buf, node->content.data() + desc.offset, bytes_to_read);
    desc.offset += bytes_to_read;
    return static_cast<std::int64_t>(bytes_to_read);
}

std::int64_t VirtualFileSystem::write(VfsDescriptor& desc, const void* buf, std::size_t count) {
    if (!desc.is_valid() || !buf || count == 0) return 0;
    if ((desc.flags & kO_ACCMODE) == kO_RDONLY) return -1; // EBADF

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto node = desc.node;

    if (node->is_device() && node->dev_write) {
        return static_cast<std::int64_t>(node->dev_write(buf, count));
    }

    if (!node->is_regular_file()) {
        return -1; // EISDIR
    }

    if ((desc.flags & kO_APPEND) != 0) {
        desc.offset = node->content.size();
    }

    const std::size_t new_end = static_cast<std::size_t>(desc.offset) + count;
    if (new_end > node->content.size()) {
        node->content.resize(new_end);
        node->stat.size = new_end;
        node->stat.blocks = (new_end + 511) / 512;
    }

    std::memcpy(node->content.data() + desc.offset, buf, count);
    desc.offset += count;
    return static_cast<std::int64_t>(count);
}

std::int64_t VirtualFileSystem::lseek(VfsDescriptor& desc, std::int64_t offset, int whence) {
    if (!desc.is_valid()) return -1;
    if (desc.node->is_device()) return 0; // Seeking on character device is a no-op or ESPIPE

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::int64_t new_offset = 0;

    switch (whence) {
        case kSeekSet:
            new_offset = offset;
            break;
        case kSeekCur:
            new_offset = static_cast<std::int64_t>(desc.offset) + offset;
            break;
        case kSeekEnd:
            new_offset = static_cast<std::int64_t>(desc.node->stat.size) + offset;
            break;
        default:
            return -1; // EINVAL
    }

    if (new_offset < 0) return -1; // EINVAL

    desc.offset = static_cast<std::uint64_t>(new_offset);
    return new_offset;
}

bool VirtualFileSystem::fstat(const VfsDescriptor& desc, VfsStat& out_stat) const {
    if (!desc.is_valid()) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    out_stat = desc.node->stat;
    return true;
}

void VirtualFileSystem::close(VfsDescriptor& desc) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    desc.node = nullptr;
    desc.offset = 0;
    desc.flags = 0;
}

} // namespace winios::vfs
