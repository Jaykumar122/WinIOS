#include "runtime/darwin_bsd_subsystem.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// DarwinVfs Implementation
// -----------------------------------------------------------------------------
DarwinVfs::DarwinVfs() {
    root_ = std::make_shared<VfsNode>();
    root_->name = "/";
    root_->type = VfsNodeType::Directory;
    root_->mode = 0755;
    root_->inode = 1;

    populate_default_rootfs();
}

void DarwinVfs::populate_default_rootfs() {
    create_directory("/sbin");
    create_directory("/bin");
    create_directory("/usr/lib");
    create_directory("/usr/bin");
    create_directory("/usr/libexec");
    create_directory("/System/Library/Frameworks");
    create_directory("/Applications");
    create_directory("/private/var/tmp");
    create_directory("/tmp");
    create_directory("/var");
    create_directory("/var/run");
    create_directory("/var/log");
    create_directory("/var/Keychains");
    create_directory("/etc");
    create_directory("/dev");

    // Standard devices
    const char dummy[] = "";
    write_file("/dev/null", dummy, 0);
    write_file("/dev/zero", dummy, 0);
    write_file("/dev/random", dummy, 0);

    // Initial dummy system binaries
    const char launchd_mock[] = "\xCF\xFA\xED\xFE"; // Mock binary placeholder
    write_file("/sbin/launchd", launchd_mock, 4);
    write_file("/usr/lib/dyld", launchd_mock, 4);
}

std::string DarwinVfs::canonicalize_path(const std::string& path) const {
    if (path.empty()) return "/";
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string comp;
    while (std::getline(ss, comp, '/')) {
        if (comp.empty() || comp == ".") continue;
        if (comp == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(comp);
        }
    }
    std::string res;
    for (const auto& p : parts) {
        res += "/" + p;
    }
    return res.empty() ? "/" : res;
}

std::shared_ptr<VfsNode> DarwinVfs::resolve_path_locked(const std::string& path, int max_symlinks) const {
    if (path.empty() || path == "/") {
        return root_;
    }

    std::shared_ptr<VfsNode> curr = root_;
    std::stringstream ss(path);
    std::string comp;
    while (std::getline(ss, comp, '/')) {
        if (comp.empty() || comp == ".") continue;
        if (comp == "..") {
            // Root has no parent in our simple VFS
            continue;
        }

        auto it = curr->children.find(comp);
        if (it == curr->children.end()) {
            return nullptr;
        }

        curr = it->second;
        if (curr->type == VfsNodeType::Symlink) {
            if (max_symlinks <= 0 || curr->symlink_target.empty()) {
                return nullptr; // Cycle or empty symlink
            }

            std::string remaining;
            std::string rem_comp;
            while (std::getline(ss, rem_comp, '/')) {
                if (!rem_comp.empty()) {
                    remaining += "/" + rem_comp;
                }
            }

            const std::string& target = curr->symlink_target;
            if (target[0] == '/') {
                return resolve_path_locked(target + remaining, max_symlinks - 1);
            } else {
                return resolve_path_locked("/" + target + remaining, max_symlinks - 1);
            }
        }
    }

    return curr;
}

std::shared_ptr<VfsNode> DarwinVfs::resolve_path(const std::string& path, int max_symlinks) const {
    std::lock_guard<std::mutex> guard(lock_);
    return resolve_path_locked(path, max_symlinks);
}

bool DarwinVfs::create_directory_locked(const std::string& path) {
    if (path.empty() || path == "/") return true;

    std::shared_ptr<VfsNode> curr = root_;
    std::stringstream ss(path);
    std::string comp;
    while (std::getline(ss, comp, '/')) {
        if (comp.empty()) continue;

        auto it = curr->children.find(comp);
        if (it == curr->children.end()) {
            auto new_dir = std::make_shared<VfsNode>();
            new_dir->name = comp;
            new_dir->type = VfsNodeType::Directory;
            new_dir->mode = 0755;
            new_dir->inode = next_inode_++;
            curr->children[comp] = new_dir;
            curr = new_dir;
        } else {
            curr = it->second;
        }
    }

    return true;
}

