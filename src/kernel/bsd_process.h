#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/ipc.h"

namespace winios::kernel {

using pid_t = std::int32_t;

enum class ProcessState : std::uint32_t {
    Starting  = 1, // SIDL: Process being initialized
    Runnable  = 2, // SRUN: Process active / ready to execute
    Sleeping  = 3, // SSLEEP: Process waiting on resource/event
    Stopped   = 4, // SSTOP: Process stopped
    Zombie    = 5  // SZOMB: Process exited, awaiting reap
};

// Bounded file descriptor representation
enum class FileType : std::uint8_t {
    Console = 0,
    MemoryPipe = 1,
    Regular = 2
};

struct FileDescriptor {
    bool is_open{false};
    FileType type{FileType::Console};
    bool readable{true};
    bool writable{true};
    std::uint64_t offset{0};
    std::string path;
};

class FileDescriptorTable {
public:
    static constexpr int kMaxFiles = 64;

    FileDescriptorTable();
    ~FileDescriptorTable() = default;

    int allocate_fd(FileType type = FileType::Regular, bool readable = true, bool writable = true, const std::string& path = "");
    bool install_fd(int fd, FileType type, bool readable, bool writable, const std::string& path = "");
    bool close_fd(int fd);
    [[nodiscard]] const FileDescriptor* get_fd(int fd) const;
    [[nodiscard]] FileDescriptor* get_fd_mut(int fd);
    [[nodiscard]] bool is_valid_fd(int fd) const;
    [[nodiscard]] std::size_t open_count() const;

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    std::array<FileDescriptor, kMaxFiles> fds_{};
};

class BsdProcess {
public:
    BsdProcess(pid_t pid, pid_t ppid, std::string name, KernelTask* task = nullptr);
    ~BsdProcess() = default;

    [[nodiscard]] pid_t pid() const noexcept { return pid_; }
    [[nodiscard]] pid_t ppid() const noexcept { return ppid_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ProcessState state() const noexcept { return state_; }
    [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] KernelTask* task() const noexcept { return task_; }
    [[nodiscard]] const std::vector<KernelThread*>& threads() const noexcept { return threads_; }
    [[nodiscard]] FileDescriptorTable& fd_table() noexcept { return fd_table_; }
    [[nodiscard]] const FileDescriptorTable& fd_table() const noexcept { return fd_table_; }
    [[nodiscard]] ProcessIpcSpace& ipc_space() noexcept { return ipc_space_; }
    [[nodiscard]] const ProcessIpcSpace& ipc_space() const noexcept { return ipc_space_; }

    void set_state(ProcessState new_state) noexcept { state_ = new_state; }
    void set_task(KernelTask* task) noexcept { task_ = task; }
    void add_thread(KernelThread* thread);
    bool remove_thread(std::uint64_t tid);
    void terminate(int exit_code);

    [[nodiscard]] bool is_terminated() const noexcept { return state_ == ProcessState::Zombie; }

    void serialize(std::vector<std::uint8_t>& dst) const;
    static std::unique_ptr<BsdProcess> deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    pid_t pid_{0};
    pid_t ppid_{0};
    std::string name_;
    ProcessState state_{ProcessState::Starting};
    int exit_code_{0};
    KernelTask* task_{nullptr};
    std::vector<KernelThread*> threads_{};
    FileDescriptorTable fd_table_{};
    ProcessIpcSpace ipc_space_{};
};

class BsdProcessManager {
public:
    BsdProcessManager();
    ~BsdProcessManager() = default;

    // Initializes process manager with PID 0 (kernel_process)
    bool initialize(KernelTask* kernel_task = nullptr);

    // Create a new process with allocated PID
    BsdProcess* create_process(const std::string& name, pid_t ppid = 0, KernelTask* task = nullptr);

    // Terminate process with exit code
    bool terminate_process(pid_t pid, int exit_code);

    // Lookup process by PID
    [[nodiscard]] BsdProcess* get_process(pid_t pid);
    [[nodiscard]] const BsdProcess* get_process(pid_t pid) const;

    // Reap zombie process
    bool reap_process(pid_t pid);

    [[nodiscard]] std::size_t process_count() const noexcept { return processes_.size(); }

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t* src, std::size_t size);

private:
    pid_t next_pid_{1};
    std::unordered_map<pid_t, std::unique_ptr<BsdProcess>> processes_{};
};

} // namespace winios::kernel
