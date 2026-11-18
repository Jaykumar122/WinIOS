#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>

#include "kernel/bsd_process.h"
#include "kernel/pipe.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;

void test_fd_table_defaults_and_allocation() {
    std::cout << "[Test 1] Stage 219.12: FD Table Standard Descriptors (0, 1, 2) & Allocation...\n";

    FileDescriptorTable fdt;
    TEST_ASSERT(fdt.open_count() == 3);

    // Verify standard descriptors 0, 1, 2
    TEST_ASSERT(fdt.is_valid_fd(0));
    TEST_ASSERT(fdt.is_valid_fd(1));
    TEST_ASSERT(fdt.is_valid_fd(2));

    const auto* fd0 = fdt.get_fd(0);
    const auto* fd1 = fdt.get_fd(1);
    const auto* fd2 = fdt.get_fd(2);
    TEST_ASSERT(fd0 && fd0->readable && !fd0->writable);
    TEST_ASSERT(fd1 && !fd1->readable && fd1->writable);
    TEST_ASSERT(fd2 && !fd2->readable && fd2->writable);

    // Allocate new fd
    int fd3 = fdt.allocate_fd(FileType::Regular, true, true, "/tmp/app.data");
    TEST_ASSERT(fd3 == 3);
    TEST_ASSERT(fdt.is_valid_fd(3));
    TEST_ASSERT(fdt.open_count() == 4);

    // Close fd3
    TEST_ASSERT(fdt.close_fd(3));
    TEST_ASSERT(!fdt.is_valid_fd(3));
    TEST_ASSERT(fdt.open_count() == 3);

    // Re-allocate should reuse slot 3
    int fd3_reused = fdt.allocate_fd(FileType::Regular, true, false, "/tmp/app2.data");
    TEST_ASSERT(fd3_reused == 3);

    std::cout << "  -> Standard descriptors and allocation/slot-reuse verified.\n";
}

void test_fd_duplication() {
    std::cout << "[Test 2] Stage 219.12: File Descriptor Duplication (SYS_dup, 41)...\n";

    FileDescriptorTable fdt;
    int data_fd = fdt.allocate_fd(FileType::Regular, true, true, "/var/log/app.log");
    TEST_ASSERT(data_fd == 3);

    // Duplicate fd 3 -> should allocate next available slot (4)
    int dup_fd = fdt.duplicate_fd(data_fd);
    TEST_ASSERT(dup_fd == 4);
    TEST_ASSERT(fdt.is_valid_fd(dup_fd));

    const auto* orig = fdt.get_fd(data_fd);
    const auto* dup = fdt.get_fd(dup_fd);
    TEST_ASSERT(orig != nullptr && dup != nullptr);
    TEST_ASSERT(orig->path == dup->path);
    TEST_ASSERT(orig->readable == dup->readable);
    TEST_ASSERT(orig->writable == dup->writable);
    TEST_ASSERT(orig->type == dup->type);

    // Duplicate an invalid fd should fail
    TEST_ASSERT(fdt.duplicate_fd(99) == -1);

    std::cout << "  -> File descriptor duplication (SYS_dup) verified.\n";
}

void test_pipe_ring_buffer_semantics() {
    std::cout << "[Test 3] Stage 219.13: Pipe Circular Ring Buffer & Close Semantics...\n";

    Pipe pipe;
    TEST_ASSERT(pipe.bytes_available() == 0);
    TEST_ASSERT(pipe.space_available() > 0);

    const char msg[] = "Hello WinIOS Pipe IPC";
    const size_t len = sizeof(msg);

    // Write to pipe
    int64_t written = pipe.write(msg, len);
    TEST_ASSERT(written == static_cast<int64_t>(len));
    TEST_ASSERT(pipe.bytes_available() == len);

    // Read from pipe
    char buf[64]{0};
    int64_t nread = pipe.read(buf, sizeof(buf));
    TEST_ASSERT(nread == static_cast<int64_t>(len));
    TEST_ASSERT(std::strcmp(buf, msg) == 0);
    TEST_ASSERT(pipe.bytes_available() == 0);

    // Test EOF when writer closes
    pipe.close_write();
    int64_t eof_read = pipe.read(buf, sizeof(buf));
    TEST_ASSERT(eof_read == 0); // 0 = EOF

    // Test EPIPE when reader closes
    Pipe pipe2;
    pipe2.close_read();
    int64_t broken_write = pipe2.write(msg, len);
    TEST_ASSERT(broken_write == -32); // -EPIPE (Broken pipe)

    std::cout << "  -> Pipe FIFO buffering, EOF, and broken pipe (EPIPE) verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.13: VFS File Descriptors & Pipe Subsystem Tests\n";
    std::cout << "=================================================================\n";

    test_fd_table_defaults_and_allocation();
    test_fd_duplication();
    test_pipe_ring_buffer_semantics();

    std::cout << "\n[PASS] All Stage 219.13 tests passed successfully!\n";
    return 0;
}
