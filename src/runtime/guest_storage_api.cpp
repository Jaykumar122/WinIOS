#include "runtime/guest_storage_api.h"
#include <cstring>
#include <algorithm>

namespace winios::runtime {

GuestStorageApi::GuestStorageApi(std::shared_ptr<storage::IsolatedStorage> storage, uint32_t process_id)
    : storage_(std::move(storage)), process_id_(process_id) {}

GuestStorageApi::~GuestStorageApi() {
    CloseAllForProcess();
}

int GuestStorageApi::OpenFile(const std::string& path, uint32_t mode_flags) {
    if (!storage_ || path.empty()) return -1;
    if (!storage_->is_path_safe(path)) return -1;

    std::lock_guard<std::mutex> lock(mutex_);
    int fd = next_fd_++;

    OpenHandle h{};
    h.fd = fd;
    h.path = path;
    h.mode_flags = mode_flags;
    h.cursor = 0;
    h.is_dirty = false;

    if (storage_->exists(path)) {
        if (mode_flags & static_cast<uint32_t>(FileMode::kTruncate)) {
            h.buffer.clear();
            h.is_dirty = true;
        } else {
            storage_->read(path, h.buffer);
            if (mode_flags & static_cast<uint32_t>(FileMode::kAppend)) {
                h.cursor = h.buffer.size();
            }
        }
    } else {
        if (!(mode_flags & static_cast<uint32_t>(FileMode::kCreate))) {
            return -1; // File not found and create not specified
        }
        h.is_dirty = true;
    }

    handles_[fd] = std::move(h);
    return fd;
}

int GuestStorageApi::ReadFile(int fd, void* buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(fd);
    if (it == handles_.end()) return -1;

    auto& h = it->second;
    if (h.cursor >= h.buffer.size()) return 0; // EOF

    size_t to_read = std::min(size, static_cast<size_t>(h.buffer.size() - h.cursor));
    std::memcpy(buffer, h.buffer.data() + h.cursor, to_read);
    h.cursor += to_read;
    return static_cast<int>(to_read);
}

int GuestStorageApi::WriteFile(int fd, const void* data, size_t size) {
    if (!data || size == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(fd);
    if (it == handles_.end()) return -1;

    auto& h = it->second;
    const auto* byte_data = static_cast<const uint8_t*>(data);

    if (h.cursor + size > h.buffer.size()) {
        h.buffer.resize(static_cast<size_t>(h.cursor + size));
    }
    std::memcpy(h.buffer.data() + h.cursor, byte_data, size);
    h.cursor += size;
    h.is_dirty = true;

    // Immediately persist to isolated storage
    storage_->atomic_write(h.path, h.buffer.data(), h.buffer.size(), false);
    return static_cast<int>(size);
}

int64_t GuestStorageApi::SeekFile(int fd, int64_t offset, int whence) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(fd);
    if (it == handles_.end()) return -1;

    auto& h = it->second;
    int64_t new_cursor = static_cast<int64_t>(h.cursor);
    if (whence == 0) { // SEEK_SET
        new_cursor = offset;
    } else if (whence == 1) { // SEEK_CUR
        new_cursor += offset;
    } else if (whence == 2) { // SEEK_END
        new_cursor = static_cast<int64_t>(h.buffer.size()) + offset;
    } else {
        return -1;
    }

    if (new_cursor < 0) return -1;
    h.cursor = static_cast<uint64_t>(new_cursor);
    return new_cursor;
}

bool GuestStorageApi::CloseFile(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(fd);
    if (it == handles_.end()) return false;

    if (it->second.is_dirty && storage_) {
        storage_->atomic_write(it->second.path, it->second.buffer.data(), it->second.buffer.size(), true);
    }
    handles_.erase(it);
    return true;
}

bool GuestStorageApi::RemoveFile(const std::string& path) {
    if (!storage_ || !storage_->is_path_safe(path)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return storage_->remove(path) == storage::StorageResult::Success;
}

bool GuestStorageApi::GetFileMetadata(const std::string& path, FileMetadata* meta_out) {
    if (!storage_ || !meta_out || !storage_->is_path_safe(path)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!storage_->exists(path)) {
        meta_out->exists = false;
        return false;
    }
    std::vector<uint8_t> buf;
    if (storage_->read(path, buf) != storage::StorageResult::Success) {
        meta_out->exists = false;
        return false;
    }
    meta_out->exists = true;
    meta_out->size = buf.size();
    meta_out->is_directory = false;
    meta_out->modified_time_us = 1000000;
    return true;
}

bool GuestStorageApi::EnumerateDirectory(const std::string& dir_path, std::vector<std::string>* entries_out) {
    if (!entries_out || !storage_ || !storage_->is_path_safe(dir_path)) return false;
    entries_out->clear();
    // Default root sandbox entries can be enumerated if known
    return true;
}

void GuestStorageApi::CloseAllForProcess() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [fd, h] : handles_) {
        if (h.is_dirty && storage_) {
            storage_->atomic_write(h.path, h.buffer.data(), h.buffer.size(), true);
        }
    }
    handles_.clear();
}

size_t GuestStorageApi::GetOpenHandleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handles_.size();
}

} // namespace winios::runtime
