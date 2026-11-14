#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "memory/memory.h"

namespace winios::kernel {

inline constexpr std::size_t kMaxIpcPayloadSize = 32;
inline constexpr std::size_t kDefaultPortCapacity = 16;

#pragma pack(push, 1)
struct KernelMessage {
    std::uint64_t sender_tid{0};
    std::uint64_t receiver_tid{0};
    std::uint32_t msg_id{0};
    std::uint32_t payload_size{0};
    std::uint64_t timestamp{0};
    std::uint8_t payload[kMaxIpcPayloadSize]{0};
};
#pragma pack(pop)
static_assert(sizeof(KernelMessage) == 64, "KernelMessage must be exactly 64 bytes");

class IpcPort {
public:
    IpcPort(std::uint32_t port_id, std::uint64_t owner_tid, std::size_t capacity = kDefaultPortCapacity);
    ~IpcPort() = default;

    // Non-blocking operations
    bool try_send(const KernelMessage& msg, PriorityScheduler& sched);
    bool try_recv(KernelMessage& out_msg, PriorityScheduler& sched);

    // Blocking operations
    bool send_blocking(KernelThread* sender, const KernelMessage& msg, PriorityScheduler& sched, CPU& cpu);
    bool recv_blocking(KernelThread* receiver, KernelMessage& out_msg, PriorityScheduler& sched, CPU& cpu);

    [[nodiscard]] std::uint32_t port_id() const noexcept { return port_id_; }
    [[nodiscard]] std::uint64_t owner_tid() const noexcept { return owner_tid_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t message_count() const noexcept { return messages_.size(); }
    [[nodiscard]] bool is_full() const noexcept { return messages_.size() >= capacity_; }
    [[nodiscard]] bool is_empty() const noexcept { return messages_.empty(); }

    [[nodiscard]] std::size_t send_waiters_count() const noexcept { return send_waiters_.count(); }
    [[nodiscard]] std::size_t recv_waiters_count() const noexcept { return recv_waiters_.count(); }

private:
    std::uint32_t port_id_{0};
    std::uint64_t owner_tid_{0};
    std::size_t capacity_{kDefaultPortCapacity};
    std::deque<KernelMessage> messages_;
    WaitQueue send_waiters_{WakePolicy::Fifo};
    WaitQueue recv_waiters_{WakePolicy::Fifo};
};

class IpcManager {
public:
    IpcManager() = default;
    ~IpcManager() = default;

    // Creates an IPC port
    IpcPort* create_port(std::uint64_t owner_tid, std::size_t capacity = kDefaultPortCapacity);

    // Lookup port by ID
    [[nodiscard]] IpcPort* get_port(std::uint32_t port_id);
    [[nodiscard]] const IpcPort* get_port(std::uint32_t port_id) const;

    // Destroy port
    bool destroy_port(std::uint32_t port_id, PriorityScheduler& sched);

    // Guest memory buffer transfer helpers
    bool send_from_guest(std::uint32_t port_id, std::uint64_t sender_tid, std::uint64_t guest_msg_addr,
                         Memory& mem, PriorityScheduler& sched);
    bool recv_to_guest(std::uint32_t port_id, std::uint64_t receiver_tid, std::uint64_t guest_msg_addr,
                       Memory& mem, PriorityScheduler& sched);

    [[nodiscard]] std::size_t port_count() const noexcept { return ports_.size(); }

private:
    std::uint32_t next_port_id_{1};
    std::unordered_map<std::uint32_t, std::unique_ptr<IpcPort>> ports_;
};

} // namespace winios::kernel
