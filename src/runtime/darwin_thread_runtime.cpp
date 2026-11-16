#include "runtime/darwin_thread_runtime.h"
#include "runtime/darwin_bsd_subsystem.h"

#include <cstring>

namespace winios::runtime {

bool DarwinThreadRuntime::initialize_thread_structure(
    Memory& memory,
    std::uint64_t pthread_va,
    std::uint64_t tid,
    std::uint64_t stack_base,
    std::uint64_t stack_size,
    std::uint32_t mig_reply_port,
    std::uint32_t mach_thread_self,
    std::uint64_t tls_base,
    std::uint64_t tls_size) {

    if (memory.size() == 0) {
        return false;
    }

    std::size_t offset = (pthread_va < memory.size())
                             ? static_cast<std::size_t>(pthread_va)
                             : static_cast<std::size_t>(pthread_va % memory.size());
    if (offset + sizeof(DarwinPthreadHeader) > memory.size()) {
        return false;
    }

    DarwinPthreadHeader hdr{};
    // Initialize TSD slots
    // Slot 0: pointer to pthread structure itself (pthread_self())
    hdr.tsd_slots[kDarwinTsdThreadSelf] = pthread_va;
    // Slot 1: pointer to errno integer
    std::uint64_t errno_addr = pthread_va + offsetof(DarwinPthreadHeader, errno_val);
    hdr.tsd_slots[kDarwinTsdErrno] = errno_addr;
    // Slot 2: Mach MIG reply port
    hdr.tsd_slots[kDarwinTsdMigReplyPort] = static_cast<std::uint64_t>(mig_reply_port);
    // Slot 3: Mach thread self port
    hdr.tsd_slots[kDarwinTsdMachThreadSelf] = static_cast<std::uint64_t>(mach_thread_self);
    // Slot 4: Default QoS class (e.g. QOS_CLASS_USER_INTERACTIVE = 0x21)
    hdr.tsd_slots[kDarwinTsdThreadQos] = 0x21;
    // Slot 5: Dispatch queue (0 for main thread default)
    hdr.tsd_slots[kDarwinTsdDispatchQueue] = 0;

    hdr.magic = kDarwinPthreadMagic;
    hdr.thread_id = tid;
    hdr.stack_base = stack_base;
    hdr.stack_size = stack_size;
    hdr.errno_val = 0;
    hdr.padding = 0;
    hdr.tls_base = tls_base;
    hdr.tls_size = tls_size;

    std::memcpy(memory.raw_data() + offset, &hdr, sizeof(hdr));
    return true;
}

void DarwinThreadRuntime::bind_cpu_thread(
    CPU& cpu,
    std::uint64_t pthread_va,
    std::uint64_t tls_base) {

    // In Apple's Darwin ARM64 userspace:
    // TPIDRRO_EL0 holds the read-only pointer to the current thread's TSD/pthread structure
    cpu.set_tpidrro_el0(pthread_va);

    // TPIDR_EL0 holds the read/write thread-local storage area base
    cpu.set_tpidr_el0(tls_base);
}

bool DarwinThreadRuntime::read_pthread_header(
    const Memory& memory,
    std::uint64_t pthread_va,
    DarwinPthreadHeader& out_header) {

    if (memory.size() == 0) {
        return false;
    }

    std::size_t offset = (pthread_va < memory.size())
                             ? static_cast<std::size_t>(pthread_va)
                             : static_cast<std::size_t>(pthread_va % memory.size());
    if (offset + sizeof(DarwinPthreadHeader) > memory.size()) {
        return false;
    }

    std::memcpy(&out_header, memory.raw_data() + offset, sizeof(DarwinPthreadHeader));
    return true;
}

bool DarwinThreadRuntime::set_thread_errno(
    Memory& memory,
    std::uint64_t pthread_va,
    std::int32_t new_errno) {

    if (memory.size() == 0) {
        return false;
    }

    std::size_t offset = (pthread_va < memory.size())
                             ? static_cast<std::size_t>(pthread_va)
                             : static_cast<std::size_t>(pthread_va % memory.size());
    if (offset + sizeof(DarwinPthreadHeader) > memory.size()) {
        return false;
    }

    std::size_t errno_off = offset + offsetof(DarwinPthreadHeader, errno_val);
    memory.write_u32(errno_off, static_cast<std::uint32_t>(new_errno));
    return true;
}

std::uint64_t DarwinThreadRuntime::resolve_tlv_address(
    const CPU& cpu,
    const Memory& memory,
    std::uint64_t tlv_descriptor_va) {

    if (memory.size() == 0) {
        return 0;
    }

    std::size_t offset = (tlv_descriptor_va < memory.size())
                             ? static_cast<std::size_t>(tlv_descriptor_va)
                             : static_cast<std::size_t>(tlv_descriptor_va % memory.size());
    if (offset + sizeof(DarwinTlvDescriptorRaw) > memory.size()) {
        return 0;
    }

    DarwinTlvDescriptorRaw desc{};
    std::memcpy(&desc, memory.raw_data() + offset, sizeof(desc));

    if (desc.key == 0) {
        // Key 0: offset relative to TPIDR_EL0 (user TLS base)
        return cpu.get_tpidr_el0() + desc.offset;
    } else if (desc.key == 1) {
        // Key 1: offset relative to TPIDRRO_EL0 (pthread structure)
        return cpu.get_tpidrro_el0() + desc.offset;
    }

    // Default fallback to TPIDR_EL0
    return cpu.get_tpidr_el0() + desc.offset;
}

bool DarwinThreadRuntime::allocate_thread_stack(
    DarwinBsdSubsystem& bsd,
    std::uint32_t pid,
    std::size_t stack_size,
    std::size_t guard_size,
    std::uint64_t& out_stack_base,
    std::uint64_t& out_initial_sp) {

    const std::uint64_t kPageMask = 0x3FFFULL; // 16 KB granule
    std::size_t aligned_stack = (stack_size + kPageMask) & ~kPageMask;
    std::size_t aligned_guard = (guard_size + kPageMask) & ~kPageMask;

    // 1. Allocate guard page with PROT_NONE (0)
    std::uint64_t guard_addr = bsd.mmap(pid, 0, aligned_guard, 0 /* PROT_NONE */, 0x1002, -1, 0);
    if (guard_addr == 0) {
        return false;
    }

    // 2. Allocate usable stack immediately following guard page (PROT_READ | PROT_WRITE)
    std::uint64_t stack_addr = bsd.mmap(pid, 0, aligned_stack, 0x3 /* PROT_READ | PROT_WRITE */, 0x1002, -1, 0);
    if (stack_addr == 0) {
        bsd.munmap(pid, guard_addr, aligned_guard);
        return false;
    }

    out_stack_base = guard_addr;
    out_initial_sp = (stack_addr + aligned_stack) & ~0xFULL; // 16-byte aligned
    return true;
}

std::uint64_t DarwinThreadRuntime::create_managed_thread(
    DarwinBsdSubsystem& bsd,
    Memory& memory,
    std::uint32_t pid,
    std::uint64_t entry_point,
    std::uint64_t arg,
    std::size_t stack_size,
    std::size_t guard_size,
    std::size_t tls_size) {

    std::uint64_t stack_base = 0;
    std::uint64_t initial_sp = 0;
    if (!allocate_thread_stack(bsd, pid, stack_size, guard_size, stack_base, initial_sp)) {
        return 0;
    }

    // Allocate TLS area
    const std::uint64_t kPageMask = 0x3FFFULL;
    std::size_t aligned_tls = (tls_size + sizeof(DarwinPthreadHeader) + kPageMask) & ~kPageMask;
    std::uint64_t tls_alloc = bsd.mmap(pid, 0, aligned_tls, 0x3, 0x1002, -1, 0);
    if (tls_alloc == 0) {
        bsd.munmap(pid, stack_base, guard_size + stack_size);
        return 0;
    }

    std::uint64_t pthread_va = tls_alloc;
    std::uint64_t tls_base = tls_alloc + sizeof(DarwinPthreadHeader);

    std::lock_guard<std::mutex> guard(lock_);
    std::uint64_t tid = next_tid_++;

    // Register with bsd subsystem
    std::uint64_t bsd_tid = bsd.create_thread(pid, entry_point, initial_sp, tls_base);
    if (bsd_tid > 0) {
        DarwinThreadContext ctx{};
        if (bsd.get_thread_context(pid, bsd_tid, ctx)) {
            ctx.x[0] = arg;
            bsd.set_thread_context(pid, bsd_tid, ctx);
        }
    }

    // Initialize pthread header and TSD
    if (!initialize_thread_structure(memory, pthread_va, tid, stack_base, stack_size,
                                    static_cast<std::uint32_t>(tid * 10 + 2),
                                    static_cast<std::uint32_t>(tid * 10 + 3),
                                    tls_base, tls_size)) {
        return 0;
    }

    DarwinManagedThread thread{};
    thread.tid = tid;
    thread.pid = pid;
    thread.state = DarwinThreadState::Running;
    thread.entry_point = entry_point;
    thread.arg = arg;
    thread.stack_base = stack_base;
    thread.stack_size = stack_size;
    thread.guard_size = guard_size;
    thread.pthread_va = pthread_va;
    thread.tls_base = tls_base;
    thread.tls_size = tls_size;
    thread.initial_sp = initial_sp;

    threads_[tid] = thread;
    return tid;
}

bool DarwinThreadRuntime::suspend_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state != DarwinThreadState::Running) {
        return false;
    }
    it->second.state = DarwinThreadState::Suspended;
    return true;
}

