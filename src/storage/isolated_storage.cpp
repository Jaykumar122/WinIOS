#include "storage/isolated_storage.h"
#include "crypto/sha256.h"

#include <algorithm>
#include <sstream>

namespace winios::storage {

IsolatedStorage::IsolatedStorage(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                                 std::string app_id,
                                 std::size_t quota_bytes)
    : vfs_(std::move(vfs))
    , app_id_(std::move(app_id))
    , quota_bytes_(quota_bytes) {

    root_path_ = "/data/" + app_id_;
    if (vfs_) {
        if (!vfs_->exists("/data")) {
            vfs_->mkdir("/data");
        }
        if (!vfs_->exists(root_path_)) {
            vfs_->mkdir(root_path_);
        }
    }
}

bool IsolatedStorage::is_path_safe(const std::string& rel_path) const noexcept {
    if (rel_path.empty()) return false;

    // Reject leading slashes (must be relative)
    if (rel_path.front() == '/' || rel_path.front() == '\\') return false;

    // Reject null characters
    if (rel_path.find('\0') != std::string::npos) return false;

    // Reject directory traversal attempts
    if (rel_path == ".." || rel_path.find("../") != std::string::npos ||
        rel_path.find("/..") != std::string::npos ||
        rel_path.find("..\\") != std::string::npos ||
        rel_path.find("\\..") != std::string::npos ||
        rel_path.find(':') != std::string::npos) {
        return false;
    }

    return true;
}

std::string IsolatedStorage::resolve_path(const std::string& rel_path) const {
    if (!is_path_safe(rel_path)) return "";
    return root_path_ + "/" + rel_path;
}

StorageResult IsolatedStorage::atomic_write(const std::string& rel_path,
                                           const void* data,
                                           std::size_t size,
                                           bool verify_sha256) {
    if (!vfs_) return StorageResult::IoError;
    if (!is_path_safe(rel_path)) return StorageResult::InvalidPath;

    std::lock_guard<std::mutex> guard(lock_);

    const std::string target_path = root_path_ + "/" + rel_path;

    // Calculate existing file size if it exists, so overwrite doesn't spuriously fail quota
    std::size_t existing_file_size = 0;
    if (vfs_->exists(target_path)) {
        vfs::VfsStat st{};
        if (vfs_->stat(target_path, st, false)) {
            existing_file_size = static_cast<std::size_t>(st.size);
        }
    }

    // Check quota
    const std::size_t current_usage = current_usage_bytes();
    const std::size_t projected_usage = (current_usage >= existing_file_size)
                                            ? (current_usage - existing_file_size + size)
                                            : size;

    if (projected_usage > quota_bytes_) {
        return StorageResult::QuotaExceeded;
    }

    const std::string tmp_path = target_path + ".tmp." + std::to_string(++tx_counter_);

    // 1. Ensure parent directories exist recursively
    const std::size_t last_slash = rel_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string cur = root_path_;
        const std::string rel_dir = rel_path.substr(0, last_slash);
        std::size_t start = 0;
        while (start < rel_dir.size()) {
            const std::size_t slash = rel_dir.find('/', start);
            const std::string part = (slash == std::string::npos) ? rel_dir.substr(start) : rel_dir.substr(start, slash - start);
            if (!part.empty()) {
                cur += "/" + part;
                if (!vfs_->exists(cur)) {
                    vfs_->mkdir(cur);
                }
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    }

    // 2. Write data to temporary transaction file
    if (!vfs_->create_file(tmp_path)) {
        return StorageResult::IoError;
    }
    if (size > 0 && !vfs_->write_file(tmp_path, data, size)) {
        vfs_->unlink(tmp_path);
        return StorageResult::IoError;
    }

    // 3. Verify SHA-256 integrity if requested
    if (verify_sha256 && size > 0) {
        std::vector<std::uint8_t> readback;
        if (!vfs_->read_file(tmp_path, readback) || readback.size() != size) {
            vfs_->unlink(tmp_path);
            return StorageResult::IoError;
        }
        const std::string expected_hash = crypto::Sha256::hash_hex(data, size);
        const std::string actual_hash = crypto::Sha256::hash_hex(readback);
        if (expected_hash != actual_hash) {
            vfs_->unlink(tmp_path);
            return StorageResult::CorruptedData;
        }
    }

    // 4. Atomic commit: remove target if exists, then replace
    if (vfs_->exists(target_path)) {
        vfs_->unlink(target_path);
    }

    // Copy temp to target then unlink temp
    if (!vfs_->create_file(target_path) ||
        (size > 0 && !vfs_->write_file(target_path, data, size))) {
        vfs_->unlink(tmp_path);
        return StorageResult::IoError;
    }
    vfs_->unlink(tmp_path);

    return StorageResult::Success;
}

StorageResult IsolatedStorage::read(const std::string& rel_path,
                                    std::vector<std::uint8_t>& out_data) const {
    if (!vfs_) return StorageResult::IoError;
    if (!is_path_safe(rel_path)) return StorageResult::InvalidPath;

    std::lock_guard<std::mutex> guard(lock_);
    const std::string target_path = root_path_ + "/" + rel_path;
    if (!vfs_->exists(target_path)) {
        return StorageResult::FileNotFound;
    }

    if (!vfs_->read_file(target_path, out_data)) {
        return StorageResult::IoError;
    }

    return StorageResult::Success;
}

StorageResult IsolatedStorage::remove(const std::string& rel_path) {
    if (!vfs_) return StorageResult::IoError;
    if (!is_path_safe(rel_path)) return StorageResult::InvalidPath;

    std::lock_guard<std::mutex> guard(lock_);
    const std::string target_path = root_path_ + "/" + rel_path;
    if (!vfs_->exists(target_path)) {
        return StorageResult::FileNotFound;
    }

    if (!vfs_->unlink(target_path)) {
        return StorageResult::IoError;
    }

    return StorageResult::Success;
}

bool IsolatedStorage::exists(const std::string& rel_path) const {
    if (!vfs_ || !is_path_safe(rel_path)) return false;
    std::lock_guard<std::mutex> guard(lock_);
    return vfs_->exists(root_path_ + "/" + rel_path);
}

std::size_t IsolatedStorage::recover_dir_transactions(const std::string& dir) {
    if (!vfs_ || !vfs_->exists(dir)) return 0;
    std::vector<vfs::VfsDirEntry> entries;
    if (!vfs_->readdir(dir, entries)) return 0;

    std::size_t recovered = 0;
    for (const auto& entry : entries) {
        if (entry.name == "." || entry.name == "..") continue;
        const std::string full_sub = dir + "/" + entry.name;
        if (entry.type == 4) { // DT_DIR
            recovered += recover_dir_transactions(full_sub);
        } else if (entry.name.find(".tmp.") != std::string::npos) {
            vfs_->unlink(full_sub);
            recovered++;
        }
    }
    return recovered;
}

std::size_t IsolatedStorage::recover_transactions() {
    std::lock_guard<std::mutex> guard(lock_);
    return recover_dir_transactions(root_path_);
}

std::size_t IsolatedStorage::calculate_dir_usage(const std::string& dir) const {
    if (!vfs_ || !vfs_->exists(dir)) return 0;
    std::vector<vfs::VfsDirEntry> entries;
    if (!vfs_->readdir(dir, entries)) return 0;

    std::size_t total = 0;
    for (const auto& entry : entries) {
        if (entry.name == "." || entry.name == "..") continue;
        const std::string full_sub = dir + "/" + entry.name;
        if (entry.type == 4) { // DT_DIR
            total += calculate_dir_usage(full_sub);
        } else {
            vfs::VfsStat st{};
            if (vfs_->stat(full_sub, st, false)) {
                total += static_cast<std::size_t>(st.size);
            }
        }
    }
    return total;
}

std::size_t IsolatedStorage::current_usage_bytes() const {
    return calculate_dir_usage(root_path_);
}

} // namespace winios::storage
