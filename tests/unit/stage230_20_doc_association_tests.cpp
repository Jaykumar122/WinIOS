#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "documents/document_association_manager.h"
#include "documents/document_registry.h"
#include "packaging/app_registry.h"

using namespace winios::documents;
using namespace winios::packaging;

static void test_mime_and_extension_registration() {
    std::cout << "[Test 230.20.1] MIME and extension registration...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    AppRegistration app1{};
    app1.bundle_id = "com.winios.texteditor";
    app1.name = "TextEditor";
    app1.exec_path = "bin/texteditor";
    app1.supported_doc_types = {"text/plain", ".txt", ".md"};
    assert(app_reg->register_app(app1));
    assert(app_reg->set_default_app_for_type("text/plain", app1.bundle_id));
    assert(app_reg->set_default_app_for_type(".txt", app1.bundle_id));
    assert(app_reg->set_default_app_for_type(".md", app1.bundle_id));

    AppRegistration app2{};
    app2.bundle_id = "com.winios.pdfviewer";
    app2.name = "PdfViewer";
    app2.exec_path = "bin/pdfviewer";
    app2.supported_doc_types = {"application/pdf", ".pdf"};
    assert(app_reg->register_app(app2));
    assert(app_reg->set_default_app_for_type("application/pdf", app2.bundle_id));
    assert(app_reg->set_default_app_for_type(".pdf", app2.bundle_id));

    // Resolve by MIME
    assert(mgr.resolve_handler("text/plain") == "com.winios.texteditor");
    assert(mgr.resolve_handler("application/pdf") == "com.winios.pdfviewer");

    // Resolve by file path extension
    assert(mgr.resolve_handler("", "/Documents/notes.txt") == "com.winios.texteditor");
    assert(mgr.resolve_handler("", "/Documents/report.pdf") == "com.winios.pdfviewer");
    assert(mgr.resolve_handler("", "/Documents/readme.md") == "com.winios.texteditor");

    // Candidate list
    auto txt_handlers = mgr.list_handlers("text/plain");
    assert(txt_handlers.size() == 1);
    assert(txt_handlers[0].bundle_id == "com.winios.texteditor");

    std::cout << "  -> Passed MIME and extension registration.\n";
}

static void test_default_override_and_explicit_selection() {
    std::cout << "[Test 230.20.2] Default app override and explicit selection...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    AppRegistration editor{};
    editor.bundle_id = "com.winios.editor";
    editor.name = "Editor";
    editor.exec_path = "bin/editor";
    editor.supported_doc_types = {".md"};
    assert(app_reg->register_app(editor));
    assert(app_reg->set_default_app_for_type(".md", editor.bundle_id));

    AppRegistration markview{};
    markview.bundle_id = "com.winios.markview";
    markview.name = "MarkView";
    markview.exec_path = "bin/markview";
    markview.supported_doc_types = {".md"};
    assert(app_reg->register_app(markview));

    // Initially resolves to editor
    assert(mgr.resolve_handler("", "spec.md") == "com.winios.editor");

    // Override default to MarkView
    assert(app_reg->set_default_app_for_type(".md", "com.winios.markview"));
    assert(mgr.resolve_handler("", "spec.md") == "com.winios.markview");

    // Candidate list includes both
    auto handlers = mgr.list_handlers(".md");
    assert(handlers.size() == 2);

    std::cout << "  -> Passed default override and explicit selection.\n";
}

static void test_missing_handler_behavior() {
    std::cout << "[Test 230.20.3] Missing handler behavior...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    assert(mgr.resolve_handler("image/unknown") == "");
    assert(mgr.resolve_handler("", "/Documents/data.xyz") == "");

    // Register a document with unknown type
    auto handle = doc_reg->register_document(101, "com.winios.creator", "data.xyz",
                                             "/Documents/data.xyz", "application/octet-stream", 1024);
    assert(handle != kInvalidDocumentHandle);

    // Handoff should fail with NoHandlerForType
    auto res = mgr.handoff_document(101, handle);
    assert(res.status == HandoffStatus::NoHandlerForType);
    assert(res.grant_token == 0);

    std::cout << "  -> Passed missing handler behavior.\n";
}

