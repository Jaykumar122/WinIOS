#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <string>

namespace winios::runtime {

enum class AsyncOpType : uint32_t {
    kRead = 1,
    kWrite = 2,
    kFlush = 3,
    kDelay = 4
};

enum class AsyncOpStatus : uint32_t {
    kPending = 0,
    kCompleted = 1,
    kCancelled = 2,
    kFailed = 3
};

struct AsyncIoRequest {
    uint32_t request_id{0};
    uint32_t process_id{0};
    AsyncOpType type{AsyncOpType::kRead};
    int fd{-1};
    uint64_t buffer_ptr{0};
    size_t size{0};
    uint32_t timeout_ms{0};
    AsyncOpStatus status{AsyncOpStatus::kPending};
    int bytes_transferred{0};
    uint32_t error_code{0};
};

class GuestAsyncIoManager {
public:
    static constexpr size_t kMaxQueueCapacity = 64;

    explicit GuestAsyncIoManager(uint32_t process_id);
    ~GuestAsyncIoManager();

    uint32_t SubmitRequest(AsyncOpType type, int fd, uint64_t buf_ptr, size_t sz);
    bool     CancelRequest(uint32_t request_id);
    bool     PollCompletion(AsyncIoRequest* completed_req_out);
    bool     ExecutePending(size_t max_ops = 8);

    size_t   GetPendingCount() const;
    size_t   GetCompletedCount() const;
    void     CancelAllForProcess();

private:
    uint32_t process_id_{0};
    uint32_t next_req_id_{1};
    mutable std::mutex mutex_;
    std::vector<AsyncIoRequest> pending_requests_;
    std::queue<AsyncIoRequest> completed_queue_;
};

} // namespace winios::runtime