bool DarwinVfs::create_directory(const std::string& path) {
    if (path.empty() || path == "/") return true;
    std::lock_guard<std::mutex> guard(lock_);
    return create_directory_locked(path);
}

bool DarwinVfs::create_symlink(const std::string& target, const std::string& linkpath) {
    if (target.empty() || linkpath.empty()) return false;
    std::lock_guard<std::mutex> guard(lock_);

    std::string dir_path;
    std::string filename;
    auto last_slash = linkpath.rfind('/');
    if (last_slash == std::string::npos) {
        dir_path = "/";
        filename = linkpath;
    } else {
        dir_path = linkpath.substr(0, last_slash);
        filename = linkpath.substr(last_slash + 1);
        if (dir_path.empty()) dir_path = "/";
    }

    create_directory_locked(dir_path);
    auto parent = resolve_path_locked(dir_path, 32);
    if (!parent || !parent->is_directory()) return false;

    auto node = std::make_shared<VfsNode>();
    node->name = filename;
    node->type = VfsNodeType::Symlink;
    node->mode = 0777;
    node->inode = next_inode_++;
    node->symlink_target = target;
    parent->children[filename] = node;
    return true;
}

bool DarwinVfs::write_file(const std::string& path, const void* data, std::size_t size) {
    std::string dir_path;
    std::string filename;
    auto last_slash = path.rfind('/');
    if (last_slash == std::string::npos) {
        dir_path = "/";
        filename = path;
    } else {
        dir_path = path.substr(0, last_slash);
        filename = path.substr(last_slash + 1);
        if (dir_path.empty()) dir_path = "/";
    }

    create_directory(dir_path);
    auto parent = resolve_path(dir_path);
    if (!parent) return false;

    std::lock_guard<std::mutex> guard(lock_);
    auto node = std::make_shared<VfsNode>();
    node->name = filename;
    node->type = VfsNodeType::RegularFile;
    node->mode = 0644;
    node->inode = next_inode_++;
    node->size = size;
    if (data && size > 0) {
        node->content.resize(size);
        std::memcpy(node->content.data(), data, size);
    }
    parent->children[filename] = node;
    return true;
}

std::optional<std::vector<std::uint8_t>> DarwinVfs::read_file(const std::string& path) const {
    auto node = resolve_path(path);
    if (!node || node->type != VfsNodeType::RegularFile) {
        return std::nullopt;
    }
    return node->content;
}

std::vector<std::string> DarwinVfs::list_directory(const std::string& path) const {
    auto node = resolve_path(path);
    if (!node || !node->is_directory()) {
        return {};
    }

    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::string> names;
    names.reserve(node->children.size());
    for (const auto& [name, child] : node->children) {
        (void)child;
        names.push_back(name);
    }
    return names;
}

// -----------------------------------------------------------------------------
// DarwinBsdSubsystem Implementation
// -----------------------------------------------------------------------------
DarwinBsdSubsystem::DarwinBsdSubsystem() {
    // Process 1: /sbin/launchd (Root of all Darwin userspace)
    create_process(0, "/sbin/launchd", 0);
}

std::shared_ptr<DarwinProcess> DarwinBsdSubsystem::get_process(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it != processes_.end()) {
        return it->second;
    }
    return nullptr;
}

std::uint32_t DarwinBsdSubsystem::create_process(std::uint32_t ppid, const std::string& exec_path, std::uint32_t uid) {
    return create_process_with_pid(0, ppid, exec_path, uid);
}

std::uint32_t DarwinBsdSubsystem::create_process_with_pid(std::uint32_t requested_pid, std::uint32_t ppid, const std::string& exec_path, std::uint32_t uid) {
    std::lock_guard<std::mutex> guard(lock_);
    std::uint32_t pid = requested_pid != 0 ? requested_pid : ((processes_.empty() && ppid == 0) ? 1 : next_pid_++);
    if (pid >= next_pid_) {
        next_pid_ = pid + 1;
    }

    auto proc = std::make_shared<DarwinProcess>();
    proc->pid = pid;
    proc->ppid = ppid;
    proc->uid = uid;
    proc->gid = uid;
    proc->state = ProcessState::Running;
    proc->executable_path = exec_path;

    // Standard descriptors: 0, 1, 2
    auto dev_null = vfs_.resolve_path("/dev/null");
    proc->fds[0] = FileDescriptor{0, dev_null, nullptr, 0, 0};
    proc->fds[1] = FileDescriptor{1, dev_null, nullptr, 1, 0};
    proc->fds[2] = FileDescriptor{2, dev_null, nullptr, 1, 0};
    proc->next_fd = 3;

    // Default primary thread (TID 1)
    DarwinThreadContext main_thread;
    main_thread.tid = 1;
    main_thread.sp = 0x00007FFFFFE00000ULL;
    main_thread.stack_base = 0x00007FFFFFD00000ULL;
    main_thread.stack_size = 0x100000; // 1 MB stack
    proc->threads.push_back(main_thread);

    processes_[pid] = proc;
    return pid;
}

