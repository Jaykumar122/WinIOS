#pragma once

#include "documents/document_types.h"
#include "vfs/vfs.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::documents {

class RecentDocumentManager {
public:
    explicit RecentDocumentManager(std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~RecentDocumentManager() = default;

    RecentDocumentManager(const RecentDocumentManager&) = delete;
    RecentDocumentManager& operator=(const RecentDocumentManager&) = delete;

    void add_recent(const std::string& bundle_id,
                    DocumentHandle doc_handle,
                    const std::string& display_name,
                    const std::string& canonical_path);

    [[nodiscard]] std::vector<RecentDocumentEntry> get_recent(const std::string& bundle_id) const;
    bool remove_recent(const std::string& bundle_id, const std::string& canonical_path);
    void clear_recent(const std::string& bundle_id);
    void prune_missing();

    // Persistence
    [[nodiscard]] std::string serialize() const;
    bool deserialize(const std::string& data);

    void clear();

private:
    [[nodiscard]] std::uint64_t current_time_us() const;

    mutable std::recursive_mutex                            lock_;
    std::shared_ptr<vfs::VirtualFileSystem>                 vfs_;
    std::map<std::string, std::deque<RecentDocumentEntry>>  recents_; // bundle_id -> list
};

} // namespace winios::documents
