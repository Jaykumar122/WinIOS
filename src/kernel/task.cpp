#include "task.h"

#include <algorithm>
#include <cstring>

namespace winios::kernel {

KernelTask::KernelTask(std::uint32_t pid,
                       std::string name,
                       std::shared_ptr<memory::XnuVmMap> map)
    : pid_(pid), name_(std::move(name)), map_(std::move(map)) {}

std::size_t KernelTask::thread_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.size();
}

std::vector<std::shared_ptr<KernelThread>> KernelTask::threads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_;
}

bool KernelTask::add_thread(std::shared_ptr<KernelThread> thread) {
    if (!thread) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : threads_) {
        if (t->tid() == thread->tid()) return false;
    }
    threads_.push_back(std::move(thread));
    return true;
}

bool KernelTask::remove_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(threads_.begin(), threads_.end(), [tid](const auto& t) {
        return t->tid() == tid;
    });
    if (it == threads_.end()) return false;
    threads_.erase(it, threads_.end());
    return true;
}

std::shared_ptr<KernelThread> KernelTask::get_thread(std::uint64_t tid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : threads_) {
        if (t->tid() == tid) return t;
    }
    return nullptr;
}

bool KernelTask::suspend() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == TaskState::Terminated) return false;
    ++suspend_count_;
    state_ = TaskState::Suspended;
    return true;
}

bool KernelTask::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == TaskState::Terminated) return false;
    if (suspend_count_ > 0) {
        --suspend_count_;
    }
    if (suspend_count_ == 0) {
        state_ = TaskState::Active;
    }
    return true;
}

bool KernelTask::terminate() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = TaskState::Terminated;
    for (auto& t : threads_) {
        t->transition_state(ThreadState::Terminated);
    }
    return true;
}

TaskStats KernelTask::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskStats s{};
    s.pid = pid_;
    s.name = name_;
    s.state = state_;
    s.suspend_count = suspend_count_;
    s.thread_count = threads_.size();
    for (const auto& t : threads_) {
        s.total_runtime_ticks += t->total_runtime_ticks();
    }
    return s;
}

std::shared_ptr<KernelTask> KernelTaskManager::create_kernel_task(std::shared_ptr<memory::XnuVmMap> kernel_map) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (kernel_task_) return kernel_task_;

    auto task = std::make_shared<KernelTask>(0, "kernel_task", std::move(kernel_map));
    kernel_task_ = task;
    tasks_.push_back(task);
    next_pid_ = 1;
    return task;
}

std::shared_ptr<KernelTask> KernelTaskManager::create_task(const std::string& name,
                                                           std::shared_ptr<memory::XnuVmMap> map) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t pid = next_pid_++;
    auto task = std::make_shared<KernelTask>(pid, name, std::move(map));
    tasks_.push_back(task);
    return task;
}

bool KernelTaskManager::terminate_task(std::uint32_t pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& t : tasks_) {
        if (t->pid() == pid) {
            return t->terminate();
        }
    }
    return false;
}

std::shared_ptr<KernelTask> KernelTaskManager::get_task(std::uint32_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : tasks_) {
        if (t->pid() == pid) return t;
    }
    return nullptr;
}

std::shared_ptr<KernelTask> KernelTaskManager::kernel_task() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return kernel_task_;
}

std::size_t KernelTaskManager::task_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::vector<std::shared_ptr<KernelTask>> KernelTaskManager::all_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_;
}

void KernelTaskManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
    kernel_task_ = nullptr;
    next_pid_ = 0;
}

std::vector<std::uint8_t> KernelTaskManager::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint8_t> buf;

    auto append_u32 = [&buf](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 4);
    };

    append_u32(next_pid_);
    append_u32(static_cast<std::uint32_t>(tasks_.size()));

    for (const auto& t : tasks_) {
        append_u32(t->pid());
        append_u32(static_cast<std::uint32_t>(t->state()));
        append_u32(t->suspend_count());
        const auto& name = t->name();
        append_u32(static_cast<std::uint32_t>(name.size()));
        buf.insert(buf.end(), name.begin(), name.end());
    }

    return buf;
}

bool KernelTaskManager::deserialize(const std::uint8_t* data, std::size_t size, std::string* out_error) {
    if (!data || size < 8) {
        if (out_error) *out_error = "KernelTaskManager::deserialize: buffer too small";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();

    std::size_t off = 0;
    std::memcpy(&next_pid_, data + off, 4); off += 4;
    std::uint32_t count = 0;
    std::memcpy(&count, data + off, 4); off += 4;

    for (std::uint32_t i = 0; i < count; ++i) {
        if (off + 16 > size) {
            if (out_error) *out_error = "KernelTaskManager::deserialize: truncated task record";
            return false;
        }
        std::uint32_t pid = 0, st = 0, susp = 0, nlen = 0;
        std::memcpy(&pid, data + off, 4); off += 4;
        std::memcpy(&st, data + off, 4); off += 4;
        std::memcpy(&susp, data + off, 4); off += 4;
        std::memcpy(&nlen, data + off, 4); off += 4;

        if (off + nlen > size) {
            if (out_error) *out_error = "KernelTaskManager::deserialize: truncated name";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(data + off), nlen);
        off += nlen;

        auto task = std::make_shared<KernelTask>(pid, name, nullptr);
        if (st == static_cast<std::uint32_t>(TaskState::Suspended)) {
            task->suspend();
        } else if (st == static_cast<std::uint32_t>(TaskState::Terminated)) {
            task->terminate();
        }

        if (pid == 0) {
            kernel_task_ = task;
        }
        tasks_.push_back(task);
    }

    return true;
}

} // namespace winios::kernel
