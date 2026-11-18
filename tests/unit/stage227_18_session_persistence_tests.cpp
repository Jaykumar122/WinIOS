#include "documents/desktop_session_manager.h"
#include "documents/recent_document_manager.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

int main() {
    using namespace winios;
    using namespace winios::documents;
    using namespace winios::packaging;

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    vfs->mkdir("/Documents");
    const std::string dummy = "Session Doc Content\n";
    vfs->write_file("/Documents/existing_doc.txt", dummy.data(), dummy.size());

    auto app_reg = std::make_shared<ApplicationRegistry>();
    AppRegistration editor{};
    editor.bundle_id = "com.winios.editor";
    editor.name = "Editor";
    editor.exec_path = "bin/editor";
    editor.is_active = true;
    app_reg->register_app(editor);

    auto recent_mgr = std::make_shared<RecentDocumentManager>(vfs);
    DesktopSessionManager session_mgr(app_reg, recent_mgr, vfs);

    // 1. Build session data
    DesktopSessionData sdata{};
    sdata.session_version = 1;
    sdata.save_timestamp_us = 1234567890ULL;
    sdata.display_width = 800;
    sdata.display_height = 600;
    sdata.active_window_index = 0;

    SessionWindowRecord w1{};
    w1.bundle_id = "com.winios.editor";
    w1.title = "existing_doc.txt - Editor";
    w1.x = 50;
    w1.y = 80;
    w1.width = 500;
    w1.height = 350;
    w1.state = 0; // Normal
    w1.z_order = 1;
    w1.associated_doc_path = "/Documents/existing_doc.txt";
    sdata.windows.push_back(w1);

    SessionWindowRecord w2_uninstalled{};
    w2_uninstalled.bundle_id = "com.winios.missing_app";
    w2_uninstalled.title = "Ghost Window";
    w2_uninstalled.x = 200;
    w2_uninstalled.y = 200;
    w2_uninstalled.width = 300;
    w2_uninstalled.height = 200;
    sdata.windows.push_back(w2_uninstalled);

    RecentDocumentEntry rec1{};
    rec1.bundle_id = "com.winios.editor";
    rec1.display_name = "existing_doc.txt";
    rec1.canonical_path = "/Documents/existing_doc.txt";
    rec1.last_opened_time_us = 1234567800ULL;
    sdata.recent_documents.push_back(rec1);

    // 2. Serialize and Deserialize
    std::string text = session_mgr.serialize(sdata);
    TEST_ASSERT(!text.empty());

    DesktopSessionData parsed{};
    TEST_ASSERT(session_mgr.deserialize(text, parsed));
    TEST_ASSERT(parsed.session_version == 1);
    TEST_ASSERT(parsed.display_width == 800);
    TEST_ASSERT(parsed.windows.size() == 2);
    TEST_ASSERT(parsed.recent_documents.size() == 1);

    // 3. Save to VFS and Load from VFS
    TEST_ASSERT(session_mgr.save_session_to_vfs(sdata, "/System/Library/Sessions/desktop_session.json"));
    TEST_ASSERT(vfs->exists("/System/Library/Sessions/desktop_session.json"));
    TEST_ASSERT(!vfs->exists("/System/Library/Sessions/desktop_session.json.tmp"));

    DesktopSessionData loaded{};
    TEST_ASSERT(session_mgr.load_session_from_vfs(loaded, "/System/Library/Sessions/desktop_session.json"));
    TEST_ASSERT(loaded.windows.size() == 2);

    // 4. Sanitize for restore (prunes missing app w2_uninstalled)
    DesktopSessionData clean = session_mgr.sanitize_for_restore(loaded);
    TEST_ASSERT(clean.windows.size() == 1); // Only w1 kept!
    TEST_ASSERT(clean.windows[0].bundle_id == "com.winios.editor");
    TEST_ASSERT(clean.recent_documents.size() == 1);

    // 5. Corrupted session data handling
    DesktopSessionData corrupt_out{};
    TEST_ASSERT(session_mgr.deserialize("gibberish=1\n[invalid]\n", corrupt_out));
    TEST_ASSERT(corrupt_out.windows.empty()); // Recovers safely with empty windows without crashing!

    std::cout << "stage227_18_session_persistence_tests: ALL PASSED" << std::endl;
    return 0;
}