std::uint32_t DarwinBsdSubsystem::fork(std::uint32_t parent_pid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(parent_pid);
    if (it == processes_.end()) {
        return 0;
    }

    const auto& parent = it->second;
    std::uint32_t child_pid = next_pid_++;

    auto child = std::make_shared<DarwinProcess>();
    child->pid = child_pid;
    child->ppid = parent_pid;
    child->uid = parent->uid;
    child->gid = parent->gid;
    child->state = ProcessState::Running;
    child->executable_path = parent->executable_path;
    child->fds = parent->fds;
    child->next_fd = parent->next_fd;
    child->vm_regions = parent->vm_regions;
    child->brk_base = parent->brk_base;
    child->brk_curr = parent->brk_curr;
    child->threads = parent->threads;
    child->next_tid = parent->next_tid;

    processes_[child_pid] = child;
    return child_pid;
}

bool DarwinBsdSubsystem::execve(
    std::uint32_t pid,
    const std::string& binary_path,
    const std::vector<std::string>& argv,
    const std::vector<std::string>& envp,
    Memory& memory) {

    auto proc = get_process(pid);
    if (!proc) return false;

    // Read binary from VFS
    auto file_bytes = vfs_.read_file(binary_path);
    if (!file_bytes.has_value()) {
        return false;
    }

    // Configure userspace loader
    loader::UserspaceLoadConfig cfg;
    cfg.aslr_enabled = true;
    cfg.aslr_slide = 0x20000ULL;
    cfg.argv = argv;
    cfg.envp = envp;
    cfg.executable_path = binary_path;

    auto load_res = loader::UserspaceMachOLoader::load_into_memory(
        file_bytes->data(), file_bytes->size(), memory, cfg);

    if (!load_res.is_success()) {
        return false;
    }

    // Update process state
    proc->executable_path = binary_path;
    proc->vm_regions.clear();
    for (const auto& s : load_res.segments) {
        if (!s.is_pagezero && s.vmsize > 0) {
            proc->vm_regions.push_back(VmRegion{s.mapped_vaddr, s.vmsize, s.initprot, 0});
        }
    }

    return true;
}

void DarwinBsdSubsystem::exit_process(std::uint32_t pid, int exit_code) {
    std::shared_ptr<DarwinProcess> proc;
    std::uint32_t parent_pid = 0;
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = processes_.find(pid);
        if (it != processes_.end()) {
            proc = it->second;
            proc->state = ProcessState::Zombie;
            proc->exit_code = exit_code;
            parent_pid = proc->ppid;
        }
    }

    // Send SIGCHLD to parent
    if (parent_pid > 0) {
        kill(parent_pid, kDarwinSigChld);
    }
}

std::int32_t DarwinBsdSubsystem::waitpid(
    std::uint32_t parent_pid,
    std::int32_t target_pid,
    int& out_status,
    int options) {

    std::lock_guard<std::mutex> guard(lock_);

    bool child_found = false;
    for (auto& [pid, proc] : processes_) {
        if (proc->ppid == parent_pid) {
            if (target_pid == -1 || static_cast<std::uint32_t>(target_pid) == pid) {
                child_found = true;
                if (proc->state == ProcessState::Zombie) {
                    out_status = proc->exit_code;
                    proc->state = ProcessState::Terminated;
                    return static_cast<std::int32_t>(pid);
                }
            }
        }
    }

    // In POSIX / Darwin waitpid:
    // If WNOHANG (bit 0, value 1) is specified and matching child exists but has not exited,
    // waitpid returns 0 immediately.
    constexpr int kDarwinWNohang = 1;
    if (child_found && (options & kDarwinWNohang) != 0) {
        return 0;
    }

    return -1;
}

