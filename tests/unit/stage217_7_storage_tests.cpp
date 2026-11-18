#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "vfs/vfs.h"
#include "storage/isolated_storage.h"
#include "runtime/guest_storage_api.h"
#include "runtime/guest_async_io.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::storage;

void test_guest_storage_sandboxed_io() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto iso = std::make_shared<IsolatedStorage>(vfs, "com.test.app");
    GuestStorageApi api(iso, 10);

    // Reject unsafe path
    int bad_fd = api.OpenFile("../../etc/passwd", static_cast<uint32_t>(FileMode::kReadOnly));
    TEST_ASSERT(bad_fd == -1);

    // Open and write new file
    int fd = api.OpenFile("hello.txt", static_cast<uint32_t>(FileMode::kWriteOnly) |
                                       static_cast<uint32_t>(FileMode::kCreate));
    TEST_ASSERT(fd >= 3);
    TEST_ASSERT(api.GetOpenHandleCount() == 1);

    const std::string text = "WinIOS Storage API Test";
    int written = api.WriteFile(fd, text.data(), text.size());
    TEST_ASSERT(written == static_cast<int>(text.size()));
    TEST_ASSERT(api.CloseFile(fd));
    TEST_ASSERT(api.GetOpenHandleCount() == 0);

    // Read back
    int rfd = api.OpenFile("hello.txt", static_cast<uint32_t>(FileMode::kReadOnly));
    TEST_ASSERT(rfd >= 3);
    char read_buf[64] = {0};
    int bytes_read = api.ReadFile(rfd, read_buf, sizeof(read_buf) - 1);
    TEST_ASSERT(bytes_read == static_cast<int>(text.size()));
    TEST_ASSERT(text == std::string(read_buf));

    // Seek test
    TEST_ASSERT(api.SeekFile(rfd, 7, 0) == 7); // Seek to "Storage..."
    char sub_buf[16] = {0};
    api.ReadFile(rfd, sub_buf, 7);
    TEST_ASSERT(std::string(sub_buf) == "Storage");
    TEST_ASSERT(api.CloseFile(rfd));

    // Metadata
    FileMetadata meta{};
    TEST_ASSERT(api.GetFileMetadata("hello.txt", &meta));
    TEST_ASSERT(meta.exists);
    TEST_ASSERT(meta.size == text.size());

    // Remove
    TEST_ASSERT(api.RemoveFile("hello.txt"));
    TEST_ASSERT(!api.GetFileMetadata("hello.txt", &meta));

    std::cout << "[PASS] test_guest_storage_sandboxed_io\n";
}

void test_guest_async_io_foundation() {
    GuestAsyncIoManager aio(10);
    TEST_ASSERT(aio.GetPendingCount() == 0);

    uint32_t req1 = aio.SubmitRequest(AsyncOpType::kWrite, 3, 0x1000, 128);
    uint32_t req2 = aio.SubmitRequest(AsyncOpType::kRead, 4, 0x2000, 256);
    TEST_ASSERT(req1 != 0 && req2 != 0);
    TEST_ASSERT(aio.GetPendingCount() == 2);

    // Cancel req2
    TEST_ASSERT(aio.CancelRequest(req2));
    TEST_ASSERT(aio.GetPendingCount() == 1);

    // Execute req1
    TEST_ASSERT(aio.ExecutePending());
    TEST_ASSERT(aio.GetPendingCount() == 0);

    // Poll completions: req2 (cancelled), req1 (completed)
    AsyncIoRequest comp{};
    TEST_ASSERT(aio.PollCompletion(&comp));
    TEST_ASSERT(comp.request_id == req2);
    TEST_ASSERT(comp.status == AsyncOpStatus::kCancelled);

    TEST_ASSERT(aio.PollCompletion(&comp));
    TEST_ASSERT(comp.request_id == req1);
    TEST_ASSERT(comp.status == AsyncOpStatus::kCompleted);
    TEST_ASSERT(comp.bytes_transferred == 128);

    std::cout << "[PASS] test_guest_async_io_foundation\n";
}

int main() {
    test_guest_storage_sandboxed_io();
    test_guest_async_io_foundation();
    std::cout << "All Stage 217.7 & 217.10 storage and async I/O tests passed.\n";
    return 0;
}
