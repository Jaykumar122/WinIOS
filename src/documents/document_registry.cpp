#include "documents/document_registry.h"
#include "packaging/canonical_path_validator.h"
#include "security/security_utils.h"

#include <algorithm>

namespace winios::documents {

DocumentRegistry::DocumentRegistry(std::shared_ptr<security::CapabilityManager> cap_mgr,
                                   std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr)
    : cap_mgr_(std::move(cap_mgr)), identity_mgr_(std::move(identity_mgr)) {}

std::uint64_t DocumentRegistry::current_time_us() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool DocumentRegistry::sanitize_and_validate_path(const std::string& in_path, std::string& out_canonical) {
    if (in_path.empty()) return false;
    // Must begin with '/'
    if (in_path.front() != '/') return false;

    // Disallow path traversal sequences, backslashes, and encoded variants
    if (in_path.find("..") != std::string::npos || in_path.find('\\') != std::string::npos ||
        in_path.find("%2e") != std::string::npos || in_path.find("%2E") != std::string::npos ||
        in_path.find("%2f") != std::string::npos || in_path.find("%2F") != std::string::npos ||
        in_path.find("%5c") != std::string::npos || in_path.find("%5C") != std::string::npos) {
        return false;
    }

    // Canonical path validation using CanonicalPathValidator on relative segment
    std::string rel_path = in_path.substr(1);
    std::string norm_rel;
    if (packaging::CanonicalPathValidator::validate_and_normalize_bundle_path(rel_path, norm_rel) !=
        packaging::PathValidationStatus::Valid) {
        return false;
    }

    std::string canonical = "/" + norm_rel;

    // Permitted virtual document roots: /Documents, /Shared, /Downloads, /tmp, /Users, /var/mobile
    const std::vector<std::string> valid_prefixes = {
        "/Documents", "/Shared", "/Downloads", "/tmp", "/Users", "/var/mobile"
    };
    bool valid_prefix = false;
    for (const auto& p : valid_prefixes) {
        if (canonical.rfind(p, 0) == 0) {
            // Must be exact match or followed by '/'
            if (canonical.length() == p.length() || canonical[p.length()] == '/') {
                valid_prefix = true;
                break;
            }
        }
    }
    if (!valid_prefix) return false;

    out_canonical = canonical;
    return true;
}

DocumentHandle DocumentRegistry::register_document(std::uint32_t caller_pid,
                                                   const std::string& bundle_id,
                                                   const std::string& display_name,
                                                   const std::string& canonical_path,
                                                   const std::string& mime_type,
                                                   std::uint64_t initial_size,
                                                   bool is_read_only) {
    std::string clean_path;
    if (!sanitize_and_validate_path(canonical_path, clean_path)) {
        return kInvalidDocumentHandle;
    }
    if (display_name.empty()) {
        return kInvalidDocumentHandle;
    }
    if (initial_size > kMaxDocumentSizeBytes) {
        return kInvalidDocumentHandle;
    }

    std::lock_guard<std::recursive_mutex> guard(lock_);

    // Check capability if manager attached
    if (cap_mgr_) {
        if (!cap_mgr_->check(caller_pid, security::Capability::DocumentCreate)) {
            return kInvalidDocumentHandle;
        }
    }

    // Check duplicate path: if already registered and valid
    for (const auto& [id, doc] : documents_) {
        if (doc.canonical_path == clean_path) {
            auto gen_it = generations_.find(id);
            if (gen_it != generations_.end()) {
                // If caller is owner or PID 1, return existing handle
                if (doc.owner_pid == caller_pid || caller_pid == 1 || doc.owner_bundle_id == bundle_id) {
                    return make_doc_handle(id, gen_it->second);
                }
            }
        }
    }

    const std::uint32_t id = next_id_++;
    std::uint32_t gen = 1;
    auto gen_it = generations_.find(id);
    if (gen_it != generations_.end()) {
        gen = gen_it->second;
    } else {
        generations_[id] = 1;
    }

    const auto now = current_time_us();
    const DocumentHandle handle = make_doc_handle(id, gen);

    DocumentMetadata meta{};
    meta.handle = handle;
    meta.doc_id = "doc_" + std::to_string(id);
    meta.owner_bundle_id = bundle_id;
    meta.owner_pid = caller_pid;
    meta.display_name = display_name;
    meta.canonical_path = clean_path;
    meta.mime_type = mime_type.empty() ? "text/plain" : mime_type;
    meta.file_size = initial_size;
    meta.created_time_us = now;
    meta.modified_time_us = now;
    meta.is_read_only = is_read_only;

    documents_[id] = meta;
    return handle;
}

bool DocumentRegistry::unregister_document(DocumentHandle handle, std::uint32_t caller_pid) {
    if (handle == kInvalidDocumentHandle) return false;

    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = documents_.find(id);
    if (it == documents_.end()) return false;

    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) {
        return false; // Stale handle
    }

