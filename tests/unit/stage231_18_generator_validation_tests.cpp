#include "tools/app_generator.h"
#include "tools/winios_app_cli.h"
#include "loader/app_launcher.h"
#include "loader/app_inspector.h"
#include "loader/embedded_campaign229_workloads.inl"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "packaging/package_installer.h"
#include "security/publisher_trust_store.h"
#include "security/capability_manager.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::tools;

static void test_source_and_manifest_generation() {
    std::cout << "[Test 1] Source, manifest, and build script generation for SDK v2.4...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.docviewer";
    cfg.app_name = "DocViewer";
    cfg.display_name = "WinIOS Document Viewer";
    cfg.version = "2.4.0";
    cfg.category = "Productivity";
    cfg.author = "Core Team";
    cfg.description = "Document viewing application";
    cfg.permissions = {"DocumentsRead", "DocumentsWrite", "SettingsRead"};
    cfg.supported_doc_types = {"text/plain", "application/json", "text/markdown"};
    cfg.min_sdk_version = 0x00020400; // v2.4
    cfg.target_abi = "2.4";

    // 1. C source code generation
    std::string src = AppGenerator::generate_c_source(cfg);
    assert(src.find("DocViewer") != std::string::npos);
    assert(src.find("com.winios.docviewer") != std::string::npos);
    assert(src.find("0x20400") != std::string::npos || src.find("0x00020400") != std::string::npos);
    assert(src.find("#include \"winios/winios.h\"") != std::string::npos);
    assert(src.find("winios_log_info") != std::string::npos);
    assert(src.find("winios_exit(0)") != std::string::npos);

    // 2. Manifest INI generation
    std::string ini = AppGenerator::generate_manifest_ini(cfg);
    assert(ini.find("format_version=3.0") != std::string::npos);
    assert(ini.find("bundle_id=com.winios.docviewer") != std::string::npos);
    assert(ini.find("name=DocViewer") != std::string::npos);
    assert(ini.find("display_name=WinIOS Document Viewer") != std::string::npos);
    assert(ini.find("target_abi_version=2.4") != std::string::npos);
    assert(ini.find("executable_name=bin/DocViewer") != std::string::npos);
    assert(ini.find("category=Productivity") != std::string::npos);

    // Verify manifest deserialization
    packaging::PackageManifest m{};
    bool parse_ok = packaging::PackageManifest::deserialize(ini, m);
    assert(parse_ok);
    (void)parse_ok;
    assert(m.bundle_id == "com.winios.docviewer");
    assert(m.format_version == "3.0");
    assert(m.permissions.size() == 3);
    assert(m.supported_doc_types.size() == 3);

    // 3. Build script generation
    std::string bld = AppGenerator::generate_build_script(cfg);
    assert(bld.find("zig cc -target aarch64-freestanding") != std::string::npos);
    assert(bld.find("-marm64_ios") != std::string::npos);
    assert(bld.find("-e _main") != std::string::npos);
    assert(bld.find("bin/DocViewer") != std::string::npos);

    std::cout << "  -> Passed.\n";
}

static void test_package_generation_and_mach_o_inspection() {
    std::cout << "[Test 2] Package generation with ARM64 binary and Mach-O inspection...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.startupgen";
    cfg.app_name = "StartupGen";
    cfg.display_name = "Startup Generator App";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead"};

    auto startup_bin = loader::get_embedded_campaign230_app_startup();
    assert(!startup_bin.empty());

    // Inspect binary directly with AppInspector
    auto insp_res = loader::AppInspector::inspect_binary(startup_bin.data(), startup_bin.size());
    assert(insp_res.is_valid);
    assert(insp_res.magic == loader::AppInspector::kMachO64Magic);
    assert(insp_res.cputype == loader::AppInspector::kCpuTypeArm64);
    assert(insp_res.page_aligned_16k);

    // Generate package
    auto pkg = AppGenerator::generate_package(cfg, startup_bin);
    assert(pkg.manifest.bundle_id == "com.winios.startupgen");
    assert(pkg.manifest.format_version == "3.0");
    assert(pkg.manifest.files.size() == 2);

    // Pack into bytes
    auto packed = pkg.pack_v3();
    assert(packed.size() > 64);

    // Validate package data
    packaging::AppPackage unpacked{};
    auto val_res = packaging::AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    assert(val_res == packaging::PackageValidationResult::Valid);
    (void)val_res;
    assert(unpacked.manifest.bundle_id == "com.winios.startupgen");
    assert(unpacked.manifest.executable_name == "bin/StartupGen");

    std::cout << "  -> Passed.\n";
}

