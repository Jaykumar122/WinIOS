#include "documents/recent_document_manager.h"

#include <chrono>
#include <sstream>

namespace winios::documents {

RecentDocumentManager::RecentDocumentManager(std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : vfs_(std::move(vfs)) {}

std::uint64_t RecentDocumentManager::current_time_us() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void RecentDocumentManager::add_recent(const std::string& bundle_id,
                                       DocumentHandle doc_handle,
                                       const std::string& display_name,
                                       const std::string& canonical_path) {
    if (bundle_id.empty() || canonical_path.empty()) return;

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto& list = recents_[bundle_id];

    // Check if already in list -> move to front with updated timestamp
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->canonical_path == canonical_path) {
            it->doc_handle = doc_handle;
            it->display_name = display_name;
            it->last_opened_time_us = current_time_us();
            auto entry = *it;
            list.erase(it);
            list.push_front(entry);
            return;
        }
    }

    RecentDocumentEntry entry{};
    entry.doc_handle = doc_handle;
    entry.bundle_id = bundle_id;
    entry.display_name = display_name;
    entry.canonical_path = canonical_path;
    entry.last_opened_time_us = current_time_us();

    list.push_front(std::move(entry));

    // Bound to kMaxRecentHistoryCount (16)
    while (list.size() > kMaxRecentHistoryCount) {
        list.pop_back();
    }
}

std::vector<RecentDocumentEntry> RecentDocumentManager::get_recent(const std::string& bundle_id) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = recents_.find(bundle_id);
    if (it == recents_.end()) return {};

    std::vector<RecentDocumentEntry> result;
    result.reserve(it->second.size());
    for (const auto& entry : it->second) {
        result.push_back(entry);
    }
    return result;
}

bool RecentDocumentManager::remove_recent(const std::string& bundle_id, const std::string& canonical_path) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = recents_.find(bundle_id);
    if (it == recents_.end()) return false;

    for (auto entry_it = it->second.begin(); entry_it != it->second.end(); ++entry_it) {
        if (entry_it->canonical_path == canonical_path) {
            it->second.erase(entry_it);
            return true;
        }
    }
    return false;
}

void RecentDocumentManager::clear_recent(const std::string& bundle_id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    recents_.erase(bundle_id);
}

void RecentDocumentManager::prune_missing() {
    if (!vfs_) return;
    std::lock_guard<std::recursive_mutex> guard(lock_);

    for (auto& [bid, list] : recents_) {
        for (auto it = list.begin(); it != list.end();) {
            if (!vfs_->exists(it->canonical_path)) {
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::string RecentDocumentManager::serialize() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::ostringstream ss;
    ss << "bundle_count=" << recents_.size() << "\n";
    for (const auto& [bid, list] : recents_) {
        ss << "[bundle]\n";
        ss << "bundle_id=" << bid << "\n";
        ss << "entry_count=" << list.size() << "\n";
        for (const auto& entry : list) {
            ss << "[entry]\n";
            ss << "doc_handle=" << entry.doc_handle << "\n";
            ss << "display_name=" << entry.display_name << "\n";
            ss << "canonical_path=" << entry.canonical_path << "\n";
            ss << "last_opened_time_us=" << entry.last_opened_time_us << "\n";
        }
    }
    return ss.str();
}

bool RecentDocumentManager::deserialize(const std::string& data) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    recents_.clear();
    std::istringstream ss(data);
    std::string line;
    std::string cur_bundle;
    RecentDocumentEntry cur_entry{};
    bool in_entry = false;

    auto finish_entry = [&]() {
        if (in_entry && !cur_bundle.empty() && !cur_entry.canonical_path.empty()) {
            recents_[cur_bundle].push_back(cur_entry);
        }
        cur_entry = RecentDocumentEntry{};
        in_entry = false;
    };

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line == "[bundle]") {
            finish_entry();
            cur_bundle.clear();
            continue;
        }
        if (line == "[entry]") {
            finish_entry();
            in_entry = true;
            cur_entry.bundle_id = cur_bundle;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "bundle_id") cur_bundle = val;
        else if (in_entry) {
            if (key == "doc_handle") {
                try { cur_entry.doc_handle = std::stoull(val); } catch (...) {}
            }
            else if (key == "display_name") cur_entry.display_name = val;
            else if (key == "canonical_path") cur_entry.canonical_path = val;
            else if (key == "last_opened_time_us") {
                try { cur_entry.last_opened_time_us = std::stoull(val); } catch (...) {}
            }
        }
    }
    finish_entry();
    return true;
}

void RecentDocumentManager::clear() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    recents_.clear();
}

} // namespace winios::documents