    // Must be owner or system (PID 1)
    if (it->second.owner_pid != caller_pid && caller_pid != 1) {
        return false;
    }

    // Check capability
    if (cap_mgr_ && caller_pid != 1) {
        if (!cap_mgr_->check(caller_pid, security::Capability::DocumentDelete)) {
            return false;
        }
    }

    // Invalidate handle via generation increment with non-zero wraparound safety
    std::uint32_t next_gen = gen + 1;
    if (next_gen == 0) next_gen = 1;
    generations_[id] = next_gen;

    documents_.erase(it);

    // Revoke any outstanding grants for this document
    for (auto& [tok, grant] : grants_) {
        if (grant.doc_handle == handle) {
            grant.is_revoked = true;
        }
    }

    return true;
}

bool DocumentRegistry::is_handle_valid(DocumentHandle handle) const {
    if (handle == kInvalidDocumentHandle) return false;
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) {
        return false;
    }
    return documents_.find(id) != documents_.end();
}

std::optional<DocumentMetadata> DocumentRegistry::get_metadata(DocumentHandle handle) const {
    if (handle == kInvalidDocumentHandle) return std::nullopt;
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) {
        return std::nullopt;
    }
    auto it = documents_.find(id);
    if (it != documents_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<DocumentMetadata> DocumentRegistry::get_by_path(const std::string& canonical_path) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (const auto& [id, doc] : documents_) {
        if (doc.canonical_path == canonical_path) {
            auto gen_it = generations_.find(id);
            if (gen_it != generations_.end()) {
                DocumentMetadata meta = doc;
                meta.handle = make_doc_handle(id, gen_it->second);
                return meta;
            }
        }
    }
    return std::nullopt;
}

std::vector<DocumentMetadata> DocumentRegistry::list_documents_for_process(std::uint32_t caller_pid) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::vector<DocumentMetadata> result;
    const auto now = current_time_us();

    for (const auto& [id, doc] : documents_) {
        bool allowed = (doc.owner_pid == caller_pid || caller_pid == 1);
        if (!allowed) {
            // Check grants
            for (const auto& [tok, grant] : grants_) {
                if (grant.doc_handle == doc.handle && grant.grantee_pid == caller_pid && !grant.is_revoked) {
                    if (identity_mgr_ && grant.grantee_generation > 0) {
                        if (identity_mgr_->get_generation(caller_pid) != grant.grantee_generation) {
                            continue;
                        }
                    }
                    if (grant.expiry_time_us == 0 || grant.expiry_time_us > now) {
                        allowed = true;
                        break;
                    }
                }
            }
        }
        if (allowed) {
            result.push_back(doc);
        }
    }
    return result;
}

std::vector<DocumentMetadata> DocumentRegistry::list_all_documents() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::vector<DocumentMetadata> result;
    result.reserve(documents_.size());
    for (const auto& [_, doc] : documents_) {
        result.push_back(doc);
    }
    return result;
}