static void test_signing_installation_and_launch_pipeline() {
    std::cout << "[Test 3] End-to-end signing, installation, and launch pipeline...\n";

    // Setup platform subsystems
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Applications");
    vfs->mkdir("/tmp");

    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto installer = std::make_shared<packaging::PackageInstaller>(vfs, registry);
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);

    // Initialize Publisher Trust Store with Platform Key
    security::PublisherTrustStore trust_store;
    security::PublisherKey plat_key{};
    plat_key.key_id = "platform_core_key";
    plat_key.publisher_id = "com.winios.system";
    plat_key.role = security::PublisherRole::Platform;
    plat_key.key_bytes = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                          0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                          0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
    plat_key.valid_from = 0;
    plat_key.valid_until = 0xFFFFFFFF;
    plat_key.is_revoked = false;
    assert(trust_store.register_key(plat_key));

    // Generate App Package
    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.settingsgen";
    cfg.app_name = "SettingsGen";
    cfg.display_name = "Settings Generator App";
    cfg.version = "1.0.0";
    cfg.permissions = {"SettingsRead"};

    auto settings_bin = loader::get_embedded_campaign230_app_settings();
    assert(!settings_bin.empty());

    auto pkg = AppGenerator::generate_package(cfg, settings_bin);
    auto packed = pkg.pack_v3();

    // Sign Package with Platform Key
    bool sign_ok = security::PublisherTrustStore::sign_package(
        packed, plat_key, plat_key.valid_from, plat_key.valid_until);
    assert(sign_ok);
    (void)sign_ok;

    // Verify Signature Status
    auto verify_res = trust_store.verify_package_signature(
        packed.data(), packed.size(), security::TrustPolicy::Development);
    assert(verify_res.status == security::SignatureStatus::PlatformSigned);
    assert(verify_res.publisher_id == "com.winios.system");

    // Write signed package to VFS
    assert(vfs->create_file("/tmp/settingsgen.wios"));
    assert(vfs->write_file("/tmp/settingsgen.wios", packed.data(), packed.size()));

    // Install using PackageInstaller
    packaging::AppPackage unpacked_pkg{};
    assert(packaging::AppPackage::unpack(packed.data(), packed.size(), unpacked_pkg));
    auto inst_res = installer->install(unpacked_pkg);
    assert(inst_res == packaging::InstallStatus::Success);
    (void)inst_res;

    // Verify installed files in VFS
    std::vector<uint8_t> exec_bytes;
    assert(vfs->read_file("/Applications/com.winios.settingsgen.app/bin/SettingsGen", exec_bytes));
    assert(exec_bytes == settings_bin);

    // Verify registry entry
    auto reg_entry = registry->get_app("com.winios.settingsgen");
    assert(reg_entry.has_value());
    assert(reg_entry->name == "SettingsGen");
    assert(reg_entry->is_active);

    // Launch Application
    auto launch_res = launcher->launch("com.winios.settingsgen");
    assert(launch_res.status == loader::LaunchStatus::Success);
    assert(launch_res.handle.pid >= 100);
    assert(launch_res.handle.generation >= 1);

    // Verify running in Identity Manager
    assert(identity_mgr->get_active_generation(launch_res.handle.pid) == launch_res.handle.generation);

    // Terminate Process
    assert(launcher->terminate(launch_res.handle));
    assert(!identity_mgr->is_valid_handle(launch_res.handle));
    assert(!identity_mgr->get_instance(launch_res.handle).has_value());

    // Uninstall Application with Purge
    assert(installer->uninstall("com.winios.settingsgen", true) == packaging::InstallStatus::Success);
    assert(!registry->get_app("com.winios.settingsgen").has_value());

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
    std::cout << "=== Stage 231.18: Application Generator and SDK Template Validation ===\n";
    test_source_and_manifest_generation();
    test_package_generation_and_mach_o_inspection();
    test_signing_installation_and_launch_pipeline();
    std::cout << "=== All Stage 231.18 Tests PASSED ===\n";
    return 0;
}
