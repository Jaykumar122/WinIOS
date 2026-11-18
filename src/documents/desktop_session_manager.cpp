#include "documents/desktop_session_manager.h"

#include <chrono>
#include <sstream>

namespace winios::documents {

DesktopSessionManager::DesktopSessionManager(std::shared_ptr<packaging::ApplicationRegistry> app_registry,
                                             std::shared_ptr<RecentDocumentManager> recent_mgr,
                                             std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : app_registry_(std::move(app_registry)),
      recent_mgr_(std::move(recent_mgr)),
      vfs_(std::move(vfs)) {}

std::uint64_t DesktopSessionManager::current_time_us() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string DesktopSessionManager::serialize(const DesktopSessionData& session) const {
    std::ostringstream ss;
    ss << "session_version=" << session.session_version << "\n";
    ss << "save_timestamp_us=" << (session.save_timestamp_us == 0 ? current_time_us() : session.save_timestamp_us) << "\n";
    ss << "display_width=" << session.display_width << "\n";
    ss << "display_height=" << session.display_height << "\n";
    ss << "active_window_index=" << session.active_window_index << "\n";
    ss << "window_count=" << session.windows.size() << "\n";

    for (const auto& win : session.windows) {
        ss << "[window]\n";
        ss << "bundle_id=" << win.bundle_id << "\n";
        ss << "title=" << win.title << "\n";
        ss << "x=" << win.x << "\n";
        ss << "y=" << win.y << "\n";
        ss << "width=" << win.width << "\n";
        ss << "height=" << win.height << "\n";
        ss << "state=" << win.state << "\n";
        ss << "z_order=" << win.z_order << "\n";
        ss << "associated_doc_path=" << win.associated_doc_path << "\n";
    }

    ss << "recent_count=" << session.recent_documents.size() << "\n";
    for (const auto& rec : session.recent_documents) {
        ss << "[recent]\n";
        ss << "bundle_id=" << rec.bundle_id << "\n";
        ss << "display_name=" << rec.display_name << "\n";
        ss << "canonical_path=" << rec.canonical_path << "\n";
        ss << "last_opened_time_us=" << rec.last_opened_time_us << "\n";
    }

    return ss.str();
}

bool DesktopSessionManager::deserialize(const std::string& data, DesktopSessionData& out_session) const {
    if (data.empty()) return false;

    DesktopSessionData session{};
    session.session_version = 0;
    std::istringstream ss(data);
    std::string line;
    SessionWindowRecord cur_win{};
    RecentDocumentEntry cur_rec{};
    bool in_window = false;
    bool in_recent = false;

    auto finish_window = [&]() {
        if (in_window && !cur_win.bundle_id.empty()) {
            session.windows.push_back(cur_win);
        }
        cur_win = SessionWindowRecord{};
        in_window = false;
    };

    auto finish_recent = [&]() {
        if (in_recent && !cur_rec.canonical_path.empty()) {
            session.recent_documents.push_back(cur_rec);
        }
        cur_rec = RecentDocumentEntry{};
        in_recent = false;
    };

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line == "[window]") {
            finish_recent();
            finish_window();
            in_window = true;
            continue;
        }
        if (line == "[recent]") {
            finish_window();
            finish_recent();
            in_recent = true;
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (in_window) {
            if (key == "bundle_id") cur_win.bundle_id = val;
            else if (key == "title") cur_win.title = val;
            else if (key == "x") {
                try { cur_win.x = std::stoi(val); } catch (...) {}
            }
            else if (key == "y") {
                try { cur_win.y = std::stoi(val); } catch (...) {}
            }
            else if (key == "width") {
                try { cur_win.width = std::stoul(val); } catch (...) {}
            }
            else if (key == "height") {
                try { cur_win.height = std::stoul(val); } catch (...) {}
            }
            else if (key == "state") {
                try { cur_win.state = std::stoul(val); } catch (...) {}
            }
            else if (key == "z_order") {
                try { cur_win.z_order = std::stoul(val); } catch (...) {}
            }
            else if (key == "associated_doc_path") cur_win.associated_doc_path = val;
            continue;
        }

        if (in_recent) {
            if (key == "bundle_id") cur_rec.bundle_id = val;
            else if (key == "display_name") cur_rec.display_name = val;
            else if (key == "canonical_path") cur_rec.canonical_path = val;
            else if (key == "last_opened_time_us") {
                try { cur_rec.last_opened_time_us = std::stoull(val); } catch (...) {}
            }
            continue;
        }

        // Global session header keys
        if (key == "session_version") {
            try { session.session_version = std::stoul(val); } catch (...) {}
        }
        else if (key == "save_timestamp_us") {
            try { session.save_timestamp_us = std::stoull(val); } catch (...) {}
        }
        else if (key == "display_width") {
            try { session.display_width = std::stoul(val); } catch (...) {}
        }
        else if (key == "display_height") {
            try { session.display_height = std::stoul(val); } catch (...) {}
        }
        else if (key == "active_window_index") {
            try { session.active_window_index = std::stoul(val); } catch (...) {}
        }
    }
    finish_window();
    finish_recent();

    if (session.session_version == 0) {
        return false;
    }

    out_session = std::move(session);
    return true;
}

bool DesktopSessionManager::save_session_to_vfs(const DesktopSessionData& session,
                                                const std::string& vfs_path) {
    if (!vfs_ || vfs_path.empty()) return false;

    const std::string serialized = serialize(session);
    const std::string tmp_path = vfs_path + ".tmp";

    // Ensure parent directory exists
    const auto slash_pos = vfs_path.find_last_of('/');
    if (slash_pos != std::string::npos && slash_pos > 0) {
        const std::string parent_dir = vfs_path.substr(0, slash_pos);
        vfs_->mkdir(parent_dir);
    }

    // Atomic save: write to .tmp, then overwrite target path
    if (!vfs_->write_file(tmp_path, serialized.data(), serialized.size())) {
        return false;
    }

    if (!vfs_->write_file(vfs_path, serialized.data(), serialized.size())) {
        vfs_->unlink(tmp_path);
        return false;
    }

    vfs_->unlink(tmp_path);
    return true;
}

bool DesktopSessionManager::load_session_from_vfs(DesktopSessionData& out_session,
                                                  const std::string& vfs_path) const {
    if (!vfs_ || vfs_path.empty() || !vfs_->exists(vfs_path)) {
        return false;
    }

    std::vector<std::uint8_t> data;
    if (!vfs_->read_file(vfs_path, data) || data.empty()) {
        return false;
    }

    const std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    return deserialize(text, out_session);
}

DesktopSessionData DesktopSessionManager::sanitize_for_restore(const DesktopSessionData& raw_session) const {
    DesktopSessionData clean = raw_session;
    clean.windows.clear();
    clean.recent_documents.clear();

    // 1. Sanitize windows: keep only windows whose app is installed and active
    for (const auto& win : raw_session.windows) {
        if (app_registry_) {
            if (!app_registry_->is_installed(win.bundle_id) || !app_registry_->is_active(win.bundle_id)) {
                continue; // Skip unavailable app
            }
        }
        // If window has associated doc path, verify it exists in VFS
        if (!win.associated_doc_path.empty() && vfs_) {
            if (!vfs_->exists(win.associated_doc_path)) {
                // Keep window, but detach missing doc
                SessionWindowRecord safe_win = win;
                safe_win.associated_doc_path.clear();
                safe_win.title = win.title + " (Untitled)";
                clean.windows.push_back(safe_win);
                continue;
            }
        }
        clean.windows.push_back(win);
    }

    // 2. Sanitize recents: keep only recents that exist in VFS
    for (const auto& rec : raw_session.recent_documents) {
        if (vfs_ && !vfs_->exists(rec.canonical_path)) {
            continue; // Skip non-existent file
        }
        clean.recent_documents.push_back(rec);
    }

    // Adjust active window index
    if (clean.active_window_index >= clean.windows.size()) {
        clean.active_window_index = clean.windows.empty() ? 0 : static_cast<std::uint32_t>(clean.windows.size() - 1);
    }

    return clean;
}

} // namespace winios::documents