static void test_permission_preserving_handoff_and_anti_forgery() {
    std::cout << "[Test 230.20.4] Permission-preserving handoff and anti-forgery check...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    AppRegistration viewer{};
    viewer.bundle_id = "com.winios.viewer";
    viewer.name = "Viewer";
    viewer.exec_path = "bin/viewer";
    viewer.supported_doc_types = {"text/plain"};
    assert(app_reg->register_app(viewer));
    assert(app_reg->set_default_app_for_type("text/plain", viewer.bundle_id));

    // Process 101 registers doc
    auto handle = doc_reg->register_document(101, "com.winios.author", "secret.txt",
                                             "/Documents/secret.txt", "text/plain", 512);
    assert(handle != kInvalidDocumentHandle);

    // Rogue Process 105 (unauthorized) attempts to handoff Process 101's document
    auto rogue_res = mgr.handoff_document(105, handle);
    assert(rogue_res.status == HandoffStatus::CallerUnauthorized);
    assert(rogue_res.grant_token == 0);

    // Legitimate owner Process 101 performs handoff to target viewer process 102
    auto valid_res = mgr.handoff_document(101, handle, "", 102, DocumentAccessMode::Read);
    assert(valid_res.status == HandoffStatus::Success);
    assert(valid_res.grant_token != 0);
    assert(valid_res.target_bundle_id == "com.winios.viewer");

    // Target process 102 can now access the document
    assert(doc_reg->check_access(handle, 102, DocumentAccessMode::Read));
    // But cannot write
    assert(!doc_reg->check_access(handle, 102, DocumentAccessMode::Write));

    std::cout << "  -> Passed permission-preserving handoff and anti-forgery check.\n";
}

static void test_uninstall_cleanup() {
    std::cout << "[Test 230.20.5] Application uninstall cleanup...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    AppRegistration app{};
    app.bundle_id = "com.winios.cleanupapp";
    app.name = "CleanupApp";
    app.exec_path = "bin/cleanupapp";
    app.supported_doc_types = {"application/custom-data", ".cdata"};
    assert(app_reg->register_app(app));
    assert(app_reg->set_default_app_for_type("application/custom-data", app.bundle_id));
    assert(app_reg->set_default_app_for_type(".cdata", app.bundle_id));

    assert(mgr.resolve_handler("application/custom-data") == "com.winios.cleanupapp");
    assert(mgr.resolve_handler("", "file.cdata") == "com.winios.cleanupapp");

    // Uninstall
    mgr.on_app_uninstalled("com.winios.cleanupapp");

    // After uninstall, associations must be purged
    assert(mgr.resolve_handler("application/custom-data") == "");
    assert(mgr.resolve_handler("", "file.cdata") == "");
    assert(!app_reg->is_installed("com.winios.cleanupapp"));

    std::cout << "  -> Passed application uninstall cleanup.\n";
}

static void test_inactive_target_app() {
    std::cout << "[Test 230.20.6] Inactive target application handling...\n";
    auto app_reg = std::make_shared<ApplicationRegistry>();
    auto doc_reg = std::make_shared<DocumentRegistry>();
    DocumentAssociationManager mgr(app_reg, doc_reg);

    AppRegistration app{};
    app.bundle_id = "com.winios.disabledapp";
    app.name = "DisabledApp";
    app.exec_path = "bin/disabledapp";
    app.supported_doc_types = {"text/plain"};
    assert(app_reg->register_app(app));
    assert(app_reg->set_default_app_for_type("text/plain", app.bundle_id));

    auto handle = doc_reg->register_document(101, "com.winios.owner", "test.txt",
                                             "/Documents/test.txt", "text/plain", 100);

    // Disable app
    assert(app_reg->set_active("com.winios.disabledapp", false));

    // Explicit handoff to disabled app should return TargetAppInactive
    auto res = mgr.handoff_document(101, handle, "com.winios.disabledapp");
    assert(res.status == HandoffStatus::TargetAppInactive);

    std::cout << "  -> Passed inactive target application handling.\n";
}

int main() {
    std::cout << "=== Running Stage 230.20 Document Association Tests ===\n";
    test_mime_and_extension_registration();
    test_default_override_and_explicit_selection();
    test_missing_handler_behavior();
    test_permission_preserving_handoff_and_anti_forgery();
    test_uninstall_cleanup();
    test_inactive_target_app();
    std::cout << "=== All Stage 230.20 Tests Passed! ===\n";
    return 0;
}
