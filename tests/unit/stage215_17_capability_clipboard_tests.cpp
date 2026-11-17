#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"

using namespace winios::security;
using namespace winios::runtime;

void test_capability_manager() {
    CapabilityManager caps;

    // PID 1 (Shell) has implicit full permissions
    assert(caps.check(1, Capability::DisplayWindow));
    assert(caps.check(1, Capability::ClipboardRead));
    assert(caps.check(1, Capability::ClipboardWrite));

    // PID 10 (unregistered app) has zero permissions
    assert(!caps.check(10, Capability::DisplayWindow));
    assert(!caps.check(10, Capability::StoragePrivate));

    // Grant named permissions to PID 10
    caps.grant_named(10, {"display.window", "clipboard.read"});
    assert(caps.check(10, Capability::DisplayWindow));
    assert(caps.check(10, Capability::ClipboardRead));
    assert(!caps.check(10, Capability::ClipboardWrite)); // Not granted!

    // Grant single capability
    caps.grant(10, Capability::ClipboardWrite);
    assert(caps.check(10, Capability::ClipboardWrite));

    // Revoke all
    caps.revoke_all(10);
    assert(!caps.check(10, Capability::DisplayWindow));
    assert(!caps.check(10, Capability::ClipboardRead));

    std::cout << "[PASS] test_capability_manager passed\n";
}

void test_system_clipboard_security() {
    auto caps = std::make_shared<CapabilityManager>();
    SystemClipboard clip(caps);

    // Setup:
    // PID 20: Writer (granted clipboard.write only)
    caps->grant_named(20, {"clipboard.write"});
    // PID 30: Reader (granted clipboard.read only)
    caps->grant_named(30, {"clipboard.read"});
    // PID 40: Unauthorized (no clipboard permissions)

    // 1. Unauthorized PID 40 attempts write -> rejected
    assert(!clip.set_text(40, "malicious payload"));

    // 2. Reader PID 30 attempts write -> rejected
    assert(!clip.set_text(30, "reader payload"));

    // 3. Writer PID 20 writes -> allowed
    const std::string text = "Shared clipboard content between WinIOS apps";
    assert(clip.set_text(20, text));
    assert(clip.has_data());
    assert(clip.data_size() == text.size());

    // 4. Unauthorized PID 40 attempts read -> rejected
    std::string out_text;
    assert(!clip.get_text(40, out_text));
    assert(out_text.empty());

    // 5. Reader PID 30 reads -> allowed and receives text
    assert(clip.get_text(30, out_text));
    assert(out_text == text);

    // 6. Audit log validation
    const auto& log = clip.audit_log();
    assert(log.size() >= 5);
    // Find denied write
    assert(log[0].caller_pid == 40 && !log[0].allowed && log[0].action == "set");
    // Find allowed write
    assert(log[2].caller_pid == 20 && log[2].allowed && log[2].action == "set");
    // Find allowed read
    assert(log[4].caller_pid == 30 && log[4].allowed && log[4].action == "get");

    std::cout << "[PASS] test_system_clipboard_security passed\n";
}

int main() {
    std::cout << "Starting Stage 215.17 Capability & Clipboard Security Tests...\n";
    test_capability_manager();
    test_system_clipboard_security();
    std::cout << "All Stage 215.17 Capability & Clipboard tests PASSED!\n";
    return 0;
}
