#include "runtime/darwin_sync_primitives.h"

#include <cstring>
#include <algorithm>

namespace winios::runtime {

DarwinSyncManager::DarwinSyncManager(DarwinThreadRuntime* thread_runtime)
    : thread_runtime_(thread_runtime) {}

std::size_t DarwinSyncManager::resolve_offset(const Memory& mem, std::uint64_t va, std::size_t size) {
    if (mem.size() == 0 || size > mem.size()) {
        return static_cast<std::size_t>(-1);
    }
    std::size_t off = (va < mem.size())
                          ? static_cast<std::size_t>(va)
                          : static_cast<std::size_t>(va % mem.size());
    if (off + size > mem.size()) {
        return static_cast<std::size_t>(-1);
    }
    return off;
}

bool DarwinSyncManager::read_raw_mutex(const Memory& mem, std::uint64_t va, DarwinPthreadMutexRaw& out_raw) const {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadMutexRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(&out_raw, mem.raw_data() + off, sizeof(DarwinPthreadMutexRaw));
    return true;
}

bool DarwinSyncManager::write_raw_mutex(Memory& mem, std::uint64_t va, const DarwinPthreadMutexRaw& raw) {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadMutexRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(mem.raw_data() + off, &raw, sizeof(DarwinPthreadMutexRaw));
    return true;
}

bool DarwinSyncManager::read_raw_cond(const Memory& mem, std::uint64_t va, DarwinPthreadCondRaw& out_raw) const {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadCondRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(&out_raw, mem.raw_data() + off, sizeof(DarwinPthreadCondRaw));
    return true;
}

bool DarwinSyncManager::write_raw_cond(Memory& mem, std::uint64_t va, const DarwinPthreadCondRaw& raw) {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadCondRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(mem.raw_data() + off, &raw, sizeof(DarwinPthreadCondRaw));
    return true;
}

bool DarwinSyncManager::read_raw_rwlock(const Memory& mem, std::uint64_t va, DarwinPthreadRwlockRaw& out_raw) const {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadRwlockRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(&out_raw, mem.raw_data() + off, sizeof(DarwinPthreadRwlockRaw));
    return true;
}

bool DarwinSyncManager::write_raw_rwlock(Memory& mem, std::uint64_t va, const DarwinPthreadRwlockRaw& raw) {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadRwlockRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(mem.raw_data() + off, &raw, sizeof(DarwinPthreadRwlockRaw));
    return true;
}

bool DarwinSyncManager::read_raw_once(const Memory& mem, std::uint64_t va, DarwinPthreadOnceRaw& out_raw) const {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadOnceRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(&out_raw, mem.raw_data() + off, sizeof(DarwinPthreadOnceRaw));
    return true;
}

bool DarwinSyncManager::write_raw_once(Memory& mem, std::uint64_t va, const DarwinPthreadOnceRaw& raw) {
    std::size_t off = resolve_offset(mem, va, sizeof(DarwinPthreadOnceRaw));
    if (off == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::memcpy(mem.raw_data() + off, &raw, sizeof(DarwinPthreadOnceRaw));
    return true;
}

// -----------------------------------------------------------------------------
// Mutex Primitives
// -----------------------------------------------------------------------------
int DarwinSyncManager::mutex_init(Memory& mem, std::uint64_t mutex_va, std::int32_t type) {
    std::lock_guard<std::mutex> guard(lock_);
    if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_ERRORCHECK && type != PTHREAD_MUTEX_RECURSIVE) {
        return DARWIN_EINVAL;
    }

    DarwinPthreadMutexRaw raw{};
    raw.sig = _PTHREAD_MUTEX_SIG_INIT;
    raw.type = type;
    raw.owner_tid = 0;
    raw.recursion_count = 0;
    raw.waiter_count = 0;

    if (!write_raw_mutex(mem, mutex_va, raw)) {
        return DARWIN_EINVAL;
    }

    MutexState state{};
    state.va = mutex_va;
    state.type = type;
    state.owner_tid = 0;
    state.recursion_count = 0;
    mutexes_[mutex_va] = std::move(state);

    return DARWIN_SUCCESS;
}

int DarwinSyncManager::mutex_lock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid, std::uint64_t timeout_ns) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadMutexRaw raw{};
    if (!read_raw_mutex(mem, mutex_va, raw) || raw.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    if (thread_runtime_ && thread_runtime_->is_thread_cancelled(tid)) {
        return DARWIN_ECANCELED;
    }

    auto it = mutexes_.find(mutex_va);
    if (it == mutexes_.end()) {
        MutexState state{};
        state.va = mutex_va;
        state.type = raw.type;
        state.owner_tid = raw.owner_tid;
        state.recursion_count = raw.recursion_count;
        it = mutexes_.emplace(mutex_va, std::move(state)).first;
    }
    MutexState& m = it->second;

    if (m.owner_tid == tid) {
        if (m.type == PTHREAD_MUTEX_RECURSIVE) {
            m.recursion_count++;
            raw.recursion_count = m.recursion_count;
            write_raw_mutex(mem, mutex_va, raw);
            telemetry_.mutex_locks++;
            return DARWIN_SUCCESS;
        }
        if (m.type == PTHREAD_MUTEX_ERRORCHECK) {
            return DARWIN_EDEADLK;
        }
        // Normal mutex relocking is self-deadlock
        telemetry_.mutex_deadlocks_detected++;
        return DARWIN_EDEADLK;
    }

    if (m.owner_tid == 0) {
        // Free to acquire
        m.owner_tid = tid;
        m.recursion_count = 1;
        raw.owner_tid = tid;
        raw.recursion_count = 1;
        write_raw_mutex(mem, mutex_va, raw);
        telemetry_.mutex_locks++;
        return DARWIN_SUCCESS;
    }

    // Contention
    telemetry_.mutex_contentions++;

    // Check for deadlock cycle
    if (would_deadlock(tid, mutex_va)) {
        telemetry_.mutex_deadlocks_detected++;
        return DARWIN_EDEADLK;
    }

    if (timeout_ns > 0) {
        telemetry_.cond_timeouts++;
        return DARWIN_ETIMEDOUT;
    }

    if (std::find(m.waiters.begin(), m.waiters.end(), tid) == m.waiters.end()) {
        m.waiters.push_back(tid);
    }
    raw.waiter_count = static_cast<std::uint32_t>(m.waiters.size());
    write_raw_mutex(mem, mutex_va, raw);

    if (thread_runtime_) {
        thread_runtime_->block_thread(tid);
    }

    return DARWIN_EBUSY;
}

