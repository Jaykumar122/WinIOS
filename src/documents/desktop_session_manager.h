#pragma once

#include "documents/document_types.h"
#include "documents/recent_document_manager.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::documents {

struct SessionWindowRecord {
    std::string   bundle_id;
    std::string   title;
    std::int32_t  x{0};
    std::int32_t  y{0};
    std::uint32_t width{400};
    std::uint32_t height{300};
    std::uint32_t state{0}; // 0=Normal, 1=Minimized, 4=Maximized
    std::uint32_t z_order{0};
    std::string   associated_doc_path;
};

struct DesktopSessionData {
    std::uint32_t session_version{1};
    std::uint64_t save_timestamp_us{0};
    std::uint32_t display_width{800};
    std::uint32_t display_height{600};
    std::uint32_t active_window_index{0};
    std::vector<SessionWindowRecord> windows;
    std::vector<RecentDocumentEntry> recent_documents;
};

class DesktopSessionManager {
public:
    DesktopSessionManager(std::shared_ptr<packaging::ApplicationRegistry> app_registry = nullptr,
                          std::shared_ptr<RecentDocumentManager> recent_mgr = nullptr,
                          std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~DesktopSessionManager() = default;

    DesktopSessionManager(const DesktopSessionManager&) = delete;
    DesktopSessionManager& operator=(const DesktopSessionManager&) = delete;

    [[nodiscard]] std::string serialize(const DesktopSessionData& session) const;
    bool deserialize(const std::string& data, DesktopSessionData& out_session) const;

    bool save_session_to_vfs(const DesktopSessionData& session,
                             const std::string& vfs_path = "/System/Library/Sessions/desktop_session.json");

    bool load_session_from_vfs(DesktopSessionData& out_session,
                               const std::string& vfs_path = "/System/Library/Sessions/desktop_session.json") const;

    // Filter restored session against installed packages and existing files
    [[nodiscard]] DesktopSessionData sanitize_for_restore(const DesktopSessionData& raw_session) const;

private:
    [[nodiscard]] std::uint64_t current_time_us() const;

    mutable std::recursive_mutex                      lock_;
    std::shared_ptr<packaging::ApplicationRegistry>   app_registry_;
    std::shared_ptr<RecentDocumentManager>            recent_mgr_;
    std::shared_ptr<vfs::VirtualFileSystem>           vfs_;
};

} // namespace winios::documents
