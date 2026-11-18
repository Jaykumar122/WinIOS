#pragma once

#include "settings/settings_types.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace winios::settings {

class TaskManager {
public:
    using LifecycleHandler = std::function<bool(uint32_t pid, bool graceful)>;

    TaskManager();
    ~TaskManager() = default;

    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    // Task Registry
    bool register_task(const TaskInfo& task);
    bool unregister_task(uint32_t pid);
    bool update_task_state(uint32_t pid, TaskState state);
    bool update_task_memory(uint32_t pid, uint64_t memory_bytes);
    bool set_foreground_task(uint32_t pid);
    [[nodiscard]] bool get_task(uint32_t pid, TaskInfo& out_task) const;
    [[nodiscard]] std::vector<TaskInfo> enumerate_tasks() const;
    [[nodiscard]] size_t get_task_count() const;

    // Task Switcher Overlay
    [[nodiscard]] bool is_switcher_visible() const noexcept;
    void show_switcher() noexcept;
    void hide_switcher() noexcept;
    void toggle_switcher() noexcept;

    void cycle_next() noexcept;
    void cycle_prev() noexcept;
    [[nodiscard]] size_t get_selected_index() const noexcept;
    [[nodiscard]] uint32_t get_selected_pid() const noexcept;
    bool activate_selected_task() noexcept;

    // Lifecycle Controls
    bool terminate_task(uint32_t pid, bool graceful = true);
    bool terminate_task_versioned(uint32_t pid, uint32_t expected_generation, bool graceful);
    bool force_kill_task(uint32_t pid);
    [[nodiscard]] uint32_t get_task_generation(uint32_t pid) const;

    using TaskCleanupCallback = std::function<void(uint32_t pid, uint64_t primary_window)>;
    void register_cleanup_callback(TaskCleanupCallback cb) {
        std::lock_guard<std::recursive_mutex> lock(lock_);
        if (cb) cleanup_callbacks_.push_back(std::move(cb));
    }

    void set_lifecycle_handler(LifecycleHandler handler) {
        std::lock_guard<std::recursive_mutex> lock(lock_);
        lifecycle_handler_ = std::move(handler);
    }

private:
    mutable std::recursive_mutex     lock_;
    std::vector<TaskInfo>            tasks_;
    bool                             switcher_visible_{false};
    size_t                           selected_index_{0};
    LifecycleHandler                 lifecycle_handler_;
    std::vector<TaskCleanupCallback> cleanup_callbacks_;
    uint32_t                         generation_counter_{100};
};

} // namespace winios::settings