bool DarwinThreadRuntime::resume_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state != DarwinThreadState::Suspended) {
        return false;
    }
    it->second.state = DarwinThreadState::Running;
    return true;
}

bool DarwinThreadRuntime::block_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state != DarwinThreadState::Running) {
        return false;
    }
    it->second.state = DarwinThreadState::Blocked;
    return true;
}

bool DarwinThreadRuntime::unblock_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state != DarwinThreadState::Blocked) {
        return false;
    }
    it->second.state = DarwinThreadState::Running;
    return true;
}

bool DarwinThreadRuntime::terminate_thread(std::uint64_t tid, int exit_code) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state == DarwinThreadState::Terminated) {
        return false;
    }
    it->second.state = DarwinThreadState::Terminated;
    it->second.exit_code = exit_code;
    return true;
}

bool DarwinThreadRuntime::cancel_thread(std::uint64_t tid) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end() || it->second.state == DarwinThreadState::Terminated) {
        return false;
    }
    it->second.cancelled = true;
    return true;
}

bool DarwinThreadRuntime::is_thread_cancelled(std::uint64_t tid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it == threads_.end()) {
        return false;
    }
    return it->second.cancelled;
}

std::optional<DarwinManagedThread> DarwinThreadRuntime::get_thread(std::uint64_t tid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = threads_.find(tid);
    if (it != threads_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<DarwinManagedThread> DarwinThreadRuntime::get_threads_for_process(std::uint32_t pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<DarwinManagedThread> res;
    for (const auto& [_, t] : threads_) {
        if (t.pid == pid) {
            res.push_back(t);
        }
    }
    return res;
}

std::size_t DarwinThreadRuntime::active_thread_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t cnt = 0;
    for (const auto& [_, t] : threads_) {
        if (t.state != DarwinThreadState::Terminated) {
            cnt++;
        }
    }
    return cnt;
}

std::size_t DarwinThreadRuntime::total_thread_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return threads_.size();
}

} // namespace winios::runtime
