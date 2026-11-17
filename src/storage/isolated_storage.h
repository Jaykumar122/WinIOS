#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vfs/vfs.h"

namespace winios::storage {

enum class StorageResult : std::uint32_t {
    Success = 0,
    InvalidPath = 1,
    AccessDenied = 2,
    QuotaExceeded = 3,
    FileNotFound = 4,
    IoError = 5,
    CorruptedData = 6
};

inline const char* storage_result_to_string(StorageResult res) noexcept {
    switch (res) {
    case StorageResult::Success: return "Success";
    case StorageResult::InvalidPath: return "InvalidPath";
    case StorageResult::AccessDenied: return "AccessDenied";
    case StorageResult::QuotaExceeded: return "QuotaExceeded";
    case StorageResult::FileNotFound: return "FileNotFound";
    case StorageResult::IoError: return "IoError";
    case StorageResult::CorruptedData: return "CorruptedData";
    default: return "UnknownStorageResult";
    }
}

class IsolatedStorage {
public:
    static constexpr std::size_t kDefaultAppQuotaBytes = 10 * 1024 * 1024; // 10 MB

    explicit IsolatedStorage(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                             std::string app_id,
                             std::size_t quota_bytes = kDefaultAppQuotaBytes);
    ~IsolatedStorage() = default;

    // Validate that a relative path stays within the sandbox
    [[nodiscard]] bool is_path_safe(const std::string& rel_path) const noexcept;

    // Resolve full VFS path (/data/<app_id>/<rel_path>)
    [[nodiscard]] std::string resolve_path(const std::string& rel_path) const;

    // Atomic write: write to temp file, flush, rename
    StorageResult atomic_write(const std::string& rel_path,
                               const void* data,
                               std::size_t size,
                               bool verify_sha256 = true);

    // Read file contents
    StorageResult read(const std::string& rel_path,
                       std::vector<std::uint8_t>& out_data) const;

    // Delete file
    StorageResult remove(const std::string& rel_path);

    // Check if file exists
    [[nodiscard]] bool exists(const std::string& rel_path) const;

    // Clean up any stale .tmp files from crashed transactions
    std::size_t recover_transactions();

    // Usage and quota
    [[nodiscard]] std::size_t current_usage_bytes() const;
    [[nodiscard]] std::size_t quota_bytes() const noexcept { return quota_bytes_; }
    [[nodiscard]] const std::string& app_id() const noexcept { return app_id_; }

private:
    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    std::string                             app_id_;
    std::string                             root_path_;
    std::size_t                             quota_bytes_;
    mutable std::mutex                      lock_;
    std::uint64_t                           tx_counter_{0};
};

} // namespace winios::storage
