#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Darwin BSD Signals
// -----------------------------------------------------------------------------
inline constexpr int kDarwinSigHup    = 1;
inline constexpr int kDarwinSigInt    = 2;
inline constexpr int kDarwinSigQuit   = 3;
inline constexpr int kDarwinSigIll    = 4;
inline constexpr int kDarwinSigTrap   = 5;
inline constexpr int kDarwinSigAbrt   = 6;
inline constexpr int kDarwinSigKill   = 9;
inline constexpr int kDarwinSigBus    = 10;
inline constexpr int kDarwinSigSegv   = 11;
inline constexpr int kDarwinSigPipe   = 13;
inline constexpr int kDarwinSigTerm   = 15;
inline constexpr int kDarwinSigStop   = 17;
inline constexpr int kDarwinSigChld   = 20;

// -----------------------------------------------------------------------------
// Darwin Time & Stat Structures (ARM64 ABI)
// -----------------------------------------------------------------------------
#pragma pack(push, 4)
struct DarwinTimespec64 {
    std::int64_t tv_sec{0};
    std::int64_t tv_nsec{0};
};

struct DarwinTimeval64 {
    std::int64_t tv_sec{0};
    std::int32_t tv_usec{0};
    std::int32_t pad{0};
};

struct DarwinStat64Raw {
    std::int32_t     st_dev{0};           // [0..3]
    std::uint16_t    st_mode{0};          // [4..5]
    std::uint16_t    st_nlink{0};         // [6..7]
    std::uint64_t    st_ino{0};           // [8..15]
    std::uint32_t    st_uid{0};           // [16..19]
    std::uint32_t    st_gid{0};           // [20..23]
    std::int32_t     st_rdev{0};          // [24..27]
    std::uint32_t    st_pad0{0};          // [28..31]
    DarwinTimespec64 st_atimespec{};      // [32..47]
    DarwinTimespec64 st_mtimespec{};      // [48..63]
    DarwinTimespec64 st_ctimespec{};      // [64..79]
    DarwinTimespec64 st_birthtimespec{};  // [80..95]
    std::int64_t     st_size{0};          // [96..103]
    std::int64_t     st_blocks{0};        // [104..111]
    std::int32_t     st_blksize{4096};    // [112..115]
    std::uint32_t    st_flags{0};         // [116..119]
    std::uint32_t    st_gen{0};           // [120..123]
    std::int32_t     st_lspare{0};        // [124..127]
    std::int64_t     st_qspare[2]{0, 0};  // [128..143]
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Darwin Dirent64 Structure
// -----------------------------------------------------------------------------
constexpr std::uint8_t DT_UNKNOWN = 0;
constexpr std::uint8_t DT_FIFO    = 1;
constexpr std::uint8_t DT_CHR     = 2;
constexpr std::uint8_t DT_DIR     = 4;
constexpr std::uint8_t DT_BLK     = 6;
constexpr std::uint8_t DT_REG     = 8;
constexpr std::uint8_t DT_LNK     = 10;
constexpr std::uint8_t DT_SOCK    = 12;

#pragma pack(push, 1)
struct DarwinDirent64Raw {
    std::uint64_t d_ino{0};
    std::uint64_t d_seekoff{0};
    std::uint16_t d_reclen{0};
    std::uint16_t d_namlen{0};
    std::uint8_t  d_type{0};
    char          d_name[256]{0};
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Darwin VFS & File Descriptors
// -----------------------------------------------------------------------------
enum class VfsNodeType {
    RegularFile,
    Directory,
    CharacterDevice,
    BlockDevice,
    Symlink
};

struct VfsNode {
    std::string                      name;
    VfsNodeType                      type{VfsNodeType::RegularFile};
    std::uint32_t                    mode{0644};
    std::uint64_t                    inode{1};
    std::uint64_t                    size{0};
    std::vector<std::uint8_t>        content;
    std::string                      symlink_target;
    std::map<std::string, std::shared_ptr<VfsNode>> children;