bool DocumentRegistry::update_file_size(DocumentHandle handle, std::uint64_t new_size, std::uint32_t caller_pid) {
    if (new_size > kMaxDocumentSizeBytes) return false;
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = documents_.find(id);
    if (it == documents_.end()) return false;

    if (!check_access(handle, caller_pid, DocumentAccessMode::Write)) {
        return false;
    }

    it->second.file_size = new_size;
    it->second.modified_time_us = current_time_us();
    return true;
}

bool DocumentRegistry::update_modified_time(DocumentHandle handle, std::uint32_t caller_pid) {
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = documents_.find(id);
    if (it == documents_.end()) return false;

    if (!check_access(handle, caller_pid, DocumentAccessMode::Write)) {
        return false;
    }

    it->second.modified_time_us = current_time_us();
    return true;
}

bool DocumentRegistry::check_access(DocumentHandle handle,
                                    std::uint32_t caller_pid,
                                    DocumentAccessMode mode) const {
    if (handle == kInvalidDocumentHandle) return false;
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = documents_.find(id);
    if (it == documents_.end()) return false;

    // Check process validity if identity manager is active
    if (identity_mgr_ && caller_pid != 1) {
        if (!identity_mgr_->is_valid_pid(caller_pid)) {
            return false;
        }
    }

    // Capability check
    if (cap_mgr_ && caller_pid != 1) {
        if (has_access_flag(mode, DocumentAccessMode::Read)) {
            if (!cap_mgr_->check(caller_pid, security::Capability::DocumentRead)) return false;
        }
        if (has_access_flag(mode, DocumentAccessMode::Write)) {
            if (!cap_mgr_->check(caller_pid, security::Capability::DocumentWrite)) return false;
        }
    }

    // Read only constraint
    if (it->second.is_read_only && has_access_flag(mode, DocumentAccessMode::Write)) {
        return false;
    }

    // Direct ownership check (owner or PID 1 system)
    if (it->second.owner_pid == caller_pid || caller_pid == 1) {
        return true;
    }

    // Check explicit sharing grant with generational validation
    const auto now = current_time_us();
    for (const auto& [tok, grant] : grants_) {
        if (grant.doc_handle == handle && grant.grantee_pid == caller_pid && !grant.is_revoked) {
            if (grant.expiry_time_us != 0 && grant.expiry_time_us <= now) {
                continue; // Expired
            }
            if (identity_mgr_ && grant.grantee_generation > 0) {
                if (identity_mgr_->get_generation(caller_pid) != grant.grantee_generation) {
                    continue; // Generation mismatch: stale recycled PID!
                }
            }
            const auto granted_mode = static_cast<DocumentAccessMode>(grant.mode);
            if (has_access_flag(mode, DocumentAccessMode::Write) && !has_access_flag(granted_mode, DocumentAccessMode::Write)) {
                return false;
            }
            if (has_access_flag(mode, DocumentAccessMode::Read) && !has_access_flag(granted_mode, DocumentAccessMode::Read)) {
                return false;
            }
            return true;
        }
    }

    return false;
}

