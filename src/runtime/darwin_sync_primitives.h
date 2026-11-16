#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_thread_runtime.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// POSIX / Darwin Mutex Types
// -----------------------------------------------------------------------------
constexpr std::int32_t PTHREAD_MUTEX_NORMAL     = 0;
constexpr std::int32_t PTHREAD_MUTEX_ERRORCHECK = 1;
constexpr std::int32_t PTHREAD_MUTEX_RECURSIVE  = 2;
constexpr std::int32_t PTHREAD_MUTEX_DEFAULT    = PTHREAD_MUTEX_NORMAL;

// -----------------------------------------------------------------------------
// Darwin Pthread Magic Signatures
// -----------------------------------------------------------------------------
constexpr std::uint32_t _PTHREAD_MUTEX_SIG_INIT  = 0x32AAABA7u;
constexpr std::uint32_t _PTHREAD_COND_SIG_INIT   = 0x3CB0B10Cu;
constexpr std::uint32_t _PTHREAD_RWLOCK_SIG_INIT = 0x2DA888CAu;
constexpr std::uint32_t _PTHREAD_ONCE_SIG_INIT   = 0x30B11BACu;

// -----------------------------------------------------------------------------
// Darwin BSD Errno Codes
// -----------------------------------------------------------------------------
constexpr std::int32_t DARWIN_SUCCESS    = 0;
constexpr std::int32_t DARWIN_EPERM      = 1;
constexpr std::int32_t DARWIN_ESRCH      = 3;
constexpr std::int32_t DARWIN_EDEADLK    = 11;
constexpr std::int32_t DARWIN_EBUSY      = 16;
constexpr std::int32_t DARWIN_EINVAL     = 22;
constexpr std::int32_t DARWIN_ETIMEDOUT  = 60;
constexpr std::int32_t DARWIN_ECANCELED  = 89;

// -----------------------------------------------------------------------------
// Raw Guest Memory Layouts
// -----------------------------------------------------------------------------
#pragma pack(push, 8)
struct DarwinPthreadMutexRaw {
    std::uint32_t sig;            // _PTHREAD_MUTEX_SIG_INIT
    std::int32_t  type;           // Mutex type
    std::uint64_t owner_tid;      // Owning TID (0 if unlocked)
    std::uint32_t recursion_count;// Recursive acquisition count
    std::uint32_t waiter_count;   // Count of waiters
    std::uint64_t opaque[6];      // Opaque buffer (72 bytes total)
};

struct DarwinPthreadCondRaw {
    std::uint32_t sig;            // _PTHREAD_COND_SIG_INIT
    std::uint32_t sequence;       // Sequence/generation number
    std::uint32_t waiter_count;   // Count of waiters
    std::uint32_t padding;
    std::uint64_t opaque[4];      // Opaque buffer (48 bytes total)
};

struct DarwinPthreadRwlockRaw {
    std::uint32_t sig;            // _PTHREAD_RWLOCK_SIG_INIT
    std::uint32_t state;          // 0 = unlocked, 1 = read, 2 = write
    std::uint64_t writer_tid;     // Owning writer TID (0 if none)
    std::uint32_t reader_count;   // Active readers count
    std::uint32_t waiter_count;   // Waiters count
    std::uint64_t opaque[6];      // Opaque buffer (72 bytes total)
};

struct DarwinPthreadOnceRaw {
    std::uint32_t sig;            // _PTHREAD_ONCE_SIG_INIT
    std::uint32_t status;         // 0: uninit, 1: running, 2: completed
};
#pragma pack(pop)

static_assert(sizeof(DarwinPthreadMutexRaw) == 72, "DarwinPthreadMutexRaw size check");
static_assert(sizeof(DarwinPthreadCondRaw) == 48, "DarwinPthreadCondRaw size check");
static_assert(sizeof(DarwinPthreadRwlockRaw) == 72, "DarwinPthreadRwlockRaw size check");
static_assert(sizeof(DarwinPthreadOnceRaw) == 8, "DarwinPthreadOnceRaw size check");

// -----------------------------------------------------------------------------
// Synchronization Telemetry
// -----------------------------------------------------------------------------
struct DarwinSyncTelemetry {
    std::uint64_t mutex_locks{0};
    std::uint64_t mutex_unlocks{0};
    std::uint64_t mutex_contentions{0};
    std::uint64_t mutex_deadlocks_detected{0};
    std::uint64_t cond_waits{0};
    std::uint64_t cond_signals{0};
    std::uint64_t cond_broadcasts{0};
    std::uint64_t cond_timeouts{0};
    std::uint64_t cond_cancellations{0};
    std::uint64_t rwlock_read_locks{0};
    std::uint64_t rwlock_write_locks{0};
    std::uint64_t once_executions{0};
};

