// =============================================================================
// WinIOS — Stage 204.12: Virtual Filesystem (VFS) Architecture Unit Tests
// =============================================================================

#include "runtime/darwin_bsd_subsystem.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Standard Darwin Rootfs Layout & Virtual Devices
// -----------------------------------------------------------------------------
void test_default_rootfs_and_devices() {
    std::cout << "[Stage 204.12 Test 1] Standard Rootfs Layout & Devices...\n";

    DarwinVfs vfs;

    // 1. Verify standard system directories
    const char* expected_dirs[] = {
        "/", "/bin", "/sbin", "/usr/lib", "/usr/bin",
        "/System/Library/Frameworks", "/Applications",
        "/tmp", "/var", "/etc", "/dev",
        "/private/var/mobile/Containers/Data/Application"
    };
    for (const char* dir : expected_dirs) {
        TEST_CHECK(vfs.exists(dir), std::string("Directory exists: ") + dir);
        TEST_CHECK(vfs.is_directory(dir), std::string("Is directory: ") + dir);
    }

    // 2. Verify virtual character devices
    const char* devices[] = {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom"};
    for (const char* dev : devices) {
        TEST_CHECK(vfs.exists(dev), std::string("Device exists: ") + dev);
        VfsNode stat_info;
        TEST_CHECK(vfs.stat_path(dev, stat_info), std::string("Stat device: ") + dev);
        TEST_CHECK(stat_info.type == VfsNodeType::CharacterDevice, "Device type is CharacterDevice");
        TEST_CHECK(stat_info.mode == 0666, "Device permissions are 0666");
    }

    std::cout << "  -> Standard rootfs layout & devices verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: File Creation, Read/Write, and Data Integrity
// -----------------------------------------------------------------------------
void test_file_io_operations() {
    std::cout << "[Stage 204.12 Test 2] File I/O Operations & Data Integrity...\n";

    DarwinVfs vfs;
    std::string test_data = "Hello Darwin ARM64 Userspace VFS!";

    // Write file into deeply nested directory
    bool w_ok = vfs.write_file("/tmp/nested/test/sample.txt", test_data.data(), test_data.size());
    TEST_CHECK(w_ok, "write_file succeeded with automatic directory creation");
    TEST_CHECK(vfs.exists("/tmp/nested/test/sample.txt"), "Target file exists");
    TEST_CHECK(vfs.is_regular_file("/tmp/nested/test/sample.txt"), "Target is regular file");

    // Read back and compare bytes
    auto content = vfs.read_file("/tmp/nested/test/sample.txt");
    TEST_CHECK(content.has_value(), "read_file succeeded");
    if (content.has_value()) {
        std::string read_str(content->begin(), content->end());
        TEST_CHECK(read_str == test_data, "Read content exactly matches written payload");
    }

    // Query stat
    VfsNode st;
    TEST_CHECK(vfs.stat_path("/tmp/nested/test/sample.txt", st), "stat_path succeeded");
    TEST_CHECK(st.size == test_data.size(), "stat size matches payload length");
    TEST_CHECK(st.name == "sample.txt", "stat name matches filename");

    std::cout << "  -> File I/O operations & integrity verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Symlink Resolution & Cycle Detection
// -----------------------------------------------------------------------------
void test_symlink_resolution_and_cycles() {
    std::cout << "[Stage 204.12 Test 3] Symlink Resolution & Cycles...\n";

    DarwinVfs vfs;
    std::string secret = "SuperSecretPayload";
    vfs.write_file("/private/var/secret.key", secret.data(), secret.size());

    // 1. Create valid symlink: /etc/key.lnk -> /private/var/secret.key
    bool s_ok = vfs.create_symlink("/private/var/secret.key", "/etc/key.lnk");
    TEST_CHECK(s_ok, "create_symlink succeeded");

    auto resolved = vfs.resolve_path("/etc/key.lnk");
    TEST_CHECK(resolved != nullptr, "Symlink resolved to target node");
    if (resolved) {
        TEST_CHECK(resolved->name == "secret.key", "Resolved target is secret.key");
        std::string payload(resolved->content.begin(), resolved->content.end());
        TEST_CHECK(payload == secret, "Target payload matches");
    }

    // 2. Cyclic Symlink: linkA -> linkB, linkB -> linkA
    TEST_CHECK(vfs.create_symlink("/tmp/linkB", "/tmp/linkA"), "Created linkA");
    TEST_CHECK(vfs.create_symlink("/tmp/linkA", "/tmp/linkB"), "Created linkB");

    auto cycle_node = vfs.resolve_path("/tmp/linkA");
    TEST_CHECK(cycle_node == nullptr, "Cyclic symlink traversal cleanly terminates and returns nullptr");

    std::cout << "  -> Symlink resolution & cycle safety verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Directory Listing & File/Directory Deletion
// -----------------------------------------------------------------------------
void test_directory_enumeration_and_deletion() {
    std::cout << "[Stage 204.12 Test 4] Directory Enumeration & Deletion...\n";

    DarwinVfs vfs;
    vfs.create_directory("/test_dir/sub");
    vfs.write_file("/test_dir/file1.dat", "1", 1);
    vfs.write_file("/test_dir/file2.dat", "2", 1);

    auto entries = vfs.list_directory("/test_dir");
    TEST_CHECK(entries.size() == 3, "Directory contains 3 children");
    TEST_CHECK(std::find(entries.begin(), entries.end(), "sub") != entries.end(), "Found 'sub'");
    TEST_CHECK(std::find(entries.begin(), entries.end(), "file1.dat") != entries.end(), "Found 'file1.dat'");
    TEST_CHECK(std::find(entries.begin(), entries.end(), "file2.dat") != entries.end(), "Found 'file2.dat'");

    // 1. Delete regular file
    TEST_CHECK(vfs.remove_file("/test_dir/file1.dat"), "Removed regular file");
    TEST_CHECK(!vfs.exists("/test_dir/file1.dat"), "file1.dat no longer exists");

    // 2. Cannot delete non-empty directory
    TEST_CHECK(!vfs.remove_directory("/test_dir"), "Cannot remove non-empty directory");

    // 3. Delete sub-directory then test_dir
    TEST_CHECK(vfs.remove_file("/test_dir/file2.dat"), "Removed file2.dat");
    TEST_CHECK(vfs.remove_directory("/test_dir/sub"), "Removed empty sub-directory");
    TEST_CHECK(vfs.remove_directory("/test_dir"), "Removed empty test_dir");
    TEST_CHECK(!vfs.exists("/test_dir"), "test_dir no longer exists");

    std::cout << "  -> Directory enumeration & deletion verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.12 Virtual Filesystem Architecture Tests ===\n";
    test_default_rootfs_and_devices();
    test_file_io_operations();
    test_symlink_resolution_and_cycles();
    test_directory_enumeration_and_deletion();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.12 Virtual Filesystem Architecture passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.12 Virtual Filesystem Architecture failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}
