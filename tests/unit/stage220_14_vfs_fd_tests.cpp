// ============================================================================
// Stage 220.14 Unit Tests: File Descriptor Lifetime, Cloning & Reference Counting
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>
#include <cstring>

#include "kernel/pipe.h"
#include "kernel/bsd_process.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;

static void test_fd_allocation_duplication_and_exhaustion() {
    std::cout << "[Test 1] FD allocation, duplication, and exhaustion defense...\n";
    FileDescriptorTable table;

    // Verify initial standard POSIX descriptors
    TEST_ASSERT(table.open_count() == 3);
    TEST_ASSERT(table.is_valid_fd(0) && table.get_fd(0)->readable && !table.get_fd(0)->writable);
    TEST_ASSERT(table.is_valid_fd(1) && !table.get_fd(1)->readable && table.get_fd(1)->writable);
    TEST_ASSERT(table.is_valid_fd(2) && !table.get_fd(2)->readable && table.get_fd(2)->writable);

    // Allocate fd 3
    int fd3 = table.allocate_fd(FileType::Regular, true, true, "/var/log/sys.log");
    TEST_ASSERT(fd3 == 3);
    TEST_ASSERT(table.open_count() == 4);

    // Duplicate fd 3 -> should get fd 4
    int fd4 = table.duplicate_fd(fd3);
    TEST_ASSERT(fd4 == 4);
    TEST_ASSERT(table.get_fd(fd4)->path == "/var/log/sys.log");
    TEST_ASSERT(table.open_count() == 5);

    // Close fd 3: fd 4 remains open
    TEST_ASSERT(table.close_fd(fd3));
    TEST_ASSERT(!table.is_valid_fd(fd3));
    TEST_ASSERT(table.is_valid_fd(fd4));

    // Exhaust remaining descriptors up to kMaxFiles (64)
    std::vector<int> allocated;
    for (int i = 0; i < FileDescriptorTable::kMaxFiles; ++i) {
        int fd = table.allocate_fd(FileType::Regular, true, true, "fill");
        if (fd >= 0) {
            allocated.push_back(fd);
        }
    }
    TEST_ASSERT(table.open_count() == FileDescriptorTable::kMaxFiles);

    // Table is full: next allocation and duplicate must fail (-1)
    TEST_ASSERT(table.allocate_fd(FileType::Regular, true, true, "overflow") == -1);
    TEST_ASSERT(table.duplicate_fd(fd4) == -1);

    // Close one descriptor and verify allocation succeeds in that exact slot
    int freed_slot = allocated.back();
    TEST_ASSERT(table.close_fd(freed_slot));
    TEST_ASSERT(table.open_count() == FileDescriptorTable::kMaxFiles - 1);

    int reallocated = table.allocate_fd(FileType::Regular, true, true, "reclaimed");
    TEST_ASSERT(reallocated == freed_slot);
    TEST_ASSERT(table.open_count() == FileDescriptorTable::kMaxFiles);

    std::cout << "  Passed allocation, duplication, and exhaustion tests.\n";
}

static void test_clone_on_fork_and_shared_pipe_lifetime() {
    std::cout << "[Test 2] Clone-on-fork and shared pipe reference lifetime...\n";
    FileDescriptorTable parent_table;

    // Create a shared pipe in parent
    auto pipe = std::make_shared<Pipe>();
    int p_read = parent_table.allocate_fd(FileType::MemoryPipe, true, false, "pipe:r");
    int p_write = parent_table.allocate_fd(FileType::MemoryPipe, false, true, "pipe:w");
    TEST_ASSERT(p_read == 3 && p_write == 4);

    parent_table.get_fd_mut(p_read)->pipe = pipe;
    parent_table.get_fd_mut(p_read)->is_pipe_reader = true;
    parent_table.get_fd_mut(p_write)->pipe = pipe;
    parent_table.get_fd_mut(p_write)->is_pipe_writer = true;

    // Simulate fork: child clones parent's descriptor table
    FileDescriptorTable child_table;
    child_table.clone_from(parent_table);

    TEST_ASSERT(child_table.is_valid_fd(3) && child_table.is_valid_fd(4));
    TEST_ASSERT(child_table.get_fd(3)->pipe == pipe);
    TEST_ASSERT(child_table.get_fd(4)->pipe == pipe);

    // Parent writes to pipe
    const char msg[] = "Cross-Process IPC Token 0xFEED";
    const std::int64_t written = pipe->write(msg, sizeof(msg));
    TEST_ASSERT(written == sizeof(msg));

    // Parent closes its write end
    TEST_ASSERT(parent_table.close_fd(p_write));
    TEST_ASSERT(!parent_table.is_valid_fd(p_write));

    // Pipe is NOT closed because child still holds write fd
    TEST_ASSERT(!pipe->is_write_closed());

    // Child reads from its read end
    char buf[64] = {};
    const std::int64_t bytes_read = child_table.get_fd(3)->pipe->read(buf, sizeof(buf));
    TEST_ASSERT(bytes_read == sizeof(msg));
    TEST_ASSERT(std::strcmp(buf, msg) == 0);

    // Child closes its write end -> pipe writer count drops to 0
    TEST_ASSERT(child_table.close_fd(p_write));
    child_table.get_fd_mut(3)->pipe->close_write();
    TEST_ASSERT(pipe->is_write_closed());

    // Subsequent read on empty pipe with closed writers returns 0 (EOF)
    const std::int64_t eof_read = pipe->read(buf, sizeof(buf));
    TEST_ASSERT(eof_read == 0);

    std::cout << "  Passed clone-on-fork and shared pipe lifetime tests.\n";
}

int main() {
    std::cout << "=== Stage 220.14 File Descriptor Table Lifetime & Cloning Tests ===\n";
    test_fd_allocation_duplication_and_exhaustion();
    test_clone_on_fork_and_shared_pipe_lifetime();
    std::cout << "All Stage 220.14 tests passed successfully.\n";
    return 0;
}
