#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

#include "storage/isolated_storage.h"

namespace winios::runtime {

enum class FileMode : uint32_t {
    kReadOnly = 1,
    kWriteOnly = 2,
    kReadWrite = 3,
    kCreate = 4,
    kTruncate = 8,
    kAppend = 16
};

struct FileMetadata {
    uint64_t size{0};
    uint64_t modified_time_us{0};
    bool is_directory{false};
    bool exists{false};
};

class GuestStorageApi {
public:
    explicit GuestStorageApi(std::shared_ptr<storage::IsolatedStorage> storage, uint32_t process_id);
    ~GuestStorageApi();

    int  OpenFile(const std::string& path, uint32_t mode_flags);
    int  ReadFile(int fd, void* buffer, size_t size);
    int  WriteFile(int fd, const void* data, size_t size);
    int64_t SeekFile(int fd, int64_t offset, int whence);
    bool CloseFile(int fd);
    bool RemoveFile(const std::string& path);
    bool GetFileMetadata(const std::string& path, FileMetadata* meta_out);
    bool EnumerateDirectory(const std::string& dir_path, std::vector<std::string>* entries_out);

    void CloseAllForProcess();
    size_t GetOpenHandleCount() const;

private:
    struct OpenHandle {
        int fd;
        std::string path;
        uint32_t mode_flags;
        uint64_t cursor;
        std::vector<uint8_t> buffer;
        bool is_dirty;
    };

    std::shared_ptr<storage::IsolatedStorage> storage_;
    uint32_t process_id_{0};
    mutable std::mutex mutex_;
    int next_fd_{3}; // 0, 1, 2 reserved
    std::unordered_map<int, OpenHandle> handles_;
};

} // namespace winios::runtime