    [[nodiscard]] bool is_directory() const noexcept { return type == VfsNodeType::Directory; }
};

struct PipeChannel {
    std::mutex                lock;
    std::vector<std::uint8_t> buffer;
    bool                      write_closed{false};
    bool                      read_closed{false};
};

struct FileDescriptor {
    int                          fd{-1};
    std::shared_ptr<VfsNode>     node;
    std::shared_ptr<PipeChannel> pipe;
    std::uint32_t                flags{0};
    std::uint64_t                offset{0};

    [[nodiscard]] bool is_pipe() const noexcept { return pipe != nullptr; }
};

class DarwinVfs {
public:
    DarwinVfs();
    ~DarwinVfs() = default;

    void populate_default_rootfs();

    std::shared_ptr<VfsNode> resolve_path(const std::string& path, int max_symlinks = 32) const;
    bool create_directory(const std::string& path);
    bool create_symlink(const std::string& target, const std::string& linkpath);
    std::string canonicalize_path(const std::string& path) const;
    bool write_file(const std::string& path, const void* data, std::size_t size);
    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) const;
    std::vector<std::string> list_directory(const std::string& path) const;

private:
    std::shared_ptr<VfsNode> resolve_path_locked(const std::string& path, int max_symlinks) const;
    bool create_directory_locked(const std::string& path);

    std::shared_ptr<VfsNode> root_;
    mutable std::mutex       lock_;
    std::uint64_t            next_inode_{100};
};

// -----------------------------------------------------------------------------
// Darwin Process & BSD Subsystem
// -----------------------------------------------------------------------------
enum class ProcessState {
    Running,
    Sleeping,
    Stopped,
    Zombie,
    Terminated
};

struct VmRegion {
    std::uint64_t vmaddr{0};
    std::uint64_t size{0};
    std::uint32_t prot{0};
    std::uint32_t flags{0};
};

struct DarwinThreadContext {
    std::uint64_t tid{1};
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint64_t tpidr_el0{0};
    std::uint64_t stack_base{0};
    std::size_t   stack_size{0};
    std::uint64_t x[31]{0};
};

struct DarwinProcess {
    std::uint32_t                     pid{0};
    std::uint32_t                     ppid{0};
    std::uint32_t                     uid{0};
    std::uint32_t                     gid{0};
    ProcessState                      state{ProcessState::Running};
    int                               exit_code{0};
    std::string                       executable_path;
    std::unordered_map<int, FileDescriptor> fds;
    int                               next_fd{3}; // 0, 1, 2 are stdin, stdout, stderr
    std::uint64_t                     pending_signals{0};
    std::uint64_t                     signal_mask{0};
    std::vector<VmRegion>             vm_regions;
    std::uint64_t                     brk_base{0x0000000180000000ULL};
    std::uint64_t                     brk_curr{0x0000000180000000ULL};
    std::vector<DarwinThreadContext>  threads;
    std::uint64_t                     next_tid{2};
};

class DarwinBsdSubsystem {
public:
    DarwinBsdSubsystem();
    ~DarwinBsdSubsystem() = default;

    DarwinVfs& vfs() noexcept { return vfs_; }
    const DarwinVfs& vfs() const noexcept { return vfs_; }

    // Process Lifecycle
    std::shared_ptr<DarwinProcess> get_process(std::uint32_t pid);
    std::uint32_t create_process(std::uint32_t ppid, const std::string& exec_path, std::uint32_t uid = 0);
    std::uint32_t create_process_with_pid(std::uint32_t pid, std::uint32_t ppid, const std::string& exec_path, std::uint32_t uid = 0);
    std::uint32_t fork(std::uint32_t parent_pid);
    bool execve(
        std::uint32_t pid,
        const std::string& binary_path,
        const std::vector<std::string>& argv,
        const std::vector<std::string>& envp,
        Memory& memory);
    void exit_process(std::uint32_t pid, int exit_code);
    std::int32_t waitpid(std::uint32_t parent_pid, std::int32_t target_pid, int& out_status, int options);

