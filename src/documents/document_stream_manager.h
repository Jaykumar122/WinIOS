#pragma once

#include "documents/document_registry.h"
#include "documents/document_types.h"
#include "vfs/vfs.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::documents {

struct DocumentStream {
    StreamHandle       handle{kInvalidStreamHandle};
    DocumentHandle     doc_handle{kInvalidDocumentHandle};
    std::uint32_t      owner_pid{0};
    DocumentAccessMode mode{DocumentAccessMode::Read};
    std::uint64_t      cursor{0};
    std::vector<std::uint8_t> buffer;
    bool               is_dirty{false};
    std::uint32_t      generation{1};
};

class DocumentStreamManager {
public:
    DocumentStreamManager(std::shared_ptr<DocumentRegistry> registry,
                          std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~DocumentStreamManager() = default;

    DocumentStreamManager(const DocumentStreamManager&) = delete;
    DocumentStreamManager& operator=(const DocumentStreamManager&) = delete;

    StreamHandle open_stream(DocumentHandle doc_handle,
                             std::uint32_t caller_pid,
                             DocumentAccessMode mode);

    bool read_stream(StreamHandle stream_handle,
                     std::uint32_t caller_pid,
                     void* dst,
                     std::size_t len,
                     std::size_t& bytes_read);

    bool write_stream(StreamHandle stream_handle,
                      std::uint32_t caller_pid,
                      const void* src,
                      std::size_t len,
                      std::size_t& bytes_written);

    bool seek_stream(StreamHandle stream_handle,
                     std::uint32_t caller_pid,
                     std::uint64_t offset,
                     std::uint64_t& new_cursor);

    bool flush_stream(StreamHandle stream_handle,
                      std::uint32_t caller_pid);

    bool close_stream(StreamHandle stream_handle,
                      std::uint32_t caller_pid);

    void close_all_for_process(std::uint32_t pid);

    [[nodiscard]] bool is_stream_valid(StreamHandle handle) const;
    [[nodiscard]] std::size_t get_stream_size(StreamHandle handle) const;
    [[nodiscard]] bool get_stream_content(StreamHandle handle, std::vector<std::uint8_t>& out_content) const;

    // Atomic Save & Recovery
    bool atomic_save_document(DocumentHandle doc_handle,
                              std::uint32_t caller_pid,
                              const void* data,
                              std::size_t size);

    bool recover_staged_save(const std::string& canonical_path);

    void clear();

private:
    mutable std::recursive_mutex             lock_;
    std::shared_ptr<DocumentRegistry>        registry_;
    std::shared_ptr<vfs::VirtualFileSystem>  vfs_;
    std::uint32_t                            next_stream_id_{1};
    std::map<std::uint32_t, std::uint32_t>   generations_; // id -> generation
    std::map<std::uint32_t, DocumentStream>  streams_;     // id -> stream
};

} // namespace winios::documents
