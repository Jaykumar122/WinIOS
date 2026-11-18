#include "devices/network_service.h"
#include "security/capability_manager.h"

#include <chrono>
#include <algorithm>
#include <cstring>

namespace winios::devices {

VirtualNetworkService::VirtualNetworkService()
    : VirtualDeviceBase(kDefaultNetworkDeviceId,
                        1,
                        DeviceClass::Network,
                        "WinIOS Virtual Loopback Adapter",
                        "WinIOS NetStack",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::NetworkAccess)) {
    stats_.queue_capacity = static_cast<std::uint32_t>(kMaxQueueCapacity);
    stats_.mtu = static_cast<std::uint32_t>(kMaxMTU);
}

bool VirtualNetworkService::send_packet(const std::uint8_t* data, std::size_t length) {
    if (!data || length == 0 || length > kMaxMTU) {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != DeviceState::Running && state_ != DeviceState::Ready) {
        return false;
    }

    if (queue_.size() >= kMaxQueueCapacity) {
        // Dropped due to full queue
        ++stats_.packets_dropped;
        return false;
    }

    const auto now_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    QueuedPacket pkt;
    pkt.payload.assign(data, data + length);
    pkt.timestamp_us = now_us;

    queue_.push_back(std::move(pkt));
    ++stats_.packets_sent;
    stats_.bytes_sent += length;
    stats_.queue_depth = static_cast<std::uint32_t>(queue_.size());

    return true;
}

bool VirtualNetworkService::receive_packet(std::uint8_t* out_buffer, std::size_t max_length, std::size_t* out_length) {
    if (!out_buffer || max_length == 0 || !out_length) {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != DeviceState::Running && state_ != DeviceState::Ready) {
        return false;
    }

    if (queue_.empty()) {
        *out_length = 0;
        return false;
    }

    const auto& front = queue_.front();
    const std::size_t copy_size = std::min(max_length, front.payload.size());
    std::memcpy(out_buffer, front.payload.data(), copy_size);
    *out_length = copy_size;

    ++stats_.packets_received;
    stats_.bytes_received += copy_size;

    queue_.pop_front();
    stats_.queue_depth = static_cast<std::uint32_t>(queue_.size());

    return true;
}

NetworkStatsData VirtualNetworkService::get_stats() const {
    std::lock_guard<std::mutex> guard(lock_);
    NetworkStatsData copy = stats_;
    copy.queue_depth = static_cast<std::uint32_t>(queue_.size());
    copy.state = static_cast<std::uint32_t>(state_);
    return copy;
}

std::size_t VirtualNetworkService::queue_depth() const {
    std::lock_guard<std::mutex> guard(lock_);
    return queue_.size();
}

bool VirtualNetworkService::has_pending_packets() const {
    std::lock_guard<std::mutex> guard(lock_);
    return !queue_.empty();
}

void VirtualNetworkService::flush_queue() {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.clear();
    stats_.queue_depth = 0;
}

bool VirtualNetworkService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.clear();
    stats_.queue_depth = 0;
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualNetworkService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualNetworkService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        queue_.clear();
        stats_.queue_depth = 0;
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualNetworkService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualNetworkService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualNetworkService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.clear();
    stats_ = NetworkStatsData{};
    stats_.queue_capacity = static_cast<std::uint32_t>(kMaxQueueCapacity);
    stats_.mtu = static_cast<std::uint32_t>(kMaxMTU);
    state_ = DeviceState::Ready;
}

} // namespace winios::devices
