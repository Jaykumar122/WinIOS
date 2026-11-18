#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <string>

namespace winios::runtime {

constexpr size_t kMaxIpcMessageSize = 4096;
constexpr size_t kMaxIpcPortQueueDepth = 32;

struct IpcMessage {
    uint32_t sender_pid{0};
    uint32_t target_port{0};
    uint32_t message_id{0};
    uint32_t data_size{0};
    std::vector<uint8_t> data;
};

class GuestIpcRouter {
public:
    static GuestIpcRouter& Get();

    uint32_t CreatePort(uint32_t owner_pid, const std::string& name = "");
    bool     DestroyPort(uint32_t port_id, uint32_t caller_pid);
    bool     Send(uint32_t sender_pid, uint32_t target_port, const void* data, size_t size, uint32_t msg_id = 0);
    bool     Receive(uint32_t caller_pid, uint32_t port_id, IpcMessage* msg_out);

    void     CleanupProcess(uint32_t process_id);
    size_t   GetPortCount() const;

private:
    GuestIpcRouter() = default;
    ~GuestIpcRouter() = default;

    struct Port {
        uint32_t id;
        uint32_t owner_pid;
        std::string name;
        std::queue<IpcMessage> message_queue;
    };

    mutable std::mutex mutex_;
    uint32_t next_port_id_{100};
    std::unordered_map<uint32_t, Port> ports_;
};

} // namespace winios::runtime