bool DocumentRegistry::check_access(DocumentHandle handle,
                                    kernel::ProcessHandle caller,
                                    DocumentAccessMode mode) const {
    if (handle == kInvalidDocumentHandle) return false;
    const std::uint32_t id = doc_handle_id(handle);
    const std::uint32_t gen = doc_handle_gen(handle);

    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto gen_it = generations_.find(id);
    if (gen_it == generations_.end() || gen_it->second != gen) return false;

    auto it = documents_.find(id);
    if (it == documents_.end()) return false;

    // Check caller handle validity
    if (identity_mgr_ && caller.pid != 1) {
        if (!identity_mgr_->is_valid_handle(caller)) {
            return false;
        }
    }

    // Capability check
    if (cap_mgr_ && caller.pid != 1) {
        if (has_access_flag(mode, DocumentAccessMode::Read)) {
            if (!cap_mgr_->check(caller.pid, security::Capability::DocumentRead)) return false;
        }
        if (has_access_flag(mode, DocumentAccessMode::Write)) {
            if (!cap_mgr_->check(caller.pid, security::Capability::DocumentWrite)) return false;
        }
    }

    // Read only constraint
    if (it->second.is_read_only && has_access_flag(mode, DocumentAccessMode::Write)) {
        return false;
    }

    // Direct ownership check
    if (it->second.owner_pid == caller.pid || caller.pid == 1) {
        return true;
    }

    // Check explicit sharing grant matching PID and generation
    const auto now = current_time_us();
    for (const auto& [tok, grant] : grants_) {
        if (grant.doc_handle == handle && grant.grantee_pid == caller.pid && !grant.is_revoked) {
            if (grant.expiry_time_us != 0 && grant.expiry_time_us <= now) {
                continue;
            }
            if (grant.grantee_generation > 0 && grant.grantee_generation != caller.generation) {
                continue; // Generation mismatch!
            }
            const auto granted_mode = static_cast<DocumentAccessMode>(grant.mode);
            if (has_access_flag(mode, DocumentAccessMode::Write) && !has_access_flag(granted_mode, DocumentAccessMode::Write)) {
                return false;
            }
            if (has_access_flag(mode, DocumentAccessMode::Read) && !has_access_flag(granted_mode, DocumentAccessMode::Read)) {
                return false;
            }
            return true;
        }
    }

    return false;
}

DocumentAccessGrant DocumentRegistry::create_grant(DocumentHandle handle,
                                                   std::uint32_t grantor_pid,
                                                   std::uint32_t grantee_pid,
                                                   const std::string& grantee_bundle_id,
                                                   DocumentAccessMode mode,
                                                   std::uint64_t expiry_us) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (!is_handle_valid(handle)) return {};

    const std::uint32_t id = doc_handle_id(handle);
    auto it = documents_.find(id);
    if (it == documents_.end()) return {};

    // Grantor must own document or be PID 1
    if (it->second.owner_pid != grantor_pid && grantor_pid != 1) {
        return {};
    }

    // Capability check
    if (cap_mgr_ && grantor_pid != 1) {
        if (!cap_mgr_->check(grantor_pid, security::Capability::DocumentShare)) {
            return {};
        }
    }

    const auto now = current_time_us();
    const std::uint64_t token = next_grant_token_++;
    const std::string token_hex = security::SecureRandom::get_random_hex(16);

    uint32_t grantor_gen = 0;
    uint32_t grantee_gen = 0;
    if (identity_mgr_) {
        if (grantor_pid != 1) grantor_gen = identity_mgr_->get_generation(grantor_pid);
        grantee_gen = identity_mgr_->get_generation(grantee_pid);
    }

    DocumentAccessGrant grant{};
    grant.grant_token = token;
    grant.grant_token_hex = token_hex;
    grant.doc_handle = handle;
    grant.grantor_pid = grantor_pid;
    grant.grantor_generation = grantor_gen;
    grant.grantee_pid = grantee_pid;
    grant.grantee_generation = grantee_gen;
    grant.grantee_bundle_id = grantee_bundle_id;
    grant.mode = static_cast<std::uint32_t>(mode);
    grant.granted_time_us = now;
    grant.expiry_time_us = (expiry_us > 0) ? (now + expiry_us) : 0;
    grant.is_revoked = false;

    grants_[token] = grant;
    grants_by_hex_[token_hex] = token;
    return grant;
}