std::uint64_t DarwinBsdSubsystem::create_thread(
    std::uint32_t pid,
    std::uint64_t entry_point,
    std::uint64_t stack_ptr,
    std::uint64_t tpidr) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return 0;
    }

    auto& proc = it->second;
    std::uint64_t tid = proc->next_tid++;

    DarwinThreadContext thread;
    thread.tid = tid;
    thread.pc = entry_point;
    thread.sp = stack_ptr;
    thread.tpidr_el0 = tpidr;
    thread.stack_base = stack_ptr > 0x100000 ? stack_ptr - 0x100000 : 0;
    thread.stack_size = 0x100000;

    proc->threads.push_back(thread);
    return tid;
}

bool DarwinBsdSubsystem::get_thread_context(
    std::uint32_t pid,
    std::uint64_t tid,
    DarwinThreadContext& out_ctx) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return false;
    }

    for (const auto& t : it->second->threads) {
        if (t.tid == tid) {
            out_ctx = t;
            return true;
        }
    }

    return false;
}

bool DarwinBsdSubsystem::set_thread_context(
    std::uint32_t pid,
    std::uint64_t tid,
    const DarwinThreadContext& ctx) {

    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return false;
    }

    for (auto& t : it->second->threads) {
        if (t.tid == tid) {
            t = ctx;
            return true;
        }
    }

    return false;
}

bool DarwinBsdSubsystem::terminate_thread(std::uint32_t pid, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return false;
    }

    auto& threads = it->second->threads;
    auto tit = std::remove_if(threads.begin(), threads.end(), [tid](const DarwinThreadContext& t) {
        return t.tid == tid;
    });

    if (tit != threads.end()) {
        threads.erase(tit, threads.end());
        return true;
    }
    return false;
}

std::vector<std::uint64_t> DarwinBsdSubsystem::list_threads(std::uint32_t pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = processes_.find(pid);
    if (it == processes_.end()) {
        return {};
    }

    std::vector<std::uint64_t> result;
    for (const auto& t : it->second->threads) {
        result.push_back(t.tid);
    }
    return result;
}

int DarwinBsdSubsystem::bsdthread_create(
    std::uint32_t pid,
    std::uint64_t func,
    std::uint64_t arg,
    std::uint64_t stack,
    std::uint64_t flags) {

    (void)flags;
    std::uint64_t tid = create_thread(pid, func, stack, 0);
    if (tid == 0) {
        return -1;
    }

    DarwinThreadContext ctx;
    if (get_thread_context(pid, tid, ctx)) {
        ctx.x[0] = arg;
        set_thread_context(pid, tid, ctx);
    }

    return static_cast<int>(tid);
}

int DarwinBsdSubsystem::bsdthread_terminate(
    std::uint32_t pid,
    std::uint64_t stackaddr,
    std::size_t size,
    std::uint32_t kthport,
    std::uint32_t sem) {

    (void)stackaddr;
    (void)size;
    (void)sem;
    std::uint64_t tid = (kthport != 0) ? kthport : 1;
    return terminate_thread(pid, tid) ? 0 : -1;
}

int DarwinBsdSubsystem::open(std::uint32_t pid, const std::string& path, std::uint32_t flags, std::uint32_t mode) {
    (void)mode;
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto node = vfs_.resolve_path(path);
    if (!node) {
        constexpr std::uint32_t kOCreat = 0x0200u;
        if ((flags & kOCreat) != 0) {
            vfs_.write_file(path, nullptr, 0);
            node = vfs_.resolve_path(path);
        }
    }
    if (!node) return -1;

    int fd = proc->next_fd++;
    proc->fds[fd] = FileDescriptor{fd, node, nullptr, flags, 0};
    return fd;
}

int DarwinBsdSubsystem::close(std::uint32_t pid, int fd) {
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end()) return -1;

    if (it->second.is_pipe()) {
        std::lock_guard<std::mutex> pguard(it->second.pipe->lock);
        if (it->second.flags == 1) { // O_WRONLY
            it->second.pipe->write_closed = true;
        } else {
            it->second.pipe->read_closed = true;
        }
    }

    proc->fds.erase(it);
    return 0;
}

