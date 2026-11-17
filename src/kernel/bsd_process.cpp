#include "kernel/bsd_process.h"

#include <algorithm>
#include <cstring>

namespace winios::kernel {

// -----------------------------------------------------------------------------
// FileDescriptorTable Implementation
// -----------------------------------------------------------------------------

FileDescriptorTable::FileDescriptorTable() {
    // Standard Unix descriptors: 0=stdin, 1=stdout, 2=stderr
    install_fd(0, FileType::Console, true, false, "/dev/console");
    install_fd(1, FileType::Console, false, true, "/dev/console");
    install_fd(2, FileType::Console, false, true, "/dev/console");
}

int FileDescriptorTable::allocate_fd(FileType type, bool readable, bool writable, const std::string& path) {
    for (int i = 0; i < kMaxFiles; ++i) {
        if (!fds_[static_cast<std::size_t>(i)].is_open) {
            install_fd(i, type, readable, writable, path);
            return i;
        }
    }
    return -1; // Exhausted
}

bool FileDescriptorTable::install_fd(int fd, FileType type, bool readable, bool writable, const std::string& path) {
    if (fd < 0 || fd >= kMaxFiles) {
        return false;
    }
    auto& f = fds_[static_cast<std::size_t>(fd)];
    f.is_open = true;
    f.type = type;
    f.readable = readable;
    f.writable = writable;
    f.offset = 0;
    f.path = path;
    return true;
}

bool FileDescriptorTable::close_fd(int fd) {
    if (fd < 0 || fd >= kMaxFiles || !fds_[static_cast<std::size_t>(fd)].is_open) {
        return false;
    }
    fds_[static_cast<std::size_t>(fd)] = FileDescriptor{};
    return true;
}

const FileDescriptor* FileDescriptorTable::get_fd(int fd) const {
    if (fd < 0 || fd >= kMaxFiles || !fds_[static_cast<std::size_t>(fd)].is_open) {
        return nullptr;
    }
    return &fds_[static_cast<std::size_t>(fd)];
}

FileDescriptor* FileDescriptorTable::get_fd_mut(int fd) {
    if (fd < 0 || fd >= kMaxFiles || !fds_[static_cast<std::size_t>(fd)].is_open) {
        return nullptr;
    }
    return &fds_[static_cast<std::size_t>(fd)];
}

bool FileDescriptorTable::is_valid_fd(int fd) const {
    return (fd >= 0 && fd < kMaxFiles && fds_[static_cast<std::size_t>(fd)].is_open);
}

std::size_t FileDescriptorTable::open_count() const {
    std::size_t count = 0;
    for (const auto& f : fds_) {
        if (f.is_open) ++count;
    }
    return count;
}

void FileDescriptorTable::serialize(std::vector<std::uint8_t>& dst) const {
    const std::uint32_t count = static_cast<std::uint32_t>(open_count());
    const auto* p_count = reinterpret_cast<const std::uint8_t*>(&count);
    dst.insert(dst.end(), p_count, p_count + sizeof(count));

    for (int i = 0; i < kMaxFiles; ++i) {
        const auto& f = fds_[static_cast<std::size_t>(i)];
        if (f.is_open) {
            const std::uint32_t fd_num = static_cast<std::uint32_t>(i);
            const auto* p_fd = reinterpret_cast<const std::uint8_t*>(&fd_num);
            dst.insert(dst.end(), p_fd, p_fd + sizeof(fd_num));

            dst.push_back(static_cast<std::uint8_t>(f.type));
            dst.push_back(f.readable ? 1u : 0u);
            dst.push_back(f.writable ? 1u : 0u);

            const auto* p_off = reinterpret_cast<const std::uint8_t*>(&f.offset);
            dst.insert(dst.end(), p_off, p_off + sizeof(f.offset));

            const std::uint32_t path_len = static_cast<std::uint32_t>(f.path.size());
            const auto* p_len = reinterpret_cast<const std::uint8_t*>(&path_len);
            dst.insert(dst.end(), p_len, p_len + sizeof(path_len));
            dst.insert(dst.end(), f.path.begin(), f.path.end());
        }
    }
}

bool FileDescriptorTable::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(std::uint32_t)) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);
    remaining -= sizeof(count);

    fds_.fill(FileDescriptor{});

    for (std::uint32_t i = 0; i < count; ++i) {
        if (remaining < sizeof(std::uint32_t) + 3 + sizeof(std::uint64_t) + sizeof(std::uint32_t)) {
            return false;
        }
        std::uint32_t fd = 0;
        std::memcpy(&fd, src, sizeof(fd));
        src += sizeof(fd);
        remaining -= sizeof(fd);

        const auto type = static_cast<FileType>(*src++);
        const bool readable = (*src++ != 0);
        const bool writable = (*src++ != 0);
        remaining -= 3;

        std::uint64_t offset = 0;
        std::memcpy(&offset, src, sizeof(offset));
        src += sizeof(offset);
        remaining -= sizeof(offset);

        std::uint32_t path_len = 0;
        std::memcpy(&path_len, src, sizeof(path_len));
        src += sizeof(path_len);
        remaining -= sizeof(path_len);

        if (remaining < path_len) return false;
        std::string path(reinterpret_cast<const char*>(src), path_len);
        src += path_len;
        remaining -= path_len;

        if (fd < static_cast<std::uint32_t>(kMaxFiles)) {
            auto& f = fds_[fd];
            f.is_open = true;
            f.type = type;
            f.readable = readable;
            f.writable = writable;
            f.offset = offset;
            f.path = std::move(path);
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// BsdProcess Implementation
// -----------------------------------------------------------------------------

BsdProcess::BsdProcess(pid_t pid, pid_t ppid, std::string name, KernelTask* task)
    : pid_(pid), ppid_(ppid), name_(std::move(name)), state_(ProcessState::Starting), task_(task) {}

void BsdProcess::add_child(pid_t child_pid) {
    if (std::find(children_.begin(), children_.end(), child_pid) == children_.end()) {
        children_.push_back(child_pid);
    }
}

bool BsdProcess::remove_child(pid_t child_pid) {
    auto it = std::find(children_.begin(), children_.end(), child_pid);
    if (it != children_.end()) {
        children_.erase(it);
        return true;
    }
    return false;
}

void BsdProcess::add_thread(KernelThread* thread) {
    if (thread && std::find(threads_.begin(), threads_.end(), thread) == threads_.end()) {
        threads_.push_back(thread);
    }
}

bool BsdProcess::remove_thread(std::uint64_t tid) {
    auto it = std::remove_if(threads_.begin(), threads_.end(),
                             [tid](const KernelThread* t) { return t && t->tid() == tid; });
    if (it != threads_.end()) {
        threads_.erase(it, threads_.end());
        return true;
    }
    return false;
}

void BsdProcess::terminate(int exit_code) {
    state_ = ProcessState::Zombie;
    exit_code_ = exit_code;
    for (auto* t : threads_) {
        if (t) {
            t->terminate(exit_code);
        }
    }
}

void BsdProcess::serialize(std::vector<std::uint8_t>& dst) const {
    const auto* p_pid = reinterpret_cast<const std::uint8_t*>(&pid_);
    dst.insert(dst.end(), p_pid, p_pid + sizeof(pid_));

    const auto* p_ppid = reinterpret_cast<const std::uint8_t*>(&ppid_);
    dst.insert(dst.end(), p_ppid, p_ppid + sizeof(ppid_));

    const auto state_val = static_cast<std::uint32_t>(state_);
    const auto* p_state = reinterpret_cast<const std::uint8_t*>(&state_val);
    dst.insert(dst.end(), p_state, p_state + sizeof(state_val));

    const auto* p_exit = reinterpret_cast<const std::uint8_t*>(&exit_code_);
    dst.insert(dst.end(), p_exit, p_exit + sizeof(exit_code_));

    const std::uint32_t name_len = static_cast<std::uint32_t>(name_.size());
    const auto* p_len = reinterpret_cast<const std::uint8_t*>(&name_len);
    dst.insert(dst.end(), p_len, p_len + sizeof(name_len));
    dst.insert(dst.end(), name_.begin(), name_.end());

    fd_table_.serialize(dst);

    // Serialize children
    const std::uint32_t child_count = static_cast<std::uint32_t>(children_.size());
    const auto* p_cc = reinterpret_cast<const std::uint8_t*>(&child_count);
    dst.insert(dst.end(), p_cc, p_cc + sizeof(child_count));
    for (pid_t c : children_) {
        const auto* pc = reinterpret_cast<const std::uint8_t*>(&c);
        dst.insert(dst.end(), pc, pc + sizeof(c));
    }

    dst.push_back(parent_notified_ ? 1u : 0u);
}

std::unique_ptr<BsdProcess> BsdProcess::deserialize(const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(pid_t) * 2 + sizeof(std::uint32_t) + sizeof(int) + sizeof(std::uint32_t)) {
        return nullptr;
    }

    pid_t pid = 0;
    std::memcpy(&pid, src, sizeof(pid));
    src += sizeof(pid);
    remaining -= sizeof(pid);

    pid_t ppid = 0;
    std::memcpy(&ppid, src, sizeof(ppid));
    src += sizeof(ppid);
    remaining -= sizeof(ppid);

    std::uint32_t state_val = 0;
    std::memcpy(&state_val, src, sizeof(state_val));
    src += sizeof(state_val);
    remaining -= sizeof(state_val);

    int exit_code = 0;
    std::memcpy(&exit_code, src, sizeof(exit_code));
    src += sizeof(exit_code);
    remaining -= sizeof(exit_code);

    std::uint32_t name_len = 0;
    std::memcpy(&name_len, src, sizeof(name_len));
    src += sizeof(name_len);
    remaining -= sizeof(name_len);

    if (remaining < name_len) return nullptr;
    std::string name(reinterpret_cast<const char*>(src), name_len);
    src += name_len;
    remaining -= name_len;

    auto proc = std::make_unique<BsdProcess>(pid, ppid, std::move(name));
    proc->state_ = static_cast<ProcessState>(state_val);
    proc->exit_code_ = exit_code;

    if (!proc->fd_table_.deserialize(src, remaining)) {
        return nullptr;
    }

    // Deserialize children if present (for backward compatibility, check remaining size)
    if (remaining >= sizeof(std::uint32_t)) {
        std::uint32_t child_count = 0;
        std::memcpy(&child_count, src, sizeof(child_count));
        src += sizeof(child_count);
        remaining -= sizeof(child_count);

        for (std::uint32_t i = 0; i < child_count; ++i) {
            if (remaining < sizeof(pid_t)) break;
            pid_t c = 0;
            std::memcpy(&c, src, sizeof(c));
            src += sizeof(c);
            remaining -= sizeof(c);
            proc->children_.push_back(c);
        }

        if (remaining >= 1) {
            proc->parent_notified_ = (*src++ != 0);
            remaining -= 1;
        }
    }

    return proc;
}

// -----------------------------------------------------------------------------
// BsdProcessManager Implementation
// -----------------------------------------------------------------------------

BsdProcessManager::BsdProcessManager() = default;

bool BsdProcessManager::initialize(KernelTask* kernel_task) {
    processes_.clear();
    next_pid_ = 1;

    // Create PID 0 (kernel_process)
    auto kern_proc = std::make_unique<BsdProcess>(0, 0, "kernel_task", kernel_task);
    kern_proc->set_state(ProcessState::Runnable);
    processes_[0] = std::move(kern_proc);
    return true;
}

BsdProcess* BsdProcessManager::create_process(const std::string& name, pid_t ppid, KernelTask* task) {
    // Find next available PID
    pid_t pid = next_pid_++;
    if (next_pid_ >= kMaxPids) {
        next_pid_ = 1;
    }
    // Handle PID collision
    while (processes_.find(pid) != processes_.end()) {
        pid = next_pid_++;
        if (next_pid_ >= kMaxPids) {
            next_pid_ = 1;
        }
    }

    auto proc = std::make_unique<BsdProcess>(pid, ppid, name, task);
    proc->set_state(ProcessState::Runnable);

    // Register with parent if ppid exists
    if (ppid != 0) {
        auto parent_it = processes_.find(ppid);
        if (parent_it != processes_.end()) {
            parent_it->second->add_child(pid);
        }
    }

    BsdProcess* ret = proc.get();
    processes_[pid] = std::move(proc);
    return ret;
}

bool BsdProcessManager::terminate_process(pid_t pid, int exit_code) {
    if (pid == 0) {
        return false; // Cannot terminate kernel process
    }
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return false;
    }
    auto* proc = it->second.get();
    proc->terminate(exit_code);

    // Reparent orphans to PID 1 (Darwin XNU standard bsd/kern/kern_exit.c)
    BsdProcess* init_proc = get_process(1);
    for (pid_t child_pid : proc->children()) {
        BsdProcess* child = get_process(child_pid);
        if (child) {
            child->set_ppid(1);
            if (init_proc && init_proc->pid() != child_pid) {
                init_proc->add_child(child_pid);
            }
        }
    }
    proc->clear_children();

    // Notify parent
    BsdProcess* parent = get_process(proc->ppid());
    if (parent) {
        proc->notify_parent();
    }

    return true;
}

