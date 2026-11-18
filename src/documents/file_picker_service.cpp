#include "documents/file_picker_service.h"

#include <chrono>
#include <algorithm>

namespace winios::documents {

FilePickerService::FilePickerService(std::shared_ptr<DocumentRegistry> registry,
                                     std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : registry_(std::move(registry)), vfs_(std::move(vfs)) {}

std::uint64_t FilePickerService::current_time_us() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool FilePickerService::request_open(std::uint32_t caller_pid,
                                     std::uint64_t caller_window,
                                     const std::string& filter_extension,
                                     std::uint32_t& out_req_id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const std::uint32_t req_id = next_req_id_++;

    FilePickerRequest req{};
    req.request_id = req_id;
    req.caller_pid = caller_pid;
    req.caller_window = caller_window;
    req.mode = FilePickerMode::OpenDocument;
    req.filter_extension = filter_extension;
    req.state = FilePickerState::Active;
    req.start_time_us = current_time_us();

    requests_[req_id] = std::move(req);
    out_req_id = req_id;
    return true;
}

bool FilePickerService::request_save(std::uint32_t caller_pid,
                                     std::uint64_t caller_window,
                                     const std::string& default_name,
                                     const std::string& filter_extension,
                                     std::uint32_t& out_req_id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const std::uint32_t req_id = next_req_id_++;

    FilePickerRequest req{};
    req.request_id = req_id;
    req.caller_pid = caller_pid;
    req.caller_window = caller_window;
    req.mode = FilePickerMode::SaveDocument;
    req.default_name = default_name.empty() ? "Untitled.txt" : default_name;
    req.filter_extension = filter_extension;
    req.state = FilePickerState::Active;
    req.start_time_us = current_time_us();

    requests_[req_id] = std::move(req);
    out_req_id = req_id;
    return true;
}

bool FilePickerService::accept_selection(std::uint32_t req_id,
                                         const std::string& chosen_path,
                                         const std::string& display_name) {
    std::string clean_path;
    if (!DocumentRegistry::sanitize_and_validate_path(chosen_path, clean_path)) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = requests_.find(req_id);
    if (it == requests_.end() || it->second.state != FilePickerState::Active) {
        return false;
    }

    auto& req = it->second;

    // Check extension filter
    if (!req.filter_extension.empty() && req.filter_extension != "*") {
        if (clean_path.size() < req.filter_extension.size() ||
            clean_path.substr(clean_path.size() - req.filter_extension.size()) != req.filter_extension) {
            return false;
        }
    }

    // Determine display name if not specified
    std::string dname = display_name;
    if (dname.empty()) {
        const auto slash_pos = clean_path.find_last_of('/');
        dname = (slash_pos != std::string::npos) ? clean_path.substr(slash_pos + 1) : clean_path;
    }

    // Look up or auto-register document in registry if registry is set
    DocumentHandle doc_h = kInvalidDocumentHandle;
    if (registry_) {
        auto existing = registry_->get_by_path(clean_path);
        if (existing) {
            doc_h = existing->handle;
        } else if (req.mode == FilePickerMode::SaveDocument) {
            doc_h = registry_->register_document(req.caller_pid, "picker", dname, clean_path);
        }
    }

    req.state = FilePickerState::Accepted;
    req.result.state = FilePickerState::Accepted;
    req.result.doc_handle = doc_h;
    req.result.selected_path = clean_path;
    req.result.display_name = dname;

    return true;
}

bool FilePickerService::cancel_request(std::uint32_t req_id, std::uint32_t caller_pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = requests_.find(req_id);
    if (it == requests_.end()) return false;

    if (it->second.caller_pid != caller_pid && caller_pid != 1) {
        return false; // Unauthorized cancel
    }

    it->second.state = FilePickerState::Cancelled;
    it->second.result.state = FilePickerState::Cancelled;
    return true;
}

std::optional<FilePickerResult> FilePickerService::poll_result(std::uint32_t req_id, std::uint32_t caller_pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = requests_.find(req_id);
    if (it == requests_.end()) return std::nullopt;

    if (it->second.caller_pid != caller_pid && caller_pid != 1) {
        return std::nullopt;
    }

    if (it->second.state == FilePickerState::Active) {
        return std::nullopt; // Still awaiting selection
    }

    FilePickerResult res = it->second.result;
    requests_.erase(it); // Consume result
    return res;
}

std::optional<FilePickerRequest> FilePickerService::get_request(std::uint32_t req_id) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = requests_.find(req_id);
    if (it != requests_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void FilePickerService::cleanup_for_process(std::uint32_t pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = requests_.begin(); it != requests_.end();) {
        if (it->second.caller_pid == pid) {
            it = requests_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::string> FilePickerService::list_candidates(const std::string& dir_path,
                                                           const std::string& filter_extension) const {
    std::vector<std::string> candidates;
    if (!vfs_) return candidates;

    std::string clean_dir;
    if (!DocumentRegistry::sanitize_and_validate_path(dir_path, clean_dir)) {
        return candidates;
    }

    std::vector<vfs::VfsDirEntry> entries;
    if (vfs_->readdir(clean_dir, entries)) {
        for (const auto& entry : entries) {
            if (entry.name == "." || entry.name == "..") continue;
            if (!filter_extension.empty() && filter_extension != "*") {
                if (entry.name.size() < filter_extension.size() ||
                    entry.name.substr(entry.name.size() - filter_extension.size()) != filter_extension) {
                    continue;
                }
            }
            std::string full_p = clean_dir;
            if (full_p.back() != '/') full_p += '/';
            full_p += entry.name;
            candidates.push_back(full_p);
        }
    }
    return candidates;
}

void FilePickerService::clear() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    requests_.clear();
    next_req_id_ = 1;
}

} // namespace winios::documents