std::int64_t DarwinBsdSubsystem::read(std::uint32_t pid, int fd, void* buffer, std::size_t count) {
    auto proc = get_process(pid);
    if (!proc || !buffer || count == 0) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end()) return -1;

    auto& desc = it->second;

    if (desc.is_pipe()) {
        if (desc.flags == 1) {
            return -1; // Write-only pipe descriptor
        }
        std::lock_guard<std::mutex> pguard(desc.pipe->lock);
        if (desc.pipe->buffer.empty()) {
            if (desc.pipe->write_closed) {
                return 0; // EOF
            }
            return 0; // No data available
        }
        std::size_t to_read = std::min(count, desc.pipe->buffer.size());
        std::memcpy(buffer, desc.pipe->buffer.data(), to_read);
        desc.pipe->buffer.erase(desc.pipe->buffer.begin(), desc.pipe->buffer.begin() + to_read);
        return static_cast<std::int64_t>(to_read);
    }

    if (!desc.node) return -1;

    auto& content = desc.node->content;
    if (desc.offset >= content.size()) {
        return 0; // EOF
    }

    std::size_t available = content.size() - static_cast<std::size_t>(desc.offset);
    std::size_t to_read = std::min(count, available);
    std::memcpy(buffer, content.data() + desc.offset, to_read);
    desc.offset += to_read;
    return static_cast<std::int64_t>(to_read);
}

std::int64_t DarwinBsdSubsystem::write(std::uint32_t pid, int fd, const void* buffer, std::size_t count) {
    auto proc = get_process(pid);
    if (!proc || !buffer || count == 0) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end()) return -1;

    auto& desc = it->second;

    if (desc.is_pipe()) {
        if (desc.flags == 0) {
            return -1; // Read-only pipe descriptor
        }
        std::lock_guard<std::mutex> pguard(desc.pipe->lock);
        if (desc.pipe->read_closed) {
            return -1; // Broken pipe (EPIPE)
        }
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        desc.pipe->buffer.insert(desc.pipe->buffer.end(), bytes, bytes + count);
        return static_cast<std::int64_t>(count);
    }

    if (!desc.node) return -1;

    auto& content = desc.node->content;
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);

    if (desc.offset + count > content.size()) {
        content.resize(static_cast<std::size_t>(desc.offset + count));
    }
    std::memcpy(content.data() + desc.offset, bytes, count);
    desc.offset += count;
    desc.node->size = content.size();
    return static_cast<std::int64_t>(count);
}

int DarwinBsdSubsystem::pipe(std::uint32_t pid, int fds[2]) {
    auto proc = get_process(pid);
    if (!proc || !fds) return -1;

    auto pipe_chan = std::make_shared<PipeChannel>();
    int fd0 = proc->next_fd++;
    int fd1 = proc->next_fd++;

    FileDescriptor r_desc;
    r_desc.fd = fd0;
    r_desc.pipe = pipe_chan;
    r_desc.flags = 0; // O_RDONLY
    r_desc.offset = 0;

    FileDescriptor w_desc;
    w_desc.fd = fd1;
    w_desc.pipe = pipe_chan;
    w_desc.flags = 1; // O_WRONLY
    w_desc.offset = 0;

    proc->fds[fd0] = std::move(r_desc);
    proc->fds[fd1] = std::move(w_desc);

    fds[0] = fd0;
    fds[1] = fd1;
    return 0;
}

int DarwinBsdSubsystem::dup(std::uint32_t pid, int oldfd) {
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto it = proc->fds.find(oldfd);
    if (it == proc->fds.end()) return -1;

    int newfd = proc->next_fd++;
    FileDescriptor cloned = it->second;
    cloned.fd = newfd;
    proc->fds[newfd] = cloned;
    return newfd;
}

