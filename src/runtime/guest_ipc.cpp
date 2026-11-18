#include "runtime/guest_ipc.h"
#include <cstring>

namespace winios::runtime {

GuestIpcRouter& GuestIpcRouter::Get() {
    static GuestIpcRouter instance;
    return instance;
}

uint32_t GuestIpcRouter::CreatePort(uint32_t owner_pid, const std::string& name) {
    if (owner_pid == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t port_id = next_port_id_++;
    Port p{};
    p.id = port_id;
    p.owner_pid = owner_pid;
    p.name = name;
    ports_[port_id] = std::move(p);
    return port_id;
}

bool GuestIpcRouter::DestroyPort(uint32_t port_id, uint32_t caller_pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ports_.find(port_id);
    if (it == ports_.end()) return false;
    if (it->second.owner_pid != caller_pid && caller_pid != 0) {
        return false; // Unauthorized port destruction
    }
    ports_.erase(it);
    return true;
}

bool GuestIpcRouter::Send(uint32_t sender_pid, uint32_t target_port, const void* data, size_t size, uint32_t msg_id) {
    if (size > kMaxIpcMessageSize) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ports_.find(target_port);
    if (it == ports_.end()) return false; // Port does not exist

    if (it->second.message_queue.size() >= kMaxIpcPortQueueDepth) {
        return false; // Queue full
    }

    IpcMessage msg{};
    msg.sender_pid = sender_pid;
    msg.target_port = target_port;
    msg.message_id = msg_id;
    msg.data_size = static_cast<uint32_t>(size);
    if (data && size > 0) {
        msg.data.resize(size);
        std::memcpy(msg.data.data(), data, size);
    }
    it->second.message_queue.push(std::move(msg));
    return true;
}

bool GuestIpcRouter::Receive(uint32_t caller_pid, uint32_t port_id, IpcMessage* msg_out) {
    if (!msg_out) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ports_.find(port_id);
    if (it == ports_.end()) return false;
    if (it->second.owner_pid != caller_pid) {
        return false; // Unauthorized message access
    }

    if (it->second.message_queue.empty()) return false;
    *msg_out = it->second.message_queue.front();
    it->second.message_queue.pop();
    return true;
}

void GuestIpcRouter::CleanupProcess(uint32_t process_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = ports_.begin(); it != ports_.end();) {
        if (it->second.owner_pid == process_id) {
            it = ports_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t GuestIpcRouter::GetPortCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ports_.size();
}

} // namespace winios::runtime
