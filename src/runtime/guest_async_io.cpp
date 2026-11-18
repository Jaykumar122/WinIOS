#include "runtime/guest_async_io.h"

namespace winios::runtime {

GuestAsyncIoManager::GuestAsyncIoManager(uint32_t process_id)
    : process_id_(process_id) {}

GuestAsyncIoManager::~GuestAsyncIoManager() {
    CancelAllForProcess();
}

uint32_t GuestAsyncIoManager::SubmitRequest(AsyncOpType type, int fd, uint64_t buf_ptr, size_t sz) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_requests_.size() >= kMaxQueueCapacity) {
        return 0; // Queue limit exceeded (backpressure)
    }

    uint32_t req_id = next_req_id_++;
    AsyncIoRequest req{};
    req.request_id = req_id;
    req.process_id = process_id_;
    req.type = type;
    req.fd = fd;
    req.buffer_ptr = buf_ptr;
    req.size = sz;
    req.status = AsyncOpStatus::kPending;
    req.bytes_transferred = 0;
    req.error_code = 0;

    pending_requests_.push_back(req);
    return req_id;
}

bool GuestAsyncIoManager::CancelRequest(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ++it) {
        if (it->request_id == request_id) {
            it->status = AsyncOpStatus::kCancelled;
            completed_queue_.push(*it);
            pending_requests_.erase(it);
            return true;
        }
    }
    return false;
}

bool GuestAsyncIoManager::PollCompletion(AsyncIoRequest* completed_req_out) {
    if (!completed_req_out) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (completed_queue_.empty()) return false;

    *completed_req_out = completed_queue_.front();
    completed_queue_.pop();
    return true;
}

bool GuestAsyncIoManager::ExecutePending(size_t max_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t executed = 0;
    while (!pending_requests_.empty() && executed < max_ops) {
        auto req = pending_requests_.front();
        pending_requests_.erase(pending_requests_.begin());

        req.status = AsyncOpStatus::kCompleted;
        req.bytes_transferred = static_cast<int>(req.size);
        completed_queue_.push(req);
        executed++;
    }
    return executed > 0;
}

size_t GuestAsyncIoManager::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_requests_.size();
}

size_t GuestAsyncIoManager::GetCompletedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completed_queue_.size();
}

void GuestAsyncIoManager::CancelAllForProcess() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& req : pending_requests_) {
        req.status = AsyncOpStatus::kCancelled;
        completed_queue_.push(req);
    }
    pending_requests_.clear();
}

} // namespace winios::runtime