int DarwinBsdSubsystem::dup2(std::uint32_t pid, int oldfd, int newfd) {
    auto proc = get_process(pid);
    if (!proc || newfd < 0) return -1;

    auto it = proc->fds.find(oldfd);
    if (it == proc->fds.end()) return -1;

    if (oldfd == newfd) {
        return newfd;
    }

    auto target_it = proc->fds.find(newfd);
    if (target_it != proc->fds.end()) {
        close(pid, newfd);
    }

    FileDescriptor cloned = it->second;
    cloned.fd = newfd;
    proc->fds[newfd] = cloned;
    return newfd;
}

int DarwinBsdSubsystem::getdirentries64(std::uint32_t pid, int fd, void* buf, std::size_t nbytes, std::int64_t* basep) {
    auto proc = get_process(pid);
    if (!proc || !buf || nbytes < sizeof(DarwinDirent64Raw)) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end() || !it->second.node || !it->second.node->is_directory()) {
        return -1;
    }

    auto& desc = it->second;
    const auto& node = desc.node;

    struct EntryInfo {
        std::uint64_t inode;
        std::uint8_t  type;
        std::string   name;
    };

    std::vector<EntryInfo> all_entries;
    all_entries.push_back({node->inode, DT_DIR, "."});
    all_entries.push_back({node->inode == 1 ? 1 : node->inode - 1, DT_DIR, ".."});

    for (const auto& kv : node->children) {
        std::uint8_t dtype = DT_REG;
        if (kv.second->type == VfsNodeType::Directory) dtype = DT_DIR;
        else if (kv.second->type == VfsNodeType::Symlink) dtype = DT_LNK;
        else if (kv.second->type == VfsNodeType::CharacterDevice) dtype = DT_CHR;
        else if (kv.second->type == VfsNodeType::BlockDevice) dtype = DT_BLK;
        all_entries.push_back({kv.second->inode, dtype, kv.first});
    }

    std::size_t curr_idx = static_cast<std::size_t>(desc.offset);
    if (basep) {
        *basep = static_cast<std::int64_t>(curr_idx);
    }

    if (curr_idx >= all_entries.size()) {
        return 0; // EOF
    }

    auto* out_ptr = static_cast<std::uint8_t*>(buf);
    std::size_t bytes_written = 0;

    while (curr_idx < all_entries.size()) {
        const auto& e = all_entries[curr_idx];
        std::size_t reclen = sizeof(DarwinDirent64Raw);
        if (bytes_written + reclen > nbytes) {
            break;
        }

        DarwinDirent64Raw raw{};
        raw.d_ino = e.inode;
        raw.d_seekoff = static_cast<std::uint64_t>(curr_idx + 1);
        raw.d_reclen = static_cast<std::uint16_t>(reclen);
        raw.d_namlen = static_cast<std::uint16_t>(std::min(e.name.size(), static_cast<std::size_t>(255)));
        raw.d_type = e.type;
        std::memcpy(raw.d_name, e.name.c_str(), raw.d_namlen);
        raw.d_name[raw.d_namlen] = '\0';

        std::memcpy(out_ptr + bytes_written, &raw, sizeof(raw));
        bytes_written += reclen;
        curr_idx++;
    }

    desc.offset = curr_idx;
    return static_cast<int>(bytes_written);
}

std::uint64_t DarwinBsdSubsystem::mmap(
    std::uint32_t pid,
    std::uint64_t addr,
    std::size_t length,
    std::uint32_t prot,
    std::uint32_t flags,
    int fd,
    std::uint64_t offset) {

    (void)fd;
    (void)offset;
    auto proc = get_process(pid);
    if (!proc || length == 0) return 0;

    // 1. Integer overflow protection
    if (addr + length < addr) {
        return 0; // Overflow
    }

    // 2. Strict Apple Silicon W^X policy: Simultaneous Write + Execute rejected unless MAP_JIT (0x0800)
    constexpr std::uint32_t kProtWrite = 0x02u;
    constexpr std::uint32_t kProtExecute = 0x04u;
    constexpr std::uint32_t kMapJit = 0x0800u;
    if ((prot & (kProtWrite | kProtExecute)) == (kProtWrite | kProtExecute)) {
        if ((flags & kMapJit) == 0) {
            return 0; // W^X violation
        }
    }

    const std::uint64_t kPageMask = 0x3FFFULL; // 16 KB page granule
    const std::size_t aligned_len = static_cast<std::size_t>((length + kPageMask) & ~kPageMask);

    std::uint64_t alloc_addr = addr;
    if (alloc_addr == 0) {
        alloc_addr = proc->brk_curr;
        proc->brk_curr += aligned_len;
    } else {
        // Enforce 16 KB alignment
        if ((alloc_addr & kPageMask) != 0) {
            return 0; // Misaligned
        }

        // Overlap rejection
        for (const auto& r : proc->vm_regions) {
            std::uint64_t r_end = r.vmaddr + ((r.size + kPageMask) & ~kPageMask);
            std::uint64_t new_end = alloc_addr + aligned_len;
            if (std::max(alloc_addr, r.vmaddr) < std::min(new_end, r_end)) {
                return 0; // Conflicting mapping
            }
        }
    }

    proc->vm_regions.push_back(VmRegion{alloc_addr, aligned_len, prot, flags});
    return alloc_addr;
}

