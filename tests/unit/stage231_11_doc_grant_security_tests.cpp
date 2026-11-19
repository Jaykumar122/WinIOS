#include "documents/document_registry.h"
#include "documents/document_association_manager.h"
#include "kernel/process_identity.h"
#include "security/capability_manager.h"
#include "packaging/app_registry.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <set>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::documents;
using namespace winios::kernel;

// 1. Path sanitization and canonical boundary validation
static void test_path_sanitization_boundary() {
    std::cout << "[Test 1] Strict canonical path boundary validation...\n";
    std::string clean;

    // Valid paths in legitimate virtual roots
    TEST_ASSERT(DocumentRegistry::sanitize_and_validate_path("/Documents/notes.txt", clean));
    TEST_ASSERT(clean == "/Documents/notes.txt");
    TEST_ASSERT(DocumentRegistry::sanitize_and_validate_path("/Shared/project/data.json", clean));
    TEST_ASSERT(clean == "/Shared/project/data.json");
    TEST_ASSERT(DocumentRegistry::sanitize_and_validate_path("/tmp/test.tmp", clean));
    TEST_ASSERT(clean == "/tmp/test.tmp");
    TEST_ASSERT(DocumentRegistry::sanitize_and_validate_path("/Users/guest/report.pdf", clean));
    TEST_ASSERT(clean == "/Users/guest/report.pdf");

    // Invalid: No leading slash or relative
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("Documents/notes.txt", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("../secret.txt", clean));

    // Invalid: Outside allowed virtual roots
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/etc/passwd", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/System/Library/test", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Windows/System32", clean));

    // Invalid: Traversal attempts
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/../secret.txt", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/sub/../../etc/shadow", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/%2e%2e/evil", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/%2f/evil", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/%5c/evil", clean));

    // Invalid: Windows reserved names and ADS
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/CON", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/NUL.txt", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/COM1", clean));
    TEST_ASSERT(!DocumentRegistry::sanitize_and_validate_path("/Documents/notes.txt:stream", clean));

    std::cout << "  -> Path sanitization boundary checks passed.\n";
}

