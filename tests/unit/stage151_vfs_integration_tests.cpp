#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/darwin_bsd_subsystem.h"

using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Canonical Darwin VFS Directory Tree
bool test_root_hierarchy() {
    std::cout << "[Test 1] Darwin Root Filesystem Hierarchy...\n";

    DarwinVfs vfs;
    auto root = vfs.resolve_path("/");
    TEST_ASSERT(root != nullptr, "Root directory / must exist");
    TEST_ASSERT(root->is_directory(), "Root must be a directory");
    TEST_ASSERT(root->mode == 0755, "Root directory mode must be 0755");

    // Standard Darwin/iOS paths
    TEST_ASSERT(vfs.resolve_path("/sbin") != nullptr, "/sbin must exist");
    TEST_ASSERT(vfs.resolve_path("/bin") != nullptr, "/bin must exist");
    TEST_ASSERT(vfs.resolve_path("/usr/bin") != nullptr, "/usr/bin must exist");
    TEST_ASSERT(vfs.resolve_path("/usr/lib") != nullptr, "/usr/lib must exist");
    TEST_ASSERT(vfs.resolve_path("/usr/libexec") != nullptr, "/usr/libexec must exist");
    TEST_ASSERT(vfs.resolve_path("/System/Library/Frameworks") != nullptr, "/System/Library/Frameworks must exist");
    TEST_ASSERT(vfs.resolve_path("/Applications") != nullptr, "/Applications must exist");
    TEST_ASSERT(vfs.resolve_path("/private/var/tmp") != nullptr, "/private/var/tmp must exist");
    TEST_ASSERT(vfs.resolve_path("/dev") != nullptr, "/dev must exist");

    std::cout << "  -> PASS: Canonical Darwin VFS Root Hierarchy Verified\n";
    return true;
}

// Test 2: Path Resolution Robustness (Redundant Slashes, Dot, Dot-Dot, Non-existent)
bool test_path_resolution() {
    std::cout << "[Test 2] Path Resolution Robustness & Edge Cases...\n";

    DarwinVfs vfs;

    // Normal path
    auto n1 = vfs.resolve_path("/sbin/launchd");
    TEST_ASSERT(n1 != nullptr, "Must resolve /sbin/launchd");

    // Redundant slashes
    auto n2 = vfs.resolve_path("///sbin///launchd");
    TEST_ASSERT(n2 != nullptr, "Must resolve multiple slashes");
    TEST_ASSERT(n1 == n2, "Resolved node must match");

    // Current directory '.' and parent directory '..'
    auto n3 = vfs.resolve_path("/sbin/./launchd");
    TEST_ASSERT(n3 != nullptr, "Must resolve '.' component");
    TEST_ASSERT(n1 == n3, "Resolved node must match");

    // Non-existent path
    auto n_bad = vfs.resolve_path("/usr/nonexistent/tool");
    TEST_ASSERT(n_bad == nullptr, "Non-existent path must resolve to nullptr");

    std::cout << "  -> PASS: Path Resolution Robustness Verified\n";
    return true;
}

// Test 3: Nested Directory Creation and Traversal
bool test_nested_directory_creation() {
    std::cout << "[Test 3] Nested Directory Creation and Deep Traversal...\n";

    DarwinVfs vfs;
    const std::string deep_path = "/private/var/mobile/Containers/Data/Application/TestApp/Documents";

    bool ok = vfs.create_directory(deep_path);
    TEST_ASSERT(ok, "create_directory deep path must succeed");

    auto node = vfs.resolve_path(deep_path);
    TEST_ASSERT(node != nullptr, "Deep directory must resolve");
    TEST_ASSERT(node->is_directory(), "Resolved deep node must be a directory");
    TEST_ASSERT(node->mode == 0755, "Directory mode must default to 0755");

    std::cout << "  -> PASS: Nested Directory Creation Verified\n";
    return true;
}