bool DarwinBsdSubsystem::munmap(std::uint32_t pid, std::uint64_t addr, std::size_t length) {
    auto proc = get_process(pid);
    if (!proc || length == 0) return false;

    const std::uint64_t kPageMask = 0x3FFFULL;
    if ((addr & kPageMask) != 0) return false; // Must be page-aligned

    auto it = std::remove_if(proc->vm_regions.begin(), proc->vm_regions.end(), [&](const VmRegion& r) {
        return r.vmaddr == addr;
    });

    if (it != proc->vm_regions.end()) {
        proc->vm_regions.erase(it, proc->vm_regions.end());
        return true;
    }
    return false;
}

bool DarwinBsdSubsystem::check_vm_access(
    std::uint32_t pid,
    std::uint64_t addr,
    std::size_t length,
    std::uint32_t required_prot) const {

    auto proc = const_cast<DarwinBsdSubsystem*>(this)->get_process(pid);
    if (!proc || length == 0) return false;

    if (addr + length < addr) return false; // Overflow

    // Check if [addr, addr + length) is completely covered by valid vm_regions with required_prot
    for (const auto& r : proc->vm_regions) {
        if (r.vmaddr <= addr && (addr + length) <= (r.vmaddr + r.size)) {
            // Guard regions (PROT_NONE) reject all access
            if (r.prot == 0) return false;
            // Verify permission subset
            if ((r.prot & required_prot) == required_prot) {
                return true;
            }
        }
    }
    return false;
}

bool DarwinBsdSubsystem::kill(std::uint32_t target_pid, int signal) {
    auto proc = get_process(target_pid);
    if (!proc) return false;

    proc->pending_signals |= (1ULL << signal);

    if (signal == kDarwinSigKill) {
        proc->state = ProcessState::Terminated;
    }
    return true;
}

std::int64_t DarwinBsdSubsystem::lseek(std::uint32_t pid, int fd, std::int64_t offset, int whence) {
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end() || !it->second.node) return -1;

    auto& desc = it->second;
    std::int64_t new_offset = 0;

    switch (whence) {
        case 0: // SEEK_SET
            new_offset = offset;
            break;
        case 1: // SEEK_CUR
            new_offset = static_cast<std::int64_t>(desc.offset) + offset;
            break;
        case 2: // SEEK_END
            new_offset = static_cast<std::int64_t>(desc.node->size) + offset;
            break;
        default:
            return -1;
    }

    if (new_offset < 0) {
        return -1;
    }

    desc.offset = static_cast<std::uint64_t>(new_offset);
    return new_offset;
}

int DarwinBsdSubsystem::fstat64(std::uint32_t pid, int fd, DarwinStat64Raw& out_stat) {
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto it = proc->fds.find(fd);
    if (it == proc->fds.end() || !it->second.node) return -1;

    const auto& node = it->second.node;
    std::memset(&out_stat, 0, sizeof(DarwinStat64Raw));
    out_stat.st_dev = 1;
    out_stat.st_mode = static_cast<std::uint16_t>(node->mode);
    if (node->is_directory()) {
        out_stat.st_mode |= 0040000;
    } else {
        out_stat.st_mode |= 0100000;
    }
    out_stat.st_nlink = 1;
    out_stat.st_ino = node->inode;
    out_stat.st_uid = proc->uid;
    out_stat.st_gid = proc->gid;
    out_stat.st_size = static_cast<std::int64_t>(node->size);
    out_stat.st_blocks = (out_stat.st_size + 511) / 512;
    out_stat.st_blksize = 4096;
    out_stat.st_atimespec.tv_sec = 1700000000;
    out_stat.st_mtimespec.tv_sec = 1700000000;
    out_stat.st_ctimespec.tv_sec = 1700000000;
    out_stat.st_birthtimespec.tv_sec = 1700000000;

    return 0;
}

