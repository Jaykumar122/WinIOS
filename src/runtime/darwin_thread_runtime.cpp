#include "runtime/darwin_thread_runtime.h"

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

    if (pthread_va + sizeof(DarwinPthreadHeader) > memory.size()) {
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

    std::memcpy(memory.raw_data() + pthread_va, &hdr, sizeof(hdr));
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

    if (pthread_va + sizeof(DarwinPthreadHeader) > memory.size()) {
        return false;
    }

    std::memcpy(&out_header, memory.raw_data() + pthread_va, sizeof(DarwinPthreadHeader));
    return true;
}

bool DarwinThreadRuntime::set_thread_errno(
    Memory& memory,
    std::uint64_t pthread_va,
    std::int32_t new_errno) {

    if (pthread_va + sizeof(DarwinPthreadHeader) > memory.size()) {
        return false;
    }

    std::size_t errno_off = static_cast<std::size_t>(pthread_va + offsetof(DarwinPthreadHeader, errno_val));
    memory.write_u32(errno_off, static_cast<std::uint32_t>(new_errno));
    return true;
}

std::uint64_t DarwinThreadRuntime::resolve_tlv_address(
    const CPU& cpu,
    const Memory& memory,
    std::uint64_t tlv_descriptor_va) {

    if (tlv_descriptor_va + sizeof(DarwinTlvDescriptorRaw) > memory.size()) {
        return 0;
    }

    DarwinTlvDescriptorRaw desc{};
    std::memcpy(&desc, memory.raw_data() + tlv_descriptor_va, sizeof(desc));

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

} // namespace winios::runtime
