#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "vfs/vfs.h"

using namespace winios::vfs;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

void test_path_normalization() {
    std::cout << "[Test 1] Path Normalization & Canonicalization..." << std::endl;

    TEST_CHECK(VirtualFileSystem::normalize_path("") == "/", "Empty path normalizes to '/'");
    TEST_CHECK(VirtualFileSystem::normalize_path("/") == "/", "Root path normalizes to '/'");
    TEST_CHECK(VirtualFileSystem::normalize_path("///usr///bin//") == "/usr/bin", "Consecutive slashes normalized");
    TEST_CHECK(VirtualFileSystem::normalize_path("/usr/local/../bin/./foo") == "/usr/bin/foo", "Dot and dot-dot resolved");
    TEST_CHECK(VirtualFileSystem::normalize_path("../../../etc/passwd") == "/etc/passwd", "Leading dot-dots clamped at root");
    TEST_CHECK(VirtualFileSystem::normalize_path("lib/libSystem.dylib", "/usr") == "/usr/lib/libSystem.dylib", "Relative path with CWD normalized");
}

void test_standard_hierarchy_and_devices() {
    std::cout << "[Test 2] Standard Darwin Rootfs Layout and Devices..." << std::endl;

    VirtualFileSystem vfs;

    const std::vector<std::string> expected_dirs = {
        "/", "/bin", "/sbin", "/etc", "/tmp", "/var",
        "/usr", "/usr/bin", "/usr/lib", "/System", "/System/Library",
        "/Applications", "/dev"
    };

    for (const auto& d : expected_dirs) {
        TEST_CHECK(vfs.exists(d), "Directory exists: " + d);
        VfsStat st{};
        TEST_CHECK(vfs.stat(d, st), "Stat directory: " + d);
        TEST_CHECK(st.type == VfsNodeType::Directory, "Node type is Directory");
    }

    // Verify /dev/null
    auto fd_null = vfs.open("/dev/null", kO_RDWR);
    TEST_CHECK(fd_null != nullptr && fd_null->is_valid(), "Opened /dev/null");
    char null_buf[16]{1, 2, 3};
    TEST_CHECK(vfs.read(*fd_null, null_buf, sizeof(null_buf)) == 0, "/dev/null read returns 0 (EOF)");
    TEST_CHECK(vfs.write(*fd_null, "test", 4) == 4, "/dev/null write consumes bytes");

    // Verify /dev/zero
    auto fd_zero = vfs.open("/dev/zero", kO_RDONLY);
    TEST_CHECK(fd_zero != nullptr && fd_zero->is_valid(), "Opened /dev/zero");
    char zero_buf[32];
    std::memset(zero_buf, 0xAA, sizeof(zero_buf));
    TEST_CHECK(vfs.read(*fd_zero, zero_buf, sizeof(zero_buf)) == 32, "/dev/zero read fills buffer");
    bool all_zero = true;
    for (char c : zero_buf) {
        if (c != 0) { all_zero = false; break; }
    }
    TEST_CHECK(all_zero, "/dev/zero bytes are all zero");

    // Verify /dev/random
    auto fd_rand = vfs.open("/dev/random", kO_RDONLY);
    TEST_CHECK(fd_rand != nullptr && fd_rand->is_valid(), "Opened /dev/random");
    char rand_buf[64]{0};
    TEST_CHECK(vfs.read(*fd_rand, rand_buf, sizeof(rand_buf)) == 64, "/dev/random read returns random stream");
    bool not_all_zero = false;
    for (char c : rand_buf) {
        if (c != 0) { not_all_zero = true; break; }
    }
    TEST_CHECK(not_all_zero, "/dev/random output is non-zero");
}

void test_file_operations_and_seeking() {
    std::cout << "[Test 3] File Creation, Write, Read, Append, and Lseek..." << std::endl;

    VirtualFileSystem vfs;

    // Create and write file
    const std::string text = "Hello WinIOS VFS!";
    TEST_CHECK(vfs.write_file("/tmp/greeting.txt", text.data(), text.size()), "Wrote /tmp/greeting.txt");

    std::vector<std::uint8_t> read_bytes;
    TEST_CHECK(vfs.read_file("/tmp/greeting.txt", read_bytes), "Read /tmp/greeting.txt");
    TEST_CHECK(read_bytes.size() == text.size(), "Read size matches written size");
    std::string read_str(read_bytes.begin(), read_bytes.end());
    TEST_CHECK(read_str == text, "Read content matches exactly");

    // Descriptor operations with seek
    auto desc = vfs.open("/tmp/greeting.txt", kO_RDWR);
    TEST_CHECK(desc != nullptr, "Opened file with kO_RDWR");

    // Read first 5 bytes
    char buf[16]{0};
    TEST_CHECK(vfs.read(*desc, buf, 5) == 5, "Read 5 bytes");
    buf[5] = '\0';
    TEST_CHECK(std::string(buf) == "Hello", "Read 'Hello'");

    // Seek to position 6
    TEST_CHECK(vfs.lseek(*desc, 6, kSeekSet) == 6, "SeekSet to 6");
    TEST_CHECK(vfs.read(*desc, buf, 6) == 6, "Read 6 bytes");
    buf[6] = '\0';
    TEST_CHECK(std::string(buf) == "WinIOS", "Read 'WinIOS'");

    // Seek from current (+1 byte to skip space)
    TEST_CHECK(vfs.lseek(*desc, 1, kSeekCur) == 13, "SeekCur +1 to 13");
    TEST_CHECK(vfs.read(*desc, buf, 4) == 4, "Read 4 bytes");
    buf[4] = '\0';
    TEST_CHECK(std::string(buf) == "VFS!", "Read 'VFS!'");

    // Seek to end and append
    TEST_CHECK(vfs.lseek(*desc, 0, kSeekEnd) == static_cast<std::int64_t>(text.size()), "SeekEnd to EOF");
    const std::string extra = " Extended.";
    TEST_CHECK(vfs.write(*desc, extra.data(), extra.size()) == static_cast<std::int64_t>(extra.size()), "Appended bytes");

    // Verify new file size via stat
    VfsStat st{};
    TEST_CHECK(vfs.fstat(*desc, st), "fstat succeeded");
    TEST_CHECK(st.size == text.size() + extra.size(), "File size updated correctly");

    vfs.close(*desc);
    TEST_CHECK(!desc->is_valid(), "Descriptor closed");
}

