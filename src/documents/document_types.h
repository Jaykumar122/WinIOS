#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winios::documents {

using DocumentHandle = std::uint64_t;
using StreamHandle   = std::uint64_t;

inline constexpr DocumentHandle kInvalidDocumentHandle = 0;
inline constexpr StreamHandle   kInvalidStreamHandle   = 0;

inline constexpr std::size_t kMaxDocumentSizeBytes  = 10 * 1024 * 1024; // 10 MB limit
inline constexpr std::size_t kMaxRecentHistoryCount = 16;
inline constexpr std::size_t kMaxOpenStreamsPerApp  = 32;

// 64-bit Generation Handle helpers: (generation << 32) | id
inline std::uint32_t doc_handle_id(DocumentHandle h) noexcept {
    return static_cast<std::uint32_t>(h & 0xFFFFFFFFULL);
}

inline std::uint32_t doc_handle_gen(DocumentHandle h) noexcept {
    return static_cast<std::uint32_t>((h >> 32) & 0xFFFFFFFFULL);
}

inline DocumentHandle make_doc_handle(std::uint32_t id, std::uint32_t gen) noexcept {
    return (static_cast<std::uint64_t>(gen) << 32) | static_cast<std::uint64_t>(id);
}

inline std::uint32_t stream_handle_id(StreamHandle h) noexcept {
    return static_cast<std::uint32_t>(h & 0xFFFFFFFFULL);
}

inline std::uint32_t stream_handle_gen(StreamHandle h) noexcept {
    return static_cast<std::uint32_t>((h >> 32) & 0xFFFFFFFFULL);
}

inline StreamHandle make_stream_handle(std::uint32_t id, std::uint32_t gen) noexcept {
    return (static_cast<std::uint64_t>(gen) << 32) | static_cast<std::uint64_t>(id);
}

// Access modes
enum class DocumentAccessMode : std::uint32_t {
    None      = 0,
    Read      = 1 << 0,
    Write     = 1 << 1,
    ReadWrite = Read | Write
};

inline DocumentAccessMode operator|(DocumentAccessMode a, DocumentAccessMode b) noexcept {
    return static_cast<DocumentAccessMode>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline bool has_access_flag(DocumentAccessMode mask, DocumentAccessMode flag) noexcept {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(flag)) != 0;
}

// Document Metadata
struct DocumentMetadata {
    DocumentHandle handle{kInvalidDocumentHandle};
    std::string    doc_id; // Stable UUID or sequence string e.g. "doc-1001"
    std::string    owner_bundle_id;
    std::uint32_t  owner_pid{0};
    std::string    display_name;
    std::string    canonical_path; // Virtual path in VFS e.g. "/Documents/notes.txt"
    std::string    mime_type{"text/plain"};
    std::uint64_t  file_size{0};
    std::uint64_t  created_time_us{0};
    std::uint64_t  modified_time_us{0};
    bool           is_read_only{false};
};

// Document Access Grant (for cross-process sharing)
struct DocumentAccessGrant {
    std::uint64_t  grant_token{0};
    DocumentHandle doc_handle{kInvalidDocumentHandle};
    std::uint32_t  grantor_pid{0};
    std::uint32_t  grantee_pid{0};
    std::string    grantee_bundle_id;
    std::uint32_t  mode{static_cast<std::uint32_t>(DocumentAccessMode::Read)};
    std::uint64_t  granted_time_us{0};
    std::uint64_t  expiry_time_us{0}; // 0 = permanent until revoked or process exit
    bool           is_revoked{false};
};

// Recent Document Entry
struct RecentDocumentEntry {
    DocumentHandle doc_handle{kInvalidDocumentHandle};
    std::string    bundle_id;
    std::string    display_name;
    std::string    canonical_path;
    std::uint64_t  last_opened_time_us{0};
};

// File Picker Mode and State
enum class FilePickerMode : std::uint32_t {
    OpenDocument = 1,
    SaveDocument = 2
};

enum class FilePickerState : std::uint32_t {
    Idle      = 0,
    Active    = 1,
    Accepted  = 2,
    Cancelled = 3
};

struct FilePickerResult {
    FilePickerState state{FilePickerState::Idle};
    DocumentHandle  doc_handle{kInvalidDocumentHandle};
    std::string     selected_path;
    std::string     display_name;
};

} // namespace winios::documents
