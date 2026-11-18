#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

namespace winios::devices {

#pragma pack(push, 8)
struct NetworkStatsData {
    std::uint64_t packets_sent{0};
    std::uint64_t packets_received{0};
    std::uint64_t packets_dropped{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t bytes_received{0};
    std::uint32_t queue_depth{0};
    std::uint32_t queue_capacity{64};
    std::uint32_t mtu{1514};
    std::uint32_t state{0};
};

struct PacketHeaderData {
    std::uint32_t length{0};
    std::uint32_t flags{0};
    std::uint64_t timestamp_us{0};
};
#pragma pack(pop)

class VirtualNetworkService : public VirtualDeviceBase {
public:
    static constexpr std::uint32_t kDefaultNetworkDeviceId = 7;
    static constexpr std::size_t   kMaxMTU                 = 1514;
    static constexpr std::size_t   kMaxQueueCapacity       = 64;

    VirtualNetworkService();
    ~VirtualNetworkService() override = default;

    // Send / Receive
    bool send_packet(const std::uint8_t* data, std::size_t length);
    bool receive_packet(std::uint8_t* out_buffer, std::size_t max_length, std::size_t* out_length);

    // Queries
    [[nodiscard]] NetworkStatsData get_stats() const;
    [[nodiscard]] std::size_t queue_depth() const;
    [[nodiscard]] bool has_pending_packets() const;
    void flush_queue();

    // Lifecycle
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    struct QueuedPacket {
        std::vector<std::uint8_t> payload;
        std::uint64_t             timestamp_us{0};
    };

    mutable std::mutex lock_;
    std::deque<QueuedPacket> queue_;
    NetworkStatsData   stats_{};
};

} // namespace winios::devices
