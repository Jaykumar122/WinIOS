#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <memory>

#include "services/configuration_service.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::services;
using namespace winios::vfs;

void test_typed_config_setters_and_getters() {
    std::cout << "[Test 1] Typed Configuration Setters and Getters...\n";

    ConfigurationService config;

    // Set typed values
    TEST_ASSERT(config.set_string("com.winios.settings", "theme", "dark_mode"));
    TEST_ASSERT(config.set_int("com.winios.settings", "refresh_rate_hz", 60));
    TEST_ASSERT(config.set_double("com.winios.settings", "ui_scale", 1.25));
    TEST_ASSERT(config.set_bool("com.winios.settings", "enable_vfs", true));

    // Get typed values
    TEST_ASSERT(config.get_string("com.winios.settings", "theme") == "dark_mode");
    TEST_ASSERT(config.get_int("com.winios.settings", "refresh_rate_hz") == 60);
    TEST_ASSERT(config.get_double("com.winios.settings", "ui_scale") == 1.25);
    TEST_ASSERT(config.get_bool("com.winios.settings", "enable_vfs") == true);

    // Default values for missing keys
    TEST_ASSERT(config.get_string("com.winios.settings", "missing_key", "default_val") == "default_val");
    TEST_ASSERT(config.get_int("com.winios.settings", "missing_key", 42) == 42);

    std::cout << "[PASS] Typed setters and getters verified.\n";
}

void test_namespace_isolation_and_limits() {
    std::cout << "[Test 2] Namespace Isolation and Resource Bounds...\n";

    ConfigurationService config;

    config.set_string("com.winios.appA", "secret", "valueA");
    config.set_string("com.winios.appB", "secret", "valueB");

    // Must be isolated by bundle ID
    TEST_ASSERT(config.get_string("com.winios.appA", "secret") == "valueA");
    TEST_ASSERT(config.get_string("com.winios.appB", "secret") == "valueB");

    // Key length limit (64 chars)
    std::string long_key(65, 'k');
    TEST_ASSERT(!config.set_string("com.winios.appA", long_key, "val"));

    std::cout << "[PASS] Namespace isolation and limits verified.\n";
}

void test_vfs_persistence() {
    std::cout << "[Test 3] VFS Configuration Persistence...\n";

    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->mkdir("/data");
    vfs->mkdir("/data/com.winios.settings");

    ConfigurationService config(vfs);
    config.set_string("com.winios.settings", "mode", "airplane");
    config.set_int("com.winios.settings", "volume", 80);

    TEST_ASSERT(config.save_to_vfs("com.winios.settings"));
    TEST_ASSERT(vfs->exists("/data/com.winios.settings/config.kv"));

    ConfigurationService loaded(vfs);
    TEST_ASSERT(loaded.load_from_vfs("com.winios.settings"));
    TEST_ASSERT(loaded.get_string("com.winios.settings", "mode") == "airplane");
    TEST_ASSERT(loaded.get_int("com.winios.settings", "volume") == 80);

    std::cout << "[PASS] VFS configuration persistence verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.17: CONFIGURATION SERVICE TESTS\n";
    std::cout << "========================================\n";

    test_typed_config_setters_and_getters();
    test_namespace_isolation_and_limits();
    test_vfs_persistence();

    std::cout << "\nALL STAGE 224.17 TESTS PASSED (100%).\n";
    return 0;
}