int DarwinSyncManager::mutex_trylock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadMutexRaw raw{};
    if (!read_raw_mutex(mem, mutex_va, raw) || raw.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = mutexes_.find(mutex_va);
    if (it == mutexes_.end()) {
        MutexState state{};
        state.va = mutex_va;
        state.type = raw.type;
        state.owner_tid = raw.owner_tid;
        state.recursion_count = raw.recursion_count;
        it = mutexes_.emplace(mutex_va, std::move(state)).first;
    }
    MutexState& m = it->second;

    if (m.owner_tid == 0) {
        m.owner_tid = tid;
        m.recursion_count = 1;
        raw.owner_tid = tid;
        raw.recursion_count = 1;
        write_raw_mutex(mem, mutex_va, raw);
        telemetry_.mutex_locks++;
        return DARWIN_SUCCESS;
    }

    if (m.owner_tid == tid && m.type == PTHREAD_MUTEX_RECURSIVE) {
        m.recursion_count++;
        raw.recursion_count = m.recursion_count;
        write_raw_mutex(mem, mutex_va, raw);
        telemetry_.mutex_locks++;
        return DARWIN_SUCCESS;
    }

    return DARWIN_EBUSY;
}

int DarwinSyncManager::mutex_unlock(Memory& mem, std::uint64_t mutex_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadMutexRaw raw{};
    if (!read_raw_mutex(mem, mutex_va, raw) || raw.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = mutexes_.find(mutex_va);
    if (it == mutexes_.end() || it->second.owner_tid != tid) {
        return DARWIN_EPERM;
    }
    MutexState& m = it->second;

    if (m.type == PTHREAD_MUTEX_RECURSIVE && m.recursion_count > 1) {
        m.recursion_count--;
        raw.recursion_count = m.recursion_count;
        write_raw_mutex(mem, mutex_va, raw);
        telemetry_.mutex_unlocks++;
        return DARWIN_SUCCESS;
    }

    // Fully releasing mutex
    telemetry_.mutex_unlocks++;
    if (!m.waiters.empty()) {
        std::uint64_t next_tid = m.waiters.front();
        m.waiters.pop_front();
        m.owner_tid = next_tid;
        m.recursion_count = 1;
        raw.owner_tid = next_tid;
        raw.recursion_count = 1;
        raw.waiter_count = static_cast<std::uint32_t>(m.waiters.size());
        write_raw_mutex(mem, mutex_va, raw);
        if (thread_runtime_) {
            thread_runtime_->unblock_thread(next_tid);
        }
    } else {
        m.owner_tid = 0;
        m.recursion_count = 0;
        raw.owner_tid = 0;
        raw.recursion_count = 0;
        raw.waiter_count = 0;
        write_raw_mutex(mem, mutex_va, raw);
    }

    return DARWIN_SUCCESS;
}

int DarwinSyncManager::mutex_destroy(Memory& mem, std::uint64_t mutex_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadMutexRaw raw{};
    if (!read_raw_mutex(mem, mutex_va, raw) || raw.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = mutexes_.find(mutex_va);
    if (it != mutexes_.end()) {
        if (it->second.owner_tid != 0 || !it->second.waiters.empty()) {
            return DARWIN_EBUSY;
        }
        mutexes_.erase(it);
    } else if (raw.owner_tid != 0 || raw.waiter_count != 0) {
        return DARWIN_EBUSY;
    }

    // Zero out memory
    std::memset(&raw, 0, sizeof(raw));
    write_raw_mutex(mem, mutex_va, raw);
    return DARWIN_SUCCESS;
}

// -----------------------------------------------------------------------------
// Condition Variable Primitives
// -----------------------------------------------------------------------------
int DarwinSyncManager::cond_init(Memory& mem, std::uint64_t cond_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw{};
    raw.sig = _PTHREAD_COND_SIG_INIT;
    raw.sequence = 0;
    raw.waiter_count = 0;
    raw.padding = 0;

    if (!write_raw_cond(mem, cond_va, raw)) {
        return DARWIN_EINVAL;
    }

    CondState state{};
    state.va = cond_va;
    state.sequence = 0;
    condvars_[cond_va] = std::move(state);

    return DARWIN_SUCCESS;
}

int DarwinSyncManager::cond_wait(Memory& mem, std::uint64_t cond_va, std::uint64_t mutex_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw_cond{};
    if (!read_raw_cond(mem, cond_va, raw_cond) || raw_cond.sig != _PTHREAD_COND_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    DarwinPthreadMutexRaw raw_mutex{};
    if (!read_raw_mutex(mem, mutex_va, raw_mutex) || raw_mutex.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto m_it = mutexes_.find(mutex_va);
    if (m_it == mutexes_.end() || m_it->second.owner_tid != tid) {
        return DARWIN_EPERM;
    }

    if (thread_runtime_ && thread_runtime_->is_thread_cancelled(tid)) {
        return DARWIN_ECANCELED;
    }

    // Atomically release mutex
    MutexState& m = m_it->second;
    m.owner_tid = 0;
    m.recursion_count = 0;
    raw_mutex.owner_tid = 0;
    raw_mutex.recursion_count = 0;
    write_raw_mutex(mem, mutex_va, raw_mutex);

    // Register waiter on condvar
    auto c_it = condvars_.find(cond_va);
    if (c_it == condvars_.end()) {
        CondState cstate{};
        cstate.va = cond_va;
        cstate.sequence = raw_cond.sequence;
        c_it = condvars_.emplace(cond_va, std::move(cstate)).first;
    }
    c_it->second.waiters.emplace_back(tid, mutex_va);
    raw_cond.waiter_count = static_cast<std::uint32_t>(c_it->second.waiters.size());
    write_raw_cond(mem, cond_va, raw_cond);

    if (thread_runtime_) {
        thread_runtime_->block_thread(tid);
    }
    telemetry_.cond_waits++;

    return DARWIN_SUCCESS;
}

int DarwinSyncManager::cond_timedwait(Memory& mem, std::uint64_t cond_va, std::uint64_t mutex_va, std::uint64_t tid, std::uint64_t timeout_ns) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw_cond{};
    if (!read_raw_cond(mem, cond_va, raw_cond) || raw_cond.sig != _PTHREAD_COND_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    DarwinPthreadMutexRaw raw_mutex{};
    if (!read_raw_mutex(mem, mutex_va, raw_mutex) || raw_mutex.sig != _PTHREAD_MUTEX_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto m_it = mutexes_.find(mutex_va);
    if (m_it == mutexes_.end() || m_it->second.owner_tid != tid) {
        return DARWIN_EPERM;
    }

    if (thread_runtime_ && thread_runtime_->is_thread_cancelled(tid)) {
        return DARWIN_ECANCELED;
    }

    // If timeout is non-zero and we are simulating an expired timeout:
    if (timeout_ns > 0) {
        telemetry_.cond_timeouts++;
        return DARWIN_ETIMEDOUT;
    }

    return cond_wait(mem, cond_va, mutex_va, tid);
}

int DarwinSyncManager::cond_signal(Memory& mem, std::uint64_t cond_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw_cond{};
    if (!read_raw_cond(mem, cond_va, raw_cond) || raw_cond.sig != _PTHREAD_COND_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = condvars_.find(cond_va);
    if (it == condvars_.end() || it->second.waiters.empty()) {
        return DARWIN_SUCCESS;
    }

    auto [w_tid, assoc_mutex_va] = it->second.waiters.front();
    it->second.waiters.pop_front();
    raw_cond.sequence++;
    raw_cond.waiter_count = static_cast<std::uint32_t>(it->second.waiters.size());
    write_raw_cond(mem, cond_va, raw_cond);

    if (thread_runtime_) {
        thread_runtime_->unblock_thread(w_tid);
    }

    // Re-acquire or transfer ownership of the associated mutex to woken thread
    auto m_it = mutexes_.find(assoc_mutex_va);
    if (m_it != mutexes_.end()) {
        if (m_it->second.owner_tid == 0) {
            m_it->second.owner_tid = w_tid;
            m_it->second.recursion_count = 1;
            DarwinPthreadMutexRaw raw_m{};
            if (read_raw_mutex(mem, assoc_mutex_va, raw_m)) {
                raw_m.owner_tid = w_tid;
                raw_m.recursion_count = 1;
                write_raw_mutex(mem, assoc_mutex_va, raw_m);
            }
        } else {
            m_it->second.waiters.push_back(w_tid);
        }
    }

    telemetry_.cond_signals++;
    return DARWIN_SUCCESS;
}

int DarwinSyncManager::cond_broadcast(Memory& mem, std::uint64_t cond_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw_cond{};
    if (!read_raw_cond(mem, cond_va, raw_cond) || raw_cond.sig != _PTHREAD_COND_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = condvars_.find(cond_va);
    if (it == condvars_.end() || it->second.waiters.empty()) {
        return DARWIN_SUCCESS;
    }

    while (!it->second.waiters.empty()) {
        auto [w_tid, assoc_mutex_va] = it->second.waiters.front();
        it->second.waiters.pop_front();

        if (thread_runtime_) {
            thread_runtime_->unblock_thread(w_tid);
        }

        auto m_it = mutexes_.find(assoc_mutex_va);
        if (m_it != mutexes_.end()) {
            if (m_it->second.owner_tid == 0) {
                m_it->second.owner_tid = w_tid;
                m_it->second.recursion_count = 1;
                DarwinPthreadMutexRaw raw_m{};
                if (read_raw_mutex(mem, assoc_mutex_va, raw_m)) {
                    raw_m.owner_tid = w_tid;
                    raw_m.recursion_count = 1;
                    write_raw_mutex(mem, assoc_mutex_va, raw_m);
                }
            } else {
                m_it->second.waiters.push_back(w_tid);
            }
        }
    }

    raw_cond.sequence++;
    raw_cond.waiter_count = 0;
    write_raw_cond(mem, cond_va, raw_cond);

    telemetry_.cond_broadcasts++;
    return DARWIN_SUCCESS;
}

int DarwinSyncManager::cond_destroy(Memory& mem, std::uint64_t cond_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadCondRaw raw_cond{};
    if (!read_raw_cond(mem, cond_va, raw_cond) || raw_cond.sig != _PTHREAD_COND_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = condvars_.find(cond_va);
    if (it != condvars_.end()) {
        if (!it->second.waiters.empty()) {
            return DARWIN_EBUSY;
        }
        condvars_.erase(it);
    } else if (raw_cond.waiter_count != 0) {
        return DARWIN_EBUSY;
    }

    std::memset(&raw_cond, 0, sizeof(raw_cond));
    write_raw_cond(mem, cond_va, raw_cond);
    return DARWIN_SUCCESS;
}

// -----------------------------------------------------------------------------
// Read/Write Lock Primitives
// -----------------------------------------------------------------------------
int DarwinSyncManager::rwlock_init(Memory& mem, std::uint64_t rwlock_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadRwlockRaw raw{};
    raw.sig = _PTHREAD_RWLOCK_SIG_INIT;
    raw.state = 0;
    raw.writer_tid = 0;
    raw.reader_count = 0;
    raw.waiter_count = 0;

    if (!write_raw_rwlock(mem, rwlock_va, raw)) {
        return DARWIN_EINVAL;
    }

    RwlockState state{};
    state.va = rwlock_va;
    rwlocks_[rwlock_va] = std::move(state);
    return DARWIN_SUCCESS;
}

int DarwinSyncManager::rwlock_rdlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadRwlockRaw raw{};
    if (!read_raw_rwlock(mem, rwlock_va, raw) || raw.sig != _PTHREAD_RWLOCK_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = rwlocks_.find(rwlock_va);
    if (it == rwlocks_.end()) {
        RwlockState state{};
        state.va = rwlock_va;
        it = rwlocks_.emplace(rwlock_va, std::move(state)).first;
    }
    RwlockState& rw = it->second;

    if (rw.writer_tid != 0) {
        if (rw.writer_tid == tid) {
            return DARWIN_EDEADLK;
        }
        return DARWIN_EBUSY;
    }

    rw.readers.insert(tid);
    raw.state = 1;
    raw.reader_count = static_cast<std::uint32_t>(rw.readers.size());
    write_raw_rwlock(mem, rwlock_va, raw);

    telemetry_.rwlock_read_locks++;
    return DARWIN_SUCCESS;
}

int DarwinSyncManager::rwlock_tryrdlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid) {
    return rwlock_rdlock(mem, rwlock_va, tid);
}

int DarwinSyncManager::rwlock_wrlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadRwlockRaw raw{};
    if (!read_raw_rwlock(mem, rwlock_va, raw) || raw.sig != _PTHREAD_RWLOCK_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = rwlocks_.find(rwlock_va);
    if (it == rwlocks_.end()) {
        RwlockState state{};
        state.va = rwlock_va;
        it = rwlocks_.emplace(rwlock_va, std::move(state)).first;
    }
    RwlockState& rw = it->second;

    if (rw.writer_tid == tid || rw.readers.count(tid) > 0) {
        return DARWIN_EDEADLK;
    }

    if (rw.writer_tid != 0 || !rw.readers.empty()) {
        return DARWIN_EBUSY;
    }

    rw.writer_tid = tid;
    raw.state = 2;
    raw.writer_tid = tid;
    write_raw_rwlock(mem, rwlock_va, raw);

    telemetry_.rwlock_write_locks++;
    return DARWIN_SUCCESS;
}

int DarwinSyncManager::rwlock_trywrlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid) {
    return rwlock_wrlock(mem, rwlock_va, tid);
}

int DarwinSyncManager::rwlock_unlock(Memory& mem, std::uint64_t rwlock_va, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadRwlockRaw raw{};
    if (!read_raw_rwlock(mem, rwlock_va, raw) || raw.sig != _PTHREAD_RWLOCK_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = rwlocks_.find(rwlock_va);
    if (it == rwlocks_.end()) {
        return DARWIN_EPERM;
    }
    RwlockState& rw = it->second;

    if (rw.writer_tid == tid) {
        rw.writer_tid = 0;
        raw.writer_tid = 0;
        raw.state = rw.readers.empty() ? 0u : 1u;
        write_raw_rwlock(mem, rwlock_va, raw);
        return DARWIN_SUCCESS;
    }

    auto r_it = rw.readers.find(tid);
    if (r_it != rw.readers.end()) {
        rw.readers.erase(r_it);
        raw.reader_count = static_cast<std::uint32_t>(rw.readers.size());
        raw.state = rw.readers.empty() ? 0u : 1u;
        write_raw_rwlock(mem, rwlock_va, raw);
        return DARWIN_SUCCESS;
    }

    return DARWIN_EPERM;
}

int DarwinSyncManager::rwlock_destroy(Memory& mem, std::uint64_t rwlock_va) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadRwlockRaw raw{};
    if (!read_raw_rwlock(mem, rwlock_va, raw) || raw.sig != _PTHREAD_RWLOCK_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    auto it = rwlocks_.find(rwlock_va);
    if (it != rwlocks_.end()) {
        if (it->second.writer_tid != 0 || !it->second.readers.empty() ||
            !it->second.read_waiters.empty() || !it->second.write_waiters.empty()) {
            return DARWIN_EBUSY;
        }
        rwlocks_.erase(it);
    } else if (raw.writer_tid != 0 || raw.reader_count != 0) {
        return DARWIN_EBUSY;
    }

    std::memset(&raw, 0, sizeof(raw));
    write_raw_rwlock(mem, rwlock_va, raw);
    return DARWIN_SUCCESS;
}

// -----------------------------------------------------------------------------
// Pthread Once
// -----------------------------------------------------------------------------
int DarwinSyncManager::pthread_once(Memory& mem, std::uint64_t once_va, std::uint64_t tid, const std::function<void()>& init_fn) {
    std::lock_guard<std::mutex> guard(lock_);
    DarwinPthreadOnceRaw raw{};
    if (!read_raw_once(mem, once_va, raw)) {
        return DARWIN_EINVAL;
    }

    // Auto-initialize signature if 0
    if (raw.sig == 0) {
        raw.sig = _PTHREAD_ONCE_SIG_INIT;
        raw.status = 0;
    }

    if (raw.sig != _PTHREAD_ONCE_SIG_INIT) {
        return DARWIN_EINVAL;
    }

    if (raw.status == 2) {
        // Already initialized
        return DARWIN_SUCCESS;
    }

    auto it = onces_.find(once_va);
    if (it == onces_.end()) {
        OnceState state{};
        state.va = once_va;
        state.status = raw.status;
        it = onces_.emplace(once_va, std::move(state)).first;
    }
    OnceState& ost = it->second;

    if (ost.status == 1 && ost.initializing_tid == tid) {
        // Recursive pthread_once call causes deadlock
        return DARWIN_EDEADLK;
    }

    // Set to running
    ost.status = 1;
    ost.initializing_tid = tid;
    raw.status = 1;
    write_raw_once(mem, once_va, raw);

    // Run the initialization routine
    if (init_fn) {
        init_fn();
    }

    // Set to completed
    ost.status = 2;
    ost.initializing_tid = 0;
    raw.status = 2;
    write_raw_once(mem, once_va, raw);
    telemetry_.once_executions++;

    return DARWIN_SUCCESS;
}

// -----------------------------------------------------------------------------
// Thread Cancellation & Inspection
// -----------------------------------------------------------------------------
int DarwinSyncManager::cancel_thread(Memory& mem, std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    if (thread_runtime_) {
        thread_runtime_->cancel_thread(tid);
    }

    // Unblock from any condvars
    for (auto& [_, c] : condvars_) {
        auto c_it = std::find_if(c.waiters.begin(), c.waiters.end(),
            [tid](const std::pair<std::uint64_t, std::uint64_t>& item) {
                return item.first == tid;
            });
        if (c_it != c.waiters.end()) {
            c.waiters.erase(c_it);
            telemetry_.cond_cancellations++;
            DarwinPthreadCondRaw raw_c{};
            if (read_raw_cond(mem, c.va, raw_c)) {
                raw_c.waiter_count = static_cast<std::uint32_t>(c.waiters.size());
                write_raw_cond(mem, c.va, raw_c);
            }
            if (thread_runtime_) {
                thread_runtime_->unblock_thread(tid);
            }
        }
    }

    // Remove from any mutex waiter lists
    for (auto& [_, m] : mutexes_) {
        auto m_it = std::find(m.waiters.begin(), m.waiters.end(), tid);
        if (m_it != m.waiters.end()) {
            m.waiters.erase(m_it);
            DarwinPthreadMutexRaw raw_m{};
            if (read_raw_mutex(mem, m.va, raw_m)) {
                raw_m.waiter_count = static_cast<std::uint32_t>(m.waiters.size());
                write_raw_mutex(mem, m.va, raw_m);
            }
        }
    }

    return DARWIN_SUCCESS;
}

bool DarwinSyncManager::is_thread_waiting(std::uint64_t tid) const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& [_, m] : mutexes_) {
        if (std::find(m.waiters.begin(), m.waiters.end(), tid) != m.waiters.end()) {
            return true;
        }
    }
    for (const auto& [_, c] : condvars_) {
        for (const auto& [w_tid, m_va_unused] : c.waiters) {
            (void)m_va_unused;
            if (w_tid == tid) return true;
        }
    }
    return false;
}

bool DarwinSyncManager::would_deadlock(std::uint64_t tid, std::uint64_t mutex_va) const {
    auto m_it = mutexes_.find(mutex_va);
    if (m_it == mutexes_.end() || m_it->second.owner_tid == 0) {
        return false;
    }

    std::uint64_t curr_owner = m_it->second.owner_tid;
    if (curr_owner == tid) {
        return true;
    }

    std::unordered_set<std::uint64_t> visited_threads;
    visited_threads.insert(tid);

    while (curr_owner != 0) {
        if (visited_threads.count(curr_owner) > 0) {
            return true;
        }
        visited_threads.insert(curr_owner);

        // Find which mutex curr_owner is waiting for
        std::uint64_t next_owner = 0;
        for (const auto& [_, other_m] : mutexes_) {
            if (std::find(other_m.waiters.begin(), other_m.waiters.end(), curr_owner) != other_m.waiters.end()) {
                next_owner = other_m.owner_tid;
                break;
            }
        }
        curr_owner = next_owner;
    }

    return false;
}

} // namespace winios::runtime
