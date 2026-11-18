#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace winios::platform {

struct ProfilerSnapshot {
    uint64_t instructions_retired{0};
    uint64_t syscalls_dispatched{0};
    uint64_t total_syscall_duration_ticks{0};
    uint64_t memory_allocated_bytes{0};
    uint64_t windows_processed{0};
    uint64_t events_dispatched{0};
    uint64_t persistence_ops{0};
    uint64_t persistence_duration_ticks{0};

    [[nodiscard]] double average_syscall_duration_ticks() const noexcept {
        return syscalls_dispatched > 0
            ? static_cast<double>(total_syscall_duration_ticks) / static_cast<double>(syscalls_dispatched)
            : 0.0;
    }
};

class PlatformProfiler {
public:
    PlatformProfiler() = default;
    ~PlatformProfiler() = default;

    PlatformProfiler(const PlatformProfiler&) = delete;
    PlatformProfiler& operator=(const PlatformProfiler&) = delete;

    void record_instruction_retirement(uint64_t count = 1) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        instructions_retired_ += count;
    }

    void record_syscall(uint32_t /*sys_num*/, uint64_t duration_ticks = 1) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++syscalls_dispatched_;
        total_syscall_duration_ticks_ += duration_ticks;
    }

    void record_task_execution(uint32_t pid, uint64_t ticks) {
        std::lock_guard<std::mutex> lock(mutex_);
        task_cpu_ticks_[pid] += ticks;
    }

    void record_service_request(const std::string& service_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        service_requests_[service_name]++;
    }

    void record_memory_allocation(int64_t delta_bytes) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (delta_bytes > 0) {
            memory_allocated_bytes_ += static_cast<uint64_t>(delta_bytes);
        } else if (static_cast<uint64_t>(-delta_bytes) <= memory_allocated_bytes_) {
            memory_allocated_bytes_ -= static_cast<uint64_t>(-delta_bytes);
        } else {
            memory_allocated_bytes_ = 0;
        }
    }

    void record_window_presentation() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++windows_processed_;
    }

    void record_event_dispatch() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++events_dispatched_;
    }

    void record_persistence_operation(uint64_t duration_ticks = 1) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++persistence_ops_;
        persistence_duration_ticks_ += duration_ticks;
    }

    [[nodiscard]] ProfilerSnapshot take_snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ProfilerSnapshot s{};
        s.instructions_retired = instructions_retired_;
        s.syscalls_dispatched = syscalls_dispatched_;
        s.total_syscall_duration_ticks = total_syscall_duration_ticks_;
        s.memory_allocated_bytes = memory_allocated_bytes_;
        s.windows_processed = windows_processed_;
        s.events_dispatched = events_dispatched_;
        s.persistence_ops = persistence_ops_;
        s.persistence_duration_ticks = persistence_duration_ticks_;
        return s;
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        instructions_retired_ = 0;
        syscalls_dispatched_ = 0;
        total_syscall_duration_ticks_ = 0;
        memory_allocated_bytes_ = 0;
        windows_processed_ = 0;
        events_dispatched_ = 0;
        persistence_ops_ = 0;
        persistence_duration_ticks_ = 0;
        task_cpu_ticks_.clear();
        service_requests_.clear();
    }

    static PlatformProfiler& instance() noexcept {
        static PlatformProfiler prof;
        return prof;
    }

private:
    mutable std::mutex                  mutex_;
    uint64_t                            instructions_retired_{0};
    uint64_t                            syscalls_dispatched_{0};
    uint64_t                            total_syscall_duration_ticks_{0};
    uint64_t                            memory_allocated_bytes_{0};
    uint64_t                            windows_processed_{0};
    uint64_t                            events_dispatched_{0};
    uint64_t                            persistence_ops_{0};
    uint64_t                            persistence_duration_ticks_{0};
    std::map<uint32_t, uint64_t>        task_cpu_ticks_;
    std::map<std::string, uint64_t>     service_requests_;
};

} // namespace winios::platform