    // Thread Lifecycle
    std::uint64_t create_thread(std::uint32_t pid, std::uint64_t entry_point, std::uint64_t stack_ptr, std::uint64_t tpidr = 0);
    bool get_thread_context(std::uint32_t pid, std::uint64_t tid, DarwinThreadContext& out_ctx);
    bool set_thread_context(std::uint32_t pid, std::uint64_t tid, const DarwinThreadContext& ctx);
    bool terminate_thread(std::uint32_t pid, std::uint64_t tid);
    std::vector<std::uint64_t> list_threads(std::uint32_t pid) const;
    int bsdthread_create(std::uint32_t pid, std::uint64_t func, std::uint64_t arg, std::uint64_t stack, std::uint64_t flags);
    int bsdthread_terminate(std::uint32_t pid, std::uint64_t stackaddr, std::size_t size, std::uint32_t kthport, std::uint32_t sem);

    // File Descriptor Operations
    int open(std::uint32_t pid, const std::string& path, std::uint32_t flags, std::uint32_t mode = 0644);
    int close(std::uint32_t pid, int fd);
    std::int64_t read(std::uint32_t pid, int fd, void* buffer, std::size_t count);
    std::int64_t pread(std::uint32_t pid, int fd, void* buffer, std::size_t count, std::int64_t offset);
    std::int64_t write(std::uint32_t pid, int fd, const void* buffer, std::size_t count);
    std::int64_t lseek(std::uint32_t pid, int fd, std::int64_t offset, int whence);
    int fstat64(std::uint32_t pid, int fd, DarwinStat64Raw& out_stat);
    int stat64(std::uint32_t pid, const std::string& path, DarwinStat64Raw& out_stat);
    int ioctl(std::uint32_t pid, int fd, std::uint64_t request, void* argp);
    int pipe(std::uint32_t pid, int fds[2]);
    int dup(std::uint32_t pid, int oldfd);
    int dup2(std::uint32_t pid, int oldfd, int newfd);
    int getdirentries64(std::uint32_t pid, int fd, void* buf, std::size_t nbytes, std::int64_t* basep);

    // Identity
    std::uint32_t getuid(std::uint32_t pid) const noexcept;
    std::uint32_t geteuid(std::uint32_t pid) const noexcept;
    std::uint32_t getgid(std::uint32_t pid) const noexcept;
    std::uint32_t getegid(std::uint32_t pid) const noexcept;

    // Time
    int gettimeofday(DarwinTimeval64& out_tv) const noexcept;
    int clock_gettime(std::uint32_t clock_id, DarwinTimespec64& out_ts) const noexcept;

    // Memory Management
    std::uint64_t mmap(
        std::uint32_t pid,
        std::uint64_t addr,
        std::size_t length,
        std::uint32_t prot,
        std::uint32_t flags,
        int fd,
        std::uint64_t offset);
    bool munmap(std::uint32_t pid, std::uint64_t addr, std::size_t length);
    bool mprotect(std::uint32_t pid, std::uint64_t addr, std::size_t length, std::uint32_t new_prot);
    [[nodiscard]] bool check_vm_access(
        std::uint32_t pid,
        std::uint64_t addr,
        std::size_t length,
        std::uint32_t required_prot) const;

    // Signal Delivery
    bool kill(std::uint32_t target_pid, int signal);
    [[nodiscard]] bool has_pending_signal(std::uint32_t pid, int signal);

private:
    DarwinVfs                                            vfs_;
    std::uint32_t                                        next_pid_{2}; // 1 is /sbin/launchd
    mutable std::mutex                                   lock_;
    std::unordered_map<std::uint32_t, std::shared_ptr<DarwinProcess>> processes_;
};

} // namespace winios::runtime
