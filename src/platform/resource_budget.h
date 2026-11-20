#pragma once

#include "platform/platform_error.h"
#include <cstdint>
#include <map>
#include <mutex>

namespace winios::platform {

struct AppResourceBudgetConfig {
    uint64_t max_memory_bytes{16 * 1024 * 1024};
    uint32_t max_windows{8};
    uint32_t max_document_streams{16};
    uint32_t max_ipc_queue_depth{64};
    uint32_t max_event_queue_depth{128};
    uint64_t cpu_step_slice{100000};
    uint32_t max_timers{8};
};

class ResourceBudgetManager {
public:
    static constexpr uint64_t kDefaultMaxMemoryPerTask = 16 * 1024 * 1024; // 16 MB
    static constexpr uint32_t kDefaultMaxWindowsPerTask = 8;
    static constexpr uint32_t kDefaultMaxDocumentStreams = 16;
    static constexpr uint32_t kDefaultMaxSubscriptions = 32;
    static constexpr size_t   kDefaultMaxPayloadBytes = 32 * 1024; // 32 KB

    ResourceBudgetManager() = default;
    ~ResourceBudgetManager() = default;

    void set_task_budget(uint32_t pid, const AppResourceBudgetConfig& budget) {
        std::lock_guard<std::mutex> lock(mutex_);
        task_budgets_[pid] = budget;
    }

    AppResourceBudgetConfig get_task_budget(uint32_t pid) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_budgets_.find(pid);
        if (it != task_budgets_.end()) {
            return it->second;
        }
        AppResourceBudgetConfig cfg{};
        cfg.max_memory_bytes = max_memory_per_task_;
        cfg.max_windows = max_windows_per_task_;
        cfg.max_document_streams = max_streams_per_task_;
        return cfg;
    }

    PlatformError allocate_memory(uint32_t pid, uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t limit = max_memory_per_task_;
        auto b_it = task_budgets_.find(pid);
        if (b_it != task_budgets_.end()) {
            limit = b_it->second.max_memory_bytes;
        }

        uint64_t current = task_memory_[pid];
        if (current + bytes > limit) {
            return PlatformError::resource_exhausted(
                PlatformDomain::Core,
                "Memory budget exceeded for PID " + std::to_string(pid));
        }
        task_memory_[pid] = current + bytes;
        return PlatformError::ok();
    }

    void free_memory(uint32_t pid, uint64_t bytes) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task_memory_[pid] <= bytes) {
            task_memory_[pid] = 0;
        } else {
            task_memory_[pid] -= bytes;
        }
    }

    PlatformError allocate_window(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t limit = max_windows_per_task_;
        auto b_it = task_budgets_.find(pid);
        if (b_it != task_budgets_.end()) {
            limit = b_it->second.max_windows;
        }

        if (task_windows_[pid] >= limit) {
            return PlatformError::resource_exhausted(
                PlatformDomain::Window,
                "Window budget exceeded for PID " + std::to_string(pid));
        }
        ++task_windows_[pid];
        return PlatformError::ok();
    }

    void release_window(uint32_t pid) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task_windows_[pid] > 0) {
            --task_windows_[pid];
        }
    }

    PlatformError open_document_stream(uint32_t pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t limit = max_streams_per_task_;
        auto b_it = task_budgets_.find(pid);
        if (b_it != task_budgets_.end()) {
            limit = b_it->second.max_document_streams;
        }

        if (task_streams_[pid] >= limit) {
            return PlatformError::resource_exhausted(
                PlatformDomain::Document,
                "Document stream budget exceeded for PID " + std::to_string(pid));
        }
        ++task_streams_[pid];
        return PlatformError::ok();
    }

    void close_document_stream(uint32_t pid) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task_streams_[pid] > 0) {
            --task_streams_[pid];
        }
    }

    PlatformError validate_payload_size(size_t payload_bytes) const noexcept {
        if (payload_bytes > max_payload_bytes_) {
            return PlatformError::invalid_argument(
                PlatformDomain::Syscall,
                "Payload size " + std::to_string(payload_bytes) + " exceeds maximum threshold");
        }
        return PlatformError::ok();
    }

    void release_all_task_resources(uint32_t pid) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        task_memory_.erase(pid);
        task_windows_.erase(pid);
        task_streams_.erase(pid);
        task_budgets_.erase(pid);
    }

    [[nodiscard]] uint64_t get_task_memory(uint32_t pid) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_memory_.find(pid);
        return it != task_memory_.end() ? it->second : 0;
    }

    [[nodiscard]] uint32_t get_task_windows(uint32_t pid) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_windows_.find(pid);
        return it != task_windows_.end() ? it->second : 0;
    }

private:
    mutable std::mutex              mutex_;
    uint64_t                        max_memory_per_task_{kDefaultMaxMemoryPerTask};
    uint32_t                        max_windows_per_task_{kDefaultMaxWindowsPerTask};
    uint32_t                        max_streams_per_task_{kDefaultMaxDocumentStreams};
    size_t                          max_payload_bytes_{kDefaultMaxPayloadBytes};
    std::map<uint32_t, uint64_t>    task_memory_;
    std::map<uint32_t, uint32_t>    task_windows_;
    std::map<uint32_t, uint32_t>    task_streams_;
    std::map<uint32_t, AppResourceBudgetConfig> task_budgets_;
};

} // namespace winios::platform