// Test 4: Directory Enumeration
bool test_directory_enumeration() {
    std::cout << "[Test 4] Directory Enumeration (list_directory)...\n";

    DarwinVfs vfs;
    auto dev_entries = vfs.list_directory("/dev");
    TEST_ASSERT(!dev_entries.empty(), "/dev must contain device nodes");

    bool has_null = false, has_zero = false, has_random = false;
    for (const auto& name : dev_entries) {
        if (name == "null") has_null = true;
        if (name == "zero") has_zero = true;
        if (name == "random") has_random = true;
    }
    TEST_ASSERT(has_null, "/dev/null must be enumerated");
    TEST_ASSERT(has_zero, "/dev/zero must be enumerated");
    TEST_ASSERT(has_random, "/dev/random must be enumerated");

    // Invalid directory listing
    auto bad_entries = vfs.list_directory("/nonexistent_folder");
    TEST_ASSERT(bad_entries.empty(), "Listing nonexistent directory must return empty list");

    std::cout << "  -> PASS: Directory Enumeration Verified\n";
    return true;
}

// Test 5: File Creation, Metadata, Inode Tracking, and Readback
bool test_file_metadata_and_readback() {
    std::cout << "[Test 5] File Creation, Metadata & Inode Allocation...\n";

    DarwinVfs vfs;
    const char payload[] = "iPhone15,4 - Apple A16 Bionic Darwin VFS Test Payload";
    const std::size_t payload_len = sizeof(payload);

    bool ok = vfs.write_file("/etc/build_info.txt", payload, payload_len);
    TEST_ASSERT(ok, "write_file must succeed");

    auto node = vfs.resolve_path("/etc/build_info.txt");
    TEST_ASSERT(node != nullptr, "File node must resolve");
    TEST_ASSERT(node->type == VfsNodeType::RegularFile, "Node must be RegularFile");
    TEST_ASSERT(node->size == payload_len, "Size match");
    TEST_ASSERT(node->mode == 0644, "File mode must be 0644");
    TEST_ASSERT(node->inode >= 100, "Inode must be dynamically allocated from inode pool");

    auto read_bytes = vfs.read_file("/etc/build_info.txt");
    TEST_ASSERT(read_bytes.has_value(), "read_file must return content");
    TEST_ASSERT(read_bytes->size() == payload_len, "Bytes size match");
    TEST_ASSERT(std::memcmp(read_bytes->data(), payload, payload_len) == 0, "Payload content match");

    std::cout << "  -> PASS: File Creation & Inode Metadata Verified\n";
    return true;
}

// Test 6: File Descriptor Lifecycle: Open, Offset Tracking, Partial Read, EOF, Close
bool test_fd_read_lifecycle() {
    std::cout << "[Test 6] File Descriptor Read Lifecycle (Open, Offset, EOF, Close)...\n";

    DarwinBsdSubsystem bsd;
    const char data[] = "0123456789ABCDEF"; // 16 bytes
    bsd.vfs().write_file("/tmp/seq.bin", data, 16);

    const std::uint32_t pid = 1;
    int fd = bsd.open(pid, "/tmp/seq.bin", 0x0); // O_RDONLY
    TEST_ASSERT(fd >= 3, "Valid FD returned");

    // Partial read 1: read 6 bytes
    char buf1[6]{0};
    std::int64_t r1 = bsd.read(pid, fd, buf1, 6);
    TEST_ASSERT(r1 == 6, "Read 6 bytes");
    TEST_ASSERT(std::memcmp(buf1, "012345", 6) == 0, "Content matches bytes 0-5");

    // Partial read 2: read next 6 bytes (offset should be 6)
    char buf2[6]{0};
    std::int64_t r2 = bsd.read(pid, fd, buf2, 6);
    TEST_ASSERT(r2 == 6, "Read next 6 bytes");
    TEST_ASSERT(std::memcmp(buf2, "6789AB", 6) == 0, "Content matches bytes 6-11");

    // Remaining read 3: read 10 bytes (only 4 remain)
    char buf3[10]{0};
    std::int64_t r3 = bsd.read(pid, fd, buf3, 10);
    TEST_ASSERT(r3 == 4, "Read remaining 4 bytes");
    TEST_ASSERT(std::memcmp(buf3, "CDEF", 4) == 0, "Content matches bytes 12-15");

    // EOF: subsequent read must return 0
    char buf4[4]{0};
    std::int64_t r4 = bsd.read(pid, fd, buf4, 4);
    TEST_ASSERT(r4 == 0, "Subsequent read at EOF must return 0");

    // Close FD
    int close_ret = bsd.close(pid, fd);
    TEST_ASSERT(close_ret == 0, "close must succeed");

    // Double close should fail
    int double_close = bsd.close(pid, fd);
    TEST_ASSERT(double_close == -1, "Double close must fail with -1");

    // Read on closed FD should fail
    std::int64_t r_closed = bsd.read(pid, fd, buf1, 1);
    TEST_ASSERT(r_closed == -1, "Read on closed FD must return -1");

    std::cout << "  -> PASS: File Descriptor Read Lifecycle Verified\n";
    return true;
}

