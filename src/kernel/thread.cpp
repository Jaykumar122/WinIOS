#include "kernel/thread.h"

#include <algorithm>
#include <cstring>

namespace winios::kernel {

KernelThread::KernelThread(std::uint64_t tid,
                           std::string name,
                           std::uint64_t entry_point,
                           std::uint64_t arg,
                           std::uint64_t stack_base,
                           std::uint64_t stack_size,
                           std::uint32_t priority,
                           bool user_mode) {
    tcb_.magic = kTcbMagic;
    tcb_.tid = tid;
    tcb_.state = static_cast<std::uint32_t>(ThreadState::Ready);
    tcb_.priority = priority;
    tcb_.time_slice = kDefaultTimeSliceTicks;
    tcb_.wait_reason = static_cast<std::uint32_t>(WaitReason::None);
    tcb_.cpu_affinity = 0x1ULL;
    tcb_.entry_point = entry_point;
    tcb_.pc = entry_point;
    tcb_.stack_base = stack_base;
    tcb_.stack_size = stack_size;
    tcb_.total_runtime_ticks = 0;
    tcb_.exit_code = 0;

    // Stack pointer alignment (16-byte aligned at top of allocated stack)
    const std::uint64_t initial_sp = (stack_base + stack_size) & ~0xFULL;
    if (user_mode) {
        tcb_.sp_el0 = initial_sp;
        tcb_.sp_el1 = 0;
        tcb_.spsr_el1 = 0x0ULL; // Mode 0b0000 = EL0t
    } else {
        tcb_.sp_el0 = 0;
        tcb_.sp_el1 = initial_sp;
        tcb_.spsr_el1 = 0x5ULL; // Mode 0b0101 = EL1h
    }

    tcb_.gpr[0] = arg; // Argument passed in X0
    tcb_.gpr[30] = 0;  // LR initialized to zero

    // Copy thread name safely
    const std::size_t copy_len = std::min(name.length(), kMaxThreadNameLen - 1);
    std::memcpy(tcb_.name, name.c_str(), copy_len);
    tcb_.name[copy_len] = '\0';
}

void KernelThread::set_state(ThreadState new_state) noexcept {
    tcb_.state = static_cast<std::uint32_t>(new_state);
}

void KernelThread::set_priority(std::uint32_t priority) noexcept {
    tcb_.priority = priority;
}

void KernelThread::set_time_slice(std::uint32_t ticks) noexcept {
    tcb_.time_slice = ticks;
}

void KernelThread::set_cpu_affinity(std::uint64_t affinity_mask) noexcept {
    tcb_.cpu_affinity = affinity_mask;
}

void KernelThread::block(WaitReason reason) noexcept {
    tcb_.state = static_cast<std::uint32_t>(ThreadState::Blocked);
    tcb_.wait_reason = static_cast<std::uint32_t>(reason);
}

void KernelThread::unblock() noexcept {
    tcb_.state = static_cast<std::uint32_t>(ThreadState::Ready);
    tcb_.wait_reason = static_cast<std::uint32_t>(WaitReason::None);
}

void KernelThread::terminate(std::int32_t code) noexcept {
    tcb_.state = static_cast<std::uint32_t>(ThreadState::Terminated);
    tcb_.exit_code = code;
}

void KernelThread::tick() noexcept {
    if (tcb_.time_slice > 0) {
        --tcb_.time_slice;
    }
    ++tcb_.total_runtime_ticks;
}

void KernelThread::save_context_from_cpu(const CPU& cpu) {
    for (std::size_t i = 0; i < 31; ++i) {
        tcb_.gpr[i] = cpu.read_register(static_cast<std::uint32_t>(i));
    }
    tcb_.pc = cpu.get_program_counter();
    tcb_.sp_el0 = cpu.get_sp_el0();
    tcb_.sp_el1 = cpu.get_sp_el1();
    tcb_.spsr_el1 = cpu.get_spsr_el1();
}

void KernelThread::load_context_to_cpu(CPU& cpu) const {
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(static_cast<std::uint32_t>(i), tcb_.gpr[i]);
    }
    cpu.set_program_counter(tcb_.pc);
    cpu.set_sp_el0(tcb_.sp_el0);
    cpu.set_sp_el1(tcb_.sp_el1);
    cpu.set_spsr_el1(static_cast<std::uint32_t>(tcb_.spsr_el1));

    // Set privilege level according to SPSR mode field
    if ((tcb_.spsr_el1 & 0xFULL) == 0x0ULL) {
        cpu.set_current_el(0);
        cpu.set_sp_sel(false);
    } else {
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
    }
}

bool KernelThread::write_to_guest_memory(Memory& mem, std::uint64_t guest_addr) const {
    if (guest_addr + sizeof(GuestTcb) > mem.size()) {
        return false;
    }

    const auto* raw_src = reinterpret_cast<const std::uint8_t*>(&tcb_);
    for (std::size_t i = 0; i < sizeof(GuestTcb); ++i) {
        mem.write_u8(static_cast<std::size_t>(guest_addr + i), raw_src[i]);
    }
    return true;
}

bool KernelThread::read_from_guest_memory(const Memory& mem, std::uint64_t guest_addr) {
    if (guest_addr + sizeof(GuestTcb) > mem.size()) {
        return false;
    }

    GuestTcb temp;
    auto* raw_dst = reinterpret_cast<std::uint8_t*>(&temp);
    for (std::size_t i = 0; i < sizeof(GuestTcb); ++i) {
        raw_dst[i] = mem.read_u8(static_cast<std::size_t>(guest_addr + i));
    }

    if (temp.magic != kTcbMagic) {
        return false;
    }

    tcb_ = temp;
    return true;
}

KernelThread* KernelThreadManager::create_thread(const std::string& name,
                                                 std::uint64_t entry_point,
                                                 std::uint64_t arg,
                                                 std::uint64_t stack_base,
                                                 std::uint64_t stack_size,
                                                 std::uint32_t priority,
                                                 bool user_mode) {
    const std::uint64_t tid = next_tid_++;
    auto thread = std::make_unique<KernelThread>(tid, name, entry_point, arg, stack_base, stack_size, priority, user_mode);
    auto* ptr = thread.get();
    threads_.push_back(std::move(thread));
    return ptr;
}

KernelThread* KernelThreadManager::get_thread(std::uint64_t tid) {
    for (auto& t : threads_) {
        if (t && t->tid() == tid) {
            return t.get();
        }
    }
    return nullptr;
}

const KernelThread* KernelThreadManager::get_thread(std::uint64_t tid) const {
    for (const auto& t : threads_) {
        if (t && t->tid() == tid) {
            return t.get();
        }
    }
    return nullptr;
}

std::vector<KernelThread*> KernelThreadManager::get_threads_by_state(ThreadState state) {
    std::vector<KernelThread*> result;
    for (auto& t : threads_) {
        if (t && t->state() == state) {
            result.push_back(t.get());
        }
    }
    return result;
}

bool KernelThreadManager::terminate_thread(std::uint64_t tid, std::int32_t exit_code) {
    auto* t = get_thread(tid);
    if (!t) {
        return false;
    }
    t->terminate(exit_code);
    return true;
}

void KernelThreadManager::reap_terminated_threads() {
    threads_.erase(
        std::remove_if(threads_.begin(), threads_.end(),
                       [](const std::unique_ptr<KernelThread>& t) {
                           return !t || t->state() == ThreadState::Terminated;
                       }),
        threads_.end());
}

} // namespace winios::kernel
