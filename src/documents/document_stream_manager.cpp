#include "documents/document_stream_manager.h"

#include <cstring>
#include <algorithm>

namespace winios::documents {

DocumentStreamManager::DocumentStreamManager(std::shared_ptr<DocumentRegistry> registry,
                                             std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : registry_(std::move(registry)), vfs_(std::move(vfs)) {}

StreamHandle DocumentStreamManager::open_stream(DocumentHandle doc_handle,
                                                std::uint32_t caller_pid,
                                                DocumentAccessMode mode) {
    if (!registry_) return kInvalidStreamHandle;

    if (!registry_->is_handle_valid(doc_handle)) {
        return kInvalidStreamHandle;
    }

    if (!registry_->check_access(doc_handle, caller_pid, mode)) {
        return kInvalidStreamHandle;
    }

    auto meta = registry_->get_metadata(doc_handle);
    if (!meta) return kInvalidStreamHandle;

    std::lock_guard<std::recursive_mutex> guard(lock_);

    // Count open streams for this process
    std::size_t proc_open_count = 0;
    for (const auto& [id, st] : streams_) {
        if (st.owner_pid == caller_pid) {
            proc_open_count++;
        }
    }
    if (proc_open_count >= kMaxOpenStreamsPerApp) {
        return kInvalidStreamHandle;
    }

    const std::uint32_t id = next_stream_id_++;
    std::uint32_t gen = 1;
    auto gen_it = generations_.find(id);
    if (gen_it != generations_.end()) {
        gen = gen_it->second;
    } else {
        generations_[id] = 1;
    }

    DocumentStream stream{};
    stream.handle = make_stream_handle(id, gen);
    stream.doc_handle = doc_handle;
    stream.owner_pid = caller_pid;
    stream.mode = mode;
    stream.cursor = 0;
    stream.is_dirty = false;
    stream.generation = gen;

    // Load initial content from VFS if available
    if (vfs_ && !meta->canonical_path.empty() && vfs_->exists(meta->canonical_path)) {
        std::vector<std::uint8_t> initial_data;
        if (vfs_->read_file(meta->canonical_path, initial_data)) {
            if (initial_data.size() <= kMaxDocumentSizeBytes) {
                stream.buffer = std::move(initial_data);
            }
        }
    }

    streams_[id] = std::move(stream);
    return make_stream_handle(id, gen);
}

bool DocumentStreamManager::read_stream(StreamHandle stream_handle,
                                        std::uint32_t caller_pid,
                                        void* dst,
                                        std::size_t len,
                                        std::size_t& bytes_read) {
    bytes_read = 0;
    if (stream_handle == kInvalidStreamHandle || dst == nullptr || len == 0) {
        return false;
    }

    const std::uint32_t id = stream_handle_id(stream_handle);
    const std::uint32_t gen = stream_handle_gen(stream_handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) {
        return false; // Stale handle
    }

    auto it = streams_.find(id);
    if (it == streams_.end()) return false;

    if (it->second.owner_pid != caller_pid && caller_pid != 1) {
        return false; // Not stream owner
    }

    if (!has_access_flag(it->second.mode, DocumentAccessMode::Read)) {
        return false;
    }

    auto& stream = it->second;
    if (stream.cursor >= stream.buffer.size()) {
        bytes_read = 0;
        return true; // EOF reached cleanly
    }

    const std::size_t available = stream.buffer.size() - static_cast<std::size_t>(stream.cursor);
    const std::size_t to_copy = std::min(len, available);

    std::memcpy(dst, stream.buffer.data() + stream.cursor, to_copy);
    stream.cursor += to_copy;
    bytes_read = to_copy;
    return true;
}

bool DocumentStreamManager::write_stream(StreamHandle stream_handle,
                                         std::uint32_t caller_pid,
                                         const void* src,
                                         std::size_t len,
                                         std::size_t& bytes_written) {
    bytes_written = 0;
    if (stream_handle == kInvalidStreamHandle || src == nullptr || len == 0) {
        return false;
    }

    const std::uint32_t id = stream_handle_id(stream_handle);
    const std::uint32_t gen = stream_handle_gen(stream_handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) {
        return false;
    }

    auto it = streams_.find(id);
    if (it == streams_.end()) return false;

    if (it->second.owner_pid != caller_pid && caller_pid != 1) {
        return false;
    }

    if (!has_access_flag(it->second.mode, DocumentAccessMode::Write)) {
        return false;
    }

    auto& stream = it->second;

    // Overflow check on cursor + len
    if (stream.cursor > UINT64_MAX - len) {
        return false;
    }

    const std::uint64_t end_pos = stream.cursor + len;
    if (end_pos > kMaxDocumentSizeBytes) {
        return false; // Exceeds 10 MB limit
    }

    if (end_pos > stream.buffer.size()) {
        stream.buffer.resize(static_cast<std::size_t>(end_pos), 0);
    }

    std::memcpy(stream.buffer.data() + stream.cursor, src, len);
    stream.cursor += len;
    stream.is_dirty = true;
    bytes_written = len;

    return true;
}

bool DocumentStreamManager::seek_stream(StreamHandle stream_handle,
                                        std::uint32_t caller_pid,
                                        std::uint64_t offset,
                                        std::uint64_t& new_cursor) {
    const std::uint32_t id = stream_handle_id(stream_handle);
    const std::uint32_t gen = stream_handle_gen(stream_handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = streams_.find(id);
    if (it == streams_.end()) return false;

    if (it->second.owner_pid != caller_pid && caller_pid != 1) return false;

    if (offset > kMaxDocumentSizeBytes) {
        return false;
    }

    if (offset > it->second.buffer.size()) {
        // Clamped or reject if beyond current content
        offset = it->second.buffer.size();
    }

    it->second.cursor = offset;
    new_cursor = offset;
    return true;
}

bool DocumentStreamManager::flush_stream(StreamHandle stream_handle,
                                         std::uint32_t caller_pid) {
    const std::uint32_t id = stream_handle_id(stream_handle);
    const std::uint32_t gen = stream_handle_gen(stream_handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = streams_.find(id);
    if (it == streams_.end()) return false;

    if (it->second.owner_pid != caller_pid && caller_pid != 1) return false;

    if (!it->second.is_dirty) {
        return true; // No pending modifications
    }

    auto meta = registry_ ? registry_->get_metadata(it->second.doc_handle) : std::nullopt;
    if (!meta) return false;

    // Stage atomic write to VFS
    if (vfs_) {
        const std::string tmp_path = meta->canonical_path + ".tmp";
        // 1. Write staging file
        if (!vfs_->write_file(tmp_path, it->second.buffer.data(), it->second.buffer.size())) {
            return false;
        }
        // 2. Promote to target path
        if (!vfs_->write_file(meta->canonical_path, it->second.buffer.data(), it->second.buffer.size())) {
            vfs_->unlink(tmp_path);
            return false;
        }
        // 3. Remove temporary file
        vfs_->unlink(tmp_path);
    }

    // Update registry metadata
    if (registry_) {
        registry_->update_file_size(it->second.doc_handle, it->second.buffer.size(), caller_pid);
        registry_->update_modified_time(it->second.doc_handle, caller_pid);
    }

    it->second.is_dirty = false;
    return true;
}

bool DocumentStreamManager::close_stream(StreamHandle stream_handle,
                                         std::uint32_t caller_pid) {
    const std::uint32_t id = stream_handle_id(stream_handle);
    const std::uint32_t gen = stream_handle_gen(stream_handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = streams_.find(id);
    if (it == streams_.end()) return false;

    if (it->second.owner_pid != caller_pid && caller_pid != 1) return false;

    // Auto-flush if dirty
    if (it->second.is_dirty && has_access_flag(it->second.mode, DocumentAccessMode::Write)) {
        auto meta = registry_ ? registry_->get_metadata(it->second.doc_handle) : std::nullopt;
        if (meta && vfs_) {
            vfs_->write_file(meta->canonical_path, it->second.buffer.data(), it->second.buffer.size());
            if (registry_) {
                registry_->update_file_size(it->second.doc_handle, it->second.buffer.size(), caller_pid);
                registry_->update_modified_time(it->second.doc_handle, caller_pid);
            }
        }
    }

    // Invalidate handle via generation increment
    std::uint32_t next_gen = gen + 1;
    if (next_gen == 0) next_gen = 1;
    generations_[id] = next_gen;

    streams_.erase(it);
    return true;
}

void DocumentStreamManager::close_all_for_process(std::uint32_t pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::vector<std::uint32_t> to_remove;
    for (const auto& [id, stream] : streams_) {
        if (stream.owner_pid == pid) {
            to_remove.push_back(id);
        }
    }
    for (std::uint32_t id : to_remove) {
        auto it = streams_.find(id);
        if (it != streams_.end()) {
            std::uint32_t next_gen = it->second.generation + 1;
            if (next_gen == 0) next_gen = 1;
            generations_[id] = next_gen;
            streams_.erase(it);
        }
    }
}

bool DocumentStreamManager::is_stream_valid(StreamHandle handle) const {
    if (handle == kInvalidStreamHandle) return false;
    const std::uint32_t id = stream_handle_id(handle);
    const std::uint32_t gen = stream_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;
    return streams_.find(id) != streams_.end();
}

std::size_t DocumentStreamManager::get_stream_size(StreamHandle handle) const {
    const std::uint32_t id = stream_handle_id(handle);
    const std::uint32_t gen = stream_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return 0;

    auto it = streams_.find(id);
    if (it != streams_.end()) {
        return it->second.buffer.size();
    }
    return 0;
}

bool DocumentStreamManager::get_stream_content(StreamHandle handle, std::vector<std::uint8_t>& out_content) const {
    const std::uint32_t id = stream_handle_id(handle);
    const std::uint32_t gen = stream_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = streams_.find(id);
    if (it != streams_.end()) {
        out_content = it->second.buffer;
        return true;
    }
    return false;
}

bool DocumentStreamManager::atomic_save_document(DocumentHandle doc_handle,
                                                 std::uint32_t caller_pid,
                                                 const void* data,
                                                 std::size_t size) {
    if (!registry_) return false;
    if (!registry_->is_handle_valid(doc_handle)) return false;
    if (!registry_->check_access(doc_handle, caller_pid, DocumentAccessMode::Write)) return false;
    if (size > kMaxDocumentSizeBytes) return false;

    auto meta = registry_->get_metadata(doc_handle);
    if (!meta) return false;

    if (vfs_) {
        const std::string tmp_path = meta->canonical_path + ".tmp";
        // Write temporary file
        if (!vfs_->write_file(tmp_path, data, size)) {
            return false;
        }
        // Promote to canonical file
        if (!vfs_->write_file(meta->canonical_path, data, size)) {
            vfs_->unlink(tmp_path);
            return false;
        }
        vfs_->unlink(tmp_path);
    }

    registry_->update_file_size(doc_handle, size, caller_pid);
    registry_->update_modified_time(doc_handle, caller_pid);
    return true;
}

bool DocumentStreamManager::recover_staged_save(const std::string& canonical_path) {
    if (!vfs_) return false;
    const std::string tmp_path = canonical_path + ".tmp";
    if (vfs_->exists(tmp_path)) {
        // Recover: read staged file and promote to canonical
        std::vector<std::uint8_t> tmp_content;
        if (vfs_->read_file(tmp_path, tmp_content)) {
            vfs_->write_file(canonical_path, tmp_content.data(), tmp_content.size());
        }
        vfs_->unlink(tmp_path);
        return true;
    }
    return false;
}

void DocumentStreamManager::clear() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    streams_.clear();
    generations_.clear();
    next_stream_id_ = 1;
}

} // namespace winios::documents