// Test 7: File Descriptor Write Operations & Capacity Growth
bool test_fd_write_lifecycle() {
    std::cout << "[Test 7] File Descriptor Write Operations & Capacity Growth...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = 1;

    // Create an empty file first
    bsd.vfs().write_file("/tmp/output.log", nullptr, 0);

    int fd = bsd.open(pid, "/tmp/output.log", 0x2); // O_RDWR
    TEST_ASSERT(fd >= 3, "Valid FD for output log");

    const char chunk1[] = "Chunk One; ";
    std::int64_t w1 = bsd.write(pid, fd, chunk1, std::strlen(chunk1));
    TEST_ASSERT(w1 == static_cast<std::int64_t>(std::strlen(chunk1)), "Write chunk 1");

    const char chunk2[] = "Chunk Two; ";
    std::int64_t w2 = bsd.write(pid, fd, chunk2, std::strlen(chunk2));
    TEST_ASSERT(w2 == static_cast<std::int64_t>(std::strlen(chunk2)), "Write chunk 2");

    bsd.close(pid, fd);

    // Read back and verify concatenation
    auto read_back = bsd.vfs().read_file("/tmp/output.log");
    TEST_ASSERT(read_back.has_value(), "Read back output log");
    std::string full_text(read_back->begin(), read_back->end());
    TEST_ASSERT(full_text == "Chunk One; Chunk Two; ", "Full text concatenation match");

    std::cout << "  -> PASS: File Descriptor Write Operations Verified\n";
    return true;
}

// Test 8: Device Nodes and Special Character Devices
bool test_special_devices() {
    std::cout << "[Test 8] Special Character Devices (/dev/null, /dev/zero)...\n";

    DarwinVfs vfs;
    auto dev_null = vfs.resolve_path("/dev/null");
    TEST_ASSERT(dev_null != nullptr, "/dev/null must exist");

    auto dev_zero = vfs.resolve_path("/dev/zero");
    TEST_ASSERT(dev_zero != nullptr, "/dev/zero must exist");

    auto dev_random = vfs.resolve_path("/dev/random");
    TEST_ASSERT(dev_random != nullptr, "/dev/random must exist");

    std::cout << "  -> PASS: Special Device Nodes Verified\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 151: DARWIN VFS & FILE DESCRIPTOR TEST SUITE      \n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Subsystem: Virtual Filesystem, Inodes, Traversal & FDs         \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_root_hierarchy()) passed++;
    if (test_path_resolution()) passed++;
    if (test_nested_directory_creation()) passed++;
    if (test_directory_enumeration()) passed++;
    if (test_file_metadata_and_readback()) passed++;
    if (test_fd_read_lifecycle()) passed++;
    if (test_fd_write_lifecycle()) passed++;
    if (test_special_devices()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 151 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
