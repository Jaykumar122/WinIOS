#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::uint64_t kTcbMagic = 0x5443425F54485244ULL; // "TCB_THRD"
inline constexpr std::size_t kMaxThreadNameLen = 32;
inline constexpr std::uint32_t kDefaultTimeSliceTicks = 10;
inline constexpr std::uint32_t kDefaultPriority = 16; // Normal priority (0..31)

enum class ThreadState : std::uint32_t {
    Ready = 0,
    Running = 1,
    Blocked = 2,
    Terminated = 3
};

enum class WaitReason : std::uint32_t {
    None = 0,
    Sleep = 1,
    WaitQueue = 2,
    IpcMessage = 3,
    IoDevice = 4,
    Mutex = 5
};

#pragma pack(push, 1)
// 512-byte packed guest TCB structure mapped directly into guest memory
struct GuestTcb {
    std::uint64_t magic{kTcbMagic};           // 0x000: Magic identification
    std::uint64_t tid{0};                     // 0x008: Thread ID
    std::uint32_t state{0};                   // 0x010: ThreadState enum
    std::uint32_t priority{kDefaultPriority}; // 0x014: Priority 0..31
    std::uint32_t time_slice{kDefaultTimeSliceTicks}; // 0x018: Remaining quantum ticks
    std::uint32_t wait_reason{0};             // 0x01C: WaitReason enum
    std::uint64_t sp_el0{0};                  // 0x020: User stack pointer
    std::uint64_t sp_el1{0};                  // 0x028: Kernel stack pointer
    std::uint64_t pc{0};                      // 0x030: Program counter
    std::uint64_t spsr_el1{0};                // 0x038: Saved process status register
    std::uint64_t gpr[31]{0};                 // 0x040 - 0x138: X0..X30 (31 * 8 = 248 bytes)
    std::uint64_t cpu_affinity{0x1};          // 0x138: Bitmask of eligible CPUs
    std::uint64_t entry_point{0};             // 0x140: Initial function pointer
    std::uint64_t stack_base{0};              // 0x148: Base of allocated stack
    std::uint64_t stack_size{0};              // 0x150: Allocated stack size
    std::uint64_t total_runtime_ticks{0};     // 0x158: Cumulative execution ticks
    std::int32_t exit_code{0};                // 0x160: Thread termination return value
    char name[kMaxThreadNameLen]{0};          // 0x164 - 0x184: Human-readable name
    std::uint8_t reserved[124]{0};            // 0x184 - 0x200: Pad to exactly 512 bytes
};
#pragma pack(pop)
static_assert(sizeof(GuestTcb) == 512, "GuestTcb must be exactly 512 bytes");

class KernelThread {
public:
    KernelThread(std::uint64_t tid,
                 std::string name,
                 std::uint64_t entry_point,
                 std::uint64_t arg,
                 std::uint64_t stack_base,
                 std::uint64_t stack_size,
                 std::uint32_t priority = kDefaultPriority,
                 bool user_mode = false);

    [[nodiscard]] std::uint64_t tid() const noexcept { return tcb_.tid; }
    [[nodiscard]] ThreadState state() const noexcept { return static_cast<ThreadState>(tcb_.state); }
    [[nodiscard]] std::uint32_t priority() const noexcept { return tcb_.priority; }
    [[nodiscard]] std::uint32_t time_slice() const noexcept { return tcb_.time_slice; }
    [[nodiscard]] WaitReason wait_reason() const noexcept { return static_cast<WaitReason>(tcb_.wait_reason); }
    [[nodiscard]] std::string name() const { return std::string(tcb_.name); }
    [[nodiscard]] std::int32_t exit_code() const noexcept { return tcb_.exit_code; }
    [[nodiscard]] std::uint64_t cpu_affinity() const noexcept { return tcb_.cpu_affinity; }
    [[nodiscard]] std::uint64_t total_runtime_ticks() const noexcept { return tcb_.total_runtime_ticks; }
    [[nodiscard]] const GuestTcb& tcb() const noexcept { return tcb_; }
    [[nodiscard]] GuestTcb& tcb() noexcept { return tcb_; }

    void set_state(ThreadState new_state) noexcept;
    void set_priority(std::uint32_t priority) noexcept;
    void set_time_slice(std::uint32_t ticks) noexcept;
    void set_cpu_affinity(std::uint64_t affinity_mask) noexcept;
    void block(WaitReason reason) noexcept;
    void unblock() noexcept;
    void terminate(std::int32_t code) noexcept;
    void tick() noexcept;

    // Context synchronization with virtual CPU
    void save_context_from_cpu(const CPU& cpu);
    void load_context_to_cpu(CPU& cpu) const;

    // Serialization to and from guest RAM
    bool write_to_guest_memory(Memory& mem, std::uint64_t guest_addr) const;
    bool read_from_guest_memory(const Memory& mem, std::uint64_t guest_addr);

private:
    GuestTcb tcb_;
};

class KernelThreadManager {
public:
    KernelThreadManager() = default;
    ~KernelThreadManager() = default;

    KernelThread* create_thread(const std::string& name,
                                std::uint64_t entry_point,
                                std::uint64_t arg,
                                std::uint64_t stack_base,
                                std::uint64_t stack_size,
                                std::uint32_t priority = kDefaultPriority,
                                bool user_mode = false);

    [[nodiscard]] KernelThread* get_thread(std::uint64_t tid);
    [[nodiscard]] const KernelThread* get_thread(std::uint64_t tid) const;
    [[nodiscard]] std::size_t thread_count() const noexcept { return threads_.size(); }
    [[nodiscard]] std::vector<KernelThread*> get_threads_by_state(ThreadState state);

    bool terminate_thread(std::uint64_t tid, std::int32_t exit_code);
    void reap_terminated_threads();

private:
    std::uint64_t next_tid_{1};
    std::vector<std::unique_ptr<KernelThread>> threads_;
};

} // namespace winios::kernel