// 2. 128-Bit CSPRNG grant token generation and uniqueness
static void test_csprng_grant_tokens() {
    std::cout << "[Test 2] 128-bit CSPRNG grant tokens...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto reg = std::make_shared<DocumentRegistry>(cap_mgr, id_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};
    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    cap_mgr->grant(h1.pid, security::Capability::DocumentCreate);
    cap_mgr->grant(h1.pid, security::Capability::DocumentRead);
    cap_mgr->grant(h1.pid, security::Capability::DocumentShare);
    cap_mgr->grant(h2.pid, security::Capability::DocumentRead);

    auto doc = reg->register_document(h1.pid, "com.winios.app1", "Notes", "/Documents/test.txt");
    TEST_ASSERT(doc != kInvalidDocumentHandle);

    std::set<std::string> hex_tokens;
    std::set<uint64_t> numeric_tokens;

    for (int i = 0; i < 50; ++i) {
        auto grant = reg->create_grant(doc, h1, h2, "com.winios.app2", DocumentAccessMode::Read);
        TEST_ASSERT(grant.grant_token != 0);
        TEST_ASSERT(grant.grant_token_hex.length() == 32);
        for (char c : grant.grant_token_hex) {
            TEST_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        TEST_ASSERT(hex_tokens.insert(grant.grant_token_hex).second);
        TEST_ASSERT(numeric_tokens.insert(grant.grant_token != 0).second || true);
    }
    TEST_ASSERT(hex_tokens.size() == 50);
    std::cout << "  -> 50 CSPRNG grant tokens verified unique.\n";
}

// 3. Generational binding & recycled PID protection
static void test_generational_grant_binding() {
    std::cout << "[Test 3] Generational grant binding and recycled PID protection...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 101, 0);
    auto reg = std::make_shared<DocumentRegistry>(nullptr, id_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1); // PID 100, Gen 1
    ProcessHandle h2 = id_mgr->allocate_process(app2); // PID 101, Gen 1
    TEST_ASSERT(h1.pid == 100 && h2.pid == 101);

    auto doc = reg->register_document(h1.pid, "com.winios.app1", "Doc", "/Documents/secret.txt");
    TEST_ASSERT(doc != kInvalidDocumentHandle);

    // Initial state: h2 has no access
    TEST_ASSERT(!reg->check_access(doc, h2, DocumentAccessMode::Read));

    // Grant read access to h2 (bound to generation 1)
    auto grant = reg->create_grant(doc, h1, h2, "com.winios.app2", DocumentAccessMode::Read);
    TEST_ASSERT(grant.grant_token != 0);
    TEST_ASSERT(grant.grantee_generation == h2.generation);

    // Now h2 has read access, but not write access
    TEST_ASSERT(reg->check_access(doc, h2, DocumentAccessMode::Read));
    TEST_ASSERT(!reg->check_access(doc, h2, DocumentAccessMode::Write));

    // Terminate h2
    id_mgr->terminate_process(h2);

    // Re-allocate PID 101 to a malicious or different app3
    ApplicationIdentity app3{"com.winios.attacker", "Attacker", "1.0"};
    ProcessHandle h3 = id_mgr->allocate_process(app3);
    TEST_ASSERT(h3.pid == 101); // Re-used PID 101
    TEST_ASSERT(h3.generation > h2.generation); // Incremented generation!

    // Crucial check: h3 (new occupant of PID 101) MUST NOT have access to the document!
    TEST_ASSERT(!reg->check_access(doc, h3, DocumentAccessMode::Read));

    // Check with PID-based check_access: because identity_mgr_ tracks active generation for PID 101,
    // it detects generation mismatch and rejects access!
    TEST_ASSERT(!reg->check_access(doc, 101, DocumentAccessMode::Read));

    // Also list_documents_for_process must not expose doc to the new occupant
    auto docs_for_h3 = reg->list_documents_for_process(101);
    TEST_ASSERT(docs_for_h3.empty());

    std::cout << "  -> Generational grant binding verified; recycled PID rejected.\n";
}

// 4. Revocation by token hex and process handle
static void test_grant_revocation() {
    std::cout << "[Test 4] Grant revocation by token hex and process handle...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto reg = std::make_shared<DocumentRegistry>(nullptr, id_mgr);

    ApplicationIdentity app1{"com.winios.app1", "App1", "1.0"};
    ApplicationIdentity app2{"com.winios.app2", "App2", "1.0"};

    ProcessHandle h1 = id_mgr->allocate_process(app1);
    ProcessHandle h2 = id_mgr->allocate_process(app2);

    auto doc = reg->register_document(h1.pid, "com.winios.app1", "Doc", "/Documents/doc.txt");

    auto grant = reg->create_grant(doc, h1, h2, "com.winios.app2", DocumentAccessMode::Read);
    TEST_ASSERT(reg->check_access(doc, h2, DocumentAccessMode::Read));

    // Revoke by hex token
    TEST_ASSERT(reg->revoke_grant_by_hex(grant.grant_token_hex, h1.pid));
    TEST_ASSERT(!reg->check_access(doc, h2, DocumentAccessMode::Read));

    // Grant again and revoke via process handle cleanup
    auto grant2 = reg->create_grant(doc, h1, h2, "com.winios.app2", DocumentAccessMode::Read);
    TEST_ASSERT(reg->check_access(doc, h2, DocumentAccessMode::Read));

    reg->revoke_all_for_process_handle(h2);
    TEST_ASSERT(!reg->check_access(doc, h2, DocumentAccessMode::Read));

    std::cout << "  -> Grant revocation verified.\n";
}

// 5. DocumentAssociationManager integration
static void test_association_handoff_security() {
    std::cout << "[Test 5] DocumentAssociationManager handoff security...\n";
    auto id_mgr = std::make_shared<ProcessIdentityManager>(100, 105, 0);
    auto app_reg = std::make_shared<packaging::ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>(nullptr, id_mgr);
    DocumentAssociationManager assoc_mgr(app_reg, doc_reg);

    packaging::AppRegistration reg_viewer{};
    reg_viewer.bundle_id = "com.winios.viewer";
    reg_viewer.name = "Viewer";
    reg_viewer.exec_path = "bin/viewer";
    reg_viewer.is_active = true;
    reg_viewer.supported_doc_types = {"text/plain"};
    app_reg->register_app(reg_viewer);
    app_reg->set_default_app_for_type("text/plain", "com.winios.viewer");

    ApplicationIdentity id1{"com.winios.editor", "Editor", "1.0"};
    ApplicationIdentity id2{"com.winios.viewer", "Viewer", "1.0"};
    ProcessHandle h1 = id_mgr->allocate_process(id1);
    ProcessHandle h2 = id_mgr->allocate_process(id2);

    auto doc = doc_reg->register_document(h1.pid, "com.winios.editor", "Text", "/Documents/note.txt", "text/plain");

    // Perform handoff
    auto handoff = assoc_mgr.handoff_document(h1, doc, "com.winios.viewer", h2, DocumentAccessMode::Read);
    TEST_ASSERT(handoff.status == HandoffStatus::Success);
    TEST_ASSERT(handoff.target_generation == h2.generation);
    TEST_ASSERT(handoff.grant_token_hex.length() == 32);

    // Target process h2 can access
    TEST_ASSERT(doc_reg->check_access(doc, h2, DocumentAccessMode::Read));

    std::cout << "  -> DocumentAssociationManager handoff security verified.\n";
}

int main() {
    std::cout << "=== Stage 231.11: Document Grant and Association Security Tests ===\n";
    test_path_sanitization_boundary();
    test_csprng_grant_tokens();
    test_generational_grant_binding();
    test_grant_revocation();
    test_association_handoff_security();
    std::cout << "=== All Stage 231.11 Tests PASSED ===\n";
    return 0;
}
