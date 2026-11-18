#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <string>
#include <optional>

namespace winios::runtime {

constexpr size_t kMaxIpcMessageSize = 4096;
constexpr size_t kMaxIpcPortQueueDepth = 32;

struct IpcMessage {
    uint32_t sender_pid{0};
    uint64_t target_port{0};
    uint32_t message_id{0};
    uint32_t data_size{0};
    std::vector<uint8_t> data;
};

class GuestIpcRouter {
public:
    static GuestIpcRouter& Get();

    // 64-bit generation-tagged port creation
    uint64_t CreatePortV2(uint32_t owner_pid, const std::string& name = "");
    bool     DestroyPortV2(uint64_t port_handle, uint32_t caller_pid);
    bool     SendV2(uint32_t sender_pid, uint64_t target_port, const void* data, size_t size, uint32_t msg_id = 0);
    bool     ReceiveV2(uint32_t caller_pid, uint64_t port_handle, IpcMessage* msg_out);

    // Named port lookup
    bool     RegisterPortName(const std::string& name, uint64_t port_handle, uint32_t caller_pid);
    uint64_t LookupPortName(const std::string& name) const;

    // Legacy 32-bit compatibility methods
    uint32_t CreatePort(uint32_t owner_pid, const std::string& name = "");
    bool     DestroyPort(uint32_t port_id, uint32_t caller_pid);
    bool     Send(uint32_t sender_pid, uint32_t target_port, const void* data, size_t size, uint32_t msg_id = 0);
    bool     Receive(uint32_t caller_pid, uint32_t port_id, IpcMessage* msg_out);

    // Lifecycle cleanup
    void     CleanupProcess(uint32_t process_id);
    size_t   GetPortCount() const;
    void     Reset();

private:
    GuestIpcRouter() = default;
    ~GuestIpcRouter() = default;

    struct PortEntry {
        uint32_t id{0};
        uint32_t generation{1};
        uint32_t owner_pid{0};
        std::string name;
        std::queue<IpcMessage> message_queue;
    };

    static uint64_t MakeHandle(uint32_t id, uint32_t gen) noexcept {
        return (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(id);
    }
    static uint32_t GetId(uint64_t handle) noexcept {
        return static_cast<uint32_t>(handle & 0xFFFFFFFF);
    }
    static uint32_t GetGen(uint64_t handle) noexcept {
        return static_cast<uint32_t>(handle >> 32);
    }

    mutable std::mutex mutex_;
    uint32_t next_port_id_{100};
    std::unordered_map<uint32_t, PortEntry> ports_;
    std::unordered_map<std::string, uint64_t> name_directory_;
};

} // namespace winios::runtime
