#include "settings/task_manager.h"
#include <algorithm>

namespace winios::settings {

TaskManager::TaskManager() = default;

bool TaskManager::register_task(const TaskInfo& task) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (task.pid == 0) return false;

    // Check if task already exists
    for (auto& t : tasks_) {
        if (t.pid == task.pid) {
            t = task;
            return true;
        }
    }

    tasks_.push_back(task);
    return true;
}

bool TaskManager::unregister_task(uint32_t pid) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(), [pid](const TaskInfo& t) {
        return t.pid == pid;
    });

    if (it == tasks_.end()) {
        return false;
    }

    tasks_.erase(it);
    if (selected_index_ >= tasks_.size() && !tasks_.empty()) {
        selected_index_ = tasks_.size() - 1;
    }
    return true;
}

bool TaskManager::update_task_state(uint32_t pid, TaskState state) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    for (auto& t : tasks_) {
        if (t.pid == pid) {
            t.state = state;
            return true;
        }
    }
    return false;
}

bool TaskManager::update_task_memory(uint32_t pid, uint64_t memory_bytes) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    for (auto& t : tasks_) {
        if (t.pid == pid) {
            t.memory_bytes = memory_bytes;
            return true;
        }
    }
    return false;
}

bool TaskManager::set_foreground_task(uint32_t pid) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    bool found = false;
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].pid == pid) {
            tasks_[i].is_foreground = true;
            selected_index_ = i;
            found = true;
        } else {
            tasks_[i].is_foreground = false;
        }
    }
    return found;
}

bool TaskManager::get_task(uint32_t pid, TaskInfo& out_task) const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    for (const auto& t : tasks_) {
        if (t.pid == pid) {
            out_task = t;
            return true;
        }
    }
    return false;
}

std::vector<TaskInfo> TaskManager::enumerate_tasks() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return tasks_;
}

size_t TaskManager::get_task_count() const {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return tasks_.size();
}

bool TaskManager::is_switcher_visible() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return switcher_visible_;
}

void TaskManager::show_switcher() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    switcher_visible_ = true;
}

void TaskManager::hide_switcher() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    switcher_visible_ = false;
}

void TaskManager::toggle_switcher() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    switcher_visible_ = !switcher_visible_;
}

void TaskManager::cycle_next() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (tasks_.empty()) return;
    selected_index_ = (selected_index_ + 1) % tasks_.size();
}

void TaskManager::cycle_prev() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (tasks_.empty()) return;
    if (selected_index_ == 0) {
        selected_index_ = tasks_.size() - 1;
    } else {
        selected_index_--;
    }
}

size_t TaskManager::get_selected_index() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return selected_index_;
}

uint32_t TaskManager::get_selected_pid() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (tasks_.empty() || selected_index_ >= tasks_.size()) {
        return 0;
    }
    return tasks_[selected_index_].pid;
}

bool TaskManager::activate_selected_task() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (tasks_.empty() || selected_index_ >= tasks_.size()) {
        return false;
    }

    uint32_t pid = tasks_[selected_index_].pid;
    for (auto& t : tasks_) {
        t.is_foreground = (t.pid == pid);
    }
    switcher_visible_ = false;
    return true;
}

bool TaskManager::terminate_task(uint32_t pid, bool graceful) {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(), [pid](const TaskInfo& t) {
        return t.pid == pid;
    });

    if (it == tasks_.end()) {
        return false;
    }

    if (lifecycle_handler_) {
        lifecycle_handler_(pid, graceful);
    }

    if (graceful) {
        it->state = TaskState::Suspended;
    } else {
        it->state = TaskState::Zombie;
        tasks_.erase(it);
        if (selected_index_ >= tasks_.size() && !tasks_.empty()) {
            selected_index_ = tasks_.size() - 1;
        }
    }
    return true;
}

bool TaskManager::force_kill_task(uint32_t pid) {
    return terminate_task(pid, false);
}

} // namespace winios::settings
