#include "kernel/sync.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace winios::kernel {

KernelSpinlock::KernelSpinlock(Memory& mem, std::uint64_t guest_addr)
    : mem_(mem), guest_addr_(guest_addr) {
    initialize();
}

void KernelSpinlock::initialize() {
    mem_.write_u32(static_cast<std::size_t>(guest_addr_), 0);
    mem_.write_u32(static_cast<std::size_t>(guest_addr_ + 4), 0);
    mem_.write_u32(static_cast<std::size_t>(guest_addr_ + 8), 0);
    mem_.write_u32(static_cast<std::size_t>(guest_addr_ + 12), 0);
    acquire_count_ = 0;
    contention_count_ = 0;
}

bool KernelSpinlock::try_acquire_host(std::uint64_t tid, bool* out_deadlock) {
    if (out_deadlock != nullptr) {
        *out_deadlock = false;
    }

    const std::uint32_t current_lock = mem_.read_u32(static_cast<std::size_t>(guest_addr_));
    const std::uint32_t current_owner = mem_.read_u32(static_cast<std::size_t>(guest_addr_ + 4));

    if (current_lock != 0) {
        if (current_owner == static_cast<std::uint32_t>(tid)) {
            if (out_deadlock != nullptr) {
                *out_deadlock = true;
            }
            return false;
        }
        ++contention_count_;
        return false;
    }

    // Unlocked -> Acquire
    mem_.write_u32(static_cast<std::size_t>(guest_addr_), 1);
    mem_.write_u32(static_cast<std::size_t>(guest_addr_ + 4), static_cast<std::uint32_t>(tid));
    ++acquire_count_;
    return true;
}

bool KernelSpinlock::release_host(std::uint64_t tid) {
    const std::uint32_t current_owner = mem_.read_u32(static_cast<std::size_t>(guest_addr_ + 4));

    if (current_owner != static_cast<std::uint32_t>(tid) && current_owner != 0) {
        return false;
    }

    mem_.write_u32(static_cast<std::size_t>(guest_addr_ + 4), 0);
    mem_.write_u32(static_cast<std::size_t>(guest_addr_), 0);
    return true;
}

bool KernelSpinlock::is_locked() const {
    return mem_.read_u32(static_cast<std::size_t>(guest_addr_)) != 0;
}

std::uint32_t KernelSpinlock::owner_tid() const {
    return mem_.read_u32(static_cast<std::size_t>(guest_addr_ + 4));
}

std::vector<std::uint32_t> KernelSpinlock::emit_guest_spinlock_acquire() {
    return {
        // loop:
        0x885FFC02u, // 0x00: LDAXR W2, [X0]
        0x35FFFFE2u, // 0x04: CBNZ W2, 0x00 (offset -1 instruction = -4 bytes)
        0x52800023u, // 0x08: MOV W3, #1
        0x8804FC03u, // 0x0C: STLXR W4, W3, [X0]
        0x35FFFF84u, // 0x10: CBNZ W4, 0x00 (offset -4 instructions = -16 bytes: -4 & 0x7FFFF = 0x7FFFC -> 0x35000000 | (0x7FFFC << 5) | 4 = 0x35FFFF84u)
        0xB9000401u, // 0x14: STR W1, [X0, #4] (store TID into owner_tid)
        0xD2800000u, // 0x18: MOV X0, #0 (return 0 success)
        0xD65F03C0u  // 0x1C: RET
    };
}

std::vector<std::uint32_t> KernelSpinlock::emit_guest_spinlock_try_acquire() {
    return {
        0x885FFC02u, // 0x00: LDAXR W2, [X0]
        0x350000E2u, // 0x04: CBNZ W2, 0x20 (+7 instructions = +28 bytes)
        0x52800023u, // 0x08: MOV W3, #1
        0x8804FC03u, // 0x0C: STLXR W4, W3, [X0]
        0x35000084u, // 0x10: CBNZ W4, 0x20 (+4 instructions = +16 bytes)
        0xB9000401u, // 0x14: STR W1, [X0, #4] (store TID)
        0xD2800000u, // 0x18: MOV X0, #0 (success)
        0xD65F03C0u, // 0x1C: RET
        // fail:
        0xD2800020u, // 0x20: MOV X0, #1 (failure / contended)
        0xD65F03C0u  // 0x24: RET
    };
}

std::vector<std::uint32_t> KernelSpinlock::emit_guest_spinlock_release() {
    return {
        0xB900041Fu, // 0x00: STR WZR, [X0, #4] (clear owner_tid)
        0x889FFC1Fu, // 0x04: STLR WZR, [X0]    (release lock word)
        0xD65F03C0u  // 0x08: RET
    };
}

WaitQueue::WaitQueue(WakePolicy policy) : policy_(policy) {}

void WaitQueue::add_waiter(KernelThread* thread) {
    if (thread != nullptr && !contains(thread)) {
        waiters_.push_back(thread);
    }
}

bool WaitQueue::remove_waiter(KernelThread* thread) {
    auto it = std::find(waiters_.begin(), waiters_.end(), thread);
    if (it != waiters_.end()) {
        waiters_.erase(it);
        return true;
    }
    return false;
}

bool WaitQueue::contains(const KernelThread* thread) const noexcept {
    return std::find(waiters_.begin(), waiters_.end(), thread) != waiters_.end();
}

bool WaitQueue::wait(KernelThread* thread, PriorityScheduler& sched, CPU& cpu) {
    if (thread == nullptr) {
        return false;
    }
    add_waiter(thread);
    return sched.block_current(WaitReason::WaitQueue, cpu);
}

KernelThread* WaitQueue::wake_one(PriorityScheduler& sched) {
    if (waiters_.empty()) {
        return nullptr;
    }

    KernelThread* candidate = nullptr;
    if (policy_ == WakePolicy::Fifo) {
        candidate = waiters_.front();
        waiters_.pop_front();
    } else {
        // Highest priority first
        auto best_it = std::max_element(
            waiters_.begin(), waiters_.end(),
            [](const KernelThread* a, const KernelThread* b) {
                return a->priority() < b->priority();
            });
        candidate = *best_it;
        waiters_.erase(best_it);
    }

    if (candidate != nullptr) {
        sched.unblock_thread(candidate);
    }
    return candidate;
}

std::size_t WaitQueue::wake_all(PriorityScheduler& sched) {
    std::size_t count = 0;
    while (!waiters_.empty()) {
        KernelThread* thread = waiters_.front();
        waiters_.pop_front();
        if (thread != nullptr) {
            sched.unblock_thread(thread);
            ++count;
        }
    }
    return count;
}

} // namespace winios::kernel
