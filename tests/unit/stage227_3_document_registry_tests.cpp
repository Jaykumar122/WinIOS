#include "documents/document_registry.h"
#include "security/capability_manager.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

int main() {
    using namespace winios;
    using namespace winios::documents;

    std::cout << "[Test Stage 227.3] Starting..." << std::endl;

    auto caps = std::make_shared<security::CapabilityManager>();
    // Grant Process 100 DocumentCreate, DocumentRead, DocumentWrite, DocumentDelete, DocumentShare
    caps->grant(100, security::Capability::DocumentCreate);
    caps->grant(100, security::Capability::DocumentRead);
    caps->grant(100, security::Capability::DocumentWrite);
    caps->grant(100, security::Capability::DocumentDelete);
    caps->grant(100, security::Capability::DocumentShare);

    // Process 200 has only DocumentRead
    caps->grant(200, security::Capability::DocumentRead);

    DocumentRegistry reg(caps);
    std::cout << "Step 1: Path sanitization" << std::endl;

    // 1. Path sanitization tests
    std::string clean;
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("../secret.txt", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("Documents/foo.txt", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/etc/passwd", clean));
    TEST_ASSERT(DocumentRegistry::sanitize_and_validate_path("/Documents/notes.txt", clean));
    TEST_ASSERT(clean == "/Documents/notes.txt");

    std::cout << "Step 2: Register document" << std::endl;
    // 2. Register document
    DocumentHandle doc_h = reg.register_document(100, "com.winios.editor", "notes.txt", "/Documents/notes.txt", "text/plain", 128);
    TEST_ASSERT(doc_h != kInvalidDocumentHandle);
    TEST_ASSERT(reg.is_handle_valid(doc_h));

    std::cout << "Step 3: Metadata" << std::endl;
    auto meta = reg.get_metadata(doc_h);
    TEST_ASSERT(meta.has_value());
    TEST_ASSERT(meta->display_name == "notes.txt");
    TEST_ASSERT(meta->canonical_path == "/Documents/notes.txt");
    TEST_ASSERT(meta->owner_pid == 100);
    TEST_ASSERT(meta->file_size == 128);

    std::cout << "Step 4: Duplicate registration" << std::endl;
    // 3. Duplicate registration by same owner returns existing handle
    DocumentHandle doc_dup = reg.register_document(100, "com.winios.editor", "notes.txt", "/Documents/notes.txt");
    TEST_ASSERT(doc_dup == doc_h);

    std::cout << "Step 5: Access verification" << std::endl;
    // 4. Access verification
    TEST_ASSERT(reg.check_access(doc_h, 100, DocumentAccessMode::Read));
    TEST_ASSERT(reg.check_access(doc_h, 100, DocumentAccessMode::Write));
    // Process 200 does not own it and has no grant yet
    TEST_ASSERT(!reg.check_access(doc_h, 200, DocumentAccessMode::Read));

    std::cout << "Step 6: Cross-process grant" << std::endl;
    // 5. Cross-process grant
    auto grant = reg.create_grant(doc_h, 100, 200, "com.winios.viewer", DocumentAccessMode::Read, 0);
    std::cout << "grant token: " << grant.grant_token << std::endl;
    TEST_ASSERT(grant.grant_token != 0);
    TEST_ASSERT(grant.is_revoked == false);

    // Now process 200 has read access via grant
    bool has_read = reg.check_access(doc_h, 200, DocumentAccessMode::Read);
    std::cout << "has_read: " << has_read << std::endl;
    TEST_ASSERT(has_read);
    // But process 200 does NOT have write access
    bool has_write = reg.check_access(doc_h, 200, DocumentAccessMode::Write);
    std::cout << "has_write: " << has_write << std::endl;
    TEST_ASSERT(!has_write);

    std::cout << "Step 7: Revoke grant" << std::endl;
    // 6. Revoke grant
    TEST_ASSERT(reg.revoke_grant(grant.grant_token, 100));
    TEST_ASSERT(!reg.check_access(doc_h, 200, DocumentAccessMode::Read));

    std::cout << "Step 8: Unregister document" << std::endl;
    // 7. Unregister document and test stale handle rejection
    TEST_ASSERT(reg.unregister_document(doc_h, 100));
    TEST_ASSERT(!reg.is_handle_valid(doc_h));
    TEST_ASSERT(!reg.get_metadata(doc_h).has_value());

    std::cout << "Step 9: Re-registration" << std::endl;
    // 8. Re-registration produces a distinct valid handle
    DocumentHandle doc_new = reg.register_document(100, "com.winios.editor", "notes.txt", "/Documents/notes.txt");
    TEST_ASSERT(doc_new != kInvalidDocumentHandle);
    TEST_ASSERT(doc_new != doc_h); // Distinct handle!

    std::cout << "stage227_3_document_registry_tests: ALL PASSED" << std::endl;
    return 0;
}
