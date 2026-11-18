#pragma once

#include "documents/document_registry.h"
#include "documents/document_types.h"
#include "vfs/vfs.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::documents {

struct FilePickerRequest {
    std::uint32_t   request_id{0};
    std::uint32_t   caller_pid{0};
    std::uint64_t   caller_window{0};
    FilePickerMode  mode{FilePickerMode::OpenDocument};
    std::string     filter_extension;
    std::string     default_name;
    FilePickerState state{FilePickerState::Idle};
    FilePickerResult result{};
    std::uint64_t   start_time_us{0};
};

class FilePickerService {
public:
    FilePickerService(std::shared_ptr<DocumentRegistry> registry,
                      std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~FilePickerService() = default;

    FilePickerService(const FilePickerService&) = delete;
    FilePickerService& operator=(const FilePickerService&) = delete;

    bool request_open(std::uint32_t caller_pid,
                      std::uint64_t caller_window,
                      const std::string& filter_extension,
                      std::uint32_t& out_req_id);

    bool request_save(std::uint32_t caller_pid,
                      std::uint64_t caller_window,
                      const std::string& default_name,
                      const std::string& filter_extension,
                      std::uint32_t& out_req_id);

    bool accept_selection(std::uint32_t req_id,
                          const std::string& chosen_path,
                          const std::string& display_name = "");

    bool cancel_request(std::uint32_t req_id, std::uint32_t caller_pid);

    [[nodiscard]] std::optional<FilePickerResult> poll_result(std::uint32_t req_id, std::uint32_t caller_pid);
    [[nodiscard]] std::optional<FilePickerRequest> get_request(std::uint32_t req_id) const;

    void cleanup_for_process(std::uint32_t pid);

    [[nodiscard]] std::vector<std::string> list_candidates(const std::string& dir_path,
                                                           const std::string& filter_extension) const;

    void clear();

private:
    [[nodiscard]] std::uint64_t current_time_us() const;

    mutable std::recursive_mutex             lock_;
    std::shared_ptr<DocumentRegistry>        registry_;
    std::shared_ptr<vfs::VirtualFileSystem>  vfs_;
    std::uint32_t                            next_req_id_{1};
    std::map<std::uint32_t, FilePickerRequest> requests_;
};

} // namespace winios::documents
