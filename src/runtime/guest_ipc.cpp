#include "runtime/guest_ipc.h"
#include <cstring>

namespace winios::runtime {

GuestIpcRouter& GuestIpcRouter::Get() {
    static GuestIpcRouter instance;
    return instance;
}

uint64_t GuestIpcRouter::CreatePortV2(uint32_t owner_pid, const std::string& name) {
    if (owner_pid == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t port_id = next_port_id_++;
    PortEntry p{};
    p.id = port_id;
    p.generation = 1;
    p.owner_pid = owner_pid;
    p.name = name;

    const uint64_t handle = MakeHandle(port_id, p.generation);
    ports_[port_id] = std::move(p);

    if (!name.empty()) {
        name_directory_[name] = handle;
    }
    return handle;
}

bool GuestIpcRouter::DestroyPortV2(uint64_t port_handle, uint32_t caller_pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t id = GetId(port_handle);
    const uint32_t gen = GetGen(port_handle);

    auto it = ports_.find(id);
    if (it == ports_.end()) return false;
    if (gen != 0 && it->second.generation != gen) return false; // Stale handle!
    if (it->second.owner_pid != caller_pid && caller_pid != 0) {
        return false; // Unauthorized port destruction
    }

    if (!it->second.name.empty()) {
        name_directory_.erase(it->second.name);
    }
    ports_.erase(it);
    return true;
}

bool GuestIpcRouter::SendV2(uint32_t sender_pid, uint64_t target_port, const void* data, size_t size, uint32_t msg_id) {
    if (size > kMaxIpcMessageSize) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    const uint32_t id = GetId(target_port);
    const uint32_t gen = GetGen(target_port);

    auto it = ports_.find(id);
    if (it == ports_.end()) return false;
    if (gen != 0 && it->second.generation != gen) return false; // Stale generation

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

bool GuestIpcRouter::ReceiveV2(uint32_t caller_pid, uint64_t port_handle, IpcMessage* msg_out) {
    if (!msg_out) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    const uint32_t id = GetId(port_handle);
    const uint32_t gen = GetGen(port_handle);

    auto it = ports_.find(id);
    if (it == ports_.end()) return false;
    if (gen != 0 && it->second.generation != gen) return false; // Stale generation
    if (it->second.owner_pid != caller_pid) {
        return false; // Unauthorized access
    }

    if (it->second.message_queue.empty()) return false;
    *msg_out = it->second.message_queue.front();
    it->second.message_queue.pop();
    return true;
}

bool GuestIpcRouter::RegisterPortName(const std::string& name, uint64_t port_handle, uint32_t caller_pid) {
    if (name.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    const uint32_t id = GetId(port_handle);
    auto it = ports_.find(id);
    if (it == ports_.end()) return false;
    if (it->second.owner_pid != caller_pid && caller_pid != 0) return false;

    it->second.name = name;
    name_directory_[name] = port_handle;
    return true;
}

uint64_t GuestIpcRouter::LookupPortName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = name_directory_.find(name);
    if (it == name_directory_.end()) return 0;

    const uint64_t handle = it->second;
    const uint32_t id = GetId(handle);
    const uint32_t gen = GetGen(handle);

    auto pit = ports_.find(id);
    if (pit == ports_.end() || pit->second.generation != gen) {
        return 0; // Stale or dead registered port
    }
    return handle;
}

// Legacy 32-bit wrappers
uint32_t GuestIpcRouter::CreatePort(uint32_t owner_pid, const std::string& name) {
    const uint64_t h = CreatePortV2(owner_pid, name);
    return GetId(h);
}

bool GuestIpcRouter::DestroyPort(uint32_t port_id, uint32_t caller_pid) {
    return DestroyPortV2(static_cast<uint64_t>(port_id), caller_pid);
}

bool GuestIpcRouter::Send(uint32_t sender_pid, uint32_t target_port, const void* data, size_t size, uint32_t msg_id) {
    return SendV2(sender_pid, static_cast<uint64_t>(target_port), data, size, msg_id);
}

bool GuestIpcRouter::Receive(uint32_t caller_pid, uint32_t port_id, IpcMessage* msg_out) {
    return ReceiveV2(caller_pid, static_cast<uint64_t>(port_id), msg_out);
}

void GuestIpcRouter::CleanupProcess(uint32_t process_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = ports_.begin(); it != ports_.end();) {
        if (it->second.owner_pid == process_id) {
            if (!it->second.name.empty()) {
                name_directory_.erase(it->second.name);
            }
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

void GuestIpcRouter::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    ports_.clear();
    name_directory_.clear();
    next_port_id_ = 100;
}

} // namespace winios::runtime