void test_directory_traversal_and_readdir() {
    std::cout << "[Test 4] Directory Hierarchy and readdir..." << std::endl;

    VirtualFileSystem vfs;

    TEST_CHECK(vfs.mkdir("/var/log"), "Created /var/log");
    TEST_CHECK(vfs.create_file("/var/log/syslog.log"), "Created /var/log/syslog.log");
    TEST_CHECK(vfs.create_file("/var/log/kernel.log"), "Created /var/log/kernel.log");
    TEST_CHECK(vfs.mkdir("/var/log/crashes"), "Created /var/log/crashes");

    std::vector<VfsDirEntry> entries;
    TEST_CHECK(vfs.readdir("/var/log", entries), "readdir /var/log");
    TEST_CHECK(entries.size() == 5, "readdir returned 5 entries (., .., syslog.log, kernel.log, crashes)");

    bool found_dot = false;
    bool found_dotdot = false;
    bool found_syslog = false;
    bool found_kernel = false;
    bool found_crashes = false;

    for (const auto& e : entries) {
        if (e.name == ".") { found_dot = true; TEST_CHECK(e.type == 4, "'.' has DT_DIR"); }
        if (e.name == "..") { found_dotdot = true; TEST_CHECK(e.type == 4, "'..' has DT_DIR"); }
        if (e.name == "syslog.log") { found_syslog = true; TEST_CHECK(e.type == 8, "'syslog.log' has DT_REG"); }
        if (e.name == "kernel.log") { found_kernel = true; TEST_CHECK(e.type == 8, "'kernel.log' has DT_REG"); }
        if (e.name == "crashes") { found_crashes = true; TEST_CHECK(e.type == 4, "'crashes' has DT_DIR"); }
    }

    TEST_CHECK(found_dot && found_dotdot && found_syslog && found_kernel && found_crashes, "All entries present");
}

void test_symlinks_and_loop_detection() {
    std::cout << "[Test 5] Symlink Resolution and Loop Detection..." << std::endl;

    VirtualFileSystem vfs;

    vfs.write_file("/usr/lib/libtarget.dylib", "DYLIB_DATA", 10);
    TEST_CHECK(vfs.symlink("/usr/lib/libtarget.dylib", "/usr/lib/libalias.dylib"), "Created symlink libalias -> libtarget");

    // Single-hop lookup
    auto target_node = vfs.lookup("/usr/lib/libalias.dylib", true);
    TEST_CHECK(target_node != nullptr, "Resolved symlink to target node");
    TEST_CHECK(target_node->name == "libtarget.dylib", "Resolved name is target file");

    // Multi-hop symlink
    TEST_CHECK(vfs.symlink("/usr/lib/libalias.dylib", "/usr/lib/libchain.dylib"), "Created chained symlink libchain -> libalias");
    auto chain_node = vfs.lookup("/usr/lib/libchain.dylib", true);
    TEST_CHECK(chain_node != nullptr && chain_node->name == "libtarget.dylib", "Resolved 2-hop symlink chain");

    // Symlink cycle detection (ELOOP)
    TEST_CHECK(vfs.symlink("/tmp/loop_b", "/tmp/loop_a"), "Created symlink loop_a -> loop_b");
    TEST_CHECK(vfs.symlink("/tmp/loop_a", "/tmp/loop_b"), "Created symlink loop_b -> loop_a");
    auto loop_node = vfs.lookup("/tmp/loop_a", true, 8);
    TEST_CHECK(loop_node == nullptr, "Symlink cycle rejected cleanly with depth limit");
}

void test_stat64_abi_conformance() {
    std::cout << "[Test 6] DarwinStat64 ABI Conformance..." << std::endl;

    VirtualFileSystem vfs;
    vfs.write_file("/etc/hosts", "127.0.0.1 localhost\n", 20);

    VfsStat st{};
    TEST_CHECK(vfs.stat("/etc/hosts", st), "stat /etc/hosts");
    TEST_CHECK(st.size == 20, "Stat size is 20");

    DarwinStat64 darwin_st{};
    st.to_darwin_stat64(darwin_st);

    TEST_CHECK(darwin_st.st_size == 20, "DarwinStat64 st_size is 20");
    TEST_CHECK(darwin_st.st_ino == st.ino, "DarwinStat64 st_ino matches VfsStat ino");
    TEST_CHECK((darwin_st.st_mode & 0170000) == 0100000, "DarwinStat64 mode has S_IFREG format");
    TEST_CHECK(darwin_st.st_blksize == 16384, "DarwinStat64 blksize is 16KB");
    TEST_CHECK(darwin_st.st_nlink == 1, "DarwinStat64 nlink is 1");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.12 Virtual File System (VFS) Tests ===" << std::endl;
    test_path_normalization();
    test_standard_hierarchy_and_devices();
    test_file_operations_and_seeking();
    test_directory_traversal_and_readdir();
    test_symlinks_and_loop_detection();
    test_stat64_abi_conformance();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.12: All VFS architecture tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.12: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