int DarwinBsdSubsystem::stat64(std::uint32_t pid, const std::string& path, DarwinStat64Raw& out_stat) {
    auto proc = get_process(pid);
    if (!proc) return -1;

    auto node = vfs_.resolve_path(path);
    if (!node) return -1;

    std::memset(&out_stat, 0, sizeof(DarwinStat64Raw));
    out_stat.st_dev = 1;
    out_stat.st_mode = static_cast<std::uint16_t>(node->mode);
    if (node->is_directory()) {
        out_stat.st_mode |= 0040000;
    } else {
        out_stat.st_mode |= 0100000;
    }
    out_stat.st_nlink = 1;
    out_stat.st_ino = node->inode;
    out_stat.st_uid = proc->uid;
    out_stat.st_gid = proc->gid;
    out_stat.st_size = static_cast<std::int64_t>(node->size);
    out_stat.st_blocks = (out_stat.st_size + 511) / 512;
    out_stat.st_blksize = 4096;
    out_stat.st_atimespec.tv_sec = 1700000000;
    out_stat.st_mtimespec.tv_sec = 1700000000;
    out_stat.st_ctimespec.tv_sec = 1700000000;
    out_stat.st_birthtimespec.tv_sec = 1700000000;

    return 0;
}

bool DarwinBsdSubsystem::mprotect(std::uint32_t pid, std::uint64_t addr, std::size_t length, std::uint32_t new_prot) {
    auto proc = get_process(pid);
    if (!proc || length == 0) return false;

    constexpr std::uint32_t kProtWrite = 0x02u;
    constexpr std::uint32_t kProtExecute = 0x04u;
    if ((new_prot & (kProtWrite | kProtExecute)) == (kProtWrite | kProtExecute)) {
        return false;
    }

    for (auto& r : proc->vm_regions) {
        if (r.vmaddr == addr && r.size >= length) {
            r.prot = new_prot;
            return true;
        }
    }
    return false;
}

std::uint32_t DarwinBsdSubsystem::getuid(std::uint32_t pid) const noexcept {
    auto proc = const_cast<DarwinBsdSubsystem*>(this)->get_process(pid);
    return proc ? proc->uid : 0;
}

std::uint32_t DarwinBsdSubsystem::geteuid(std::uint32_t pid) const noexcept {
    return getuid(pid);
}

std::uint32_t DarwinBsdSubsystem::getgid(std::uint32_t pid) const noexcept {
    auto proc = const_cast<DarwinBsdSubsystem*>(this)->get_process(pid);
    return proc ? proc->gid : 0;
}

std::uint32_t DarwinBsdSubsystem::getegid(std::uint32_t pid) const noexcept {
    return getgid(pid);
}

int DarwinBsdSubsystem::gettimeofday(DarwinTimeval64& out_tv) const noexcept {
    out_tv.tv_sec = 1700000000;
    out_tv.tv_usec = 123456;
    out_tv.pad = 0;
    return 0;
}

int DarwinBsdSubsystem::clock_gettime(std::uint32_t clock_id, DarwinTimespec64& out_ts) const noexcept {
    (void)clock_id;
    out_ts.tv_sec = 1700000000;
    out_ts.tv_nsec = 123456000;
    return 0;
}

int DarwinBsdSubsystem::ioctl(std::uint32_t pid, int fd, std::uint64_t request, void* argp) {
    (void)pid;
    (void)fd;
    (void)request;
    (void)argp;
    return 0;
}

bool DarwinBsdSubsystem::has_pending_signal(std::uint32_t pid, int signal) {
    auto proc = get_process(pid);
    if (!proc) return false;
    return (proc->pending_signals & (1ULL << signal)) != 0;
}

} // namespace winios::runtime