DocumentAccessGrant DocumentRegistry::create_grant(DocumentHandle handle,
                                                   kernel::ProcessHandle grantor,
                                                   kernel::ProcessHandle grantee,
                                                   const std::string& grantee_bundle_id,
                                                   DocumentAccessMode mode,
                                                   std::uint64_t expiry_us) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (!is_handle_valid(handle)) return {};

    if (identity_mgr_) {
        if (grantor.pid != 1 && !identity_mgr_->is_valid_handle(grantor)) return {};
        if (!identity_mgr_->is_valid_handle(grantee)) return {};
    }

    const std::uint32_t id = doc_handle_id(handle);
    auto it = documents_.find(id);
    if (it == documents_.end()) return {};

    if (it->second.owner_pid != grantor.pid && grantor.pid != 1) {
        return {};
    }

    if (cap_mgr_ && grantor.pid != 1) {
        if (!cap_mgr_->check(grantor.pid, security::Capability::DocumentShare)) {
            return {};
        }
    }

    const auto now = current_time_us();
    const std::uint64_t token = next_grant_token_++;
    const std::string token_hex = security::SecureRandom::get_random_hex(16);

    DocumentAccessGrant grant{};
    grant.grant_token = token;
    grant.grant_token_hex = token_hex;
    grant.doc_handle = handle;
    grant.grantor_pid = grantor.pid;
    grant.grantor_generation = grantor.generation;
    grant.grantee_pid = grantee.pid;
    grant.grantee_generation = grantee.generation;
    grant.grantee_bundle_id = grantee_bundle_id;
    grant.mode = static_cast<std::uint32_t>(mode);
    grant.granted_time_us = now;
    grant.expiry_time_us = (expiry_us > 0) ? (now + expiry_us) : 0;
    grant.is_revoked = false;

    grants_[token] = grant;
    grants_by_hex_[token_hex] = token;
    return grant;
}

bool DocumentRegistry::revoke_grant(std::uint64_t grant_token, std::uint32_t caller_pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = grants_.find(grant_token);
    if (it == grants_.end()) return false;

    if (it->second.grantor_pid != caller_pid && caller_pid != 1) {
        return false; // Unauthorized revocation
    }

    it->second.is_revoked = true;
    return true;
}

bool DocumentRegistry::revoke_grant_by_hex(const std::string& grant_token_hex, std::uint32_t caller_pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it_hex = grants_by_hex_.find(grant_token_hex);
    if (it_hex == grants_by_hex_.end()) return false;

    return revoke_grant(it_hex->second, caller_pid);
}

void DocumentRegistry::revoke_all_for_process(std::uint32_t pid) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& [tok, grant] : grants_) {
        if (grant.grantor_pid == pid || grant.grantee_pid == pid) {
            grant.is_revoked = true;
        }
    }
}

void DocumentRegistry::revoke_all_for_process_handle(kernel::ProcessHandle handle) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& [tok, grant] : grants_) {
        if ((grant.grantor_pid == handle.pid && (grant.grantor_generation == 0 || grant.grantor_generation == handle.generation)) ||
            (grant.grantee_pid == handle.pid && (grant.grantee_generation == 0 || grant.grantee_generation == handle.generation))) {
            grant.is_revoked = true;
        }
    }
}

std::vector<DocumentAccessGrant> DocumentRegistry::get_grants_for_document(DocumentHandle handle) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::vector<DocumentAccessGrant> result;
    for (const auto& [tok, grant] : grants_) {
        if (grant.doc_handle == handle && !grant.is_revoked) {
            result.push_back(grant);
        }
    }
    return result;
}

std::optional<DocumentAccessGrant> DocumentRegistry::get_grant_by_token(std::uint64_t token) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = grants_.find(token);
    if (it != grants_.end()) return it->second;
    return std::nullopt;
}

std::optional<DocumentAccessGrant> DocumentRegistry::get_grant_by_hex(const std::string& token_hex) const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it_hex = grants_by_hex_.find(token_hex);
    if (it_hex != grants_by_hex_.end()) {
        auto it = grants_.find(it_hex->second);
        if (it != grants_.end()) return it->second;
    }
    return std::nullopt;
}

void DocumentRegistry::clear() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    documents_.clear();
    generations_.clear();
    grants_.clear();
    grants_by_hex_.clear();
    next_id_ = 1;
    next_grant_token_ = 5001;
}

} // namespace winios::documents
