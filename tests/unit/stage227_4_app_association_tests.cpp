#include "packaging/app_registry.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

int main() {
    using namespace winios::packaging;

    ApplicationRegistry reg;

    AppRegistration editor{};
    editor.bundle_id = "com.winios.texteditor";
    editor.name = "WinIOS Text Editor";
    editor.exec_path = "bin/editor";
    editor.is_active = true;
    editor.supported_doc_types = {"text/plain", ".txt", ".md"};
    TEST_ASSERT(reg.register_app(editor));

    AppRegistration viewer{};
    viewer.bundle_id = "com.winios.markdownviewer";
    viewer.name = "Markdown Reader";
    viewer.exec_path = "bin/viewer";
    viewer.is_active = true;
    viewer.supported_doc_types = {".md", "text/markdown"};
    TEST_ASSERT(reg.register_app(viewer));

    // 1. Query apps for type
    auto txt_apps = reg.get_apps_for_type("text/plain");
    TEST_ASSERT(txt_apps.size() == 1);
    TEST_ASSERT(txt_apps[0].bundle_id == "com.winios.texteditor");

    auto md_apps = reg.get_apps_for_type(".md");
    TEST_ASSERT(md_apps.size() == 2); // Both editor and viewer support .md

    // 2. Default app fallback
    std::string def_txt = reg.get_default_app_for_type("text/plain");
    TEST_ASSERT(def_txt == "com.winios.texteditor");

    // 3. Explicit default setting
    TEST_ASSERT(reg.set_default_app_for_type(".md", "com.winios.markdownviewer"));
    std::string def_md = reg.get_default_app_for_type(".md");
    TEST_ASSERT(def_md == "com.winios.markdownviewer");

    // 4. Inactive app fallback
    reg.set_active("com.winios.markdownviewer", false);
    std::string def_md_fallback = reg.get_default_app_for_type(".md");
    TEST_ASSERT(def_md_fallback == "com.winios.texteditor"); // Falls back to editor!

    // 5. Serialization and Deserialization roundtrip
    reg.set_active("com.winios.markdownviewer", true);
    reg.set_default_app_for_type(".md", "com.winios.markdownviewer");
    std::string serialized = reg.serialize();

    ApplicationRegistry reg2;
    TEST_ASSERT(reg2.deserialize(serialized));
    TEST_ASSERT(reg2.is_installed("com.winios.texteditor"));
    TEST_ASSERT(reg2.is_installed("com.winios.markdownviewer"));
    TEST_ASSERT(reg2.get_default_app_for_type(".md") == "com.winios.markdownviewer");
    TEST_ASSERT(reg2.get_apps_for_type(".md").size() == 2);

    std::cout << "stage227_4_app_association_tests: ALL PASSED" << std::endl;
    return 0;
}
