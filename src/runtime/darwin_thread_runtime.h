#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::runtime {

class DarwinBsdSubsystem;

enum class DarwinThreadState {
    Running,
    Suspended,
    Blocked,
    Terminated
};

struct DarwinManagedThread {
    std::uint64_t       tid{0};
    std::uint32_t       pid{0};
    DarwinThreadState   state{DarwinThreadState::Running};
    std::uint64_t       entry_point{0};
    std::uint64_t       arg{0};
    std::uint64_t       stack_base{0};
    std::size_t         stack_size{0};
    std::size_t         guard_size{0x4000}; // 16 KB default
    std::uint64_t       pthread_va{0};
    std::uint64_t       tls_base{0};
    std::size_t         tls_size{0};
    std::uint64_t       initial_sp{0};
    int                 exit_code{0};
    bool                cancelled{false};
};

// Darwin Thread Specific Data (TSD) Slot Offsets
constexpr std::size_t kDarwinTsdThreadSelf       = 0;  // Slot 0 (Offset 0x00): pthread_t self-pointer
constexpr std::size_t kDarwinTsdErrno            = 1;  // Slot 1 (Offset 0x08): pointer to errno integer
constexpr std::size_t kDarwinTsdMigReplyPort     = 2;  // Slot 2 (Offset 0x10): Mach MIG reply port name
constexpr std::size_t kDarwinTsdMachThreadSelf   = 3;  // Slot 3 (Offset 0x18): Mach thread self port name
constexpr std::size_t kDarwinTsdThreadQos        = 4;  // Slot 4 (Offset 0x20): Thread QoS class
constexpr std::size_t kDarwinTsdDispatchQueue    = 5;  // Slot 5 (Offset 0x28): Current dispatch queue
constexpr std::size_t kDarwinTsdMaxSlots         = 16; // 16 fast TSD slots = 128 bytes

constexpr std::uint64_t kDarwinPthreadMagic      = 0x50544852445F5400ULL; // "PTHRD_T\0"

// Packed structure representing Darwin pthread control block in guest memory
#pragma pack(push, 8)
struct DarwinPthreadHeader {
    std::uint64_t tsd_slots[kDarwinTsdMaxSlots]; // Offsets 0x00..0x78 (128 bytes)
    std::uint64_t magic;                         // Offset 0x80: 0x50544852445F5400ULL
    std::uint64_t thread_id;                     // Offset 0x88: Guest Thread ID (TID)
    std::uint64_t stack_base;                    // Offset 0x90: Stack bottom VA
    std::uint64_t stack_size;                    // Offset 0x98: Stack size in bytes
    std::int32_t  errno_val;                     // Offset 0xA0: Integer errno
    std::uint32_t padding;                       // Offset 0xA4: 4-byte padding
    std::uint64_t tls_base;                      // Offset 0xA8: Thread Local Storage area VA
    std::uint64_t tls_size;                      // Offset 0xB0: TLS area size in bytes
};
#pragma pack(pop)

static_assert(sizeof(DarwinPthreadHeader) == 184, "DarwinPthreadHeader must be exactly 184 bytes");

// Mach-O Thread Local Variable (TLV) Descriptor
struct DarwinTlvDescriptorRaw {
    std::uint64_t thunk;  // Pointer to resolver thunk function
    std::uint64_t key;    // Key type: 0 = TPIDR_EL0 relative, 1 = TPIDRRO_EL0 relative
    std::uint64_t offset; // Byte offset of variable within thread-local block
};

// Thread Runtime Manager: Emulates Darwin pthread and TLS subsystems
class DarwinThreadRuntime {
public:
    DarwinThreadRuntime() = default;

    // Initializes a Darwin pthread control block at the specified guest virtual address
    [[nodiscard]] static bool initialize_thread_structure(
        Memory& memory,
        std::uint64_t pthread_va,
        std::uint64_t tid,
        std::uint64_t stack_base,
        std::uint64_t stack_size,
        std::uint32_t mig_reply_port,
        std::uint32_t mach_thread_self,
        std::uint64_t tls_base,
        std::uint64_t tls_size);

    // Binds thread context to CPU system registers: TPIDRRO_EL0 (read-only pthread_t) and TPIDR_EL0
    static void bind_cpu_thread(
        CPU& cpu,
        std::uint64_t pthread_va,
        std::uint64_t tls_base);

    // Reads back a DarwinPthreadHeader from guest memory
    [[nodiscard]] static bool read_pthread_header(
        const Memory& memory,
        std::uint64_t pthread_va,
        DarwinPthreadHeader& out_header);

    // Updates errno in the guest thread's pthread structure
    [[nodiscard]] static bool set_thread_errno(
        Memory& memory,
        std::uint64_t pthread_va,
        std::int32_t new_errno);

    // Resolves a Mach-O TLV (Thread Local Variable) descriptor address
    [[nodiscard]] static std::uint64_t resolve_tlv_address(
        const CPU& cpu,
        const Memory& memory,
        std::uint64_t tlv_descriptor_va);

    // Stack Allocation with Guard Page (PROT_NONE)
    [[nodiscard]] static bool allocate_thread_stack(
        DarwinBsdSubsystem& bsd,
        std::uint32_t pid,
        std::size_t stack_size,
        std::size_t guard_size,
        std::uint64_t& out_stack_base,
        std::uint64_t& out_initial_sp);

    // Managed Thread Lifecycle
    std::uint64_t create_managed_thread(
        DarwinBsdSubsystem& bsd,
        Memory& memory,
        std::uint32_t pid,
        std::uint64_t entry_point,
        std::uint64_t arg,
        std::size_t stack_size = 0x80000,
        std::size_t guard_size = 0x4000,
        std::size_t tls_size = 0x1000);

    bool suspend_thread(std::uint64_t tid);
    bool resume_thread(std::uint64_t tid);
    bool block_thread(std::uint64_t tid);
    bool unblock_thread(std::uint64_t tid);
    bool terminate_thread(std::uint64_t tid, int exit_code = 0);
    bool cancel_thread(std::uint64_t tid);
    [[nodiscard]] bool is_thread_cancelled(std::uint64_t tid) const;

    [[nodiscard]] std::optional<DarwinManagedThread> get_thread(std::uint64_t tid) const;
    [[nodiscard]] std::vector<DarwinManagedThread> get_threads_for_process(std::uint32_t pid) const;
    [[nodiscard]] std::size_t active_thread_count() const noexcept;
    [[nodiscard]] std::size_t total_thread_count() const noexcept;

private:
    mutable std::mutex lock_;
    std::uint64_t next_tid_{100};
    std::unordered_map<std::uint64_t, DarwinManagedThread> threads_;
};

} // namespace winios::runtime
