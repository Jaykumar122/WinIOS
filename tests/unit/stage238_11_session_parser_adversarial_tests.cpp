// ============================================================================
// Stage 238.11: Desktop Session Parser Adversarial & Fuzzing Suite
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

#include "tests/support/test_assert.h"
#include "documents/desktop_session_manager.h"

using namespace winios::documents;

namespace {

void test_empty_and_whitespace_rejection() {
    std::cout << "[Test 1] Empty and Whitespace Payloads Rejection...\n";
    DesktopSessionManager mgr(nullptr, nullptr, nullptr);
    DesktopSessionData out{};

    TEST_ASSERT(!mgr.deserialize("", out), "Empty string must return false");
    TEST_ASSERT(!mgr.deserialize("   \n\t  \r\n", out), "Whitespace-only string must return false");
    std::cout << "  -> Passed empty and whitespace rejection\n";
}

void test_valid_session_roundtrip() {
    std::cout << "[Test 2] Valid Session Parsing & Version Support...\n";
    DesktopSessionManager mgr(nullptr, nullptr, nullptr);

    DesktopSessionData orig{};
    orig.session_version = 1;
    orig.display_width = 1920;
    orig.display_height = 1080;
    orig.active_window_index = 0;

    SessionWindowRecord win{};
    win.bundle_id = "com.apple.notes";
    win.title = "Notes App";
    win.x = 100;
    win.y = 200;
    win.width = 800;
    win.height = 600;
    win.state = 1;
    win.z_order = 2;
    orig.windows.push_back(win);

    std::string serialized = mgr.serialize(orig);
    TEST_ASSERT(!serialized.empty());

    DesktopSessionData parsed{};
    TEST_ASSERT(mgr.deserialize(serialized, parsed));
    TEST_ASSERT(parsed.session_version == 1);
    TEST_ASSERT(parsed.display_width == 1920);
    TEST_ASSERT(parsed.windows.size() == 1);
    TEST_ASSERT(parsed.windows[0].bundle_id == "com.apple.notes");
    TEST_ASSERT(parsed.windows[0].width == 800);
    std::cout << "  -> Passed valid session parsing\n";
}

void test_unsupported_future_version() {
    std::cout << "[Test 3] Unsupported Future Version Rejection...\n";
    DesktopSessionManager mgr(nullptr, nullptr, nullptr);
    DesktopSessionData out{};

    std::string future_session = "session_version=99\ndisplay_width=1920\n";
    TEST_ASSERT(!mgr.deserialize(future_session, out), "Version 99 must be rejected as unsupported future version");
    std::cout << "  -> Passed future version rejection\n";
}

void test_corrupted_ini_recovery_policy() {
    std::cout << "[Test 4] Degraded / Corrupted INI Recovery Policy...\n";
    DesktopSessionManager mgr(nullptr, nullptr, nullptr);
    DesktopSessionData out{};

    // Valid INI syntax (sections and KV lines) but unknown/corrupted keys and missing session_version
    std::string corrupt_ini = "unknown_key=123\n[corrupted_section]\nbogus_field=abc\n";
    TEST_ASSERT(mgr.deserialize(corrupt_ini, out), "Must recover safely with empty session");
    TEST_ASSERT(out.windows.empty() == true, "Recovered session must have empty windows");
    TEST_ASSERT(out.session_version == 1, "Recovered session version must be 1");
    std::cout << "  -> Passed corrupted INI safe recovery\n";
}

void test_json_and_binary_rejection() {
    std::cout << "[Test 5] JSON and Binary Garbage Strict Rejection...\n";
    DesktopSessionManager mgr(nullptr, nullptr, nullptr);
    DesktopSessionData out{};

    const std::string bad_payloads[] = {
        "{",
        "{\"session_version\": 1}",
        "{\"windows\": []}",
        "not json at all",
        "raw_text_without_equals_or_sections",
        std::string("binary\0data\0with\0nulls", 21)
    };

    for (const auto& payload : bad_payloads) {
        TEST_ASSERT(!mgr.deserialize(payload, out), "Non-INI and JSON payloads must be rejected cleanly");
    }
    std::cout << "  -> Passed JSON and binary garbage rejection\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.11: Desktop Session Parser Adversarial Suite \n";
    std::cout << "========================================================\n";

    test_empty_and_whitespace_rejection();
    test_valid_session_roundtrip();
    test_unsupported_future_version();
    test_corrupted_ini_recovery_policy();
    test_json_and_binary_rejection();

    std::cout << "\nALL STAGE 238.11 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
