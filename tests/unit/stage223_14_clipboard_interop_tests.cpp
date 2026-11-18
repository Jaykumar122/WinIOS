#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"

using namespace winios::runtime;
using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_clipboard_permissions_and_data_flow() {
    std::cout << "[Test 1] Clipboard permission enforcement and inter-process data exchange...\n";

    auto caps = std::make_shared<CapabilityManager>();
    SystemClipboard clipboard(caps);

    uint32_t sender_pid = 101;
    uint32_t receiver_pid = 102;
    uint32_t unauthorized_pid = 103;

    // Grant sender write capability, receiver read capability
    caps->grant(sender_pid, Capability::ClipboardWrite);
    caps->grant(receiver_pid, Capability::ClipboardRead);

    // Sender copies text
    std::string test_message = "Calculated Result: 42.195 km";
    bool set_ok = clipboard.set_text(sender_pid, test_message);
    TEST_ASSERT(set_ok);
    TEST_ASSERT(clipboard.has_data());
    TEST_ASSERT(clipboard.data_size() == test_message.size());

    // Receiver pastes text
    std::string received;
    bool get_ok = clipboard.get_text(receiver_pid, received);
    TEST_ASSERT(get_ok);
    TEST_ASSERT(received == test_message);

    // Unauthorized PID attempts to read -> Must be blocked
    std::string leak_attempt;
    bool unauthorized_read = clipboard.get_text(unauthorized_pid, leak_attempt);
    TEST_ASSERT(!unauthorized_read);
    TEST_ASSERT(leak_attempt.empty());

    // Unauthorized PID attempts to write -> Must be blocked
    bool unauthorized_write = clipboard.set_text(unauthorized_pid, "Malicious overwrite");
    TEST_ASSERT(!unauthorized_write);
    (void)unauthorized_read;
    (void)unauthorized_write;

    std::cout << "[PASS] Clipboard capability enforcement verified.\n";
}

void test_clipboard_capacity_limit_and_audit_trail() {
    std::cout << "[Test 2] Clipboard maximum byte boundary and audit log recording...\n";

    auto caps = std::make_shared<CapabilityManager>();
    SystemClipboard clipboard(caps);

    uint32_t pid = 200;
    caps->grant(pid, Capability::ClipboardWrite);
    caps->grant(pid, Capability::ClipboardRead);

    // Test oversize payload exceeding 64 KB
    std::string oversize_data(SystemClipboard::kMaxClipboardBytes + 1024, 'X');
    bool oversize_ok = clipboard.set_text(pid, oversize_data);
    TEST_ASSERT(!oversize_ok);

    // Test unauthorized caller produces rejection in audit log
    clipboard.set_text(999, "Unauthorized write");
    const auto& log = clipboard.audit_log();
    TEST_ASSERT(!log.empty());
    bool found_rejection = false;
    for (const auto& rec : log) {
        if (rec.caller_pid == 999 && !rec.allowed) {
            found_rejection = true;
            break;
        }
    }
    TEST_ASSERT(found_rejection);

    // Test clear operation
    clipboard.set_text(pid, "Transient Data");
    TEST_ASSERT(clipboard.has_data());
    bool clear_ok = clipboard.clear(pid);
    TEST_ASSERT(clear_ok);
    TEST_ASSERT(!clipboard.has_data());

    std::cout << "[PASS] Capacity clamping and security audit trail verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.14: CLIPBOARD INTEROP TESTS\n";
    std::cout << "========================================\n";

    test_clipboard_permissions_and_data_flow();
    test_clipboard_capacity_limit_and_audit_trail();

    std::cout << "\nALL STAGE 223.14 TESTS PASSED (100%).\n";
    return 0;
}