// -----------------------------------------------------------------------------
// DarwinSyncManager
// -----------------------------------------------------------------------------
class DarwinSyncManager {
public:
    explicit DarwinSyncManager(DarwinThreadRuntime* thread_runtime = nullptr);
    ~DarwinSyncManager() = default;

    void set_thread_runtime(DarwinThreadRuntime* thread_runtime) noexcept {
        thread_runtime_ = thread_runtime;
    }

    [[nodiscard]] const DarwinSyncTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

    // Mutex Primitives
    int mutex_init(Memory& mem, std::uint64_t mutex_va, std::int32_t type = PTHREAD_MUTEX_NORMAL);
    int mutex_lock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid, std::uint64_t timeout_ns = 0);
    int mutex_trylock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid);
    int mutex_unlock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid);
    int mutex_destroy(Memory& mem, std::uint64_t mutex_va);

    // Condition Variable Primitives
    int cond_init(Memory& mem, std::uint64_t cond_va);
    int cond_wait(Memory& mem, std::uint64_t cond_va, std::uint64_t mutex_va, std::uint64_t tid);
    int cond_timedwait(Memory& mem, std::uint64_t cond_va, std::uint64_t mutex_va, std::uint64_t tid, std::uint64_t timeout_ns);
    int cond_signal(Memory& mem, std::uint64_t cond_va);
    int cond_broadcast(Memory& mem, std::uint64_t cond_va);
    int cond_destroy(Memory& mem, std::uint64_t cond_va);

    // Read/Write Lock Primitives
    int rwlock_init(Memory& mem, std::uint64_t rwlock_va);
    int rwlock_rdlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid);
    int rwlock_tryrdlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid);
    int rwlock_wrlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid);
    int rwlock_trywrlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid);
    int rwlock_unlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid);
    int rwlock_destroy(Memory& mem, std::uint64_t rwlock_va);

    // Once Initialization
    int pthread_once(Memory& mem, std::uint64_t once_va, std::uint64_t tid, const std::function<void()>& init_fn);

    // Cancellation & State Inspection
    int cancel_thread(Memory& mem, std::uint64_t tid);
    [[nodiscard]] bool is_thread_waiting(std::uint64_t tid) const;
    [[nodiscard]] bool would_deadlock(std::uint64_t tid, std::uint64_t mutex_va) const;

private:
    struct MutexState {
        std::uint64_t va{0};
        std::int32_t  type{PTHREAD_MUTEX_NORMAL};
        std::uint64_t owner_tid{0};
        std::uint32_t recursion_count{0};
        std::deque<std::uint64_t> waiters;
    };

    struct CondState {
        std::uint64_t va{0};
        std::uint32_t sequence{0};
        std::deque<std::pair<std::uint64_t, std::uint64_t>> waiters; // {tid, associated_mutex_va}
    };

    struct RwlockState {
        std::uint64_t va{0};
        std::uint64_t writer_tid{0};
        std::unordered_set<std::uint64_t> readers;
        std::deque<std::uint64_t> read_waiters;
        std::deque<std::uint64_t> write_waiters;
    };

    struct OnceState {
        std::uint64_t va{0};
        std::uint32_t status{0};
        std::uint64_t initializing_tid{0};
    };

    DarwinThreadRuntime* thread_runtime_{nullptr};
    DarwinSyncTelemetry  telemetry_{};
    mutable std::mutex   lock_;

    std::unordered_map<std::uint64_t, MutexState>  mutexes_;
    std::unordered_map<std::uint64_t, CondState>   condvars_;
    std::unordered_map<std::uint64_t, RwlockState> rwlocks_;
    std::unordered_map<std::uint64_t, OnceState>   onces_;

    // Helper memory accessors
    static std::size_t resolve_offset(const Memory& mem, std::uint64_t va, std::size_t size);
    bool read_raw_mutex(const Memory& mem, std::uint64_t va, DarwinPthreadMutexRaw& out_raw) const;
    bool write_raw_mutex(Memory& mem, std::uint64_t va, const DarwinPthreadMutexRaw& raw);
    bool read_raw_cond(const Memory& mem, std::uint64_t va, DarwinPthreadCondRaw& out_raw) const;
    bool write_raw_cond(Memory& mem, std::uint64_t va, const DarwinPthreadCondRaw& raw);
    bool read_raw_rwlock(const Memory& mem, std::uint64_t va, DarwinPthreadRwlockRaw& out_raw) const;
    bool write_raw_rwlock(Memory& mem, std::uint64_t va, const DarwinPthreadRwlockRaw& raw);
    bool read_raw_once(const Memory& mem, std::uint64_t va, DarwinPthreadOnceRaw& out_raw) const;
    bool write_raw_once(Memory& mem, std::uint64_t va, const DarwinPthreadOnceRaw& raw);
};

} // namespace winios::runtime