BsdProcess* BsdProcessManager::get_process(pid_t pid) {
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const BsdProcess* BsdProcessManager::get_process(pid_t pid) const {
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool BsdProcessManager::reap_process(pid_t pid) {
    if (pid == 0) {
        return false; // Cannot reap PID 0
    }
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return false;
    }
    if (!it->second->is_terminated()) {
        return false; // Can only reap zombie/terminated processes
    }

    // Unlink from parent
    BsdProcess* parent = get_process(it->second->ppid());
    if (parent) {
        parent->remove_child(pid);
    }

    processes_.erase(it);
    return true;
}

int BsdProcessManager::wait_process(pid_t parent_pid, pid_t wpid, int* out_status, int options) {
    BsdProcess* parent = get_process(parent_pid);
    if (!parent || parent->children().empty()) {
        return -10; // -ECHILD (No child processes)
    }

    if (wpid < -1 || wpid == 0) {
        return -22; // -EINVAL (Process groups not supported in bounded model)
    }

    if (wpid > 0) {
        const auto& children = parent->children();
        if (std::find(children.begin(), children.end(), wpid) == children.end()) {
            return -10; // -ECHILD (Not a child of caller)
        }
        BsdProcess* child = get_process(wpid);
        if (!child) {
            return -10; // -ECHILD
        }
        if (child->is_terminated()) {
            if (out_status) {
                *out_status = (child->exit_code() & 0xFF) << 8;
            }
            reap_process(wpid);
            return wpid;
        }
        if ((options & 1) != 0) { // WNOHANG
            return 0; // Child still running
        }
        return 0;
    }

    // wpid == -1: wait for any child
    const auto children = parent->children();
    for (pid_t child_pid : children) {
        BsdProcess* child = get_process(child_pid);
        if (child && child->is_terminated()) {
            if (out_status) {
                *out_status = (child->exit_code() & 0xFF) << 8;
            }
            reap_process(child_pid);
            return child_pid;
        }
    }

    if ((options & 1) != 0) { // WNOHANG
        return 0;
    }
    return 0;
}


void BsdProcessManager::serialize(std::vector<std::uint8_t>& dst) const {
    const auto* p_next = reinterpret_cast<const std::uint8_t*>(&next_pid_);
    dst.insert(dst.end(), p_next, p_next + sizeof(next_pid_));

    const std::uint32_t count = static_cast<std::uint32_t>(processes_.size());
    const auto* p_count = reinterpret_cast<const std::uint8_t*>(&count);
    dst.insert(dst.end(), p_count, p_count + sizeof(count));

    for (const auto& [pid, proc] : processes_) {
        if (proc) {
            proc->serialize(dst);
        }
    }
}

bool BsdProcessManager::deserialize_stream(const std::uint8_t*& src, std::size_t& remaining) {
    if (remaining < sizeof(pid_t) + sizeof(std::uint32_t)) return false;

    std::memcpy(&next_pid_, src, sizeof(next_pid_));
    src += sizeof(next_pid_);
    remaining -= sizeof(next_pid_);

    std::uint32_t count = 0;
    std::memcpy(&count, src, sizeof(count));
    src += sizeof(count);
    remaining -= sizeof(count);

    processes_.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        auto proc = BsdProcess::deserialize(src, remaining);
        if (!proc) return false;
        const pid_t pid = proc->pid();
        processes_[pid] = std::move(proc);
    }
    return true;
}

bool BsdProcessManager::deserialize(const std::uint8_t* src, std::size_t size) {
    const std::uint8_t* ptr = src;
    std::size_t remaining = size;
    return deserialize_stream(ptr, remaining);
}

} // namespace winios::kernel
