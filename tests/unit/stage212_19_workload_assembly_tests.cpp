// ============================================================================
// Stage 212.19: Multi-Process Workload Assembly and Loading Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/embedded_multi_process_workload_212.inl"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_binary_headers_and_integrity() {
    std::cout << "[Test 1] Verify Header Integrity of All Three Freestanding ARM64 Mach-O Binaries...\n";

    std::string err;

    // 1. Init coordinator
    TEST_CHECK(kInitCoordinatorMacho_len > 0, "Init coordinator binary length > 0");
    TEST_CHECK(MachoUserLoader::validate_header(kInitCoordinatorMacho_data, kInitCoordinatorMacho_len, &err),
               "Init coordinator header is valid ARM64 Mach-O");

    // 2. Service daemon
    TEST_CHECK(kServiceDaemonMacho_len > 0, "Service daemon binary length > 0");
    TEST_CHECK(MachoUserLoader::validate_header(kServiceDaemonMacho_data, kServiceDaemonMacho_len, &err),
               "Service daemon header is valid ARM64 Mach-O");

    // 3. Client app
    TEST_CHECK(kClientAppMacho_len > 0, "Client app binary length > 0");
    TEST_CHECK(MachoUserLoader::validate_header(kClientAppMacho_data, kClientAppMacho_len, &err),
               "Client app header is valid ARM64 Mach-O");

    // Verify magic and CPU type
    const std::uint32_t* init_hdr = reinterpret_cast<const std::uint32_t*>(kInitCoordinatorMacho_data);
    TEST_CHECK(init_hdr[0] == kMhMagic64, "Init coordinator MH_MAGIC_64 matches 0xFEEDFACF");
    TEST_CHECK(init_hdr[1] == static_cast<std::uint32_t>(kCpuTypeArm64), "Init coordinator CPU_TYPE_ARM64 matches 0x0100000C");
    TEST_CHECK(init_hdr[3] == 0x2, "Init coordinator MH_EXECUTE matches 0x2");
}

void test_init_coordinator_loading() {
    std::cout << "[Test 2] Load Init Coordinator Mach-O into Dedicated User Address Space...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    UserAddressSpace uas(mem, ppm, 100);
    std::vector<std::uint8_t> bin(kInitCoordinatorMacho_data, kInitCoordinatorMacho_data + kInitCoordinatorMacho_len);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Init coordinator loaded successfully");
    TEST_CHECK(img.entry_point != 0, "Init coordinator has non-zero entry point");
    TEST_CHECK(img.segments_loaded >= 1, "Init coordinator loaded at least 1 segment");
    std::cout << "  -> Entry point: 0x" << std::hex << img.entry_point
              << ", Text VMAddr: 0x" << img.text_vmaddr
              << ", Text VMSize: 0x" << img.text_vmsize << std::dec << "\n";
}

void test_service_daemon_loading() {
    std::cout << "[Test 3] Load Service Daemon Mach-O into Dedicated User Address Space...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    UserAddressSpace uas(mem, ppm, 101);
    std::vector<std::uint8_t> bin(kServiceDaemonMacho_data, kServiceDaemonMacho_data + kServiceDaemonMacho_len);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Service daemon loaded successfully");
    TEST_CHECK(img.entry_point != 0, "Service daemon has non-zero entry point");
    TEST_CHECK(img.segments_loaded >= 1, "Service daemon loaded at least 1 segment");
    std::cout << "  -> Entry point: 0x" << std::hex << img.entry_point
              << ", Text VMAddr: 0x" << img.text_vmaddr
              << ", Text VMSize: 0x" << img.text_vmsize << std::dec << "\n";
}

void test_client_app_loading() {
    std::cout << "[Test 4] Load Client App Mach-O into Dedicated User Address Space...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    UserAddressSpace uas(mem, ppm, 102);
    std::vector<std::uint8_t> bin(kClientAppMacho_data, kClientAppMacho_data + kClientAppMacho_len);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Client app loaded successfully");
    TEST_CHECK(img.entry_point != 0, "Client app has non-zero entry point");
    TEST_CHECK(img.segments_loaded >= 1, "Client app loaded at least 1 segment");
    std::cout << "  -> Entry point: 0x" << std::hex << img.entry_point
              << ", Text VMAddr: 0x" << img.text_vmaddr
              << ", Text VMSize: 0x" << img.text_vmsize << std::dec << "\n";
}

void test_distinctness_and_independence() {
    std::cout << "[Test 5] Verify Independence and Byte Distinctness Across Binaries...\n";

    TEST_CHECK(kInitCoordinatorMacho_len != kClientAppMacho_len ||
               std::memcmp(kInitCoordinatorMacho_data, kClientAppMacho_data, kClientAppMacho_len) != 0,
               "Init coordinator and Client app are distinct binaries");

    TEST_CHECK(kServiceDaemonMacho_len != kClientAppMacho_len ||
               std::memcmp(kServiceDaemonMacho_data, kClientAppMacho_data, kClientAppMacho_len) != 0,
               "Service daemon and Client app are distinct binaries");
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Stage 212.19: Multi-Process Workload Assembly and Loading Tests\n";
    std::cout << "=================================================================\n";

    test_binary_headers_and_integrity();
    test_init_coordinator_loading();
    test_service_daemon_loading();
    test_client_app_loading();
    test_distinctness_and_independence();

    if (g_failures == 0) {
        std::cout << "\n[PASS] All Stage 212.19 Workload Assembly Tests PASSED cleanly.\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] " << g_failures << " test(s) FAILED in Stage 212.19.\n";
        return 1;
    }
}
