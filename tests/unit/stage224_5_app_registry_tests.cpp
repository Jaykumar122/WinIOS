#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "packaging/app_registry.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;

void test_registry_registration_and_lifecycle() {
    std::cout << "[Test 1] Application Registry Registration & Lifecycle...\n";

    ApplicationRegistry reg;
    TEST_ASSERT(reg.app_count() == 0);

    AppRegistration app1{};
    app1.bundle_id = "com.winios.settings";
    app1.name = "Platform Settings";
    app1.version = "1.0.0";
    app1.exec_path = "bin/settings";
    app1.install_dir = "/Applications/com.winios.settings.app";
    app1.permissions = { "display.window", "config" };
    app1.is_active = true;

    TEST_ASSERT(reg.register_app(app1));
    TEST_ASSERT(reg.app_count() == 1);
    TEST_ASSERT(reg.is_installed("com.winios.settings"));
    TEST_ASSERT(reg.is_active("com.winios.settings"));

    // Reject duplicate registration
    TEST_ASSERT(!reg.register_app(app1));

    // Deactivate
    TEST_ASSERT(reg.set_active("com.winios.settings", false));
    TEST_ASSERT(!reg.is_active("com.winios.settings"));
    TEST_ASSERT(reg.app_count(true) == 0);
    TEST_ASSERT(reg.app_count(false) == 1);

    // Reactivate
    TEST_ASSERT(reg.set_active("com.winios.settings", true));
    TEST_ASSERT(reg.app_count(true) == 1);

    std::cout << "[PASS] Registration and lifecycle verified.\n";
}

void test_registry_metadata_update() {
    std::cout << "[Test 2] Metadata Update & Querying...\n";

    ApplicationRegistry reg;
    AppRegistration app{};
    app.bundle_id = "com.winios.calc";
    app.name = "Calculator";
    app.version = "1.0.0";
    app.exec_path = "bin/calc";
    app.install_dir = "/Applications/com.winios.calc.app";
    reg.register_app(app);

    // Update version to 1.1.0
    app.version = "1.1.0";
    app.update_time_us = 12345678ULL;
    TEST_ASSERT(reg.update_app(app));

    auto q = reg.get_app("com.winios.calc");
    TEST_ASSERT(q.has_value());
    TEST_ASSERT(q->version == "1.1.0");
    TEST_ASSERT(q->update_time_us == 12345678ULL);

    // Unregister
    TEST_ASSERT(reg.unregister_app("com.winios.calc"));
    TEST_ASSERT(!reg.is_installed("com.winios.calc"));
    TEST_ASSERT(reg.app_count() == 0);

    std::cout << "[PASS] Metadata update and unregister verified.\n";
}

void test_registry_serialization_roundtrip() {
    std::cout << "[Test 3] Registry Serialization & Persistence Roundtrip...\n";

    ApplicationRegistry reg;
    AppRegistration app1{};
    app1.bundle_id = "com.winios.notes";
    app1.name = "Notes v2";
    app1.version = "2.0.0";
    app1.exec_path = "bin/notes";
    app1.install_dir = "/Applications/com.winios.notes.app";
    app1.permissions = { "storage.private", "clipboard" };
    app1.is_active = true;
    reg.register_app(app1);

    AppRegistration app2{};
    app2.bundle_id = "com.winios.messenger";
    app2.name = "IPC Messenger";
    app2.version = "1.0.0";
    app2.exec_path = "bin/messenger";
    app2.install_dir = "/Applications/com.winios.messenger.app";
    app2.permissions = { "ipc" };
    app2.is_active = false;
    reg.register_app(app2);

    std::string text = reg.serialize();
    TEST_ASSERT(!text.empty());

    ApplicationRegistry loaded;
    TEST_ASSERT(loaded.deserialize(text));
    TEST_ASSERT(loaded.app_count() == 2);
    TEST_ASSERT(loaded.is_installed("com.winios.notes"));
    TEST_ASSERT(loaded.is_active("com.winios.notes"));
    TEST_ASSERT(loaded.is_installed("com.winios.messenger"));
    TEST_ASSERT(!loaded.is_active("com.winios.messenger"));

    std::cout << "[PASS] Registry serialization roundtrip verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.5: APPLICATION REGISTRY TESTS\n";
    std::cout << "========================================\n";

    test_registry_registration_and_lifecycle();
    test_registry_metadata_update();
    test_registry_serialization_roundtrip();

    std::cout << "\nALL STAGE 224.5 TESTS PASSED (100%).\n";
    return 0;
}
