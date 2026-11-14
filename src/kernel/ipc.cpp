#include "kernel/ipc.h"

#include <cstring>

namespace winios::kernel {

IpcPort::IpcPort(std::uint32_t port_id, std::uint64_t owner_tid, std::size_t capacity)
    : port_id_(port_id), owner_tid_(owner_tid), capacity_(capacity > 0 ? capacity : kDefaultPortCapacity) {}

bool IpcPort::try_send(const KernelMessage& msg, PriorityScheduler& sched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (is_full()) {
        return false;
    }
    messages_.push_back(msg);
    if (!recv_waiters_.empty()) {
        recv_waiters_.wake_one(sched);
    }
    return true;
}

bool IpcPort::try_recv(KernelMessage& out_msg, PriorityScheduler& sched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (is_empty()) {
        return false;
    }
    out_msg = messages_.front();
    messages_.pop_front();
    if (!send_waiters_.empty()) {
        send_waiters_.wake_one(sched);
    }
    return true;
}

bool IpcPort::send_blocking(KernelThread* sender, const KernelMessage& msg, PriorityScheduler& sched, CPU& cpu) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sender == nullptr) {
        return false;
    }
    if (is_full()) {
        send_waiters_.wait(sender, sched, cpu);
        return false;
    }
    messages_.push_back(msg);
    if (!recv_waiters_.empty()) {
        recv_waiters_.wake_one(sched);
    }
    return true;
}

bool IpcPort::recv_blocking(KernelThread* receiver, KernelMessage& out_msg, PriorityScheduler& sched, CPU& cpu) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (receiver == nullptr) {
        return false;
    }
    if (is_empty()) {
        recv_waiters_.wait(receiver, sched, cpu);
        return false;
    }
    out_msg = messages_.front();
    messages_.pop_front();
    if (!send_waiters_.empty()) {
        send_waiters_.wake_one(sched);
    }
    return true;
}

IpcPort* IpcManager::create_port(std::uint64_t owner_tid, std::size_t capacity) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::uint32_t id = next_port_id_++;
    auto port = std::make_unique<IpcPort>(id, owner_tid, capacity);
    IpcPort* ptr = port.get();
    ports_[id] = std::move(port);
    return ptr;
}

IpcPort* IpcManager::get_port(std::uint32_t port_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = ports_.find(port_id);
    return it != ports_.end() ? it->second.get() : nullptr;
}

const IpcPort* IpcManager::get_port(std::uint32_t port_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = ports_.find(port_id);
    return it != ports_.end() ? it->second.get() : nullptr;
}

bool IpcManager::destroy_port(std::uint32_t port_id, PriorityScheduler& sched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = ports_.find(port_id);
    if (it == ports_.end()) {
        return false;
    }
    // Wake any blocked threads before destroying
    KernelMessage dummy{};
    while (it->second->try_recv(dummy, sched)) {}
    ports_.erase(it);
    return true;
}

bool IpcManager::send_from_guest(
    std::uint32_t port_id,
    std::uint64_t sender_tid,
    std::uint64_t guest_msg_addr,
    Memory& mem,
    PriorityScheduler& sched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    IpcPort* port = get_port(port_id);
    if (port == nullptr) {
        return false;
    }

    KernelMessage msg{};
    for (std::size_t i = 0; i < sizeof(KernelMessage); i += 4) {
        const std::uint32_t word = mem.read_u32(static_cast<std::size_t>(guest_msg_addr + i));
        std::memcpy(reinterpret_cast<std::uint8_t*>(&msg) + i, &word, 4);
    }
    msg.sender_tid = sender_tid;

    return port->try_send(msg, sched);
}

bool IpcManager::recv_to_guest(
    std::uint32_t port_id,
    std::uint64_t receiver_tid,
    std::uint64_t guest_msg_addr,
    Memory& mem,
    PriorityScheduler& sched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    IpcPort* port = get_port(port_id);
    if (port == nullptr) {
        return false;
    }

    KernelMessage msg{};
    if (!port->try_recv(msg, sched)) {
        return false;
    }
    msg.receiver_tid = receiver_tid;

    for (std::size_t i = 0; i < sizeof(KernelMessage); i += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, reinterpret_cast<const std::uint8_t*>(&msg) + i, 4);
        mem.write_u32(static_cast<std::size_t>(guest_msg_addr + i), word);
    }
    return true;
}

} // namespace winios::kernel
