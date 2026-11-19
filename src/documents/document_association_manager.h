#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "documents/document_registry.h"
#include "documents/document_types.h"
#include "packaging/app_registry.h"

namespace winios::documents {

enum class HandoffStatus : std::uint32_t {
    Success = 0,
    DocumentNotFound = 1,
    CallerUnauthorized = 2,
    NoHandlerForType = 3,
    TargetAppNotInstalled = 4,
    TargetAppInactive = 5,
    InvalidArguments = 6
};

inline const char* handoff_status_to_string(HandoffStatus s) noexcept {
    switch (s) {
    case HandoffStatus::Success: return "Success";
    case HandoffStatus::DocumentNotFound: return "DocumentNotFound";
    case HandoffStatus::CallerUnauthorized: return "CallerUnauthorized";
    case HandoffStatus::NoHandlerForType: return "NoHandlerForType";
    case HandoffStatus::TargetAppNotInstalled: return "TargetAppNotInstalled";
    case HandoffStatus::TargetAppInactive: return "TargetAppInactive";
    case HandoffStatus::InvalidArguments: return "InvalidArguments";
    default: return "UnknownHandoffStatus";
    }
}

struct DocumentHandoffResult {
    HandoffStatus status{HandoffStatus::InvalidArguments};
    DocumentHandle doc_handle{kInvalidDocumentHandle};
    std::string target_bundle_id;
    std::uint32_t target_pid{0};
    std::uint64_t grant_token{0};
    std::string error_message;
};

class DocumentAssociationManager {
public:
    DocumentAssociationManager(std::shared_ptr<packaging::ApplicationRegistry> app_registry,
                               std::shared_ptr<DocumentRegistry> doc_registry)
        : app_registry_(std::move(app_registry)), doc_registry_(std::move(doc_registry)) {}

    ~DocumentAssociationManager() = default;

    // Resolve which application should handle a document based on MIME type and/or file extension
    [[nodiscard]] std::string resolve_handler(const std::string& mime_type,
                                              const std::string& file_path = "") const {
        if (!app_registry_) return "";

        // 1. Try explicit MIME type
        if (!mime_type.empty()) {
            std::string def_app = app_registry_->get_default_app_for_type(mime_type);
            if (!def_app.empty()) return def_app;
        }

        // 2. Try file extension
        if (!file_path.empty()) {
            auto dot_pos = file_path.rfind('.');
            if (dot_pos != std::string::npos) {
                std::string ext = file_path.substr(dot_pos);
                std::string def_app = app_registry_->get_default_app_for_type(ext);
                if (!def_app.empty()) return def_app;
            }
        }

        return "";
    }

    // List all candidate applications capable of handling this doc type
    [[nodiscard]] std::vector<packaging::AppRegistration> list_handlers(const std::string& doc_type) const {
        if (!app_registry_) return {};
        return app_registry_->get_apps_for_type(doc_type, true);
    }

    // Permission-preserving document handoff
    DocumentHandoffResult handoff_document(std::uint32_t caller_pid,
                                          DocumentHandle handle,
                                          const std::string& explicit_target_bundle_id = "",
                                          std::uint32_t target_pid = 0,
                                          DocumentAccessMode mode = DocumentAccessMode::Read,
                                          std::uint64_t expiry_us = 0) {
        DocumentHandoffResult res{};
        res.doc_handle = handle;

        if (!doc_registry_ || !app_registry_) {
            res.status = HandoffStatus::InvalidArguments;
            res.error_message = "Services not initialized";
            return res;
        }

        // 1. Verify document exists
        auto meta_opt = doc_registry_->get_metadata(handle);
        if (!meta_opt) {
            res.status = HandoffStatus::DocumentNotFound;
            res.error_message = "Document handle not found or generation mismatch";
            return res;
        }
        const auto& meta = *meta_opt;

        // 2. Verify caller has permission to grant access (must own or have permission)
        if (!doc_registry_->check_access(handle, caller_pid, mode)) {
            res.status = HandoffStatus::CallerUnauthorized;
            res.error_message = "Caller lacks permission to access or grant access to this document";
            return res;
        }

        // 3. Resolve target bundle ID
        std::string target_bundle_id = explicit_target_bundle_id;
        if (target_bundle_id.empty()) {
            target_bundle_id = resolve_handler(meta.mime_type, meta.canonical_path);
            if (target_bundle_id.empty()) {
                res.status = HandoffStatus::NoHandlerForType;
                res.error_message = "No default handler registered for document type: " + meta.mime_type;
                return res;
            }
        }

        // 4. Verify target app is installed and active
        auto app_opt = app_registry_->get_app(target_bundle_id);
        if (!app_opt) {
            res.status = HandoffStatus::TargetAppNotInstalled;
            res.error_message = "Target application is not installed: " + target_bundle_id;
            return res;
        }
        if (!app_opt->is_active) {
            res.status = HandoffStatus::TargetAppInactive;
            res.error_message = "Target application is currently disabled: " + target_bundle_id;
            return res;
        }

        // 5. Create permission-preserving grant in DocumentRegistry
        auto grant = doc_registry_->create_grant(handle, caller_pid, target_pid, target_bundle_id, mode, expiry_us);
        if (grant.grant_token == 0) {
            res.status = HandoffStatus::CallerUnauthorized;
            res.error_message = "Failed to create document grant";
            return res;
        }

        res.status = HandoffStatus::Success;
        res.target_bundle_id = target_bundle_id;
        res.target_pid = target_pid;
        res.grant_token = grant.grant_token;
        return res;
    }

    // Clean up when an application is uninstalled
    void on_app_uninstalled(const std::string& bundle_id, std::uint32_t active_pid = 0) {
        if (app_registry_) {
            app_registry_->unregister_app(bundle_id);
        }
        if (doc_registry_ && active_pid != 0) {
            doc_registry_->revoke_all_for_process(active_pid);
        }
    }

private:
    std::shared_ptr<packaging::ApplicationRegistry> app_registry_;
    std::shared_ptr<DocumentRegistry>               doc_registry_;
};

} // namespace winios::documents
