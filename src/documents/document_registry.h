#pragma once

#include "documents/document_types.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"

#include <chrono>
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::documents {

class DocumentRegistry {
public:
    explicit DocumentRegistry(std::shared_ptr<security::CapabilityManager> cap_mgr = nullptr,
                            std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr = nullptr);
    ~DocumentRegistry() = default;

    DocumentRegistry(const DocumentRegistry&) = delete;
    DocumentRegistry& operator=(const DocumentRegistry&) = delete;

    void set_identity_manager(std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr) {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        identity_mgr_ = std::move(identity_mgr);
    }
    [[nodiscard]] std::shared_ptr<kernel::ProcessIdentityManager> identity_manager() const {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        return identity_mgr_;
    }

    // Document lifecycle
    DocumentHandle register_document(std::uint32_t caller_pid,
                                     const std::string& bundle_id,
                                     const std::string& display_name,
                                     const std::string& canonical_path,
                                     const std::string& mime_type = "text/plain",
                                     std::uint64_t initial_size = 0,
                                     bool is_read_only = false);

    bool unregister_document(DocumentHandle handle, std::uint32_t caller_pid);

    [[nodiscard]] bool is_handle_valid(DocumentHandle handle) const;
    [[nodiscard]] std::optional<DocumentMetadata> get_metadata(DocumentHandle handle) const;
    [[nodiscard]] std::optional<DocumentMetadata> get_by_path(const std::string& canonical_path) const;
    [[nodiscard]] std::vector<DocumentMetadata> list_documents_for_process(std::uint32_t caller_pid) const;
    [[nodiscard]] std::vector<DocumentMetadata> list_all_documents() const;

    bool update_file_size(DocumentHandle handle, std::uint64_t new_size, std::uint32_t caller_pid);
    bool update_modified_time(DocumentHandle handle, std::uint32_t caller_pid);

    // Capability and ownership access verification
    [[nodiscard]] bool check_access(DocumentHandle handle,
                                    std::uint32_t caller_pid,
                                    DocumentAccessMode mode) const;

    [[nodiscard]] bool check_access(DocumentHandle handle,
                                    kernel::ProcessHandle caller,
                                    DocumentAccessMode mode) const;

    // Sharing and access grants (PID-based and ProcessHandle-bound)
    DocumentAccessGrant create_grant(DocumentHandle handle,
                                     std::uint32_t grantor_pid,
                                     std::uint32_t grantee_pid,
                                     const std::string& grantee_bundle_id,
                                     DocumentAccessMode mode,
                                     std::uint64_t expiry_us = 0);

    DocumentAccessGrant create_grant(DocumentHandle handle,
                                     kernel::ProcessHandle grantor,
                                     kernel::ProcessHandle grantee,
                                     const std::string& grantee_bundle_id,
                                     DocumentAccessMode mode,
                                     std::uint64_t expiry_us = 0);

    bool revoke_grant(std::uint64_t grant_token, std::uint32_t caller_pid);
    bool revoke_grant_by_hex(const std::string& grant_token_hex, std::uint32_t caller_pid);
    void revoke_all_for_process(std::uint32_t pid);
    void revoke_all_for_process_handle(kernel::ProcessHandle handle);
    [[nodiscard]] std::vector<DocumentAccessGrant> get_grants_for_document(DocumentHandle handle) const;
    [[nodiscard]] std::optional<DocumentAccessGrant> get_grant_by_token(std::uint64_t token) const;
    [[nodiscard]] std::optional<DocumentAccessGrant> get_grant_by_hex(const std::string& token_hex) const;

    // Path sanitization and strict validation
    static bool sanitize_and_validate_path(const std::string& in_path, std::string& out_canonical);

    void clear();

private:
    [[nodiscard]] std::uint64_t current_time_us() const;

    mutable std::recursive_mutex                   lock_;
    std::shared_ptr<security::CapabilityManager>   cap_mgr_;
    std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr_;
    std::uint32_t                                  next_id_{1};
    std::uint64_t                                  next_grant_token_{5001};
    std::map<std::uint32_t, std::uint32_t>         generations_; // id -> generation
    std::map<std::uint32_t, DocumentMetadata>      documents_;   // id -> metadata
    std::map<std::uint64_t, DocumentAccessGrant>   grants_;      // token -> grant
    std::unordered_map<std::string, std::uint64_t> grants_by_hex_; // hex token -> grant token
};

} // namespace winios::documents
