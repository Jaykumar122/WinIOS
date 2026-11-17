#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "kernel/thread.h"
#include "memory/xnu_vm_map.h"

namespace winios::kernel {

inline constexpr std::uint64_t kTaskMagic = 0x5441534B5F54534BULL; // "TASK_TSK"
inline constexpr std::size_t kMaxTaskNameLen = 32;

enum class TaskState : std::uint32_t {
    Active = 0,
    Suspended = 1,
    Terminated = 2
};

struct TaskStats {
    std::uint32_t pid{0};
    std::string name;
    TaskState state{TaskState::Active};
    std::uint32_t suspend_count{0};
    std::size_t thread_count{0};
    std::uint64_t total_runtime_ticks{0};
};

class KernelTask {
public:
    KernelTask(std::uint32_t pid,
               std::string name,
               std::shared_ptr<memory::XnuVmMap> map);
    ~KernelTask() = default;

    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] TaskState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t suspend_count() const noexcept { return suspend_count_; }
    [[nodiscard]] std::shared_ptr<memory::XnuVmMap> map() const noexcept { return map_; }
    [[nodiscard]] std::size_t thread_count() const;
    [[nodiscard]] std::vector<std::shared_ptr<KernelThread>> threads() const;

    bool add_thread(std::shared_ptr<KernelThread> thread);
    bool remove_thread(std::uint64_t tid);
    [[nodiscard]] std::shared_ptr<KernelThread> get_thread(std::uint64_t tid) const;

    bool suspend();
    bool resume();
    bool terminate();

    [[nodiscard]] TaskStats get_stats() const;

private:
    std::uint32_t pid_;
    std::string   name_;
    std::shared_ptr<memory::XnuVmMap> map_;
    TaskState     state_{TaskState::Active};
    std::uint32_t suspend_count_{0};
    std::vector<std::shared_ptr<KernelThread>> threads_;
    mutable std::mutex mutex_;
};

class KernelTaskManager {
public:
    KernelTaskManager() = default;
    ~KernelTaskManager() = default;

    // Create the initial kernel_task (PID 0)
    std::shared_ptr<KernelTask> create_kernel_task(std::shared_ptr<memory::XnuVmMap> kernel_map);

    // Create a new general task
    std::shared_ptr<KernelTask> create_task(const std::string& name,
                                            std::shared_ptr<memory::XnuVmMap> map);

    // Terminate a task by PID
    bool terminate_task(std::uint32_t pid);

    // Look up task by PID
    [[nodiscard]] std::shared_ptr<KernelTask> get_task(std::uint32_t pid) const;
    [[nodiscard]] std::shared_ptr<KernelTask> kernel_task() const;
    [[nodiscard]] std::size_t task_count() const;
    [[nodiscard]] std::vector<std::shared_ptr<KernelTask>> all_tasks() const;

    void reset();

    // Snapshot serialization & deserialization
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error = nullptr);

private:
    std::uint32_t next_pid_{0};
    std::shared_ptr<KernelTask> kernel_task_{nullptr};
    std::vector<std::shared_ptr<KernelTask>> tasks_;
    mutable std::mutex mutex_;
};

} // namespace winios::kernel
